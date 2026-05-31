# GPGPU-Sim GPU Cache 微架构调研报告

> 调研基准: gpgpu-sim_distribution (dev 分支)  
> 调研范围: GPU 内全部缓存类型及其微架构实现  
> 调研日期: 2026-05-26

---

## 一、总体架构概览

GPGPU-Sim 模拟的 GPU 包含 **5 种 L1 缓存**（位于每个 Shader Core 内）和 **1 种共享 L2 缓存**（位于每个 Memory Partition 内）。

### 1.1 每个 Shader Core 内的 L1 缓存

```
shader_core
├── m_L1I  (read_only_cache)  — 指令缓存 (Instruction Cache)
├── m_L1T  (tex_cache)        — 纹理缓存 (Texture Cache)
├── m_L1C  (read_only_cache)  — 常量缓存 (Constant Cache)
├── m_L1D  (l1_cache)         — L1 数据缓存 (L1 Data Cache)
└── 共享内存 / Register File
```

源码位置: `shader.h:1451-1453, 2525`

### 1.2 每个 Memory Partition 内的 L2 缓存

```
memory_partition_unit
├── memory_sub_partition[0..N]
│   ├── m_L2cache  (l2_cache)          — L2 数据缓存
│   ├── m_icnt_L2_queue                — 互联网络 → L2 的请求队列
│   ├── m_L2_dram_queue                — L2 → DRAM 的未命中队列
│   ├── m_dram_L2_queue                — DRAM → L2 的填充队列
│   ├── m_L2_icnt_queue                — L2 命中 → 互联网络的响应队列
│   └── m_rop                          — ROP 延迟单元
└── m_dram                              — DRAM 控制器
```

源码位置: `l2cache.h:80-260`

### 1.3 缓存类型与配置对照表

| 缓存 | 类 | 写策略 | 分配策略 | 缓存类型 | MSHR 类型 | 配置类 |
|------|-----|--------|---------|---------|-----------|-------|
| L1 指令 | `read_only_cache` | READ_ONLY | ON_MISS | NORMAL | ASSOC | `cache_config` |
| L1 纹理 | `tex_cache` | READ_ONLY | ON_MISS | NORMAL/SECTOR | TEX_FIFO/SECTOR_TEX_FIFO | `cache_config` |
| L1 常量 | `read_only_cache` | READ_ONLY | ON_MISS | NORMAL | ASSOC | `cache_config` |
| L1 数据 | `l1_cache` | WRITE_EVICT / LOCAL_WB_GLOBAL_WT | ON_MISS | SECTOR | SECTOR_ASSOC | `l1d_cache_config` |
| L2 | `l2_cache` | WRITE_BACK | ON_MISS | NORMAL | ASSOC | `l2_cache_config` |

---

## 二、类继承体系

### 2.1 完整继承树

```
cache_t                         (纯抽象接口: access + data_port_free + fill_port_free)
├── baseline_cache              (数据缓存公共基类: tag_array + MSHR + miss_queue)
│   ├── read_only_cache         (只读缓存: L1 指令/常量 使用)
│   └── data_cache              (读写缓存基类: 函数指针分派写/读策略)
│       ├── l1_cache            (L1 数据: 仅改 m_wr_alloc_type / m_wrbk_type)
│       └── l2_cache            (L2: 同上，不同的 type 枚举值)
└── tex_cache                   (纹理缓存: 完全独立的 FIFO 管线架构)
```

### 2.2 各层职责

#### cache_t（gpu-cache.h:1261-1278）
```cpp
class cache_t {
public:
    virtual ~cache_t() {}
    virtual enum cache_request_status access(
        new_addr_type addr, mem_fetch *mf, unsigned time,
        std::list<cache_event> &events) = 0;
    virtual bool data_port_free() const = 0;
    virtual bool fill_port_free() const = 0;
};
```
最顶层接口，只有 3 个纯虚函数。所有缓存类型都实现这个接口，使得 shader core 和 memory partition 可以统一调用。

#### baseline_cache（gpu-cache.h:1280-1481）
**共享的核心组件**:
- `tag_array *m_tag_array` — 标签阵列
- `mshr_table m_mshrs` — 未命中状态保持寄存器
- `std::list<mem_fetch*> m_miss_queue` — 未命中队列
- `bandwidth_management m_bandwidth_management` — 带宽管理
- `cache_stats m_stats` — 统计
- `extra_mf_fields_lookup m_extra_mf_fields` — 附加追踪字段
- `gpgpu_sim *m_gpu` — GPU 模拟器指针

**共享的方法**:
- `cycle()` — 排空 miss_queue，补充带宽
- `fill()` — 处理来自下级的填充响应
- `send_read_request()` — 发送读请求（MSHR 合并 + miss_queue 入队）
- `access_ready()` / `next_access()` — 就绪访问查询

#### read_only_cache（gpu-cache.h:1483-1507）
只读缓存。`access()` 方法直接调用 `send_read_request()`，不区分读/写。

**使用者**: L1 指令缓存、L1 常量缓存

#### data_cache（gpu-cache.h:1509-1700）
读写缓存。在 `baseline_cache` 基础上添加：
- **函数指针分派**: `m_wr_hit`, `m_wr_miss`, `m_rd_hit`, `m_rd_miss`
- **写策略处理器**: `wr_hit_wb()`, `wr_hit_wt()`, `wr_hit_we()`, `wr_hit_global_we_local_wb()`
- **写分配处理器**: `wr_miss_no_wa()`, `wr_miss_wa_naive()`, `wr_miss_wa_fetch_on_write()`, `wr_miss_wa_lazy_fetch_on_read()`
- **读处理器**: `rd_hit_base()`, `rd_miss_base()`
- `process_tag_probe()` — 统一的分派入口
- `send_write_request()` — 发送写请求
- `mem_fetch_allocator *m_memfetch_creator` — 创建 writeback/write-allocate 请求

#### l1_cache / l2_cache（gpu-cache.h:1702-1749）
极其薄的派生类。仅改变构造参数中的两个枚举值：

```cpp
// L1:
l1_cache(...) : data_cache(..., L1_WR_ALLOC_R, L1_WRBK_ACC, gpu, level) {}

// L2:
l2_cache(...) : data_cache(..., L2_WR_ALLOC_R, L2_WRBK_ACC, gpu, level) {}
```

`m_wr_alloc_type` 和 `m_wrbk_type` 决定了写分配和写回请求在互联网络中的**路由目标**（发给 L1 还是 L2 的 writeback）。缓存逻辑完全一致，0 行新增行为代码。

#### tex_cache（gpu-cache.h:1751-1943）
**完全独立**，不继承 `baseline_cache`。有自己的：
- `tag_array m_tags` — 独立的标签阵列
- `fifo<fragment_entry> m_fragment_fifo` — 片段 FIFO
- `fifo<mem_fetch*> m_request_fifo` — 请求 FIFO
- `fifo<rob_entry> m_rob` — 重排序缓冲区 (Reorder Buffer)
- `fifo<mem_fetch*> m_result_fifo` — 结果 FIFO
- `data_block *m_cache` — 简化的数据存储（只有 `m_valid` + `m_block_addr`，不用 CacheBlock）

---

## 三、各缓存的微架构详解

### 3.1 L1 指令缓存 (m_L1I)

```
类:      read_only_cache
配置:    NORMAL, WRITE_BACK→实际上 READ_ONLY（构造函数 assert）
缓存行:  128B (典型值)
MSHR:    ASSOC
```

**微架构**: 标准 baseline_cache 流程。`access()` → `probe()` → HIT 直接返回 / MISS 走 `send_read_request()` → MSHR 合并 → miss_queue 入队。`fill()` 将数据写入 tag_array。

**特点**: 最简单、最标准的只读缓存。几乎无 GPU 特有逻辑。

### 3.2 L1 常量缓存 (m_L1C)

```
类:      read_only_cache
配置:    NORMAL, READ_ONLY
缓存行:  64-128B
MSHR:    ASSOC
```

**微架构**: 与 L1 指令缓存完全相同。区别仅在于配置参数（大小、行大小、延迟）和连接的访问流不同（常量内存 vs 指令内存）。

### 3.3 L1 数据缓存 (m_L1D)

```
类:      l1_cache → data_cache → baseline_cache
配置:    SECTOR, WRITE_EVICT (全局) / LOCAL_WB_GLOBAL_WT, ON_MISS
缓存行:  128B (32B × 4 sectors)
MSHR:    SECTOR_ASSOC
Banks:   4 (byte-interleaved)
```

**微架构**:

```
access(mem_fetch *mf)
  │
  ├─ WRITE_BACK?  → 直接 fill(), 返回 HIT
  │
  ├─ probe()
  │   ├─ HIT → process_tag_probe() → m_wr_hit / m_rd_hit 函数指针
  │   │   ├─ wr_hit_wb:  标记 MODIFIED, 更新 LRU
  │   │   ├─ wr_hit_wt:  标记 MODIFIED + 发送 WRITE_REQUEST 到 miss_queue
  │   │   ├─ wr_hit_we:  失效化 + 发送 WRITE_REQUEST (global write-evict)
  │   │   └─ wr_hit_global_we_local_wb:
  │   │        GLOBAL_ACC_W → wr_hit_we
  │   │        LOCAL_ACC_W  → wr_hit_wb
  │   │
  │   └─ MISS → process_tag_probe() → m_wr_miss / m_rd_miss 函数指针
  │       ├─ wr_miss_wa_naive:        发送 WRITE + 发送 READ (allocate)
  │       ├─ wr_miss_wa_fetch_on_write: 整行写优化 / 部分写走 MSHR
  │       ├─ wr_miss_wa_lazy_fetch_on_read: 分配+标记脏+标记不可读
  │       └─ wr_miss_no_wa:           仅发送 WRITE (不分配)
  │
  └─ use_data_port() — 带宽计费
```

**Fermi L1 的特殊性**: Fermi 架构 (SM 2.x) 中，L1 和 Shared Memory 共享同一块片上 SRAM（可配置 16KB/48KB 或 48KB/16KB 分配）。`l1d_cache_config` 继承 `cache_config` 并添加：
- `l1_banks` / `l1_banks_log2` — bank 数量
- `l1_banks_byte_interleaving` — bank 交错粒度
- `l1_banks_hashing_function` — bank 哈希函数
- `m_unified_cache_size` — 统一缓存大小（用于动态 associativity 调整）

### 3.4 L2 缓存 (m_L2cache)

```
类:      l2_cache → data_cache → baseline_cache
配置:    NORMAL, WRITE_BACK, ON_MISS
缓存行:  128B (典型值)
MSHR:    ASSOC
关联度:  16-way (典型值)
```

**微架构**:

L2 位于每个 `memory_sub_partition` 内。一个 memory partition 包含多个 sub-partition，每个 sub-partition 有独立的 L2 cache 实例。

```
互联网络 (ICNT)
  │
  ▼
m_icnt_L2_queue (FIFO)  ← 来自 shader core 的请求
  │
  ▼
m_L2cache->access()      ← l2_cache (data_cache 逻辑)
  │
  ├─ HIT → m_L2_icnt_queue → 互联网络 → shader core
  │
  └─ MISS → m_L2_dram_queue → DRAM 控制器 → DRAM
                │
                ▼
           DRAM 响应 → m_dram_L2_queue → m_L2cache->fill()
                │
                ▼
           m_L2_icnt_queue → 互联网络 → shader core
```

**与 L1 的关键区别**:
- L2 是 shared（所有 shader core 共享），L1 是 private（每个 core 独立）
- L2 通过 `l2_cache_config` 使用地址翻译以防止 set camping
- L2 包含 ROP（Render Output Unit）延迟模型
- L2 不需要 bank 交错（bank 级并行由多个 sub-partition 提供）

### 3.5 纹理缓存 (m_L1T)

```
类:      tex_cache → cache_t (注意: 不继承 baseline_cache!)
配置:    NORMAL 或 SECTOR, READ_ONLY, ON_MISS
MSHR:    TEX_FIFO 或 SECTOR_TEX_FIFO
```

**微架构** — 基于 Igehy et al. 1998 论文的 FIFO 管线:

```
access(addr, mf)
  │
  ├─ fragment_fifo.full() || request_fifo.full() || rob.full()?
  │   └─ YES → RESERVATION_FAIL
  │
  ├─ m_tags.access(block_addr) → 分配缓存行
  │
  ├─ m_fragment_fifo.push(fragment_entry(mf, cache_index, is_miss, data_size))
  │
  ├─ MISS?
  │   ├─ m_rob.push(rob_entry(cache_index, mf, block_addr))
  │   ├─ m_tags.fill(cache_index) — 立即标记 valid (不等数据!)
  │   ├─ m_request_fifo.push(mf) — 排队发送内存请求
  │   ├─ mf->set_status(m_request_queue_status) — 标记请求状态
  │   └─ return MISS
  │
  └─ HIT?
      └─ return HIT_RESERVED  ← 注意: 永远不返回 HIT!
                               纹理缓存中 tag 命中不代表数据立即可用，
                               数据还需要通过 fragment_fifo 排队输出。

cycle()
  │
  ├─ 排空 request_fifo → 发送到 m_memport (互联网络)
  │
  └─ 处理 fragment_fifo:
      ├─ miss 条目: 检查 ROB 头部是否 ready → 是则推入 result_fifo
      └─ hit 条目:  直接推入 result_fifo (数据已在 m_cache 中)

fill(mf)
  │
  ├─ SECTOR_TEX_FIFO? → pending_read 倒计数，等待所有扇区到齐
  │
  ├─ 查找 m_extra_mf_fields → 获取 ROB index
  ├─ m_rob[rob_index].m_ready = true
  └─ m_rob[rob_index].m_time = time
```

**纹理缓存的 5 个 FIFO**:

| FIFO | 存储类型 | 用途 |
|------|---------|------|
| `m_fragment_fifo` | `fragment_entry` | 所有接受的请求排队等待输出（类似 miss_queue 但包含命中） |
| `m_request_fifo` | `mem_fetch*` | 未命中请求排队等待发送到下级存储 |
| `m_rob` | `rob_entry` | 重排序缓冲区——确保未命中响应按序返回 |
| `m_result_fifo` | `mem_fetch*` | 已完成请求（命中+未命中）等待 shader core 取走 |
| `m_cache` (data_block 数组) | `data_block` | 简化的数据存储（仅 valid bit + block_addr，无 LRU/状态机） |

**与 baseline_cache 的关键差异**:

| 维度 | baseline_cache | tex_cache |
|------|---------------|-----------|
| 继承 | cache_t → baseline_cache | cache_t（直接继承） |
| Tag 阵列 | `tag_array` (完整的 CacheBlock 状态机) | `tag_array` (共用同一个类) |
| 未命中处理 | MSHR + miss_queue | request_fifo + ROB |
| 数据存储 | CacheBlock (4 状态 + dirty + LRU) | data_block (仅 valid + block_addr) |
| 命中后 | 数据立即可用 | 数据需等待 fragment_fifo 排出 |
| 端口带宽 | bandwidth_management | 始终返回 true (stub) |
| 替换策略 | LRU / FIFO | 隐含在 tag_array 中（但 access 后立即 fill，等同无替换） |
| 论文依据 | 标准 CPU cache 模型 | Igehy et al. 1998, "Prefetching in a Texture Cache Architecture" |

---

## 四、共享组件分析

### 4.1 所有缓存共用的组件

| 组件 | L1I | L1C | L1D | L1T | L2 | 说明 |
|------|-----|-----|-----|-----|-----|------|
| `cache_t` 接口 | ✓ | ✓ | ✓ | ✓ | ✓ | 3 个纯虚函数 |
| `tag_array` | ✓ | ✓ | ✓ | ✓ | ✓ | 标签阵列（探针/访问/填充） |
| `cache_config` | ✓ | ✓ | ✓¹ | ✓ | ✓² | 配置基类 |
| `cache_stats` | ✓ | ✓ | ✓ | ✓ | ✓ | 统计收集 |
| `mem_fetch_interface` | ✓ | ✓ | ✓ | ✓ | ✓ | 下级存储接口 |

> ¹ L1 数据使用 `l1d_cache_config`（派生类）  
> ² L2 使用 `l2_cache_config`（派生类）

### 4.2 仅 baseline_cache 及其子类共用的组件

| 组件 | L1I | L1C | L1D | L2 | 说明 |
|------|-----|-----|-----|-----|------|
| `baseline_cache` | ✓ | ✓ | ✓ | ✓ | 公共基类（tex_cache 不使用） |
| `mshr_table` | ✓ | ✓ | ✓ | ✓ | MSHR（tex_cache 无 MSHR） |
| `m_miss_queue` | ✓ | ✓ | ✓ | ✓ | 未命中队列 |
| `bandwidth_management` | ✓ | ✓ | ✓ | ✓ | 带宽管理 |
| `send_read_request()` | ✓ | ✓ | ✓ | ✓ | MSHR 合并 + 队列 |
| `extra_mf_fields` | ✓ | ✓ | ✓ | ✓ | 缺失追踪 |
| `CacheBlock` 状态机 | ✓ | ✓ | ✓ | ✓ | 4 状态 (I→R→V→M) |

### 4.3 仅 tex_cache 使用的组件

| 组件 | 说明 |
|------|------|
| `fifo<T>` 模板 | 通用 FIFO 实现（tex_cache 内部定义） |
| `fragment_entry` | 访问流水线条目 |
| `rob_entry` | 重排序缓冲区条目 |
| `data_block` | 简化的缓存数据块（仅 valid + block_addr） |
| `extra_mf_fields` (tex 版本) | 仅含 `rob_index` + `pending_read` |
| `breakdown_request_to_sector_requests()` | 行请求分解为扇区请求（SECTOR_TEX_FIFO） |

---

## 五、配置类层次

### 5.1 配置继承树

```
cache_config                  (基础配置: nsets, lsize, assoc, 策略枚举, MSHR 参数)
├── l1d_cache_config          (L1 数据专属: bank 参数, unified_cache_size, 动态 associativity)
└── l2_cache_config           (L2 专属: 地址翻译以消除 set camping)
```

### 5.2 各配置类的差异

**cache_config** (gpu-cache.h:556-905):
- 通用参数: `m_nset`, `m_line_sz`, `m_assoc`, `m_atom_sz`
- 策略枚举: replacement, write, alloc, write_alloc, MSHR type, set_index_function
- MSHR 参数: `m_mshr_entries`, `m_mshr_max_merge`, `m_miss_queue_size`
- 纹理 FIFO 参数 (`union`): `m_fragment_fifo_entries` / `m_request_fifo_entries` / `m_rob_entries`
- 派生参数: `m_line_sz_log2`, `m_nset_log2`, `m_sector_sz_log2`
- `hash_function()` — 虚拟，允许 L2 重写
- `set_index()` — 虚拟，允许 L1/L2 重写

**l1d_cache_config** (gpu-cache.h:907-934):
- 添加: `l1_latency`, `l1_banks`, `l1_banks_log2`, `l1_banks_byte_interleaving`, `l1_banks_hashing_function`, `m_unified_cache_size`
- 重写 `get_max_cache_multiplier()` — 支持统一缓存动态 associativity (Volta)
- 添加 `set_bank()` — 计算 bank 索引

**l2_cache_config** (gpu-cache.h:936-944):
- 添加: `m_address_mapping` (linear_to_raw_address_translation)
- 重写 `set_index()` — 使用地址翻译避免 set camping

---

## 六、内存请求类型体系

GPGPU-Sim 的 `mem_access_type` 枚举（定义在 `abstract_hardware_model.h:773-777`）区分了 12 种不同的内存访问类型:

```cpp
enum mem_access_type {
    GLOBAL_ACC_R,      // 全局内存读
    LOCAL_ACC_R,       // 局部内存读
    CONST_ACC_R,       // 常量内存读
    TEXTURE_ACC_R,     // 纹理内存读
    GLOBAL_ACC_W,      // 全局内存写
    LOCAL_ACC_W,       // 局部内存写
    L1_WRBK_ACC,       // L1 写回
    L2_WRBK_ACC,       // L2 写回
    INST_ACC_R,        // 指令读取
    L1_WR_ALLOC_R,     // L1 写分配读
    L2_WR_ALLOC_R,     // L2 写分配读
    NUM_MEM_ACCESS_TYPE
};
```

这个分类在以下场景中至关重要：
1. **路由决策**: `GLOBAL_ACC_W` vs `LOCAL_ACC_W` 决定 L1 的 write-evict vs write-back 行为
2. **写回目标**: `L1_WRBK_ACC` vs `L2_WRBK_ACC` 决定写回请求在互联网络中的路由
3. **写分配**: `L1_WR_ALLOC_R` vs `L2_WR_ALLOC_R` 区分写分配读请求的来源层级
4. **统计分类**: 每种类型独立统计 hits/misses

---

## 七、缓存间复用关系图

```
                        ┌─────────────────────┐
                        │     cache_t          │  纯抽象接口
                        │  (access / ports)    │
                        └──────┬──────────────┘
                               │
               ┌───────────────┼───────────────┐
               │                               │
    ┌──────────▼──────────┐         ┌──────────▼──────────┐
    │   baseline_cache     │         │     tex_cache        │
    │                      │         │                      │
    │ ┌──────────────────┐ │         │ ┌──────────────────┐ │
    │ │    tag_array      │ │◄────────│ │    tag_array      │ │ ← 共用
    │ │   (CacheBlock)    │ │         │ │   (CacheBlock)    │ │
    │ └──────────────────┘ │         │ └──────────────────┘ │
    │ ┌──────────────────┐ │         │ ┌──────────────────┐ │
    │ │    mshr_table     │ │         │ │  fifo × 4 + ROB  │ │ ← 独有
    │ └──────────────────┘ │         │ └──────────────────┘ │
    │ ┌──────────────────┐ │         │ ┌──────────────────┐ │
    │ │  m_miss_queue     │ │         │ │   data_block[]    │ │ ← 独有
    │ └──────────────────┘ │         │ └──────────────────┘ │
    │ ┌──────────────────┐ │         │                      │
    │ │bandwidth_mgmt     │ │         │ (无 MSHR)            │
    │ └──────────────────┘ │         │ (无 miss_queue)      │
    └──────────┬───────────┘         │ (无带宽管理)         │
               │                     └─────────────────────┘
    ┌──────────┼───────────┐
    │          │           │
    ▼          ▼           ▼
read_only   data_cache   (tex_cache 已独立)
_cache        │
(L1I, L1C)    │
         ┌────┴────┐
         ▼         ▼
      l1_cache  l2_cache
      (L1D)     (L2)
      [仅改      [仅改
       wr_alloc  wr_alloc
       /wrbk     /wrbk
       枚举值]    枚举值]
```

---

## 八、关键设计要点总结

### 8.1 复用层次

- **Level 0 (全部共用)**: `cache_t` 接口 + `tag_array` + `cache_stats` + `cache_config`
- **Level 1 (数据缓存共用)**: `baseline_cache` → MSHR + miss_queue + 带宽管理 + send_read_request
- **Level 2 (读写缓存共用)**: `data_cache` → 写策略函数指针分派 + writeback 生成
- **Level 3 (实例专用)**: `l1_cache`/`l2_cache` 仅改路由类型；`tex_cache` 完全独立管线

### 8.2 tex_cache 为何不继承 baseline_cache

纹理缓存的微架构与数据缓存有本质区别：
1. **FIFO 管线 vs MSHR**: 纹理缓存使用 FIFO 管线（fragment_fifo → request_fifo → ROB → result_fifo），而数据缓存使用 MSHR + miss_queue
2. **无替换策略**: 纹理缓存 access 后立即 fill，实质上是无替换的直通模型
3. **命中不立即可用**: tag 命中后数据仍需经过 fragment_fifo，因为纹理单元有自身的流水线延迟
4. **简化的数据存储**: `data_block` 仅存 valid bit + block_addr，不需要 CacheBlock 的四状态机

### 8.3 对 openCache 的启示

1. **`data_cache` 已经是"万能"的**: L1 和 L2 的行为差异仅在于路由类型（`m_wr_alloc_type` / `m_wrbk_type`），这在独立 cache 模型中可以用 Config 或回调替代。openCache 不需要 L1/L2 派生类。

2. **`tex_cache` 不应强行合并**: 纹理缓存的 FIFO 管线与 baseline_cache 的 MSHR 模型完全不同——它们仅共享 `tag_array`。如果 openCache 要支持纹理缓存，应该作为一个**独立的类**实现，不继承 BaselineCache。

3. **`cache_t` 抽象接口是唯一真正的"公共分母"**: 所有缓存都实现的 3 个方法（`access` / `data_port_free` / `fill_port_free`）是最小公共接口。

4. **配置层次已足够**: `cache_config` → `l1d_cache_config` / `l2_cache_config` 的继承层次在独立 cache 中不需要——bank 参数和地址翻译可以参数化到基础 Config 中。

---

## 参考文献

1. Igehy, H., Eldridge, M., & Proudfoot, K. (1998). "Prefetching in a Texture Cache Architecture." *Proceedings of the 1998 Eurographics/SIGGRAPH Workshop on Graphics Hardware.*

2. Bakhoda, A., Yuan, G. L., Fung, W. W. L., Wong, H., & Aamodt, T. M. (2009). "Analyzing CUDA Workloads Using a Detailed GPU Simulator." *IEEE International Symposium on Performance Analysis of Systems and Software (ISPASS).*

3. GPGPU-Sim v4.x source code: `src/gpgpu-sim/gpu-cache.h`, `gpu-cache.cc`, `l2cache.h`, `l2cache.cc`, `shader.h`, `abstract_hardware_model.h`
