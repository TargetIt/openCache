# GPGPU-Sim L1 / L2 Data Cache 微架构深度调研

> 调研对象: GPGPU-Sim `l1_cache` / `l2_cache` → `data_cache` → `baseline_cache`  
> 源码位置: `gpu-cache.h:1280-1749`, `gpu-cache.cc:1215-2017`, `l2cache.cc:465-590`, `shader.cc:2043-2230`  
> 调研日期: 2026-05-26  
> 核心问题: **L1 和 L2 的微架构是什么？它们是阻塞还是非阻塞的？它们之间有什么差异？**

---

## 一、结论先行

### 1.1 类型判定

| 缓存 | 类型 | 关键机制 |
|------|------|---------|
| **L1 Data Cache** | **Non-Blocking** | MSHR 合并 + miss_queue + bank 延迟队列 |
| **L2 Cache** | **Non-Blocking** | MSHR 合并 + FIFO 管线（icnt_L2_queue ↔ L2_dram_queue ↔ dram_L2_queue） |
| **L1 指令/常量缓存** | **Non-Blocking** | 同 baseline_cache，MSHR + miss_queue |

三个缓存都是 **non-blocking** 的。它们的核心机制是 **MSHR (Miss Status Holding Register)** + **miss_queue**——允许多个 miss 同时在飞行中，每个 miss 在 MSHR 中有独立条目。

### 1.2 与 Texture Cache 的本质区别

| 维度 | L1D / L2 (baseline_cache 体系) | L1T (tex_cache) |
|------|-------------------------------|-----------------|
| 未命中追踪 | MSHR（哈希表，按地址合并） | ROB（FIFO，不合并） |
| 地址合并 | **是** — 同地址多请求合并到同一 MSHR 条目 | **否** — 每个请求独立 ROB 条目 |
| 输出顺序 | 无保证（MSHR ready 后即返回） | 严格保证（fragment_fifo + ROB） |
| 状态机 | CacheBlock 四状态 (I→R→V→M) | data_block 单 bit valid |
| 写支持 | 是（write-back/through/evict） | 否（READ_ONLY） |
| 替换策略 | LRU / FIFO（完整） | LRU / FIFO（同机制但预 fill tag） |

---

## 二、L1 Data Cache 微架构

### 2.1 类继承与职责

```
cache_t                         ← 3 个纯虚函数: access / data_port_free / fill_port_free
└── baseline_cache              ← 公共数据缓存基类
    ├── tag_array               ← 标签阵列
    ├── mshr_table              ← MSHR
    ├── m_miss_queue            ← 未命中队列 (list<mem_fetch*>)
    ├── bandwidth_management    ← 带宽管理 (data_port / fill_port)
    ├── extra_mf_fields_lookup  ← mem_fetch 附加追踪
    └── cache_stats             ← 统计
    └── data_cache              ← 读写缓存：函数指针分派 4×写命中 + 4×写缺失 + 2×读
        └── l1_cache            ← [仅改 m_wr_alloc_type=L1_WR_ALLOC_R, m_wrbk_type=L1_WRBK_ACC]
```

L1D 在 `ldst_unit` 构造函数中创建（`shader.cc:2617`）:
```cpp
m_L1D = new l1_cache(L1D_name, m_config->m_L1D_config, m_sid,
    get_shader_normal_cache_id(), m_icnt, m_mf_allocator,
    IN_L1D_MISS_QUEUE, core->get_gpu(), L1_GPU_CACHE);
```

### 2.2 请求处理流水线

```
[Warp Scheduler 发射内存指令]
        │
        ▼
[accessq (per-warp 访问队列)]  ← warp_inst_t 中的多个内存请求
        │
        ▼
[process_memory_access_queue_l1cache()]  ← shader.cc:2062
        │
        ├── l1_latency > 0?   YES →
        │   │
        │   ├── 请求入 l1_latency_queue[bank][latency-1]  ← 延迟流水线尾部
        │   ├── SECTOR_ASSOC → inc_store_req(data_size/SECTOR_SIZE)
        │   └── accessq.pop_back()
        │
        └── l1_latency == 0?  直接调用:
            mf = m_mf_allocator->alloc(inst, access)
            cache->access(mf->get_addr(), mf, time, events)
            process_cache_access(cache, addr, inst, events, mf, status)
```

**L1_latency_queue_cycle()** — 每周期从延迟队列头部取出请求执行 access():

```
周期 N:   请求在 l1_latency_queue[bank][latency-1] (尾部)
周期 N+1: 请求滑到 l1_latency_queue[bank][latency-2]
...
周期 N+latency-1: 请求滑到 l1_latency_queue[bank][0] (头部)
周期 N+latency:  L1_latency_queue_cycle() 检测头部非空 → 调用 cache->access()
```

这个移位寄存器模拟了 **L1 SRAM 的读延迟**（数据从 bank 读出需要 N 个周期，与 hit/miss 无关）。每个 bank 有独立的延迟队列（大小 = `l1_latency × l1_banks`）。

### 2.3 access() 内部流程

```
DataCache::access(mf)
  │
  ├─ WRITE_BACK? → m_tag_array->fill() → return HIT
  │
  ├─ m_tag_array->probe(addr)  ← 只读探针
  │
  └─ process_tag_probe(is_write, probe, mf, time, events)
       │
       ├─ [写命中] → (this->*m_wr_hit)(...)
       │   ├─ WRITE_BACK:   set_status(MODIFIED) + inc_dirty + update LRU
       │   ├─ WRITE_THROUGH: miss_queue_full check → MODIFIED + send WRITE_REQUEST
       │   ├─ WRITE_EVICT:  miss_queue_full check → INVALID + send WRITE_REQUEST
       │   └─ LOCAL_WB_GLOBAL_WT: check GLOBAL_ACC_W → WE/WB dispatch
       │
       ├─ [写缺失] → (this->*m_wr_miss)(...)
       │   ├─ NO_WR_ALLOC:        send WRITE_REQUEST only
       │   ├─ WRITE_ALLOC_NAIVE:  send WRITE + send READ (MSHR)
       │   ├─ FETCH_ON_WRITE:     整行写→直接MODIFIED / 部分写→MSHR + RAW防危
       │   └─ LAZY_FETCH_ON_READ: allocate + MODIFIED + set_readable(false)
       │
       ├─ [读命中] → (this->*m_rd_hit)(...)
       │   └─ update last_access_time + LRU promote
       │
       └─ [读缺失] → (this->*m_rd_miss)(...)
           └─ send_read_request() → MSHR 合并 → miss_queue 入队
```

### 2.4 关键数据结构

**tag_array** — 每个缓存行有完整的 `CacheBlock` 状态机:

```
INVALID → (allocate) → RESERVED → (fill) → VALID → (write hit) → MODIFIED
  ↑                                                      │
  └──────────── (evict with writeback) ←─────────────────┘
```

**MSHR (mshr_table)** — 哈希表，按 `mshr_addr` 索引:

```
m_data: unordered_map<addr_t, mshr_entry>
  └── mshr_entry: { list<mem_fetch*> m_list; bool m_has_atomic; }

MSHR 合并规则:
- 同 mshr_addr 的多个请求合并到同一 MSHR 条目 (m_list 增长)
- 合并上限: m_max_merged (配置参数)
- 新地址: 需要新的 MSHR 条目 (受 m_num_entries 限制)
- mark_ready() 后所有合并请求一起返回 (m_current_response)
```

**m_miss_queue** — `list<mem_fetch*>`，存储等待发送到下级存储的请求:

```
cycle():
  if (!m_miss_queue.empty() && m_memport->can_accept())
    m_memport->push(m_miss_queue.front())
    m_miss_queue.pop_front()
```

**bandwidth_management** — 数据端口和填充端口的占用追踪:

```
use_data_port():  data_size / port_width → m_data_port_occupied_cycles
use_fill_port():  atom_size / port_width  → m_fill_port_occupied_cycles
replenish():      每周期 -1，归零时标记 available
data_port_free(): occupied == 0
fill_port_free(): occupied == 0
```

### 2.5 从 L1 miss 到 L2 的流程

```
1. DataCache::access() 返回 MISS
   → send_read_request() 将 mf 推入 m_miss_queue
   → events 中包含 READ_REQUEST_SENT

2. shader.cc: process_cache_access() 检测到 read_sent
   → inst.accessq_pop_back() (标记请求已发出)
   → mf 留在 miss_queue 中

3. baseline_cache::cycle() 被调用 (shader.cc:2901)
   → m_miss_queue.front() 出队
   → m_memport->push(mf)  ← m_memport 是 shader_memory_interface

4. shader_memory_interface (shader.h:2734) 实现 mem_fetch_interface:
   push(mf) → ::icnt_push(cluster_id, destination, mf, ...)
   → 互联网络将 mf 路由到目标 memory_partition

5. memory_sub_partition 收到 mf:
   push(mf) → SECTOR? → breakdown_request_to_sector_requests()
           → ROP delay queue → m_icnt_L2_queue

6. L2 cache_cycle():
   → m_L2cache->access(mf->get_addr(), mf, time, events)
   → HIT: m_L2_icnt_queue.push(mf) → icnt → 回到 shader_core
   → MISS: m_L2_dram_queue.push(mf) → DRAM
```

### 2.6 Writeback 流程

```
1. write-miss handler (如 wr_miss_wa_fetch_on_write) 中:
   m_tag_array->access(block_addr, time, true, wb, evicted)
   → wb=true 表示驱逐了脏行
   
2. handler 检测到 wb:
   CacheRequest wb_req = create_writeback(evicted)
   send_write_request(wb_req, WRITE_BACK_REQUEST_SENT, events)
   → wb_req 推入 m_miss_queue

3. shader.cc: process_cache_access() 检测到 write_sent:
   → m_core->inc_store_req() 增加 store ack 计数

4. cycle() 中 miss_queue 排空:
   → m_memport->push(wb_req) → icnt → memory_partition
   
5. memory_partition 收到 L1_WRBK_ACC:
   → 如果是 L2 hit: 标记 MODIFIED + delete mf (吸收写回)
   → 如果是 L2 miss: 继续向下发到 DRAM
```

---

## 三、L2 Cache 微架构

### 3.1 类继承与职责

```
cache_t → baseline_cache → data_cache → l2_cache
```

与 L1D 完全相同的内核（`data_cache`），构造时设置:
```cpp
l2_cache(...) : data_cache(..., L2_WR_ALLOC_R, L2_WRBK_ACC, gpu, level) {}
```

### 3.2 在 Memory Partition 中的位置

```
memory_partition_unit
│
├── m_dram (DRAM 控制器)
│
└── memory_sub_partition[0..N]  ← 每个 sub-partition 有独立 L2 实例
    │
    ├── m_L2cache (l2_cache)              ← 核心缓存逻辑
    ├── m_icnt_L2_queue (FIFO)            ← 互联网络 → L2
    ├── m_L2_dram_queue (FIFO)            ← L2 miss → DRAM
    ├── m_dram_L2_queue (FIFO)            ← DRAM fill → L2
    ├── m_L2_icnt_queue (FIFO)            ← L2 响应 → 互联网络
    ├── m_rop (queue<rop_delay_t>)        ← ROP 延迟队列
    ├── m_mf_allocator                     ← mem_fetch 创建器
    └── m_request_tracker (set<mem_fetch*>) ← 飞行中请求追踪
```

### 3.3 L2 cache_cycle() 详细流程

```cpp
void memory_sub_partition::cache_cycle(unsigned cycle) {
    // ===== 步骤 1: L2 fill 响应处理 =====
    if (m_L2cache->access_ready() && !m_L2_icnt_queue->full()) {
        mem_fetch *mf = m_L2cache->next_access();
        if (mf->get_access_type() != L2_WR_ALLOC_R) {
            mf->set_reply();
            m_L2_icnt_queue->push(mf);  // 返回给互联网络 → shader core
        } else {
            // WRITE_ALLOC_R: 不回传，改为回传原始写请求
            original_wr_mf->set_reply();
            m_L2_icnt_queue->push(original_wr_mf);
            delete mf;
        }
    }

    // ===== 步骤 2: DRAM → L2 填充 =====
    if (!m_dram_L2_queue->empty()) {
        mem_fetch *mf = m_dram_L2_queue->top();
        if (m_L2cache->waiting_for_fill(mf)) {  // L2 是否在等这个 fill
            if (m_L2cache->fill_port_free()) {
                m_L2cache->fill(mf, time);      // 数据写入 L2
                m_dram_L2_queue->pop();
            }
        } else if (!m_L2_icnt_queue->full()) {
            // 不是 L2 的 fill（可能绕过了 L2）
            m_L2_icnt_queue->push(mf);
            m_dram_L2_queue->pop();
        }
    }

    // ===== 步骤 3: L2 cycle =====
    m_L2cache->cycle();  // 排空 miss_queue

    // ===== 步骤 4: 互联网络 → L2 新请求 =====
    if (!m_L2_dram_queue->full() && !m_icnt_L2_queue->empty()) {
        mem_fetch *mf = m_icnt_L2_queue->top();
        if (port_free && !output_full) {
            status = m_L2cache->access(mf->get_addr(), mf, time, events);
            
            if (status == HIT) {
                if (mf->get_access_type() == L1_WRBK_ACC) {
                    delete mf;  // 吸收写回 (L2 命中时写回数据已合并)
                } else {
                    mf->set_reply();
                    m_L2_icnt_queue->push(mf);  // 返回给 shader core
                }
                m_icnt_L2_queue->pop();
            } else if (status != RESERVATION_FAIL) {
                // MISS: mf 已由 send_read_request 推入 miss_queue
                m_icnt_L2_queue->pop();
            } else {
                // RESERVATION_FAIL: 下周期重试
            }
        }
    }

    // ===== 步骤 5: ROP 延迟 → icnt_L2_queue =====
    if (!m_rop.empty() && cycle >= m_rop.front().ready_cycle &&
        !m_icnt_L2_queue->full()) {
        m_icnt_L2_queue->push(m_rop.front().req);
        m_rop.pop();
    }
}
```

### 3.4 扇区拆分 (breakdown_request_to_sector_requests)

当 L2 配置为 SECTOR 类型时，来自 L1 的行请求需要拆分为扇区请求:

```cpp
// l2cache.cc:718-790
vector<mem_fetch*> breakdown_request_to_sector_requests(mem_fetch *mf) {
    if (data_size == SECTOR_SIZE && sector_mask.count() == 1) {
        // 已是单扇区请求 → 不拆分
        return {mf};
    }
    if (data_size == MAX_MEMORY_ACCESS_SIZE) {
        // 完整行请求 (128B) → 拆分为 4 个 32B 扇区请求
        for (i = 0; i < SECTOR_CHUNCK_SIZE; i++) {
            n_mf = alloc(addr + SECTOR_SIZE*i, type, mask, sector_mask.set(i),
                         SECTOR_SIZE, is_write, ...);
            // n_mf->get_original_mf() = mf  ← 关联到原始请求
        }
    }
    // ...其他大小的处理...
}
```

拆分后，每个子扇区请求独立经过 L2 cache pipeline。原始请求 (`mf`) 保存在 `get_original_mf()` 中。当所有扇区响应到齐后（`pending_read` 倒计数到 0），`fill()` 重组原始请求。

### 3.5 L2 的 Writeback 吸收

L2 对来自 L1 的写回请求 (`L1_WRBK_ACC`) 有特殊处理:

```cpp
// cache_cycle() 中:
if (status == HIT) {
    if (mf->get_access_type() == L1_WRBK_ACC) {
        m_request_tracker.erase(mf);
        delete mf;   // ← 吸收 L1 写回! 不返回给 shader core
    } else {
        m_L2_icnt_queue->push(mf);  // 正常返回
    }
}
```

当 L1 写回命中 L2 时，L2 直接将数据标记为 MODIFIED（通过 `wr_hit_wb` handler），然后**删除** `mf`——因为写回的源是 L1，不需要再通知 shader core。

---

## 四、Blocking vs Non-Blocking 深度分析

### 4.1 L1 Data Cache — Non-Blocking

**非阻塞的证据**:

1. **MSHR 支持多 miss 飞行**: `m_num_entries` (典型 32) 个独立的 MSHR 条目，每个可以追踪一个未完成的 miss。这意味着最多 32 个不同地址的 miss 可以同时在飞行中。

2. **MSHR 合并支持 hit-under-miss**: 同一地址的后续访问合并到已有的 MSHR 条目（`m_max_merged` 限制），不产生新的 miss。

3. **miss_queue 异步排空**: `access()` 将请求推入 `m_miss_queue` 后立即返回（MISS 状态）。`cycle()` 异步排空 miss_queue。access() 本身不会被内存延迟阻塞。

4. **bank 延迟队列不阻塞其他 bank**: `l1_latency_queue[bank][stage]` 是 per-bank 的。Bank 0 的延迟队列满了不影响 Bank 1 接受请求。

**可能阻塞的情况（结构性冒险）**:

| 冒险类型 | 检测方式 | 频率 |
|---------|---------|------|
| MSHR 满 | `m_mshrs.full(addr)` → RESERVATION_FAIL | 高并发 miss 时 |
| MSHR 合并满 | `m_mshrs.full(addr)` 对已有条目 → MSHR_MERGE_ENRTY_FAIL | 同地址高频访问时 |
| miss_queue 满 | `miss_queue_full(n)` → MISS_QUEUE_FULL | 下级存储拥塞时 |
| data_port 忙 | `!data_port_free()` → DATA_PORT_STALL | 连续大 burst 时 |
| bank 冲突 | `l1_latency_queue[bank][tail] != NULL` → BK_CONF | 同 bank 连续访问时 |
| RAW 防危 | `is_read_after_write_pending(addr)` → MSHR_RW_PENDING | 写后读同地址时 |

这些全是**结构性冒险**（资源不足），不是阻塞 cache 意义上的 "等待某个 miss 完成"。调用方收到 RESERVATION_FAIL 后 stall 该 warp，下周期重试。

### 4.2 L2 Cache — Non-Blocking

L2 的非阻塞机制与 L1 相同（共享 `baseline_cache` + `data_cache` 代码）。额外的特性：

1. **FIFO 管线解耦**: `m_icnt_L2_queue`、`m_L2_dram_queue`、`m_dram_L2_queue`、`m_L2_icnt_queue` 四个 FIFO 将 L2 与互联网络和 DRAM 解耦。L2 的 `access()` 不会被 DRAM 延迟阻塞——miss 请求推入 `m_L2_dram_queue`，响应从 `m_dram_L2_queue` 异步返回。

2. **独立 cache_cycle**: L2 有自己独立的 `cache_cycle()`，在 `memory_partition_unit::cache_cycle()` 中调用。与 shader core 的 cycle 通过 FIFO 解耦。

3. **ROP 延迟**: `m_rop` (Render Output Pipeline) 延迟队列在 L2 access 之前引入额外的延迟——这是一个 GPU 特定的概念，用于模拟光栅化输出单元的回写延迟。

### 4.3 阻塞 vs 非阻塞对比表

| 特性 | L1 Data Cache | L2 Cache | L1 Texture Cache |
|------|-------------|---------|-----------------|
| **类型** | Non-Blocking | Non-Blocking | Non-Blocking |
| **未命中机制** | MSHR (哈希合并) | MSHR (哈希合并) | ROB (FIFO 不合并) |
| **多 miss 飞行** | 是 (MSHR 条目数) | 是 (MSHR 条目数) | 是 (ROB 条目数) |
| **Hit-under-miss** | 是 (MSHR 合并) | 是 (MSHR 合并) | 是 (预 fill tag) |
| **Miss-under-miss** | 是 (不同地址) | 是 (不同地址) | 是 (每个请求独立 ROB) |
| **结构性阻塞点** | 6 种 (见上表) | 5 种 (无 bank 冲突) | 3 种 (FIFO 满) |
| **输出顺序** | 无保证 | 无保证 | 严格保证 |
| **写支持** | 是 | 是 | 否 |
| **外部延迟队列** | l1_latency_queue (bank shift reg) | ROP delay queue | fragment_fifo (内部管线) |

---

## 五、设计核心思想对比

### 5.1 L1 Data Cache 的设计哲学

**"合并优先，吞吐为王"**

L1D 的设计目标是**最大化 warp 级吞吐量**。关键设计决策：

1. **MSHR 合并**: 同一个 warp 内的多个线程可能访问同一缓存行。MSHR 将这些请求合并为一次内存访问，减少冗余的 DRAM 流量。

2. **Bank 级并行**: 多个 bank 的独立延迟队列允许每个周期接受多个请求（每个 bank 一个），最大化 bank 级并行度。

3. **SECTOR_ASSOC**: L1D 默认使用 sector cache（32B × 4 sectors = 128B 行）。扇区级别的 MSHR 合并粒度避免了行级冲突。

4. **写策略灵活性**: 4 种写命中策略 + 4 种写缺失策略通过函数指针分派，适应不同的 GPU 架构（Fermi 的 LOCAL_WB_GLOBAL_WT、Maxwell/Pascal 的 WRITE_EVICT 等）。

### 5.2 L2 Cache 的设计哲学

**"共享、大容量、吸收写回"**

L2 的设计目标是**作为所有 shader core 的统一最后一级缓存**：

1. **地址翻译**: `l2_cache_config` 使用地址翻译避免 set camping——多个 shader core 的不同地址可能映射到同一 L2 set。

2. **Writeback 吸收**: L1 写回到 L2 的数据如果命中 L2，直接在 L2 中更新并删除请求——不需要写穿到 DRAM。这减少了 DRAM 流量。

3. **FIFO 管线解耦**: 通过 4 个 FIFO 将 L2 从互联网络和 DRAM 的时序中解耦。L2 可以按自己的节奏处理请求。

4. **扇区拆分**: 当 L1 是 sector cache 而 L2 也是 sector cache 时，L2 将行请求拆分为扇区请求独立处理，提高 DRAM 带宽利用率。

### 5.3 两者共用的核心设计

L1D 和 L2 共享 **95% 以上的代码**（都继承自 `data_cache` → `baseline_cache`）。差异仅在于：

| 差异点 | L1D | L2 |
|--------|-----|-----|
| 基类 | `l1_cache` | `l2_cache` |
| `m_wr_alloc_type` | `L1_WR_ALLOC_R` | `L2_WR_ALLOC_R` |
| `m_wrbk_type` | `L1_WRBK_ACC` | `L2_WRBK_ACC` |
| 配置类 | `l1d_cache_config` (含 bank/bank_hashing) | `l2_cache_config` (含 address_mapping) |
| 外部延迟 | `l1_latency_queue` (per-bank shift reg) | `rop_delay` queue |
| 缓存类型 | 通常 SECTOR | 通常 NORMAL |
| MSHR 类型 | SECTOR_ASSOC | ASSOC |
| 替换策略 | 通常 LRU | 通常 LRU |
| Writeback 处理 | 生成 writeback | **吸收** writeback (delete mf) |

---

## 六、与 openCache 的对应关系

### 6.1 已对齐的部分

| GPGPU-Sim 组件 | openCache 对应 | 对齐状态 |
|---------------|---------------|---------|
| `baseline_cache` | `BaselineCache` | 高度对齐 |
| `data_cache` | `DataCache` | 高度对齐 |
| `tag_array` | `TagArray` | 高度对齐 |
| `mshr_table` | `MSHRTable` | 高度对齐 |
| `bandwidth_management` | `replenish_ports/use_data_port/use_fill_port` | 对齐 |
| `cache_stats` | `CacheStats` | 基本对齐 |
| `cache_config` | `CacheConfig` | 格式对齐 |

### 6.2 不在 openCache 范围内的部分

| 组件 | 位置 | 说明 |
|------|------|------|
| `l1_latency_queue` | `shader.cc` | Per-bank 移位寄存器延迟队列——属于 shader core 行为模型，不在缓存内 |
| `rop_delay` queue | `l2cache.cc` | ROP 延迟——GPU 专属，通用 cache 不需要 |
| `breakdown_request_to_sector_requests` | `l2cache.cc` | L2 扇区拆分——属于 memory_sub_partition 逻辑 |
| `mem_fetch_interface` / `icnt_push` | `shader.h/cc` | 互联网络接口——GPUGPU-Sim 专属 |
| `l1d_cache_config` / `l2_cache_config` | `gpu-cache.h` | 派生配置类——bank hashing 和地址翻译可参数化到基础 Config |
| Writeback 吸收 (delete mf on L1_WRBK_ACC) | `l2cache.cc` | L2 专属的写回处理——可通过 `CacheMemoryInterface` 回调实现 |

### 6.3 对 openCache 的启示

1. **L1D 和 L2 本质上是同一个缓存类**（`data_cache`），差异仅在于外部连接方式。openCache 已经通过 `DataCache` 正确建模。

2. **Bank 延迟队列属于外围模型**，不是缓存内核的职责。类似地，ROP 延迟、互联网络路由、DRAM 时序都不应在缓存内核中建模。

3. **Writeback 吸收机制**需要 `CacheMemoryInterface` 支持：L2 收到 L1 写回后，如果命中，应通知缓存 "写回已被下级吸收，无需返回给上级"。

4. **扇区拆分**建议通过 `CacheMemoryInterface` 的 `send_request()` 实现中处理——下级接口收到行请求后自行决定是否拆分。

---

## 参考文献

1. **GPGPU-Sim 源码** — `gpu-cache.h:1280-1749`, `gpu-cache.cc:1215-2017`, `l2cache.cc:465-880`, `shader.cc:2043-2910`, `shader.h:1341-2780`

2. **Kroft, D.** (1981). "Lockup-Free Instruction Fetch/Prefetch Cache Organization." *ISCA 1981*. — MSHR 概念的原始论文。L1/L2 的 MSHR 合并机制直接来源于此。

3. **Jouppi, N. P.** (1993). "Cache Write Policies and Performance." *ISCA 1993*. — GPGPU-Sim 注释中引用，用于解释 FETCH_ON_WRITE vs WRITE_ALLOCATE 策略差异。
