# GPGPU-Sim Cache 参考实现 — 用户手册

本目录包含从 GPGPU-Sim 提取的**原始未修改**缓存代码，可独立编译运行。适用于 GPU 架构师、性能建模工程师快速理解并集成缓存模型。

---

## 目录

1. [接口说明](#1-接口说明)
   - 1.1 [配置接口](#11-配置接口)
     - 1.1.1 [cache_config 数据结构](#111-cache_config-数据结构)
     - 1.1.2 [配置字符串格式](#112-配置字符串格式gpgu-sim-兼容)
     - 1.1.3 [参数化配置](#113-参数化配置代码直接赋值)
     - 1.1.4 [Throughput 和 Latency 的配置方式](#114-throughput-和-latency-的配置方式)
     - 1.1.5 [常用配置示例](#115-常用配置示例)
   - 1.2 [数据接口](#12-数据接口)
     - 1.2.1 [请求接口：access()](#121-请求接口access)
     - 1.2.2 [请求数据结构](#122-请求数据结构)
     - 1.2.3 [从下级 Memory 抓数据的请求接口](#123-从下级-memory-抓数据的请求接口mem_fetch_interface)
     - 1.2.4 [下级 Memory 返回给 Cache 的数据接口：fill()](#124-下级-memory-返回给-cache-的数据接口fill)
     - 1.2.5 [Invalidate / Flush 接口](#125-invalidate--flush-接口)
     - 1.2.6 [带宽/端口查询接口](#126-带宽端口查询接口)
     - 1.2.7 [读取就绪请求：access_ready() / next_access()](#127-读取就绪请求access_ready--next_access)
   - 1.3 [调度接口](#13-调度接口)
     - 1.3.1 [主程序的调用接口：构造函数](#131-主程序的调用接口构造函数)
     - 1.3.2 [如何推进时钟：cycle()](#132-如何推进时钟cycle)
     - 1.3.3 [调度接口总览：核心模拟循环](#133-调度接口总览核心模拟循环)
2. [使用示例](#2-使用示例)
3. [架构总览](#3-架构总览)
   - 3.1 [类层次结构](#31-类层次结构)
   - 3.2 [数据处理流程](#32-数据处理流程)
     - 3.2.1 [请求处理流程](#321-请求处理流程核心数据通路)
     - 3.2.2 [写策略派发流程](#322-写策略派发流程data_cache-特有)
     - 3.2.3 [多级缓存数据流](#323-多级缓存数据流l1--l2--dram)
   - 3.3 [模块化结构](#33-模块化结构)
   - 3.4 [用户使用典型场景推荐](#34-用户使用典型场景推荐)
   - 3.5 [可维可测说明](#35-可维可测说明)
     - 3.5.1 [获取统计数据](#351-获取统计数据)
     - 3.5.2 [统计项含义](#352-统计项含义)
     - 3.5.3 [打印内部状态](#353-打印内部状态调试用)
     - 3.5.4 [参数扫描示例](#354-参数扫描示例设计空间探索)
     - 3.5.5 [关键性能指标计算公式](#355-关键性能指标计算公式)
   - 3.6 [模型边界说明](#36-模型边界说明)
4. [其他](#4-其他)
   - 4.1 [快速开始](#41-快速开始)
   - 4.2 [附录 A：GPGPU-Sim → openCache 对应关系](#42-附录-agpgpu-sim--opencache-对应关系)
   - 4.3 [附录 B：缓存类型对比](#43-附录-b缓存类型对比)
   - 4.4 [附录 C：Normal vs Sector 缓存](#44-附录-cnormal-vs-sector-缓存)
   - 4.5 [附录 D：写策略选择指南](#45-附录-d写策略选择指南)
   - 4.6 [附录 E：文件结构](#46-附录-e文件结构)
   - 4.7 [附录 F：MSHR 系统详解](#47-附录-fmshr-系统详解)

---

## 1. 接口说明

本章按接口类型分为三部分：**配置接口**（如何参数化缓存模型）、**数据接口**（如何发起/接收请求和处理数据）、**调度接口**（如何驱动缓存推进时钟）。

#### 接口全景图

以下思维导图展示了缓存模型的全部对外接口及其层次关系。用户集成时只需关注这三个入口：

```
缓存模型对外接口
│
├── 1. 配置接口 ─── cache_config
│   │
│   ├── 构造方式
│   │   ├── 配置字符串 (GPGPU-Sim 兼容): "S:32:128:4,L:E:m:F:X,A:32:4,64"
│   │   └── 代码直接赋值: cfg.m_nset = 32; cfg.m_line_sz = 128; ...
│   │
│   ├── 几何参数
│   │   ├── m_cache_type        → NORMAL / SECTOR          (缓存粒度)
│   │   ├── m_nset              → unsigned                  (Set 数量)
│   │   ├── m_line_sz           → unsigned                  (行大小, Bytes)
│   │   └── m_assoc            → unsigned                  (关联度)
│   │
│   ├── 时序/吞吐量参数
│   │   ├── m_data_port_width   → unsigned (B/cycle)        (命中延迟 & 带宽)
│   │   ├── m_mshr_entries      → unsigned                  (最大并行未命中数 MLP)
│   │   ├── m_mshr_max_merge    → unsigned                  (同地址最大合并数)
│   │   └── m_miss_queue_size   → unsigned                  (未命中缓冲深度)
│   │
│   └── 策略参数
│       ├── m_write_policy      → READ_ONLY / WRITE_BACK / WRITE_THROUGH
│       │                         / WRITE_EVICT / LOCAL_WB_GLOBAL_WT
│       ├── m_write_alloc_policy → NO_WRITE_ALLOCATE / WRITE_ALLOCATE
│       │                          / FETCH_ON_WRITE / LAZY_FETCH_ON_READ
│       ├── m_alloc_policy      → ON_MISS / ON_FILL / STREAMING
│       ├── m_replacement_policy → LRU / FIFO
│       ├── m_set_index_function → LINEAR / BITWISE_XOR / HASH_IPOLY / FERMI_HASH / CUSTOM
│       ├── m_mshr_type         → ASSOC / SECTOR_ASSOC / TEX_FIFO / SECTOR_TEX_FIFO
│       └── m_wr_percent        → unsigned (0-100)           (脏行保护阈值)
│
├── 2. 数据接口 ─── 请求的发送、传输与返回
│   │
│   ├── (A) 请求入口 ─── access(addr, mf, time, events) → cache_request_status
│   │   │
│   │   ├── 参数-1: addr : new_addr_type (= unsigned long long)
│   │   │           │
│   │   │           ├── 全地址: 0x00001040  (byte 粒度)
│   │   │           ├── block_addr() = addr & ~(line_sz-1) → 0x00001000  (cache line 对齐)
│   │   │           ├── tag()       = block_addr                       (用于 tag_array 匹配)
│   │   │           ├── mshr_addr() = addr & ~(atom_sz-1)              (MSHR 粒度对齐)
│   │   │           └── set_index() = hash_function(addr)              (映射到目标 set)
│   │   │
│   │   ├── 参数-2: mf : mem_fetch*  (请求载体, 缓存系统的核心数据结构)
│   │   │           │
│   │   │           ├── 构造函数: mem_fetch(access, inst, streamID, ctrl_size,
│   │   │           │                      wid, sid, tpc, mem_config, cycle, orig, orig_wr)
│   │   │           │
│   │   │           ├── 访问器 (由 cache 代码调用)
│   │   │           │   ├── get_addr()               → new_addr_type            (请求地址)
│   │   │           │   ├── get_data_size()           → unsigned                 (数据大小, Bytes)
│   │   │           │   ├── is_write()                → bool                     (是否写请求)
│   │   │           │   ├── get_is_write()            → bool                     (同 is_write)
│   │   │           │   ├── get_access_type()         → mem_access_type          (访问类型枚举)
│   │   │           │   ├── get_access_byte_mask()    → mem_access_byte_mask_t   (有效字节位掩码)
│   │   │           │   ├── get_access_sector_mask()  → mem_access_sector_mask_t (有效 sector 掩码)
│   │   │           │   ├── get_access_warp_mask()    → active_mask_t            (活跃线程掩码)
│   │   │           │   ├── get_streamID()            → unsigned long long       (CUDA Stream ID)
│   │   │           │   ├── get_wid()                 → unsigned                 (warp ID)
│   │   │           │   ├── get_sid()                 → unsigned                 (shader core ID)
│   │   │           │   ├── get_tpc()                 → unsigned                 (TPC ID)
│   │   │           │   ├── get_status()              → mem_fetch_status         (当前队列状态)
│   │   │           │   ├── get_tlx_addr()            → tlx_addr                 (chip/partition)
│   │   │           │   └── get_inst()                → warp_inst_t&             (关联指令)
│   │   │           │
│   │   │           ├── 修改器 (由 cache 内部使用)
│   │   │           │   ├── set_addr(new_addr_type)                        (改写地址)
│   │   │           │   ├── set_data_size(unsigned)                        (改写大小)
│   │   │           │   ├── set_status(mem_fetch_status, cycle)            (更新队列状态)
│   │   │           │   ├── set_chip(unsigned)                             (设定 chip)
│   │   │           │   ├── set_partition(unsigned)                        (设定 partition)
│   │   │           │   └── set_reply()                                    (标记回复)
│   │   │           │
│   │   │           ├── 内部数据: mem_access_t m_access
│   │   │           │   ├── m_type        → mem_access_type  (GLOBAL_ACC_R/W, LOCAL_ACC_R/W,
│   │   │           │   │                                    CONST_ACC_R, TEXTURE_ACC_R,
│   │   │           │   │                                    INST_ACC_R, L1_WRBK_ACC,
│   │   │           │   │                                    L2_WRBK_ACC, L1_WR_ALLOC_R,
│   │   │           │   │                                    L2_WR_ALLOC_R)
│   │   │           │   ├── m_addr        → new_addr_type    (请求地址)
│   │   │           │   ├── m_size        → unsigned         (请求大小, Bytes)
│   │   │           │   ├── m_is_write    → bool             (读/写标志)
│   │   │           │   ├── m_access_mask → active_mask_t    (bitset<64>, 活跃线程位掩码)
│   │   │           │   ├── m_byte_mask   → mem_access_byte_mask_t  (bitset<128>, 有效字节)
│   │   │           │   └── m_sector_mask → mem_access_sector_mask_t(bitset<4>,  有效 sector)
│   │   │           │
│   │   │           └── 分配器: mem_fetch_allocator (抽象接口, 用户实现)
│   │   │               ├── alloc(addr, type, size, wr, cycle, streamID) → mem_fetch*
│   │   │               └── alloc(addr, type, mask, byte_mask, sector_mask,
│   │   │                          size, wr, cycle, wid, sid, tpc, orig, streamID) → mem_fetch*
│   │   │
│   │   ├── 参数-3: time : unsigned  (当前时钟周期)
│   │   │           │
│   │   │           └── 用途: 更新 cache_block_t.m_last_access_time (LRU 排序依据)
│   │   │                     记录 m_alloc_time (FIFO 排序依据)
│   │   │                     传递给 MSHR 和 miss_queue 的上报函数
│   │   │
│   │   ├── 参数-4: events : std::list<cache_event>&  [出参]
│   │   │           │
│   │   │           └── cache_event 结构
│   │   │               ├── m_cache_event_type : enum cache_event_type
│   │   │               │   ├── WRITE_BACK_REQUEST_SENT    (驱逐脏行产生的写回)
│   │   │               │   ├── READ_REQUEST_SENT          (未命中产生的读请求)
│   │   │               │   ├── WRITE_REQUEST_SENT         (写穿透产生的写请求)
│   │   │               │   └── WRITE_ALLOCATE_SENT        (写分配产生的分配请求)
│   │   │               │
│   │   │               └── m_evicted_block : evicted_block_info  (仅写回事件有效)
│   │   │                   ├── m_block_addr    → new_addr_type              (被驱逐块地址)
│   │   │                   ├── m_modified_size → unsigned                   (脏数据大小)
│   │   │                   ├── m_byte_mask     → mem_access_byte_mask_t     (脏字节掩码)
│   │   │                   └── m_sector_mask   → mem_access_sector_mask_t   (脏 sector 掩码)
│   │   │
│   │   │   辅助函数:
│   │   │     was_write_sent(events)          → bool  (是否包含 WRITE_BACK/WRITE 事件)
│   │   │     was_read_sent(events)           → bool  (是否包含 READ_REQUEST 事件)
│   │   │     was_writeallocate_sent(events)  → bool  (是否包含 WRITE_ALLOCATE 事件)
│   │   │
│   │   └── 返回值: cache_request_status (7 种状态)
│   │       ├── HIT              → 命中, 数据立即可用, 延迟 = ceil(data_size/data_port_width)
│   │       ├── HIT_RESERVED     → 命中但行处于 RESERVED 状态 (尚未 fill 完成)
│   │       ├── MISS             → 未命中, tag_array 已分配行 + MSHR 已分配条目
│   │       ├── SECTOR_MISS      → Sector 缓存: 该 sector 未命中, 行有效但 sector 需 fill
│   │       ├── MSHR_HIT         → 同地址已有未完成 MSHR 条目, 请求已合并到该条目
│   │       └── RESERVATION_FAIL → 无法接受请求, 原因见 cache_reservation_fail_reason:
│   │           ├── LINE_ALLOC_FAIL     (所有行都被 RESERVED, 无可替换行)
│   │           ├── MISS_QUEUE_FULL     (miss_queue 已满)
│   │           ├── MSHR_ENRTY_FAIL     (MSHR 无空闲条目)
│   │           ├── MSHR_MERGE_ENRTY_FAIL (MSHR 合并条目已满)
│   │           └── MSHR_RW_PENDING     (该地址有 pending read-after-write)
│   │
│   ├── (B) 下级通信接口 ─── mem_fetch_interface (抽象接口, 用户实现)
│   │   │
│   │   ├── 接口定义
│   │   │   ├── virtual full(size, write) → bool
│   │   │   │   ├── 参数: size    : unsigned  (请求数据大小, Bytes)
│   │   │   │   ├── 参数: write   : bool      (是否写请求)
│   │   │   │   └── 返回: true 表示下级队列已满, 需反压暂停发送
│   │   │   │
│   │   │   └── virtual push(mf) → void
│   │   │       ├── 参数: mf : mem_fetch*  (待发送的请求, 所有权转移给下级)
│   │   │       └── 语义: 将 mf 推入下级存储队列, cache 不再保留引用
│   │   │
│   │   ├── 测试桩: simple_mem_interface : public mem_fetch_interface
│   │   │   ├── 构造: simple_mem_interface(max_size=256)
│   │   │   │   └── max_queue_size : unsigned  (FIFO 队列最大深度)
│   │   │   │
│   │   │   ├── 内部: std::list<mem_fetch*> queue  (FIFO 队列, 存储待处理请求)
│   │   │   │
│   │   │   ├── full(size, write) → bool
│   │   │   │   └── 实现: return queue.size() >= max_queue_size
│   │   │   │
│   │   │   └── push(mf) → void
│   │   │       └── 实现: queue.push_back(mf)
│   │   │
│   │   └── 适配器模式示例: CacheMemAdapter : public mem_fetch_interface
│   │       ├── 用途: 将 L2 (cache_t) 包装为 mem_fetch_interface,
│   │       │         使 L1 可直接将 L2 作为下级存储连接
│   │       ├── 构造: CacheMemAdapter(read_only_cache* downstream)
│   │       ├── full(size, write) → incoming.size() >= 64
│   │       ├── push(mf)          → incoming.push_back(mf)
│   │       └── drain_to_cache(cycle) → 将 incoming 队列逐个送入 downstream->access()
│   │
│   ├── (C) 下级返回接口 ─── fill(mf, time)
│   │   │
│   │   ├── 签名: void baseline_cache::fill(mem_fetch *mf, unsigned time)
│   │   │
│   │   ├── 参数: mf   : mem_fetch*  (下级存储返回的请求, 数据已就绪)
│   │   ├── 参数: time : unsigned    (当前时钟周期)
│   │   │
│   │   ├── 内部行为:
│   │   │   1. 提取 mf 的 block_addr + sector_mask
│   │   │   2. 调用 tag_array::fill() → 将对应行状态从 RESERVED 更新为 VALID/MODIFIED
│   │   │   3. 调用 mshr_table::mark_ready() → MSHR 中标记该地址就绪
│   │   │   4. 上层通过 access_ready()/next_access() 感知完成
│   │   │
│   │   └── 调用时机: 下级存储处理完请求后, 由模拟主循环调用
│   │       for each cycle:
│   │         while (dram.queue 有响应):
│   │           resp = dram.queue.pop_front()
│   │           cache.fill(resp, cycle)
│   │
│   ├── (D) 就绪读取接口 ─── access_ready() / next_access()
│   │   │
│   │   ├── access_ready() → bool
│   │   │   └── 实现: return m_mshrs.access_ready()  (MSHR 内部 m_current_response 非空)
│   │   │
│   │   └── next_access() → mem_fetch*
│   │       └── 实现: return m_mshrs.next_access()   (弹出 MSHR 队列中下一个完成的请求)
│   │
│   ├── (E) 控制接口 ─── flush() / invalidate()
│   │   │
│   │   ├── flush()      → 遍历所有行: MODIFIED → 写回下级 → 标记 INVALID
│   │   └── invalidate() → 遍历所有行: 直接标记 INVALID (不写回, 丢弃脏数据)
│   │
│   └── (F) 带宽查询接口 ─── data_port_free() / fill_port_free()
│       │
│       ├── data_port_free() → bool
│       │   └── 实现: return bandwidth_management::data_port_free()
│       │            (m_data_port_occupied_cycles == 0)
│       │
│       └── fill_port_free() → bool
│           └── 实现: return bandwidth_management::fill_port_free()
│                    (m_fill_port_occupied_cycles == 0)
│
└── 3. 调度接口 ─── 创建实例 + 驱动时钟
    │
    ├── 缓存构造函数 (6 种缓存类型)
    │   ├── baseline_cache(name, config, core_id, type_id, memport, status, level, gpu)
    │   ├── read_only_cache(name, config, core_id, type_id, memport, status, level, gpu)
    │   ├── data_cache(name, config, core_id, type_id, memport, mfcreator,
    │   │               status, wr_alloc_type, wrbk_type, gpu, level)
    │   ├── l1_cache(name, config, core_id, type_id, memport, mfcreator, status, gpu, level)
    │   ├── l2_cache(name, config, core_id, type_id, memport, mfcreator, status, gpu, level)
    │   └── tex_cache(name, config, core_id, type_id, memport, request_status, rob_status)
    │
    ├── cycle() ─── 将 miss_queue 中请求发送到下级, 释放端口配额
    │
    └── 主循环范式 (所有接口的调用序列)
        for each cycle:
          1. access()  → 发送请求, 检查返回状态
          2. cycle()   → 推进时钟, miss→下级
          3. fill()    → 下级数据返回, 注入缓存
          4. access_ready()/next_access() → 取出完成请求
          5. get_sub_stats() → 周期结束或批量获取统计
```

> 上图即完整接口清单。以下三节逐一对配置接口、数据接口、调度接口展开详述。

### 1.1 配置接口

缓存的全部行为——几何尺寸、时序、策略——由 `cache_config` 统一控制。用户通过两种方式配置：**配置字符串**（GPGPU-Sim 兼容）或**代码直接赋值**。

#### 1.1.1 cache_config 数据结构

每个字段按功能分为三组：**几何参数**、**时序/吞吐量参数**、**策略参数**。

**几何参数**（决定缓存容量和布局）：

| 字段 | 类型 | 含义 | 约束 |
|------|------|------|------|
| `m_cache_type` | `cache_type` | `NORMAL`：以整行为粒度；`SECTOR`：以 32B sector 为粒度 | Sector 要求 `line_sz = 32×4 = 128` |
| `m_nset` | `unsigned` | Set 数量 | 2 的幂 |
| `m_line_sz` | `unsigned` | 行大小 (Bytes) | 32/64/128/256 |
| `m_assoc` | `unsigned` | 关联度（每个 set 的 way 数） | 1/2/4/8/16/24 |

派生量：`m_line_sz_log2`、`m_nset_log2`、`m_atom_sz`（Sector 缓存为 `SECTOR_SIZE=32`，Normal 缓存为 `m_line_sz`）。

**时序/吞吐量参数**（决定带宽和延迟行为）：

| 字段 | 类型 | 含义 | 默认值 |
|------|------|------|--------|
| `m_data_port_width` | `unsigned` | 每个周期数据端口可传输的字节数 | `m_line_sz` |
| `m_mshr_entries` | `unsigned` | MSHR 条目数（最大并行未命中数） | 配置字符串指定 |
| `m_mshr_max_merge` | `unsigned` | 同一地址最多合并的请求数 | 配置字符串指定 |
| `m_miss_queue_size` | `unsigned` | Miss queue 深度 | 配置字符串指定 |

**策略参数**（决定命中/未命中时的行为）：

| 字段 | 类型 | 可选值 | 含义 |
|------|------|--------|------|
| `m_write_policy` | `write_policy_t` | `READ_ONLY` / `WRITE_BACK` / `WRITE_THROUGH` / `WRITE_EVICT` / `LOCAL_WB_GLOBAL_WT` | 写命中策略 |
| `m_write_alloc_policy` | `write_allocate_policy_t` | `NO_WRITE_ALLOCATE` / `WRITE_ALLOCATE` / `FETCH_ON_WRITE` / `LAZY_FETCH_ON_READ` | 写未命中（分配）策略 |
| `m_alloc_policy` | `allocation_policy_t` | `ON_MISS` / `ON_FILL` / `STREAMING` | 缓存行分配时机 |
| `m_replacement_policy` | `replacement_policy_t` | `LRU` / `FIFO` | 替换策略 |
| `m_set_index_function` | `set_index_function` | `LINEAR` / `BITWISE_XORING` / `HASH_IPOLY` / `FERMI_HASH` / `CUSTOM` | Set 索引函数 |
| `m_mshr_type` | `mshr_config_t` | `ASSOC` / `SECTOR_ASSOC` / `TEX_FIFO` / `SECTOR_TEX_FIFO` | MSHR 类型 |
| `m_wr_percent` | `unsigned` | 0–100 | 脏行保护阈值：脏行比例低于此值时只驱逐干净行 |

**内部派生量**（初始化后自动计算，用户无需设置）：

```
m_line_sz_log2     = LOGB2(m_line_sz)
m_nset_log2        = LOGB2(m_nset)
m_atom_sz          = (m_cache_type == SECTOR) ? SECTOR_SIZE : m_line_sz
m_sector_sz_log2   = LOGB2(SECTOR_SIZE)
总大小 (KB)        = (m_assoc × m_nset × m_line_sz) / 1024
总行数              = m_nset × m_assoc
```

#### 1.1.2 配置字符串格式（GPGPU-Sim 兼容）

```
格式:
<cache_type>:<nsets>:<bsize>:<assoc>,<rep>:<wr>:<alloc>:<wr_alloc>:<set_idx>,<mshr>:<N>:<merge>,<mq>[,<data_port_width>]

简写:
<nsets>:<bsize>:<assoc>    (其余使用默认值)
```

**逐字段说明：**

| 位置 | 字段 | 字符→含义 | 说明 |
|------|------|-----------|------|
| 1 | 缓存类型 | `N` = Normal, `S` = Sector | Sector 以 32B sector 为管理粒度 |
| 2 | Set 数 | 如 32, 64, 128, 256 | 必须是 2 的幂 |
| 3 | 行大小 (B) | 32, 64, 128, 256 | Sector 缓存要求 line_sz = 32×4 = 128 |
| 4 | 关联度 | 1, 2, 4, 8, 16, 24 | 每个 set 的 way 数 |
| 5 | 替换策略 | `L` = LRU, `F` = FIFO | LRU 基于 `m_last_access_time`；FIFO 基于 `m_alloc_time` |
| 6 | 写策略 | `R` = ReadOnly, `B` = WriteBack, `T` = WriteThrough, `E` = WriteEvict, `L` = LocalWB+GlobalWT | 写命中时的行为 |
| 7 | 分配策略 | `m` = ON_MISS, `f` = ON_FILL, `s` = STREAMING | ON_FILL 适合流式访问 |
| 8 | 写分配策略 | `N` = NoWA, `W` = WriteAlloc, `F` = FetchOnWrite, `L` = LazyFetchOnRead | 写未命中时的分配行为 |
| 9 | 索引函数 | `L` = Linear, `X` = BitwiseXOR, `H` = FermiHash, `P` = IPoly, `C` = Custom | 决定地址到 set 的映射 |
| 10 | MSHR 类型 | `A` = Assoc, `S` = SectorAssoc, `F` = TexFIFO, `T` = SectorTexFIFO | 普通缓存用 A/S |
| 11 | MSHR 条目数 | 16, 32, 64, 128 | 越多 → 越高 MLP |
| 12 | MSHR 最大合并 | 4, 8 | 同一地址最多合并的请求数 |
| 13 | Miss Queue 大小 | 32, 64, 128, 256 | 缓冲未命中请求 |
| 14 | 数据端口宽度 (选填) | 默认 = line_sz | 每周期可传输字节数 |

**配置字符串解析示例：**

```cpp
cache_config cfg;
cfg.m_config_string = (char*)"S:32:128:4,L:E:m:F:X,A:32:4,64";
cfg.init(cfg.m_config_string, FuncCachePreferNone);
// 解析结果:
//   SECTOR cache, 32 sets, 128B line, 4-way assoc
//   LRU 替换, WRITE_EVICT 写策略, ON_MISS 分配, FETCH_ON_WRITE 写分配
//   BITWISE_XOR 索引, ASSOC MSHR, 32 MSHR entries, 最大合并 4
//   64-entry miss queue, data_port_width = 128 (默认 = line_sz)
```

**非法配置检测**（`init()` 中自动触发 `assert(0)`）：

- `ON_FILL` + `WRITE_BACK`：会导致死锁（fill 驱逐脏行 → 写回阻塞 → 写回导致 fill 阻塞）
- `FETCH_ON_WRITE` / `LAZY_FETCH_ON_READ` + `ON_FILL`：fetch 策略要求 `ON_MISS`
- Sector 缓存要求 `line_sz == SECTOR_SIZE * SECTOR_CHUNCK_SIZE`（即 32×4=128）

#### 1.1.3 参数化配置（代码直接赋值）

除了配置字符串，也可以直接设置 `cache_config` 的成员变量：

```cpp
cache_config cfg;
cfg.m_cache_type         = SECTOR;              // 缓存类型
cfg.m_nset               = 32;                  // set 数量
cfg.m_line_sz            = 128;                 // 行大小 (B)
cfg.m_assoc              = 4;                   // 关联度
cfg.m_replacement_policy = LRU;                 // 替换策略
cfg.m_write_policy       = LOCAL_WB_GLOBAL_WT;  // 写策略
cfg.m_alloc_policy       = ON_MISS;             // 分配策略
cfg.m_write_alloc_policy = LAZY_FETCH_ON_READ;  // 写分配策略
cfg.m_set_index_function = BITWISE_XORING_FUNCTION;
cfg.m_mshr_type          = SECTOR_ASSOC;        // MSHR 类型
cfg.m_mshr_entries       = 32;                  // MSHR 条目数
cfg.m_mshr_max_merge     = 4;                   // 最大合并数
cfg.m_miss_queue_size    = 64;                  // Miss queue 大小
cfg.m_data_port_width    = 32;                  // 数据端口宽度 (B/cycle)
cfg.m_wr_percent         = 50;                  // 脏行保护阈值
```

> **注意**：直接赋值后仍需要调用 `init()` 完成派生量（如 `m_line_sz_log2`）的计算和合法性检查。如果同时设置了 `m_config_string`，则 `init(config_string, ...)` 会解析字符串覆盖手动赋值的字段。实践中二选一即可。

#### 1.1.4 Throughput 和 Latency 的配置方式

**吞吐量（Throughput）由三个参数共同决定：**

```
峰值带宽 = data_port_width × 频率
最大并行未命中数 (MLP) ≈ mshr_entries
反压承受能力 = miss_queue_size
```

| 参数 | 作用 | 吞吐量影响 |
|------|------|-----------|
| `m_data_port_width` | 每周期数据端口可传输字节数 | **直接决定峰值带宽** |
| `m_mshr_entries` | 并行未命中请求数上限 | **限制 MLP** |
| `m_miss_queue_size` | 未命中请求缓冲深度 | **防止反压丢请求** |
| `m_mshr_max_merge` | 同地址多请求合并 | **减少下级带宽消耗** |

每个参数对性能的影响和代价：

| 参数 | 增大效果 | 代价 |
|------|----------|------|
| `m_nset` | 减少冲突 miss | 增大面积 |
| `m_assoc` | 减少冲突 miss | 增加比较器/功耗 |
| `m_line_sz` | 利用空间局部性 | 增大 fill 延迟, 浪费带宽 |
| `m_mshr_entries` | 提升 MLP（并行未命中） | 面积 |
| `m_mshr_max_merge` | 减少对下级的重复请求 | 合并等待时间变长 |
| `m_miss_queue_size` | 缓冲更多未命中请求 | 面积 |
| `m_data_port_width` | 更高带宽 (B/cycle) | 布线/功耗 |

**延迟（Latency）由两个层面控制：**

**(1) 缓存命中延迟** — 由 `m_data_port_width` 间接决定：

```
data_cycles = ceil(data_size / m_data_port_width)
```

对于 L1 缓存：设 `data_port_width = 128`（与 line_sz 等宽），则 128B 传输只需 1 周期。
对于 L2 缓存：设 `data_port_width = 32`，则 128B 传输需要 4 周期。

**(2) Fill 延迟** — 由下级 `mem_fetch_interface` 在外部控制，**不在 `cache_config` 中配置**：

```
caller 控制的延迟 = fill() 被调用的时刻 - access() 返回 MISS 的时刻
```

**端口占用计算：**

```
// 读命中: 数据端口占用 = ceil(data_size / data_port_width)
// 写回 (脏行驱逐): 数据端口占用 = evicted.modified_size / data_port_width
// Fill: fill 端口占用 = atom_sz / data_port_width
```

**典型延迟配置对比：**

```cpp
// 低延迟 L1: 宽端口，快速响应
cfg.m_data_port_width = 128;  // 1 周期完成 128B 传输 → 命中延迟 1 cycle

// 高带宽 L2: 窄端口，多周期传输
cfg.m_data_port_width = 32;   // 4 周期完成 128B 传输 → 命中延迟 4 cycles
```

**高吞吐量配置示例：**

```cpp
// GPU L2: 最大化吞吐量 — 64 MSHR × 8 合并 = 可同时处理 512 个请求
"N:256:128:16,L:B:m:F:X,A:64:8,128,32"

// 流式缓存: 最大化 MSHR — 128 MSHR × 1 合并 (不合并, 避免 stall)
"N:16:128:4,F:E:f:N:L,A:128:1,256"
```

#### 1.1.5 常用配置示例

```cpp
// === L1 数据缓存 (Fermi 风格): 16KB, sector, write-evict ===
"S:32:128:4,L:E:m:F:X,A:32:4,64"

// === L1 数据缓存 (Volta 风格): 32KB, 局部写回, 全局写驱逐 ===
"S:64:128:4,L:L:m:F:X,A:32:4,64"

// === L2 共享缓存: 512KB, write-back, 16-way ===
"N:256:128:16,L:B:m:F:X,A:64:8,128"

// === L2 共享缓存 (大): 2MB, write-back, 16-way ===
"N:1024:128:16,L:B:m:F:X,A:128:8,256"

// === 只读指令缓存: 4KB ===
"N:16:64:4,L:R:m:N:L,A:8:2,16"

// === 纹理缓存: 32KB, sector, FIFO ===
"S:64:128:4,L:R:m:N:P,F:128:4,128:2"

// === 写穿透缓存 ===
"N:64:64:8,L:T:m:N:L,A:16:4,32"

// === 流式缓存 (memcpy 优化): ON_FILL + FIFO ===
"N:16:128:4,F:E:f:N:L,A:128:1,256"
```

---

### 1.2 数据接口

数据接口定义了请求如何进入缓存、如何向下级发送、如何从下级返回、以及如何被上层取走。

#### 1.2.1 请求接口：`access()`

所有缓存访问的入口。这是 `cache_t` 抽象基类定义的纯虚函数：

```cpp
class cache_t {
public:
    virtual enum cache_request_status access(
        new_addr_type addr,           // 请求地址
        mem_fetch *mf,                // 请求对象（包含类型、大小等所有元数据）
        unsigned time,                // 当前时间戳（用于更新 LRU 信息）
        std::list<cache_event> &events // [输出] 产生的事件列表（写回、读请求等）
    ) = 0;
};
```

**调用方式：**

```cpp
std::list<cache_event> events;
enum cache_request_status status = cache.access(mf->get_addr(), &mf, current_time, events);
```

**返回值 `cache_request_status` 含义：**

| 状态 | 含义 | 上层动作 |
|------|------|----------|
| `HIT` | 命中，数据立即可用 | 直接消费数据，延迟 = 命中延迟 |
| `HIT_RESERVED` | 命中但 block 尚未填充完成 | 等待 `access_ready()` → `next_access()` |
| `MISS` | 未命中，已分配 MSHR 条目 | 等待 `cycle()` 发送 + 下级返回 + `fill()` |
| `SECTOR_MISS` | Sector 缓存中该 sector 未命中 | 同 MISS，但只填充对应 sector |
| `MSHR_HIT` | 该地址已有未完成的 MSHR 条目 | 请求已合并，等待同一条目完成 |
| `RESERVATION_FAIL` | 无法接受请求 | 重试或阻塞，下一周期再发 |

**`cache_event` 事件处理：**

```cpp
struct cache_event {
    enum cache_event_type m_cache_event_type;   // 事件类型
    evicted_block_info m_evicted_block;          // 如果是写回事件，包含被驱逐块信息
};

// 事件类型:
//   WRITE_BACK_REQUEST_SENT  — 驱逐脏行产生的写回请求
//   READ_REQUEST_SENT        — 未命中产生的读请求
//   WRITE_REQUEST_SENT       — 写穿透产生的写请求
//   WRITE_ALLOCATE_SENT      — 写分配产生的分配请求

// 辅助函数:
bool was_write_sent(const std::list<cache_event> &events);         // 是否包含写回/写穿
bool was_read_sent(const std::list<cache_event> &events);          // 是否包含读请求
bool was_writeallocate_sent(const std::list<cache_event> &events); // 是否包含写分配
```

#### 1.2.2 请求数据结构

**`mem_access_t`** — 描述一次内存访问的属性：

```cpp
struct mem_access_t {
    enum mem_access_type m_type;                    // 访问类型
    new_addr_type         m_addr;                   // 目标地址
    unsigned              m_size;                   // 请求大小 (Bytes)
    bool                  m_is_write;               // 是否为写请求
    active_mask_t         m_access_mask;            // 活跃线程掩码 (bitset<64>)
    mem_access_byte_mask_t   m_byte_mask;           // 有效字节掩码 (bitset<128>)
    mem_access_sector_mask_t m_sector_mask;         // 有效 sector 掩码 (bitset<4>)
};
```

**`mem_access_type` 枚举：**

| 枚举值 | 含义 | 用途 |
|--------|------|------|
| `GLOBAL_ACC_R` | 全局内存读 | SM 的全局 load |
| `LOCAL_ACC_R` | 局部内存读 | SM 的局部 load |
| `CONST_ACC_R` | 常量内存读 | 常量缓存 |
| `TEXTURE_ACC_R` | 纹理内存读 | 纹理缓存 |
| `GLOBAL_ACC_W` | 全局内存写 | SM 的全局 store |
| `LOCAL_ACC_W` | 局部内存写 | SM 的局部 store |
| `L1_WRBK_ACC` | L1 写回 | L1→L2 的写回流量 |
| `L2_WRBK_ACC` | L2 写回 | L2→DRAM 的写回流量 |
| `INST_ACC_R` | 指令读 | 指令缓存 |
| `L1_WR_ALLOC_R` | L1 写分配读 | L1 写未命中时发送的分配读 |
| `L2_WR_ALLOC_R` | L2 写分配读 | L2 写未命中时发送的分配读 |

**`mem_fetch`** — 封装一次完整的访存请求，是整个缓存系统的数据载体：

```cpp
class mem_fetch {
    // 核心访问器（缓存代码依赖）
    new_addr_type get_addr() const;                   // 请求地址
    unsigned get_data_size() const;                    // 数据大小
    bool is_write() const;                             // 是否写请求
    enum mem_access_type get_access_type() const;      // 访问类型
    unsigned long long get_streamID() const;           // CUDA stream ID
    mem_access_byte_mask_t get_access_byte_mask() const;    // 有效字节掩码
    mem_access_sector_mask_t get_access_sector_mask() const; // sector 掩码
    const warp_inst_t& get_inst();                     // 对应的 warp 指令
};
```

#### 1.2.3 从下级 Memory 抓数据的请求接口：`mem_fetch_interface`

缓存不直接访问 DRAM。它通过 `mem_fetch_interface` 抽象接口与下级存储通信：

```cpp
class mem_fetch_interface {
public:
    virtual bool full(unsigned size, bool write) const = 0;  // 下级队列是否已满
    virtual void push(mem_fetch *mf) = 0;                     // 将请求推入下级队列
};
```

**工作方式：** 缓存通过 `cycle()` 从内部 `miss_queue` 取出请求，调用下级的 `push()`。上层需要在下级中实现 `full()`/`push()`。

**测试用具体实现 `simple_mem_interface`：**

```cpp
class simple_mem_interface : public mem_fetch_interface {
    std::list<mem_fetch *> queue;
    unsigned max_queue_size;
public:
    simple_mem_interface(unsigned max_size = 256);
    bool full(unsigned, bool) const override;    // queue.size() >= max_queue_size
    void push(mem_fetch *mf) override;            // queue.push_back(mf)
};
```

**与下级交互的完整流程：**

```
1. cache.access() 返回 MISS
   → 请求内部进入 miss_queue，MSHR 分配条目
2. cache.cycle()
   → 从 miss_queue 取出，调用 lower_mem->push(mf)
3. 外部（upper-level 代码）检测到 lower_mem->queue 中有数据
   → 调用下级缓存/内存的 access()
4. 下级返回数据
   → 调用 cache.fill(mf, time)
```

#### 1.2.4 下级 Memory 返回给 Cache 的数据接口：`fill()`

当数据从下级存储返回时，调用 `fill()` 将请求注入缓存流水线：

```cpp
// baseline_cache::fill() — 处理下级存储的响应
void fill(mem_fetch *mf, unsigned time);

// 调用方式:
cache.fill(&returned_mf, current_time);
```

**`fill()` 内部行为：**
1. 将 `mem_fetch` 的数据写入 tag array 对应行（更新状态为 VALID/MODIFIED）
2. 在 MSHR 中标记对应地址为 ready
3. 上层通过 `access_ready()` / `next_access()` 取走就绪的请求

**数据返回的完整时序：**

```
cycle 0:   access() → MISS (请求进入 miss_queue + MSHR)
cycle 1-N: cycle() → miss_queue → lower_mem->push()
cycle N+M: lower_mem 处理完成 → cache.fill(mf, N+M)
cycle N+M: MSHR mark_ready → access_ready() 变为 true
cycle N+M: next_access() → 取出完成的请求
```

#### 1.2.5 Invalidate / Flush 接口

```cpp
// 写回所有脏行（标记为 MODIFIED 的行）到下级存储
// 行为: 遍历所有行，将 MODIFIED 行写回后标记为 INVALID
cache.flush();

// 直接无效化所有行（不写回，丢弃脏数据）
// 行为: 遍历所有行，直接标记为 INVALID
cache.invalidate();
```

内部实现通过 `tag_array`：

```cpp
void baseline_cache::flush()      { m_tag_array->flush(); }
void baseline_cache::invalidate() { m_tag_array->invalidate(); }
```

#### 1.2.6 带宽/端口查询接口

在发送请求前检查端口是否可用，避免带宽超限：

```cpp
// 数据端口 — 用于 access() 的 hit/miss 数据传输
if (cache.data_port_free()) {
    // 可以发出新请求
}

// Fill 端口 — 用于 fill() 的数据接收
if (cache.fill_port_free()) {
    // 可以接收 fill 响应
}
```

端口管理由内部 `bandwidth_management` 类自动完成，每个 `cycle()` 调用 `replenish_port_bandwidth()` 释放一个周期的带宽配额。

**端口占用规则：**

| 操作 | 占用端口 | 占用周期数 |
|------|---------|-----------|
| 读命中 | data port | `ceil(data_size / data_port_width)` |
| 写回驱逐 | data port | `modified_size / data_port_width` |
| Fill | fill port | `atom_sz / data_port_width` |

#### 1.2.7 读取就绪请求：`access_ready()` / `next_access()`

对于 `MISS` / `SECTOR_MISS` / `MSHR_HIT` 的请求，数据不会立即可用。在上层填充完成后，通过以下接口取回：

```cpp
// 检查是否有等待填充完成的请求现在就绪
while (cache.access_ready()) {
    mem_fetch *ready = cache.next_access();
    // 处理已完成的请求（记录延迟、返回给请求方等）
}
```

> **注意**：`access_ready()` / `next_access()` 返回的是经过完整 miss→fill 流程最终完成的请求。对于 `HIT`（立即可用的请求），不走此路径 —— 它们直接由 `access()` 返回值告知。

---

### 1.3 调度接口

调度接口定义了**如何创建缓存实例**和**如何推进模拟时钟**。

#### 1.3.1 主程序的调用接口：构造函数

六种缓存类型的构造函数签名及适用场景：

```cpp
// (1) baseline_cache — 公共基类（通常不直接使用）
baseline_cache(name, config, core_id, type_id, memport, status, level, gpu);

// (2) read_only_cache — 只读缓存（指令/常量/纹理类）
read_only_cache(name, config, core_id, type_id, memport, status, level, gpu);

// (3) data_cache — 通用读写缓存（可配置任意写策略）
data_cache(name, config, core_id, type_id, memport, mfcreator,
           status, wr_alloc_type, wrbk_type, gpu, level);

// (4) l1_cache — L1 数据缓存（自动设置 L1 的 wr_alloc 和 wrbk 类型）
l1_cache(name, config, core_id, type_id, memport, mfcreator, status, gpu, level);

// (5) l2_cache — L2 共享缓存（自动设置 L2 的 wr_alloc 和 wrbk 类型）
l2_cache(name, config, core_id, type_id, memport, mfcreator, status, gpu, level);

// (6) tex_cache — 纹理缓存（FIFO 流水线模型，Igehy 1998）
tex_cache(name, config, core_id, type_id, memport, request_status, rob_status);
```

**构造函数参数说明：**

| 参数 | 类型 | 含义 |
|------|------|------|
| `name` | `const char*` | 缓存名称（统计输出用） |
| `config` | `cache_config&` | 缓存配置（所有参数已初始化） |
| `core_id` | `int` | 所属的 shader core / SM ID |
| `type_id` | `int` | 缓存类型 ID（同一 core 可有多种缓存） |
| `memport` | `mem_fetch_interface*` | 连接到下级存储的接口 |
| `status` | `mem_fetch_status` | miss queue 中请求的状态标记 |
| `level` | `cache_gpu_level` | `L1_GPU_CACHE` / `L2_GPU_CACHE` / `OTHER_GPU_CACHE` |
| `gpu` | `gpgpu_sim*` | GPU 模拟器指针 |
| `mfcreator` | `mem_fetch_allocator*` | mem_fetch 分配器（data_cache 需要，用于创建写回/写分配请求） |
| `wr_alloc_type` | `mem_access_type` | 写分配请求类型：`L1_WR_ALLOC_R` 或 `L2_WR_ALLOC_R` |
| `wrbk_type` | `mem_access_type` | 写回请求类型：`L1_WRBK_ACC` 或 `L2_WRBK_ACC` |

#### 1.3.2 如何推进时钟：`cycle()`

每个时钟周期调用一次 `cycle()`，其作用是**将 miss queue 中的未命中请求发送到下级存储**：

```cpp
cache.cycle();
```

**`cycle()` 内部行为：**
1. 检查 `miss_queue` 是否有待发送请求
2. 检查下级 `mem_fetch_interface::full()` 是否可接受
3. 调用 `lower_mem->push(mf)` 发送请求
4. 调用 `bandwidth_management::replenish_port_bandwidth()` 释放周期配额

#### 1.3.3 调度接口总览：核心模拟循环

所有接口在一次主模拟循环中如何配合。以下是**唯一需要用户维护的控制流**：

```cpp
// === 缓存模型的核心调度循环（伪代码）===

for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {

    // [Step 1] 发送新请求 → access()
    if (has_pending_request()) {
        mem_fetch *mf = create_request(...);
        std::list<cache_event> events;
        enum cache_request_status s = cache.access(mf->get_addr(), mf, cycle, events);

        switch (s) {
        case HIT:
            // 数据立即可用，记录完成
            record_hit(mf, cycle);
            break;
        case MISS:
        case SECTOR_MISS:
        case MSHR_HIT:
            // 请求已入 MSHR / miss queue，等待 fill
            break;
        case RESERVATION_FAIL:
            // 重试：下一周期再发，或阻塞 pipeline
            retry_later(mf);
            break;
        }
    }

    // [Step 2] 推进缓存时钟 → cycle()
    //    将 miss_queue 中的请求推送到下级存储
    cache.cycle();

    // [Step 3] 处理下级返回的数据 → fill()
    if (lower_memory_has_response()) {
        mem_fetch *resp = get_lower_memory_response();
        cache.fill(resp, cycle);
    }

    // [Step 4] 取走就绪的请求 → access_ready() / next_access()
    while (cache.access_ready()) {
        mem_fetch *ready = cache.next_access();
        record_complete(ready, cycle);
    }
}

// [Step 5] 打印统计
cache_sub_stats css;
cache.get_sub_stats(css);
printf("Hit Rate: %.2f%%\n", 100.0 * (1.0 - (double)css.misses / css.accesses));
```

---

## 2. 使用示例

以下示例把**配置接口、数据接口、调度接口、统计接口**全部串起来，展示一个完整的 L1+L2 两级缓存系统集成。

```cpp
#include "gpu_cache_ref.h"
#include <cstdio>
#include <unordered_map>

// ============================================================
// 完整集成示例: L1 (私有, write-evict) + L2 (共享, write-back) + DRAM
// 覆盖:
//   配置接口:   cache_config 配置字符串 + 参数化配置
//   数据接口:   access() → fill() → access_ready() / next_access()
//   调度接口:   cycle() 驱动两级缓存
//   统计接口:   get_sub_stats() 获取性能指标
//   控制接口:   flush() / data_port_free()
// ============================================================

int main() {
    // --------------------------------------------------
    // 1. 配置接口: 创建 L1 和 L2 的 cache_config
    // --------------------------------------------------

    // L1: 16KB sector cache, write-evict (Fermi 风格)
    cache_config l1_cfg;
    l1_cfg.m_config_string = (char*)"S:32:128:4,L:E:m:F:X,A:32:4,64";
    l1_cfg.init(l1_cfg.m_config_string, FuncCachePreferNone);
    printf("L1 Config: %uKB, %u sets, %uB line, %u-way\n",
           l1_cfg.get_total_size_inKB(), l1_cfg.get_nset(),
           l1_cfg.get_line_sz(), 4u);
    // 还可以运行时修改: l1_cfg.m_wr_percent = 50;

    // L2: 512KB normal cache, write-back
    cache_config l2_cfg;
    l2_cfg.m_config_string = (char*)"N:256:128:16,L:B:m:F:X,A:64:8,128";
    l2_cfg.init(l2_cfg.m_config_string, FuncCachePreferNone);
    printf("L2 Config: %uKB, %u sets, %uB line, 16-way\n",
           l2_cfg.get_total_size_inKB(), l2_cfg.get_nset(),
           l2_cfg.get_line_sz());

    // --------------------------------------------------
    // 2. 创建下级存储和辅助对象
    // --------------------------------------------------
    simple_mem_interface dram(512);        // DRAM 接口
    simple_mf_allocator allocator;         // mem_fetch 分配器
    gpgpu_sim gpu;                         // GPU 模拟器存根

    // --------------------------------------------------
    // 3. 创建缓存实例
    // --------------------------------------------------
    l2_cache l2("L2_Shared", l2_cfg, 0, 0, &dram, &allocator,
                IN_PARTITION_L2_TO_DRAM_QUEUE, &gpu, L2_GPU_CACHE);

    // L1 的下级是 L2 — 需要一个 bridge 队列
    simple_mem_interface l1_to_l2_bridge(128);

    l1_cache l1("L1D_SM0", l1_cfg, 0, 0, &l1_to_l2_bridge, &allocator,
                IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

    // --------------------------------------------------
    // 4. 主模拟循环（调度接口）
    // --------------------------------------------------
    int l1_hits = 0, l1_misses = 0, l1_res_fails = 0;
    int l2_hits = 0, l2_misses = 0, l2_res_fails = 0;

    for (unsigned long long cycle = 0; cycle < 1000; cycle++) {

        // --- 4a. 数据接口: access() 发送请求到 L1 ---
        new_addr_type addr = (cycle * 317 + 101) & 0x1FFFF;
        bool is_write = (cycle % 3 == 0);  // 每 3 个请求中有 1 个写

        mem_access_sector_mask_t smask; smask.set(0);
        mem_access_t access(
            is_write ? GLOBAL_ACC_W : GLOBAL_ACC_R,
            addr, 4, is_write,
            active_mask_t(), mem_access_byte_mask_t(), smask
        );
        warp_inst_t *inst = new warp_inst_t();
        if (is_write) { inst->m_is_store = true; inst->m_is_write = true; }
        else { inst->m_is_load = true; }
        mem_fetch *mf = new mem_fetch(access, inst, 0, 0, 0, 0, 0, NULL, cycle);

        std::list<cache_event> events;
        enum cache_request_status s1 = l1.access(mf->get_addr(), mf, cycle, events);

        if (s1 == HIT || s1 == HIT_RESERVED) l1_hits++;
        else if (s1 == MISS || s1 == SECTOR_MISS) l1_misses++;
        else l1_res_fails++;

        // 检查事件（如有写回事件，记录）
        if (was_write_sent(events)) {
            // L1 驱逐了脏行，产生了下级写回流量
        }

        // --- 4b. 调度接口: L1 cycle() → 推入 bridge ---
        l1.cycle();

        // --- 4c. 数据接口: L1 miss → L2 access() ---
        while (!l1_to_l2_bridge.queue.empty()) {
            mem_fetch *req = l1_to_l2_bridge.queue.front();
            l1_to_l2_bridge.queue.pop_front();

            std::list<cache_event> l2_events;
            enum cache_request_status s2 = l2.access(
                req->get_addr(), req, cycle, l2_events);
            if (s2 == HIT || s2 == HIT_RESERVED) l2_hits++;
            else if (s2 == MISS || s2 == SECTOR_MISS) l2_misses++;
            else l2_res_fails++;
        }

        // --- 4d. 调度接口: L2 cycle() → 推入 DRAM ---
        l2.cycle();

        // --- 4e. 数据接口: DRAM → L2 fill() → L1 fill() ---
        while (!dram.queue.empty()) {
            mem_fetch *resp = dram.queue.front();
            dram.queue.pop_front();
            l2.fill(resp, cycle);   // L2 fill

            while (l2.access_ready()) {
                mem_fetch *l2_ready = l2.next_access();
                l1.fill(l2_ready, cycle);  // 返回给 L1
            }
        }

        // --- 4f. 数据接口: L1 就绪 → 取走完成的请求 ---
        while (l1.access_ready()) {
            mem_fetch *ready = l1.next_access();
            // 请求完成! 此处可记录延迟、释放资源等
        }
    }

    // --------------------------------------------------
    // 5. 控制接口: flush() 写回所有脏行
    // --------------------------------------------------
    l1.flush();
    l2.flush();
    printf("\n=== After flush() ===\n");

    // --------------------------------------------------
    // 6. 可维可测: 获取并打印统计信息
    // --------------------------------------------------
    cache_sub_stats l1_st, l2_st;
    l1.get_sub_stats(l1_st);
    l2.get_sub_stats(l2_st);

    printf("\n========== Performance Report ==========\n");
    printf("L1: accesses=%llu  misses=%llu  hit_rate=%.2f%%  "
           "pending_hits=%llu  res_fails=%llu\n",
           l1_st.accesses, l1_st.misses,
           100.0 * (1.0 - (double)l1_st.misses / l1_st.accesses),
           l1_st.pending_hits, l1_st.res_fails);
    printf("L1: data_port_util=%.2f%%  fill_port_util=%.2f%%\n",
           100.0 * l1_st.data_port_busy_cycles / l1_st.port_available_cycles,
           100.0 * l1_st.fill_port_busy_cycles / l1_st.port_available_cycles);

    printf("L2: accesses=%llu  misses=%llu  hit_rate=%.2f%%  "
           "pending_hits=%llu  res_fails=%llu\n",
           l2_st.accesses, l2_st.misses,
           100.0 * (1.0 - (double)l2_st.misses / l2_st.accesses),
           l2_st.pending_hits, l2_st.res_fails);
    printf("L2: data_port_util=%.2f%%  fill_port_util=%.2f%%\n",
           100.0 * l2_st.data_port_busy_cycles / l2_st.port_available_cycles,
           100.0 * l2_st.fill_port_busy_cycles / l2_st.port_available_cycles);

    // 也可以用 cache_stats 按类型/状态查看详细统计:
    printf("\n=== L1 Detailed Stats ===\n");
    l1.get_stats().print_stats(stdout, 0, "L1D");
    printf("\n=== L2 Detailed Stats ===\n");
    l2.get_stats().print_stats(stdout, 0, "L2");

    return 0;
}
```

**这个示例覆盖了所有接口类型：**

| 步骤 | 使用的接口 | 接口类型 |
|------|-----------|---------|
| 1 | `cache_config` / `init()` / getter 方法 | 配置接口 |
| 2 | `simple_mem_interface` / `simple_mf_allocator` | 数据接口（下级存储） |
| 3 | `l1_cache()` / `l2_cache()` 构造函数 | 调度接口（创建） |
| 4a | `l1.access()` | 数据接口（请求） |
| 4b | `l1.cycle()` | 调度接口（推进时钟） |
| 4c | `l2.access()` | 数据接口（下级请求） |
| 4d | `l2.cycle()` | 调度接口（推进时钟） |
| 4e | `l2.fill()` / `l1.fill()` | 数据接口（下级返回） |
| 4f | `l1.access_ready()` / `l1.next_access()` | 数据接口（读取就绪） |
| 5 | `flush()` | 数据接口（控制） |
| 6 | `get_sub_stats()` / `print_stats()` | 可维可测 |

---

## 3. 架构总览

### 3.1 类层次结构

```
cache_t (抽象基类, 定义 access() 接口)
  │
  ├── baseline_cache
  │     │  公共功能: tag_array + MSHR + bandwidth_management + cache_stats
  │     │  核心方法: cycle() / fill() / access_ready() / next_access()
  │     │
  │     ├── read_only_cache
  │     │     只读缓存: 指令/常量缓存
  │     │     access(): probe → HIT/MISS → send_read_request
  │     │
  │     └── data_cache
  │           通用读写缓存: 函数指针派发 4 种写命中策略 + 4 种写未命中策略
  │           access(): 根据 is_write() 派发到 m_wr_hit/m_wr_miss/m_rd_hit/m_rd_miss
  │           │
  │           ├── l1_cache
  │           │     L1 数据缓存: wr_alloc_type=L1_WR_ALLOC_R, wrbk_type=L1_WRBK_ACC
  │           │     典型配置: sector, write-evict / local-wb-global-wt
  │           │
  │           └── l2_cache
  │                 L2 共享缓存: wr_alloc_type=L2_WR_ALLOC_R, wrbk_type=L2_WRBK_ACC
  │                 典型配置: normal, write-back, write-allocate
  │
  └── tex_cache (独立实现, 不继承 baseline_cache)
       纹理缓存: FIFO 流水线模型 (Igehy et al. 1998)
       流程: fragment_fifo → request_fifo → ROB → result_fifo
```

**关键设计点：**
- `l1_cache` 和 `l2_cache` 使用**完全相同的** `data_cache::access()` 实现，行为差异**完全由 `cache_config` 决定**
- `data_cache` 通过函数指针 (`m_wr_hit`, `m_wr_miss`, `m_rd_hit`, `m_rd_miss`) 实现策略模式的派发，避免虚函数开销
- `tex_cache` 是独立继承 `cache_t` 的实现，因为它使用完全不同的 FIFO 流水线架构

### 3.2 数据处理流程

#### 3.2.1 请求处理流程（核心数据通路）

```
                    ┌──────────────────────────────────────┐
                    │            access(addr, mf, time)      │
                    │                  │                     │
                    │    ┌─────────────▼──────────────┐     │
                    │    │   tag_array::probe(addr)    │     │
                    │    │   返回: HIT / MISS /        │     │
                    │    │   HIT_RESERVED / SECTOR_MISS│     │
                    │    └─────────────┬──────────────┘     │
                    │                  │                     │
                    │     ┌────────────┼────────────┐       │
                    │     │            │            │       │
                    │   HIT        MISS/RESERVED  RES_FAIL  │
                    │     │            │            │       │
                    │     ▼            ▼            ▼       │
                    │  立即返回   检查 MSHR    返回错误码     │
                    │  (HIT)     full?         上层重试      │
                    │               │                        │
                    │          ┌────┴────┐                  │
                    │          │         │                  │
                    │      MSHR HIT   MSHR 有空位            │
                    │      (合并请求)  │                     │
                    │          │       ▼                     │
                    │          │  分配 tag array 行          │
                    │          │  添加 MSHR 条目             │
                    │          │  加入 miss_queue            │
                    │          │  返回 MISS                  │
                    │          │                             │
                    └──────────┼─────────────────────────────┘
                               │ (MISS 路径继续)
                               ▼
                    ┌──────────────────────┐
                    │      cycle()          │
                    │  miss_queue → lower   │
                    │  memport->push(mf)    │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │    下级存储处理        │
                    │  (外部延迟控制)        │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │      fill(mf, time)   │
                    │  tag_array::fill()    │
                    │  MSHR::mark_ready()   │
                    └──────────┬───────────┘
                               │
                               ▼
                    ┌──────────────────────┐
                    │   access_ready()?     │
                    │   → next_access()     │
                    │   请求完成，返回上层    │
                    └──────────────────────┘
```

#### 3.2.2 写策略派发流程（data_cache 特有）

```
data_cache::access(addr, mf, time, events)
  │
  ├── 如果 mf->is_write()
  │     ├── tag_array::probe() → HIT
  │     │     └── m_wr_hit(addr, idx, mf, time, events, status)
  │     │           根据 m_write_policy 派发到:
  │     │             WRITE_BACK        → wr_hit_wb()           (标记 MODIFIED)
  │     │             WRITE_THROUGH     → wr_hit_wt()           (写本级 + 写下级)
  │     │             WRITE_EVICT       → wr_hit_we()           (写下级 + 无效化)
  │     │             LOCAL_WB_GLOBAL_WT→ wr_hit_global_we_local_wb()
  │     │
  │     └── tag_array::probe() → MISS
  │           └── m_wr_miss(addr, idx, mf, time, events, status)
  │                 根据 m_write_alloc_policy 派发到:
  │                   NO_WRITE_ALLOCATE   → wr_miss_no_wa()           (不分配)
  │                   WRITE_ALLOCATE      → wr_miss_wa_naive()        (读+写请求)
  │                   FETCH_ON_WRITE      → wr_miss_wa_fetch_on_write (读请求)
  │                   LAZY_FETCH_ON_READ  → wr_miss_wa_lazy_fetch...  (延迟读)
  │
  └── 如果 !mf->is_write() (读)
        ├── tag_array::probe() → HIT
        │     └── m_rd_hit → rd_hit_base()   (更新 LRU, 返回 HIT)
        │
        └── tag_array::probe() → MISS
              └── m_rd_miss → rd_miss_base()  (send_read_request)
```

#### 3.2.3 多级缓存数据流（L1 → L2 → DRAM）

```
SM 请求
  │
  ▼
┌─────────────────────────────────────────────────────────────┐
│ 1. L1.access(addr, mf, t)                                   │
│    ├── HIT: 数据在 L1 → 返回 HIT                            │
│    └── MISS: MSHR 分配 → miss_queue ← mf                    │
│                                                                     │
│ 2. L1.cycle()                                                     │
│    miss_queue → l1_to_l2_bridge.push(mf)                    │
│                                                                     │
│ 3. L2.access(addr, mf, t)                                   │
│    ├── HIT: 数据在 L2 → L1.fill(mf, t)                      │
│    └── MISS: MSHR 分配 → miss_queue ← mf                    │
│                                                                     │
│ 4. L2.cycle()                                                     │
│    miss_queue → dram.push(mf)                                │
│                                                                     │
│ 5. DRAM 返回数据                                                    │
│    L2.fill(mf, t)  → L2 MSHR ready                          │
│    L2.next_access() → L1.fill(mf, t)                        │
│                                                                     │
│ 6. L1.next_access()                                           │
│    请求完成 → 返回给 SM                                       │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 模块化结构

缓存模型由以下独立模块组成，通过 `baseline_cache` 组合在一起：

```
┌──────────────────────────────────────────────────────────┐
│                    baseline_cache                         │
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  tag_array    │  │  mshr_table  │  │  bandwidth_   │  │
│  │               │  │              │  │  management   │  │
│  │  • probe()    │  │  • probe()   │  │               │  │
│  │  • access()   │  │  • full()    │  │  • use_data_  │  │
│  │  • fill()     │  │  • add()     │  │    port()     │  │
│  │  • flush()    │  │  • mark_     │  │  • use_fill_  │  │
│  │  • invalidate │  │    ready()   │  │    port()     │  │
│  │               │  │  • next_     │  │  • data_port_ │  │
│  │  包含:        │  │    access()  │  │    free()     │  │
│  │  cache_block_t│  │              │  │  • fill_port_ │  │
│  │    ├── line_  │  │  内部:       │  │    free()     │  │
│  │    │   cache_ │  │  tr1_hash_map│  │               │  │
│  │    │   block  │  │  <addr,      │  │  控制:        │  │
│  │    │          │  │   mshr_entry>│  │  data_port_   │  │
│  │    └── sector_│  │              │  │  width        │  │
│  │        cache_ │  │              │  │               │  │
│  │        block  │  │              │  │               │  │
│  └──────────────┘  └──────────────┘  └───────────────┘  │
│                                                          │
│  ┌──────────────┐  ┌──────────────────────────────────┐  │
│  │  miss_queue   │  │  cache_stats                     │  │
│  │  (list<mf*>)  │  │                                  │  │
│  │               │  │  • inc_stats(access_type,        │  │
│  │  • 缓冲未命中  │  │    access_outcome, streamID)    │  │
│  │    请求       │  │  • get_sub_stats(css)            │  │
│  │               │  │  • print_stats(fp, name)         │  │
│  └──────────────┘  │  • sample_cache_port_utility()   │  │
│                    └──────────────────────────────────┘  │
│                                                          │
│  ┌──────────────────────────────────────────────────┐    │
│  │  m_memport (mem_fetch_interface*)                 │    │
│  │  下级存储的连接点                                  │    │
│  └──────────────────────────────────────────────────┘    │
└──────────────────────────────────────────────────────────┘
```

**各模块职责：**

| 模块 | 类 | 文件 | 核心职责 |
|------|-----|------|---------|
| **标签阵列** | `tag_array` | `gpu-cache.h/cc` | 存储 cache line 的 tag、状态、替换元数据；probe/access/fill/invalidate/flush |
| **存储块** | `cache_block_t` / `line_cache_block` / `sector_cache_block` | `gpu_cache_ref.h` | 抽象 cache line 的状态机 (INVALID→RESERVED→VALID/MODIFIED) |
| **MSHR** | `mshr_table` | `gpu_cache_ref.h` | 跟踪所有未命中请求，支持同地址多请求合并 |
| **带宽管理** | `bandwidth_management` | `gpu-cache.h` (内部类) | 跟踪 data port / fill port 占用周期 |
| **Miss Queue** | `std::list<mem_fetch*>` | `baseline_cache` 成员 | 缓冲待发送到下级的未命中请求 |
| **统计** | `cache_stats` / `cache_sub_stats` | `gpu_cache_ref.h` | Hit/miss 计数、端口利用率、按访问类型分类统计 |
| **配置** | `cache_config` / `l1d_cache_config` / `l2_cache_config` | `gpu_cache_ref.h` | 所有几何/时序/策略参数的集中管理 |

### 3.4 用户使用典型场景推荐

#### 场景 1：L1 数据缓存 + L2 共享缓存（最常见）

```
推荐配置:
  L1: 每个 SM 私有, 16KB, sector, write-evict (全局) + local-wb (局部)
      "S:32:128:4,L:E:m:F:X,A:32:4,64"
  L2: 所有 SM 共享, 512KB, normal, write-back, write-allocate
      "N:256:128:16,L:B:m:F:X,A:64:8,128"

推荐类:
  L1: l1_cache (或 data_cache 配置 write-evict)
  L2: l2_cache (或 data_cache 配置 write-back)

关键操作流程:
  1. SM 请求 → L1.access()
  2. L1.cycle() → bridge push
  3. 从 bridge 取请求 → L2.access()
  4. L2.cycle() → DRAM push
  5. DRAM 响应 → L2.fill() → L1.fill()
  6. L1.next_access() → 请求完成
```

#### 场景 2：指令/常量只读缓存

```
推荐配置:
  "N:16:64:4,L:R:m:N:L,A:8:2,16"

推荐类:
  read_only_cache

特点:
  - access() 自动处理 probe → HIT/MISS 分发
  - 无需 mem_fetch_allocator (不会产生写回/写分配请求)
  - 构造函数无需 wr_alloc_type 和 wrbk_type
```

#### 场景 3：写穿透缓存（一致性要求高）

```
推荐配置:
  "N:64:64:8,L:T:m:N:L,A:16:4,32"

推荐类:
  data_cache (配置 WRITE_THROUGH + NO_WRITE_ALLOCATE)

特点:
  - 每次写都同时更新本级和下级
  - 写未命中不分配新行，直接写穿透
  - 适合 I/O 一致性场景
```

#### 场景 4：流式缓存（memcpy 优化）

```
推荐配置:
  "N:16:128:4,F:E:f:N:L,A:128:1,256"

推荐类:
  read_only_cache 或 data_cache (配置 ON_FILL + FIFO + WRITE_EVICT)

特点:
  - ON_FILL 避免 line_alloc_fail stall
  - 大 MSHR (128 entries) 最大化 MLP
  - 不合并请求 (max_merge=1) 避免 stall
```

#### 场景 5：多 SM → 共享 L2（Multi-SM GPU）

```
每个 SM:
  - 1 个私有 L1 数据缓存 (l1_cache)
  - 1 个私有 L1 指令缓存 (read_only_cache)
  - 1 个私有 L1 常量缓存 (read_only_cache)

所有 SM 共享:
  - 1 个 L2 缓存 (l2_cache)

调度循环:
  for each cycle:
    for each SM:
      SM[i].L1D.access() / SM[i].L1I.access() / SM[i].L1C.access()
      SM[i].L1D.cycle() / SM[i].L1I.cycle() / SM[i].L1C.cycle()
      将所有 SM 的 miss → L2.access()
    L2.cycle()
    处理 DRAM 响应 → L2.fill() → 路由回对应 SM 的 L1.fill()
```

### 3.5 可维可测说明

#### 3.5.1 获取统计数据

两种颗粒度的统计接口：

**方式一：通过 `get_sub_stats()` 获取聚合统计：**

```cpp
cache_sub_stats css;
cache.get_sub_stats(css);

printf("Accesses:             %llu\n", css.accesses);
printf("Misses:               %llu\n", css.misses);
printf("Hit Rate:             %.2f%%\n",
       100.0 * (1.0 - (double)css.misses / css.accesses));
printf("Pending Hits:         %llu\n", css.pending_hits);
printf("Reservation Failures: %llu\n", css.res_fails);

// 端口利用率
double data_util = (double)css.data_port_busy_cycles / css.port_available_cycles;
double fill_util = (double)css.fill_port_busy_cycles / css.port_available_cycles;
printf("Data Port Utilization: %.2f%%\n", data_util * 100);
printf("Fill Port Utilization: %.2f%%\n", fill_util * 100);
```

**方式二：通过 `cache_stats` 获取按访问类型/状态分类的详细统计：**

```cpp
// 按 [访问类型] × [请求结果] 的二维矩阵打印统计
cache.get_stats().print_stats(stdout, 0, "L1D");

// 打印预留失败按原因分类的统计
cache.get_stats().print_fail_stats(stdout, 0, "L1D");

// 编程方式获取特定统计数据
enum mem_access_type types[] = {GLOBAL_ACC_R, GLOBAL_ACC_W};
enum cache_request_status statuses[] = {HIT, MISS, RESERVATION_FAIL};
unsigned long long count = cache.get_stats().get_stats(types, 2, statuses, 3);
```

#### 3.5.2 统计项含义

| 字段 | 说明 | 诊断价值 |
|------|------|---------|
| `accesses` | 总访问次数 | 基准计数器 |
| `misses` | 总未命中次数 (MISS + SECTOR_MISS) | 命中率分母 |
| `pending_hits` | 命中但尚未填充完成的行 | 高 → MSHR 合并率高，等待 fill 中 |
| `res_fails` | 因 MSHR/队列满导致的预留失败 | 高 → 需增大 MSHR 或 miss_queue |
| `data_port_busy_cycles` | 数据端口繁忙周期数 | Data port 瓶颈指示器 |
| `fill_port_busy_cycles` | 填充端口繁忙周期数 | Fill port 瓶颈指示器 |
| `port_available_cycles` | 端口统计总周期数 | 利用率分母 |

#### 3.5.3 打印内部状态（调试用）

```cpp
// 打印缓存所有行的状态
unsigned accesses, misses;
cache.print(stdout, accesses, misses);

// 打印缓存当前状态快照
cache.display_state(stdout);

// 打印 MSHR 当前状态
// (内部用 m_mshrs.display(fp))

// 打印 cache_config
cache_config cfg;
cfg.print(stdout);  // "Size = 8192 B (32 Set x 4-way x 64 byte line)"
```

#### 3.5.4 参数扫描示例（设计空间探索）

```cpp
// 扫描 set 数以寻找最佳命中率
for (int nsets : {16, 32, 64, 128, 256}) {
    char cfg_str[64];
    snprintf(cfg_str, sizeof(cfg_str),
             "N:%d:128:4,L:B:m:F:X,A:32:4,64", nsets);

    cache_config cfg;
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);

    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    l1_cache cache("Sweep", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

    // 运行相同 trace
    run_trace(cache, "traces/benchmark.trace");

    cache_sub_stats css;
    cache.get_sub_stats(css);
    printf("nsets=%d: hit_rate=%.2f%%\n", nsets,
           100.0 * (1.0 - (double)css.misses / css.accesses));
}
```

#### 3.5.5 关键性能指标计算公式

```
命中率 (Hit Rate)     = 1 - misses / accesses
缺失率 (Miss Rate)    = misses / accesses
MPKI (Misses Per Kilo) = misses / (accesses / 1000)
数据端口利用率         = data_port_busy_cycles / port_available_cycles
填充端口利用率         = fill_port_busy_cycles / port_available_cycles
平均命中延迟           = data_size / data_port_width (cycles)
MLP (Memory-Level Parallelism) ≈ min(MSHR条目数, 实际并行未命中数)
```

### 3.6 模型边界说明

本节说明参考实现如何遵循 GPGPU-Sim 的架构设计——将**功能模型**（数据存储）和**时序模型**（缓存状态/延迟）分离。

#### GPGPU-Sim 的双模型架构

原始 GPGPU-Sim 严格分离两个模型：

```
┌──────────────────────────────────────────────┐
│  功能模型 (Functional)                        │
│                                              │
│  gpgpu_t::m_global_mem  (memory_space)       │
│  存储实际数据字节                              │
│  PTX 模拟器在此读写                            │
└──────────────────┬───────────────────────────┘
                   │ 创建 mem_fetch 请求令牌
                   ▼
┌──────────────────────────────────────────────┐
│  时序模型 (Timing)                            │
│                                              │
│  cache → interconnect → DRAM                 │
│  只跟踪: 地址 + 状态 + 延迟                    │
│  不存储: 实际数据字节                          │
│                                              │
│  mem_fetch     = 请求令牌 (地址/类型/大小)     │
│  cache_block_t = tag + state + dirty_mask    │
│  DRAM model    = 延迟模拟, 不存数据            │
└──────────────────────────────────────────────┘
```

**关键设计决策：**
- `mem_fetch` 是**请求令牌**，不是数据载体。原始 `mem_fetch.h:146` 中 `m_data_size` 的注释是 "how much data is being written"（仅表示大小）
- `cache_block_t` 只有 `tag + block_addr + state + dirty_mask`，**没有 `uint8_t data[N]`**
- 数据存储在独立的功能内存空间 (`gpgpu_t::m_global_mem`)，不在 cache 模型内部

#### 本参考实现的架构

遵循 GPGPU-Sim 的分离设计，提供两个独立组件：

| 组件 | 文件 | 角色 | 存储内容 |
|------|------|------|---------|
| **Cache Model** | `gpu_cache_ref.h/cc` | 时序模型 — tag 匹配 + 状态流转 + MSHR + 带宽管理 | tag + state + dirty_mask |
| **DataStore** | `data_store.h` | 功能模型 — 独立的数据存储 | 实际数据字节 (`std::map<addr, vector<uint8_t>>`) |
| **mem_fetch** | `gpgpu_stubs.h` | 请求令牌 — 在 pipeline 中流动 | 地址 + 类型 + 大小 + 掩码（无数据） |

#### DataStore 接口

```cpp
class DataStore {
    void write(block_addr, data, size);  // 存储数据
    vector<uint8_t> read(block_addr, size);  // 读取数据
    bool contains(block_addr);  // 是否已存储
    void clear();  // 清空
};
```

每级缓存独立维护一个 DataStore 实例：
- `DataStore l1_data` — L1 缓存对应的数据
- `DataStore l2_data` — L2 缓存对应的数据
- `DataStore dram_data` — DRAM 中的数据（预先填充）

#### 数据通路（GPGPU-Sim 分离模式）

```
读请求:
  1. SM 创建 mem_fetch 令牌 (地址/大小/类型, 无数据)
  2. L1.access(mf) → tag_array::probe()
     ├── HIT  → 从 l1_data.read(addr) 获取数据 (功能)
     └── MISS → MSHR 分配 → miss_queue → L2.access()
          ├── HIT  → l2_data→l1_data 拷贝数据 (功能) + L1.fill() (时序)
          └── MISS → dram_data→l2_data→l1_data 拷贝 (功能) + L2/L1.fill() (时序)
  3. L1.next_access() → 请求完成

写请求:
  1. SM 创建 mem_fetch 令牌 + 写入数据
  2. L1.access(mf) → tag_array::probe()
     ├── HIT  → l1_data.write(addr, data) (功能) + 标记 MODIFIED (时序)
     └── MISS → (取决于写分配策略) 可能分配行 + l1_data.write()
  3. 驱逐时: l1_data→l2_data 拷贝脏数据 (功能) + send_write_request (时序)
```

#### 使用示例

```cpp
#include "data_store.h"
#include "gpu_cache_ref.h"

// 1. 功能模型: 创建独立的数据存储
DataStore l1_data, dram_data;

// 2. 预先在 DRAM 中放入数据 (模拟程序加载)
uint8_t init[64] = {...};
dram_data.write(0x1000, init, 64);

// 3. 时序模型: 创建缓存 (只管理 tag + 状态 + 延迟)
read_only_cache l1("L1D", cfg, 0, 0, &dram_if, ...);

// 4. 访问数据
mem_fetch *mf = make_read_request(0x1010, 4);
auto status = l1.access(mf->get_addr(), mf, cycle, events);

if (status == HIT) {
    auto data = l1_data.read(block_addr, 4);  // 功能: 从 L1 DataStore 读取
}
// MISS 时: 从 dram_data 拷贝到 l1_data (功能), 然后 l1.fill() (时序)
```

完整示例见 `memory_system.h`（`SimpleTwoLevel` 类）和 `test/test_scenario.cc`（`scenario_10_data_store_dual_model`）。

#### 能力边界

| 能力 | 支持情况 | 说明 |
|------|---------|------|
| 命中/未命中决策 | 支持 | tag_array::probe() 完整实现 |
| 状态机流转 | 支持 | INVALID→RESERVED→VALID→MODIFIED |
| MSHR 未命中跟踪 | 支持 | 全相联 + sector 两种模式 |
| 带宽/端口建模 | 支持 | data_port / fill_port 占用周期跟踪 |
| 替换策略 | 支持 | LRU / FIFO |
| 写策略派发 | 支持 | 4 种写命中 × 4 种写未命中策略 |
| 数据内容存储 | **DataStore 分离** | `data_store.h`, 每级独立实例 |
| 数据内容验证 | **DataStore 分离** | 可直接读写 DataStore, 与 cache 状态对比 |
| 缓存一致性协议 | 不支持 | 无 snoop/目录协议 |
| 预取 | 不支持 | 可通过外部逻辑配合 access() 实现 |
| 功耗建模 | 不支持 | 提供访问计数, 需外部功耗模型 |
| ECC / 纠错码 | 不支持 | 不涉及可靠性建模 |

---

## 4. 其他

### 4.1 快速开始

```bash
# 一键构建+测试
./run.sh

# 手动构建
g++ -std=c++17 -Wall -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

./test_gpgpu_cache_ref

# CMake 构建
mkdir build && cd build && cmake .. && make
```

### 4.2 附录 A：GPGPU-Sim → openCache 对应关系

| GPGPU-Sim (本目录) | openCache (上层项目) | 说明 |
|-------------------|---------------------|------|
| `cache_config` | `CacheConfig` | 配置参数管理 |
| `tag_array` | `TagArray` | 标签阵列 |
| `mshr_table` | `MSHRTable` | 未命中状态保持寄存器 |
| `cache_stats` | `CacheStats` | 统计收集 |
| `cache_block_t` | `CacheBlock` | 缓存块状态机（抽象基类） |
| `line_cache_block` | `LineCacheBlock` | Normal 缓存块 |
| `sector_cache_block` | `SectorCacheBlock` | Sector 缓存块 |
| `baseline_cache` | `BaselineCache` | 公共基类 |
| `read_only_cache` | `ReadOnlyCache` | 只读缓存 |
| `data_cache` | `DataCache` | 通用读写缓存 |
| `l1_cache` | （派生类已移除, 用配置区分） | L1 数据缓存 |
| `l2_cache` | （派生类已移除, 用配置区分） | L2 共享缓存 |
| `cache_t` | （移除抽象基类, BaselineCache 可直接使用） | 顶层接口 |
| `mem_fetch` | `CacheRequest` | 请求载体 |
| `mem_fetch_interface` | `CacheMemoryInterface` | 下级存储抽象接口 |
| `cache_event` | `CacheEvent` | 缓存事件 |

### 4.3 附录 B：缓存类型对比

| 类型 | 类 | 读写 | 典型用途 | 关键特征 |
|------|-----|------|----------|---------|
| **L1 数据缓存** | `l1_cache` | 读写 | GPU SM 私有数据缓存 | sector, write-evict/write-back |
| **L2 共享缓存** | `l2_cache` | 读写 | GPU 共享 L2 | normal, write-back, write-allocate |
| **只读缓存** | `read_only_cache` | 只读 | 指令/常量缓存 | 简单 probe 模式 |
| **纹理缓存** | `tex_cache` | 只读 (FIFO) | 纹理采样 | FIFO 流水线, Igehy 1998 |
| **通用读写缓存** | `data_cache` | 读写 | 任意可配置场景 | 函数指针多态派发 |

### 4.4 附录 C：Normal vs Sector 缓存

```
NORMAL (N):
  - 以整个 cache line 为最小管理单元
  - 一次 MISS 分配并填充整个 line
  - tag_array::probe() 检查整行的状态
  - 简单, 适合: L2, 指令缓存

SECTOR (S):
  - 以 sector (32B) 为最小管理单元（每行 4 个 sector）
  - 一次 SECTOR_MISS 只分配/填充一个 sector
  - tag_array::probe() 需要 sector_mask 参数，逐个 sector 检查状态
  - 减少 fill 带宽, 更好的空间利用率
  - 适合: L1 数据缓存
```

### 4.5 附录 D：写策略选择指南

| 场景 | 写策略 | 写分配策略 |
|------|--------|-----------|
| GPU L1 (全局内存, streaming) | `WRITE_EVICT` | `FETCH_ON_WRITE` |
| GPU L1 (局部内存) | `WRITE_BACK` | `LAZY_FETCH_ON_READ` |
| GPU L2 (共享) | `WRITE_BACK` | `FETCH_ON_WRITE` |
| CPU 独占 L1 | `WRITE_BACK` | `FETCH_ON_WRITE` |
| CPU 共享 L2 | `WRITE_BACK` | `FETCH_ON_WRITE` |
| I/O 一致性 | `WRITE_THROUGH` | `NO_WRITE_ALLOCATE` |
| Memcpy / Streaming | `WRITE_EVICT` | `NO_WRITE_ALLOCATE` |
| 只读 (指令/常量/纹理) | `READ_ONLY` | `NO_WRITE_ALLOCATE` |

### 4.6 附录 E：文件结构

```
gpgpusim_cache/
├── USER_GUIDE.md              — 本手册
├── README.md                  — 简要说明
├── CMakeLists.txt             — CMake 构建
├── run.sh                     — 一键构建+测试
├── test_gpgpu_cache_ref       — 预编译的测试二进制
├── gpgpu_cache/
│   ├── gpu-cache.h            — 原始 GPGPU-Sim 缓存头文件 (未修改)
│   ├── gpu-cache.cc           — 原始 GPGPU-Sim 缓存实现 (未修改)
│   ├── gpu_cache_ref.h        — 适配后头文件 (仅改了 #include)
│   ├── gpu_cache_ref.cc       — 适配后实现 (仅改了 #include + 3 个桩函数)
│   ├── gpgpu_stubs.h          — GPGPU-Sim 依赖的桩实现
│   ├── addrdec.h / addrdec.cc — 地址解码 (未修改)
│   ├── delayqueue.h           — 延迟队列 (未修改)
│   ├── hashing.h / hashing.cc — 哈希函数 (未修改)
│   ├── mem_fetch.h            — 原始 mem_fetch (未修改)
│   └── mem_fetch_status.tup   — 状态枚举定义 (未修改)
└── test/
    ├── test_main.cc           — 13 个单元测试
    └── test_scenario.cc       — 9 个场景集成测试
```

### 4.7 附录 F：MSHR 系统详解

MSHR (Miss Status Holding Register) 负责管理所有未完成的未命中请求。

**基本操作：**

```cpp
mshr_table mshr(32, 4);  // 32 条目, 每条目最多合并 4 个请求

// 检查该地址是否已有未完成的请求
if (mshr.probe(block_addr)) {
    // MSHR hit: 已有对该地址的未完成请求
}

// 检查是否可以接受新请求（entry 未满）
if (!mshr.full(block_addr)) {
    mshr.add(block_addr, &mf);  // 添加或合并请求
}

// 数据返回时标记就绪
bool has_atomic = false;
mshr.mark_ready(block_addr, has_atomic);

// 取出就绪的请求
if (mshr.access_ready()) {
    mem_fetch *ready = mshr.next_access();
}
```

**MSHR 类型选型指南：**

| 类型 | 配置字符 | 适用缓存 |
|------|----------|---------|
| `ASSOC` | `A` | Normal 缓存 (L2, I-cache) |
| `SECTOR_ASSOC` | `S` | Sector 缓存 (L1 数据缓存) |
| `TEX_FIFO` | `F` | 纹理缓存 (下游为 normal 缓存) |
| `SECTOR_TEX_FIFO` | `T` | 纹理缓存 (下游为 sector 缓存) |
