# GPGPU-Sim Texture Cache 微架构深度调研

> 调研对象: GPGPU-Sim `tex_cache` 类  
> 源码位置: `src/gpgpu-sim/gpu-cache.h:1751-1943`, `gpu-cache.cc:2021-2164`  
> 论文依据: Igehy, Eldridge, Proudfoot. "Prefetching in a Texture Cache Architecture." *SIGGRAPH/Eurographics Workshop on Graphics Hardware*, 1998.  
> 调研日期: 2026-05-26  
> 核心问题: **Texture Cache 是阻塞 (blocking) 还是非阻塞 (non-blocking) 的？**

---

## 一、概念定义：什么是 Non-Blocking Cache

在深入分析之前，先明确本文使用的定义（源自 Kroft 1981 "Lockup-Free Cache"）：

> **Non-Blocking (Lockup-Free) Cache**: 当缓存**没有空闲的 cache line 可以分配**时（即一个 set 中所有 way 都已被占用且无法立刻驱逐），仍然能够接受新的 miss 请求——即支持 **miss-on-miss**（多个 miss 同时在飞行中）。

严格意义上，这要求缓存在面对 "set 内所有 way 均处于 RESERVED 状态（等待 fill）" 时，不返回 RESERVATION_FAIL，而是继续接受新请求。

在这个定义下：

- **Miss-on-miss 支持**: ✅ 有。ROB 支持多个未完成 miss 同时飞行。
- **Set 满时仍不阻塞**: ⚠️ **几乎不会发生**。原因见下文。

## 二、结论：Texture Cache 实际上是一个"伪 Non-Blocking" Cache

**GPGPU-Sim 的 Texture Cache 在效果上是 non-blocking 的**，但这归功于它的 "立即 fill" 策略而非结构上的 non-blocking 设计。

### 2.1 为什么几乎不阻塞

在 `access()` 的 MISS 路径中（`gpu-cache.cc:2021-2061`）：

```cpp
m_tags.access(block_addr, time, cache_index, mf);   // ① 分配行 → RESERVED
m_rob.push(rob_entry(cache_index, mf, block_addr));  // ② ROB 登记
m_tags.fill(cache_index, time, mf);                  // ③ 立即 fill → VALID!
m_request_fifo.push(mf);                             // ④ 排队发请求
```

关键在第 ③ 步：`m_tags.fill()` **在同一个 `access()` 调用内**将行从 RESERVED 转为 VALID。**RESERVED 的时间窗口为 0 周期**。因此 GPGPU-Sim `tag_array::probe()` 中的 `all_reserved` 检查（判断所有 way 是否都处于 RESERVED 状态）几乎永远不会触发——因为 way 在 `access()` 返回前就已经 VALID 了。

### 2.2 真正的阻塞点：FIFO 背压

tex_cache 的实际阻塞点是 FIFO 容量：

```cpp
if (m_fragment_fifo.full() || m_request_fifo.full() || m_rob.full())
    return RESERVATION_FAIL;
```

这是**结构性冒险 (structural hazard)**——资源（FIFO 槽位）不足，而非 "等待某个 miss 完成"。调用方下周期重试即可。

### 2.3 证据链

1. **Hit-under-miss**: tag 命中后请求立即入队 `fragment_fifo`，不会因之前的 miss 未完成而被阻塞。tag 在 `access()` 中即刻标记 VALID，后续对同地址的访问直接命中。

2. **Miss-under-miss**: ROB 可以容纳多个未完成的 miss。每个 miss 在 ROB 中分配一个独立条目，多个 miss 可以同时在飞行中。ROB 容量 = `m_rob_entries`。

3. **Set 满时不阻塞（事实上的）**: tag 在 access() 返回前已从 RESERVED→VALID，`all_reserved` 条件永假。唯一的阻塞来自 FIFO 满（资源不足）。

4. **异步完成**: 请求从 `access()` 起就进入 FIFO 管线，完成通过 `access_ready()` / `next_access()` 异步通知。

---

## 二、设计哲学与核心理念

### 2.1 论文依据

GPGPU-Sim 的 texture cache 直接实现自 Igehy et al. 1998 的论文《Prefetching in a Texture Cache Architecture》。论文的核心洞察是：

> 纹理映射的访存模式具有**极强的空间局部性**（相邻像素访问相邻纹理坐标），传统的 CPU cache 模型（MSHR + miss queue）对这种模式不够高效。纹理缓存需要一个专门的流水线架构，利用预取 (prefetching) 来隐藏内存延迟。

论文提出的架构在六种场景、四种内存系统下测试，即使在高延迟内存上也能达到**零延迟内存 97% 以上的性能**。

### 2.2 核心设计原则

从代码分析中可以提取出以下设计原则：

**原则 1: 访问与数据返回解耦 (Decoupled Access-Return)**

```
access() 阶段（同步）            return 阶段（异步）
┌─────────────────────┐          ┌─────────────────────┐
│ tag lookup + allocate │          │ fragment_fifo 排出   │
│ → MISS: ROB + fill    │   ──→    │ → result_fifo        │
│ → HIT: 直接入队       │          │ → access_ready()     │
└─────────────────────┘          └─────────────────────┘
```

`access()` **永远不返回 HIT**。即使 tag 命中，数据也需要经过 `fragment_fifo` 排队才能在 `result_fifo` 中可用。这是因为纹理单元本身有流水线延迟——tag 查找和实际数据读取不在同一周期。

**原则 2: 全 FIFO 管线 (All-FIFO Pipeline)**

缓存的所有状态都保存在 4 个 FIFO 中。没有随机访问的数据结构（除了 tag array 本身），没有链表遍历，没有复杂的状态机。这使得硬件实现可以达到很高的时钟频率。

**原则 3: 顺序提交，乱序完成，顺序输出**

- **顺序提交**: 所有请求按 `fragment_fifo` 入队顺序处理
- **乱序完成**: ROB 中的 miss 可以以任意顺序从内存返回
- **顺序输出**: `result_fifo` 保证结果按程序顺序返回给 shader core

ROB 的存在使得 "顺序提交 → 乱序完成 → 顺序输出" 成为可能。这与 CPU 的 Tomasulo 算法中的 ROB 设计理念相同，但实现极为简化。

**原则 4: 即时分配 + 提前填充 (Eager Allocation + Early Fill)**

在 `access()` 的 MISS 路径中：
```cpp
m_tags.access(block_addr, time, cache_index, mf);   // 1. 分配行
m_rob.push(rob_entry(cache_index, mf, block_addr));  // 2. ROB 登记
m_tags.fill(cache_index, time, mf);                  // 3. 立即标记 valid!
m_request_fifo.push(mf);                             // 4. 排队发送请求
```

关键在第 3 步：tag array 在**发出内存请求的同时**就被 fill 为 VALID。这意味着：
- 后续对同一缓存行的访问**立即命中**（返回 HIT_RESERVED）
- 不需要等待数据真正从内存回来
- 这是一种乐观的提前分配策略

这与 `baseline_cache` 的 ON_FILL 策略形成对比——后者必须等待 fill 响应才分配。纹理缓存的 "先 fill 再发请求" 策略更适合纹理访问的空间局部性（预取友好）。

---

## 三、微架构详解

### 3.1 整体数据流

```
                    access(addr, mf)
                         │
                    ┌────▼────┐
                    │ FIFO满?  │──YES──→ RESERVATION_FAIL (调用方下周期重试)
                    └────┬────┘
                         │NO
                    ┌────▼────┐
                    │ tag_array│
                    │ .access()│  ← 分配缓存行，返回 HIT 或 MISS
                    └────┬────┘
                         │
              ┌──────────┴──────────┐
              │                     │
           [HIT]                 [MISS]
              │                     │
              │                ┌────▼────┐
              │                │ ROB.push│  ← 在重排序缓冲区登记
              │                └────┬────┘
              │                     │
              │                ┌────▼────┐
              │                │ tag.fill│  ← 立即标记 valid! (不等数据)
              │                └────┬────┘
              │                     │
              │                ┌────▼────┐
              │                │ req_fifo│  ← 排队发送内存请求
              │                │ .push() │
              │                └────┬────┘
              │                     │
              └──────────┬──────────┘
                         │
                    ┌────▼────┐
                    │ fragment│  ← 所有请求都在此排队
                    │ _fifo   │     等待按序输出
                    │ .push() │
                    └────┬────┘
                         │
                    ══════╪═══════  access() 返回 HIT_RESERVED 或 MISS
                          │
                    ══════╪═══════  cycle() 每周期执行
                          │
              ┌──────────┴──────────┐
              │   fragment_fifo     │
              │   .peek()           │
              └──────────┬──────────┘
                         │
              ┌──────────┴──────────┐
              │                     │
          [HIT条目]            [MISS条目]
              │                     │
              │                ┌────▼────┐
              │                │ ROB头部  │  ← 检查数据是否已从内存返回
              │                │ .ready? │
              │                └────┬────┘
              │                     │
              │                ┌────▼────┐
              │                │ m_cache │  ← 标记 data_block 为 valid
              │                │ [idx]   │
              │                │ .valid  │
              │                │ = true  │
              │                └────┬────┘
              │                     │
              └──────────┬──────────┘
                         │
                    ┌────▼────┐
                    │ result  │  ← 就绪结果在此等待
                    │ _fifo   │
                    │ .push() │
                    └────┬────┘
                         │
                    ══════╪═══════  access_ready() 返回 true
                          │
                    ══════╪═══════  shader_core 调用 next_access()
                          │
                    ┌────▼────┐
                    │ mf 返回 │  ← 原始 mem_fetch 返回给 shader core
                    │ 给调用方 │
                    └─────────┘
```

### 3.2 四大 FIFO 详解

| FIFO | 容量配置字段 | 元素类型 | 语义 | 入队时机 | 出队时机 |
|------|------------|---------|------|---------|---------|
| `m_fragment_fifo` | `m_fragment_fifo_entries` | `fragment_entry{mf, idx, is_miss, size}` | 所有被接受的请求的**顺序化队列**——保证按程序顺序输出结果 | `access()` 末尾（每次 access 必入队） | `cycle()` 中头部条目就绪时 |
| `m_request_fifo` | `m_request_fifo_entries` | `mem_fetch*` | 待发送到下级存储的**内存请求队列** | `access()` MISS 路径 | `cycle()` 中 `m_memport->push()` 成功时 |
| `m_rob` | `m_rob_entries` | `rob_entry{ready, time, index, mf, block_addr}` | **重排序缓冲区**——允许 miss 乱序完成但结果顺序输出 | `access()` MISS 路径 | `cycle()` 中头部条目 `m_ready==true` 且 `fragment_fifo` 头部为对应 miss 条目时 |
| `m_result_fifo` | `m_result_fifo_entries` | `mem_fetch*` | 已完成的请求**就绪队列**——供 shader core 取走 | `cycle()` 中 HIT 条目或 MISS 条目 ROB ready | shader core 调用 `next_access()` 时 |

### 3.3 tag_array 的独特用法

纹理缓存使用 `tag_array` 的方式与数据缓存有本质区别：

| 操作 | baseline_cache | tex_cache |
|------|---------------|-----------|
| `probe()` | 在 `access()` 前调用，检查命中 | 不调用 |
| `access()` | 返回 HIT / MISS / HIT_RESERVED / RESERVATION_FAIL | **仅返回 HIT 或 MISS**（永远不会 RESERVATION_FAIL，因为 FIFO 满已在外部检查） |
| `fill()` | 在 `fill()` 回调中调用（数据从内存返回后） | **在 `access()` 中就调用**——不等数据返回！ |
| 替换策略 | LRU/FIFO 正常运作 | LRU/FIFO 正常运作（通过 `access()` 的分配路径） |

关键行为：`m_tags.fill(cache_index, time, mf)` 在 `access()` 的 MISS 路径中被调用——**在发送内存请求之前**就把 tag 标记为 VALID。这意味着一行可能处于 "tag valid 但 data_block 尚未 valid" 的中间状态。`m_cache[]`（`data_block` 数组）独立追踪数据是否真正可用。

### 3.4 data_block vs CacheBlock

纹理缓存不使用 `CacheBlock`（INVALID→RESERVED→VALID→MODIFIED 四状态机），而是使用极简的 `data_block`：

```cpp
struct data_block {
    data_block() { m_valid = false; }
    bool m_valid;                // 仅 1 bit: 数据是否有效
    new_addr_type m_block_addr;  // 块地址（用于断言校验）
};
```

**为什么？** 纹理缓存是只读的（没有 MODIFIED 状态），不需要追踪 dirty 信息。而且纹理缓存的 tag 和 data 是**分开管理的**——tag array 追踪哪些行被分配了，`data_block` 追踪哪些行的数据真正到位了。这种分离使得 "先 fill tag，后等数据" 的策略成为可能。

---

## 五、Blocking vs Non-Blocking 基于统一定义的分析

### 5.1 统一定义

按照 Kroft 1981 的定义，**non-blocking cache** 的核心判据是：

> 当 set 内没有空闲的 cache line 可分配时，缓存是否仍能接受新的 miss？

具体到代码层面：`tag_array::probe()` 中的 `all_reserved` 检查——当 set 内所有 way 都是 RESERVED（或受保护的脏行）时，是否返回 `RESERVATION_FAIL`。

### 5.2 tex_cache：事实上的 non-blocking

tex_cache 在 `access()` 中调用 `m_tags.access()` 后**立即**调用 `m_tags.fill()`。行在同一个 `access()` 调用中完成 RESERVED→VALID 转换，RESERVED 窗口为 **0 周期**。因此 `all_reserved` 条件几乎永不触发。

tex_cache 的阻塞点不是 "没有 cache line"，而是 FIFO 资源耗尽：
```cpp
if (m_fragment_fifo.full() || m_request_fifo.full() || m_rob.full())
    return RESERVATION_FAIL;
```

### 5.3 baseline_cache (L1D/L2)：结构性非阻塞

baseline_cache 的行从 `access()` 分配到 `fill()` 回调之间可能处于 RESERVED 状态**数十周期**。在高并发 miss 场景下，一个 set 的所有 way 可能同时变为 RESERVED，触发 `all_reserved` → `RESERVATION_FAIL`。

但这**不是** "一次只能处理一个 miss" 的阻塞缓存设计。MSHR 的多条目支持允许多个 miss 同时飞行（不同地址或不同 set）。阻塞仅发生在**同一个 set 的资源耗尽**时——这是结构性资源限制，不是设计哲学上的阻塞。

### 4.1 blocking cache 的定义

一个严格的 blocking cache 在发生 miss 时会：
1. 停止接受新的请求
2. 等待 miss 的数据从内存返回并写入缓存
3. 然后才恢复接受请求

GPGPU-Sim 没有实现纯 blocking cache，但 `baseline_cache` 的 ON_FILL 策略有类似的阻塞行为——必须在 fill 完成后才能处理同一缓存行的后续请求。

### 4.2 tex_cache 的非阻塞特性

tex_cache 满足非阻塞 cache 的三个关键条件：

**条件 1: Hit-under-miss**

```
周期 0: access(addr_A) → MISS  → fragment_fifo=[A], ROB=[A], request_fifo=[A]
周期 1: access(addr_A) → HIT   → fragment_fifo=[A,A]  ← 注意: 立即命中！
周期 2: access(addr_B) → MISS  → fragment_fifo=[A,A,B], ROB=[A,B], request_fifo=[A,B]
```

在周期 1，即使 addr_A 的数据尚未从内存返回，对 addr_A 的第二次访问仍然命中（返回 HIT_RESERVED）。这是因为 tag 已在周期 0 的 `access()` 中被标记为 VALID。

**条件 2: Miss-under-miss**

```
周期 0: access(addr_A) → MISS → ROB=[A], request_fifo=[A]
周期 1: access(addr_B) → MISS → ROB=[A,B], request_fifo=[A,B]
周期 2: access(addr_C) → MISS → ROB=[A,B,C], request_fifo=[A,B,C]
```

三个 miss 同时在飞行中，受限于 ROB 容量（`m_rob_entries`）。每个 miss 在 ROB 中有独立条目，内存响应可以乱序返回。

**条件 3: 无 head-of-line 阻塞**

```
ROB=[A,B,C]（入队顺序）
内存返回顺序: C → A → B（乱序！）

fill(C): ROB[2].ready = true  ← C 的数据先到
fill(A): ROB[0].ready = true  ← A 的数据后到
fill(B): ROB[1].ready = true  ← B 的数据最后到

cycle():
  fragment_fifo头部 = A → ROB[0].ready? yes → result_fifo.push(A) → pop A
  fragment_fifo头部 = A → ... → result_fifo.push(A') → pop A'
  fragment_fifo头部 = B → ROB[1].ready? yes → result_fifo.push(B) → pop B
  fragment_fifo头部 = C → ROB[2].ready? yes → result_fifo.push(C) → pop C
```

即使 C 的数据最先从内存返回，`fragment_fifo` 和 ROB 的协作确保了**结果按访问顺序输出**（A→A'→B→C）。C 的 ROB 条目即使 ready 了也不会被处理，直到 `fragment_fifo` 头部推进到 C。

**这就是非阻塞 cache 的本质特征：允许 miss 乱序完成，但保证结果顺序输出。**

### 4.3 唯一的阻塞点：FIFO 背压

全部四个 FIFO 中任意一个满了，`access()` 就返回 `RESERVATION_FAIL`：

```cpp
if (m_fragment_fifo.full() || m_request_fifo.full() || m_rob.full())
    return RESERVATION_FAIL;
```

这是**结构性冒险 (structural hazard)**，不是阻塞 cache 意义上的阻塞。它的语义是"本周期无法接受请求，请下周期重试"，而不是"等待某个 miss 完成才能继续"。

在 GPGPU-Sim 的实际运行中，shader core 的 `process_memory_access_queue()` 收到 `RESERVATION_FAIL` 后会 stall 该 warp，下周期再试：
```cpp
mem_stage_stall_type fail = process_memory_access_queue(m_L1T, inst);
if (fail != NO_RC_FAIL) {
    rc_fail = fail;  // 导致 warp stall，下周期重试
}
```

### 4.4 与 baseline_cache 的对比

| 特性 | baseline_cache (数据缓存) | tex_cache (纹理缓存) |
|------|--------------------------|---------------------|
| **类型** | Non-blocking (MSHR 合并) | Non-blocking (FIFO + ROB) |
| **Hit-under-miss** | 有限 (同一行通过 MSHR 合并) | 完全支持 (tag 立即可用) |
| **Miss-under-miss** | MSHR 条目数限制 | ROB 条目数限制 |
| **顺序保证** | 无 (调用方处理) | 严格 (fragment_fifo + ROB 保证) |
| **请求间干扰** | 可能 (MSHR 满导致 RESERVATION_FAIL) | 可能 (FIFO 满导致 RESERVATION_FAIL) |
| **写支持** | 是 | 否 (READ_ONLY) |
| **替换策略** | LRU/FIFO，通过 tag_array 标准路径 | LRU/FIFO，通过 tag_array 同路径 |
| **数据存储** | CacheBlock (4 状态 + dirty) | data_block (1 bit valid) |

---

## 五、关键设计细节

### 5.1 为什么 access() 永远不返回 HIT？

```cpp
/// NOTE: *never* returns HIT
/// since unlike a normal CPU cache, a "HIT" in texture cache does not
/// mean the data is ready (still need to get through fragment fifo)
```

核心原因：纹理流水线有自己的内部延迟。即使 tag array 命中，实际从数据 SRAM 读出数据也需要 1-N 个周期。`fragment_fifo` 模拟了这个流水线延迟——所有请求（包括命中的）都必须经过它。

这是 texture cache 与 data cache 在 API 层面的根本差异。在 data cache 中，`access()` 返回 HIT 意味着数据立即可用；在 texture cache 中，任何请求的结果都只能通过 `access_ready()`/`next_access()` 异步获取。

### 5.2 双缓存：tag_array vs m_cache[]

tex_cache 维护了**两套并行的缓存数据**：

1. **`m_tags` (tag_array)**: 完整的 CacheBlock 状态机，追踪哪些地址被分配、LRU/FIFO 替换状态。行可以处于 INVALID / RESERVED / VALID / MODIFIED 状态。

2. **`m_cache[]` (data_block 数组)**: 极简的 1-bit valid 标记。行只有 valid 或 invalid。

两者的索引一一对应（都是 `cache_index`），但生命周期不同：

```
操作      m_tags 状态         m_cache[] 状态
─────────────────────────────────────────────────
初始       INVALID             invalid
access()   RESERVED → VALID    仍为 invalid  ← tag 已 valid，数据还未到
fill()     保持 VALID          → valid       ← 数据到达
后续访问   HIT (tag 返回)      用于断言校验   ← 双重验证
```

`cycle()` 中有一个关键的断言：
```cpp
// HIT 条目处理:
assert(m_cache[e.m_cache_index].m_valid);
assert(m_cache[e.m_cache_index].m_block_addr ==
       m_config.block_addr(e.m_request->get_addr()));
```

这个断言验证了 tag 和 data 的一致性——如果 tag 说 HIT，data_block 必须已经 valid。

### 5.3 SECTOR_TEX_FIFO 的扇区处理

当配置为 `SECTOR_TEX_FIFO` 时，纹理缓存发送的是扇区级请求：

```cpp
// access() 中:
mf->set_data_size(m_config.get_line_sz());  // 请求大小改为整行

// fill() 中:
if (m_config.m_mshr_type == SECTOR_TEX_FIFO) {
    e->second.pending_read--;
    if (e->second.pending_read > 0) {
        delete mf;      // 尚未收齐所有扇区响应，丢弃当前扇区
        return;
    } else {
        mf = mf->get_original_mf();  // 所有扇区到齐，恢复到原始请求
        delete temp;                 // 删除最后一个扇区响应
    }
}
```

流程是：
1. `access()` 中将请求的 data_size 改为整行大小
2. 下级存储将行请求拆分为 N 个扇区请求（N = line_size / sector_size）
3. 每个扇区响应到达时 `pending_read--`
4. 最后一个扇区到达后，用 `get_original_mf()` 恢复到原始请求

**注意**: `pending_read` 的初值是 `m_config.m_line_sz / SECTOR_SIZE`（不是 openCache v2 修复后的 1）。这在 GPGPU-Sim 中是正确的，因为它的下级存储确实会发送 N 个扇区响应。

### 5.4 FIFO 容量的配置

在 `cache_config` 中使用 union 实现 MSHR 参数与纹理 FIFO 参数的复用：

```cpp
// gpu-cache.h:876-889
union { unsigned m_mshr_entries; unsigned m_fragment_fifo_entries; };
union { unsigned m_mshr_max_merge; unsigned m_request_fifo_entries; };
union { unsigned m_miss_queue_size; unsigned m_rob_entries; };
```

| 配置字段 | 在 data_cache 中的含义 | 在 tex_cache 中的含义 |
|---------|----------------------|---------------------|
| `m_mshr_entries` | MSHR 条目数 | `m_fragment_fifo_entries` |
| `m_mshr_max_merge` | MSHR 合并上限 | `m_request_fifo_entries` |
| `m_miss_queue_size` | Miss queue 容量 | `m_rob_entries` |
| `m_result_fifo_entries` | (仅 tex_cache 使用) | `m_result_fifo_entries` |

---

## 六、与标准 CPU Cache 模型的对比

| 维度 | 标准 CPU Non-Blocking Cache | GPGPU-Sim tex_cache | GPGPU-Sim baseline_cache |
|------|---------------------------|--------------------|------------------------|
| 未命中追踪 | MSHR (Miss Status Holding Register) | ROB (Reorder Buffer) | MSHR |
| 合并机制 | MSHR 合并同地址请求 | 无合并（每个请求独立 ROB 条目） | MSHR 合并同地址请求 |
| 顺序保证 | 无 (依赖 memory model) | 严格 (fragment_fifo + ROB) | 无 |
| 数据存储 | 统一 CacheBlock | tag_array + data_block 分离 | 统一 CacheBlock |
| 流水线 | 无内建流水线 | 4 级 FIFO 流水线 | 无内建流水线 |
| fill 时机 | 数据返回后 fill | 在 access() 中预 fill | 数据返回后 fill |
| 论文依据 | Kroft 1981 (MSHR) | Igehy et al. 1998 | 标准 CPU cache 模型 |

---

## 七、参考文献

1. **Igehy, H., Eldridge, M., & Proudfoot, K.** (1998). "Prefetching in a Texture Cache Architecture." *Proceedings of the 1998 Eurographics/SIGGRAPH Workshop on Graphics Hardware*, pp. 133–142. DOI: `10.1145/285305.285321`. Stanford FLASH Graphics Architecture Project. [论文主页](http://www-graphics.stanford.edu/papers/texture_prefetch/)

2. **GPGPU-Sim 源码**: `src/gpgpu-sim/gpu-cache.h` lines 1748-1943, `gpu-cache.cc` lines 2016-2164. 注释明确引用上述论文。

3. **Kroft, D.** (1981). "Lockup-Free Instruction Fetch/Prefetch Cache Organization." *ISCA 1981*. — MSHR (Miss Status Holding Register) 概念的原始论文，用于对比分析。

---

## 八、对 openCache 的启示

1. **tex_cache 不应该继承 BaselineCache**。两者虽然都用 `tag_array`，但数据流完全不同。tex_cache 采用 FIFO+ROB 架构，BaselineCache 采用 MSHR+miss_queue 架构。

2. **如果要为 openCache 实现一个通用纹理缓存**，建议的架构是：
   - 继承 `CacheMemoryInterface`（不继承 `BaselineCache`）
   - 复用 `TagArray` 和 `CacheStats`
   - 独立实现 `FifoPipeline`（`fragment_fifo` + `request_fifo` + `rob` + `result_fifo`）
   - 独立实现 `data_block` 数组
   - 实现 `access() → HIT_RESERVED|MISS|RESERVATION_FAIL`
   - 实现异步的 `cycle()` + `access_ready()` + `next_access()`

3. **核心差异永不消失**：纹理缓存的 "所有请求经过 FIFO" + "预 fill tag" + "ROB 重排" 的设计哲学与数据缓存的 "MSHR 合并" + "fill 后 valid" 哲学有本质区别。强行统一只会增加复杂度，不会带来收益。
