# openCache 与 GPGPU-Sim Cache 内核差异说明

> 对照基准：gpgpu-sim_distribution (dev 分支), `src/gpgpu-sim/gpu-cache.h` / `gpu-cache.cc`  
> 对照目标：openCache `src/` 目录下全部 8 个源文件  
> 核对日期：2026-05-26

---

## 总体结论

openCache 的 cache 内核是 **基于 GPGPU-Sim cache 模型的干净室重实现（cleanroom reimplementation）**，而非逐行复制。两者在以下层面高度一致：

- 缓存状态机模型（INVALID → RESERVED → VALID / MODIFIED）
- MSHR（Miss Status Holding Register）合并与就绪队列机制
- 写策略分派（write-back / write-through / write-evict / write-allocate）
- Sector cache 的扇区粒度管理
- 配置字符串的解析格式（兼容 GPGPU-Sim 的 `--gpgpu_cache:dl1` 语法）

但 openCache 在以下方面做了 **有意的简化和现代化改造**：

1. 移除 GPU 模拟器紧耦合（不再依赖 `mem_fetch` / `gpgpu_sim` / shader core）
2. C++ 现代化（`enum class`、namespace、STL 替代自定义容器、智能指针化方向）
3. 移除仅 GPU 需要的特性（texture cache、L1/L2 子类、FERMI hash、多 stream 统计）
4. 新增少量通用替换策略（PLRU、RANDOM）

---

## 详细差异清单

### 1. 代码组织与风格

| 维度 | GPGPU-Sim | openCache | 差异性质 |
|------|-----------|-----------|----------|
| 命名空间 | 全局（`::`） | `opencache` namespace | 现代化改动 |
| 枚举类型 | C 风格 `enum` | C++11 `enum class` | 类型安全性增强 |
| 文件组织 | 2 个文件 (~4100 行) 集中全部内容 | 8 个文件 (~1100 行)，按职责拆分 | 工程化改进 |
| 许可证 | BSD 3-clause (UBC/Northwestern/Purdue) | 无显式许可证头 | 需补充 |
| 头文件依赖 | `abstract_hardware_model.h`, `tr1_hash_map.h`, `gpu-misc.h`, `mem_fetch.h`, `addrdec.h` | 仅标准库 + 内部头文件 | 解耦 |

### 2. 枚举与类型定义 (`open_cache_types.h` vs gpgpu-sim 全局枚举)

| 枚举 | GPGPU-Sim 命名 | openCache 命名 | 差异 |
|------|---------------|----------------|------|
| Block 状态 | `cache_block_state`: `INVALID, RESERVED, VALID, MODIFIED` | `enum class BlockState`: `INVALID, RESERVED, VALID, MODIFIED` | **无差异** |
| 请求状态 | `cache_request_status`: `HIT, HIT_RESERVED, MISS, RESERVATION_FAIL, SECTOR_MISS, MSHR_HIT` + `NUM_CACHE_REQUEST_STATUS` | `enum class AccessStatus`: `HIT, HIT_RESERVED, MISS, RESERVATION_FAIL, SECTOR_MISS, MSHR_HIT` + `NUM_STATUS` | **无差异**（去掉了 `CACHE_` 中缀） |
| 失败原因 | `LINE_ALLOC_FAIL, MISS_QUEUE_FULL, MSHR_ENRTY_FAIL, MSHR_MERGE_ENRTY_FAIL, MSHR_RW_PENDING` + `NUM_CACHE_RESERVATION_FAIL_STATUS` | 同左，注意修正了 `ENRTY` → `ENTRY` 拼写 | **修正拼写错误** |
| 事件类型 | `WRITE_BACK_REQUEST_SENT, READ_REQUEST_SENT, WRITE_REQUEST_SENT, WRITE_ALLOCATE_SENT` | 同左（`CacheEventType`） | **无差异** |
| Cache 层级 | `L1_GPU_CACHE, L2_GPU_CACHE, OTHER_GPU_CACHE` | `CacheLevel`: `L1, L2, L3, OTHER` | 新增 L3，去掉 `_GPU` 后缀 |
| 替换策略 | `LRU, FIFO` | `LRU, FIFO, RANDOM, PLRU` | **新增** RANDOM 和 PLRU |
| 写策略 | `READ_ONLY, WRITE_BACK, WRITE_THROUGH, WRITE_EVICT, LOCAL_WB_GLOBAL_WT` | 同左 | **无差异** |
| 分配策略 | `ON_MISS, ON_FILL, STREAMING` (warning: STREAMING 被 map 到 ON_FILL) | `ON_MISS, ON_FILL` | **删除** STREAMING（volta/pascal 特性） |
| 写分配策略 | `NO_WRITE_ALLOCATE, WRITE_ALLOCATE, FETCH_ON_WRITE, LAZY_FETCH_ON_READ` | 同左（注意缺少 `WRITE_VALIDATE`） | **缺少** `wr_miss_wa_write_validate` 方法（见下文） |
| MSHR 类型 | `TEX_FIFO, ASSOC, SECTOR_TEX_FIFO, SECTOR_ASSOC` | 同左 | **无差异**，但 openCache 不支持 TEX_FIFO/SECTOR_TEX_FIFO |
| Set index 函数 | `LINEAR_SET_FUNCTION, BITWISE_XORING_FUNCTION, HASH_IPOLY_FUNCTION, FERMI_HASH_SET_FUNCTION, CUSTOM_SET_FUNCTION` | `LINEAR, BITWISE_XOR, HASH_IPOLY, CUSTOM` | **缺少** FERMI_HASH（Fermi 哈希函数） |
| Cache 类型 | `NORMAL, SECTOR` | `NORMAL, SECTOR` | **无差异** |
| 访问类型 | `mem_access_type` 含大量 GPU 特定类型 | `AccessType`: `READ, WRITE, PREFETCH, WRITE_BACK, WRITE_ALLOCATE` | **大幅简化**，去掉 GPU 特定访问类型 |

### 3. Cache Block 实现 (`cache_block.h` vs gpu-cache.h 内部 struct)

**抽象基类：**

| GPGPU-Sim (`cache_block_t`) | openCache (`CacheBlock`) | 差异 |
|------------------------------|--------------------------|------|
| `virtual void set_byte_mask(mem_fetch *mf)` | 无此重载 | openCache 不依赖 `mem_fetch` |
| `virtual void print_status()` | `virtual void print() const` | 命名差异 |
| `virtual unsigned get_modified_size()` | `virtual uint32_t get_modified_size(uint32_t sector_size)` | openCache 将 `sector_size` 参数化 |

**行缓存块：**

- GPGPU-Sim: `line_cache_block::get_modified_size()` 返回 `SECTOR_CHUNCK_SIZE * SECTOR_SIZE`（即固定返回整行大小）
- openCache: `LineCacheBlock::get_modified_size()` 返回 `4 * sector_size`，仅在 MODIFIED 状态时返回整行大小（等价但有微小差别）

**扇区缓存块：**

- GPGPU-Sim: `sector_cache_block` 有 **额外方法** `allocate_sector()`——用于向已有有效行中分配新的无效扇区
- openCache: `SectorCacheBlock` **没有** `allocate_sector()` 方法，简化了扇区分配逻辑
- GPGPU-Sim: 使用全局常量 `SECTOR_CHUNCK_SIZE`（注意原代码中拼写为 CHUNCK 而非 CHUNK）和 `SECTOR_SIZE`
- openCache: 使用 `DEFAULT_SECTOR_CHUNK_SIZE = 4`（修正了拼写）和 `DEFAULT_SECTOR_SIZE = 32`，支持 `MAX_SECTORS = 8` 和可配置的 `m_num_sectors`
- GPGPU-Sim: `m_ignore_on_fill_status`、`m_set_byte_mask_on_fill` 等使用 C 数组
- openCache: 使用 `std::bitset` 作为 `sector_mask_t`，使用 `bool m_readable[MAX_SECTORS]` 等

### 4. Tag Array (`tag_array.h/cc` vs gpu-cache.h/cc)

这是差异最大的模块之一。

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| 返回类型 | `enum cache_request_status` | `TagProbeResult` 结构体（含 `status, set_index, way_index, sector_mask`） | openCache 更丰富 |
| probe() 参数 | `(addr, idx, mem_fetch *mf, is_write, probe_mode)` | `(addr, is_write, probe_mode)` 和 `(addr, mask, is_write, probe_mode)` | 去掉了 `mem_fetch *` 和输出参数 `idx` |
| access() 参数 | `(addr, time, idx, wb, evicted, mem_fetch *mf)` | `(addr, time, is_write, wb, evicted)` | 去掉了 `mem_fetch *` |
| fill() 参数 | 三个重载，均带 `mem_fetch *mf` | 三个重载，不带 `mem_fetch *`，通过 `sector_mask + byte_mask` 传递 | 去耦 |
| 内存管理 | `cache_block_t **m_lines`（二级指针） | `std::vector<CacheBlock *> m_lines` | STL 容器替代 |
| pending_lines | `tr1_hash_map<new_addr_type, unsigned> pending_lines` | **无此结构** | **删除** pending lines 追踪（GPU 用于 coalescing） |
| new_window() | 存在，用于滑动窗口统计 | **不存在** | **删除** windowed 统计 |
| windowed_miss_rate() | 存在 | **不存在** | **删除** |
| update_cache_parameters() | 存在，支持动态调整 associativity | **不存在** | **删除** Volta 动态缓存调整 |
| add_pending_line() / remove_pending_line() | 存在 | **不存在** | **删除** |
| inc_dirty() | 存在，单独追踪 dirty count | **不存在**（通过 block 状态直接查询） | 简化 |
| is_used 标志 | 存在（跟踪缓存是否曾被访问） | **不存在** | 简化 |

**LRU 实现：**
- GPGPU-Sim: 在 `tag_array` 中维护 `m_lru_order`（`std::vector<std::vector<unsigned>>`，每个 set 一个 order list），通过该 list 跟踪访问顺序
- openCache: 使用 `std::vector<std::vector<uint32_t>> m_lru_order`，相同算法，但额外在 `find_victim()` 中支持 RANDOM 和 PLRU

**FIFO 实现：**
- GPGPU-Sim: 使用 `m_fifo_next` 数组，只在 access() 分配时推进
- openCache: 使用 `std::vector<uint32_t> m_fifo_next`，相同逻辑

### 5. Cache Config (`cache_config.h/cc` vs gpu-cache.h)

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| init() | 成员函数 `void init(char *config, FuncCache status)` | `bool parse_config_string(const char *config_str)` 返回 bool | API 差异 |
| 配置字符串 `m_config_string` | `char *` 成员变量，存储在对象上 | 无成员变量（纯函数式解析） | 更简洁 |
| to_config_string() | 不存在 | 存在 | **新增** 配置序列化 |
| m_valid / m_disabled | `bool m_valid`, `bool m_disabled` | 无（解析失败直接返回 false） | 简化 |
| FuncCache | `enum FuncCache cache_status` | 不存在 | 删除 GPU 功能缓存类型 |
| m_wr_percent | L1 streaming 相关 | 存在 `write_percent` 但有不同语义 | 保留但简化 |
| m_is_streaming | 支持 Pascal/Volta streaming cache | `is_streaming()` 仅判断 `alloc_policy == ON_FILL` | 大幅简化 |
| m_result_fifo_entries | 14 或 15 字段配置格式含此字段 | **无此字段** | 删除 |
| 派生类 | `l1d_cache_config`, `l2_cache_config` 带 bank hashing 和地址翻译 | 无派生类 | 删除 |
| original_m_assoc | 保存原始 associativity 用于动态调整 | 无 | 删除 |
| get_max_cache_multiplier() | L1 支持 unified_cache_size 动态调整 | 无 | 删除 |
| SECTOR_CHUNCK_SIZE 检查 | assert 检查 line_sz / SECTOR_SIZE == SECTOR_CHUNCK_SIZE | assert 检查 line_size % sector_size == 0 | openCache 更灵活（sector_size 可配） |
| `friend class` 声明 | 6 个 friend class 声明 | 无 friend 声明 | openCache 封装更好 |

**配置字符串格式差异：**

```
GPGPU-Sim (14-15 fields):
  <N|S>:<nsets>:<bsize>:<assoc>,<L|F>:<R|B|T|E|L>:<m|f|s>:<N|W|F|L>:<L|X|H|P|C>,<A|S|F|T>:<mshr>:<merge>:<mq>:<rf>:<dpw>

openCache (13 fields):
  <N|S>:<nsets>:<bsize>:<assoc>,<L|F>:<R|B|T|E|L>:<m|f>:<N|W|F|L>:<L|X|P|C>,<A|S|F|T>:<mshr>:<merge>:<mq>
```

差异：openCache 去掉了 `m_result_fifo_entries` 和 `m_data_port_width` 两个字段。

- GPGPU-Sim 接受 `s`（STREAMING）作为分配策略，openCache 不接受
- GPGPU-Sim `'H'` 映射到 `FERMI_HASH_SET_FUNCTION`，openCache 没有此映射
- openCache 额外支持简化的 3 字段格式：`nsets:bsize:assoc`

### 6. MSHR (`mshr.h` vs gpu-cache.h/cc 中 `mshr_table`)

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| 底层容器 | `tr1_hash_map` | `std::unordered_map` | STL 替代 |
| add() | `void add(block_addr, mem_fetch *mf)` | `bool add(block_addr, CacheRequest)` 返回成功/失败 | API 差异，openCache 传值 |
| busy() | `bool busy()` 存在 | 不存在 | **删除** |
| mark_ready() | `void mark_ready(block_addr, bool &has_atomic)` | `void mark_ready(block_addr)` | 去掉 atomic 标志 |
| next_access() | `mem_fetch *next_access()` 返回指针 | `std::vector<CacheRequest> next_ready()` 返回值 | 值语义 |
| display() | `void display()` | `void print()` | 命名差异 |
| check_mshr_parameters() | 存在，用于跨 kernel 参数一致性检查 | 不存在 | **删除**（不需要 GPU kernel 切换） |
| clear() | 不存在 | `void clear()` 存在 | **新增** |
| 请求存储 | `std::list<mem_fetch *>` | `std::vector<CacheRequest>` | STL 差异 |
| m_current_response | `std::list<new_addr_type>` 和 `bool m_current_response_ready` | `std::list<addr_t> m_ready_list` | openCache 简化为单一就绪列表 |
| pending_lines | 存在第二个 hash map `pending_lines` | 无 | **删除** |

### 7. 主 Cache 类

#### 7.1 类层次结构

```
GPGPU-Sim:
  cache_t (abstract)
  ├── baseline_cache
  │   ├── read_only_cache
  │   └── data_cache
  │       ├── l1_cache
  │       └── l2_cache
  └── tex_cache              ← openCache 无此类

openCache:
  BaselineCache
  ├── ReadOnlyCache
  └── DataCache              ← 无 L1/L2 特化子类
```

#### 7.2 BaselineCache 对比

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| 构造参数 | `(name, config, core_id, type_id, memport, status, level, gpgpu_sim *gpu)` | `(name, config, core_id, type_id, memport, level)` | 去掉 GPU 模拟器指针和 mem_fetch_status |
| 构造函数 | 新建 `tag_array` 并保存到 `m_tag_array` | 同左，额外在构造列表初始化 MSHR | 等价 |
| 第二个构造函数 | 存在（接受外部创建的 `tag_array *`，用于派生类） | 不存在 | openCache 不需要 |
| access() 签名 | `virtual enum cache_request_status access(addr, mem_fetch *mf, time, std::list<cache_event> &events)` | `virtual CacheResult access(const CacheRequest &req)` | **重大差异**：openCache 返回值而非状态码+事件列表 |
| cycle() | 调用 `m_bandwidth_management.replenish_port_bandwidth()` | 调用 `replenish_ports()` | 等价但 openCache 无内部 bandwidth_management 类 |
| fill() | `void fill(mem_fetch *mf, time)` 接受 `mem_fetch *` | `void fill(const CacheRequest &req, time)` 接受 `CacheRequest` | 参数类型差异 |
| waiting_for_fill() | 存在，通过 extra_mf_fields 查找 | 不存在 | **删除** |
| display_state() | 存在 | 不存在 | **删除** |
| force_tag_access() | 存在（cudaMemcpy 快速路径） | 不存在 | **删除** |
| inc_aggregated_stats() | 存在，支持层级统计 | 不存在 | **删除** |
| inc_aggregated_stats_pw() | 存在，支持 per-window 统计 | 不存在 | **删除** |
| extra_mf_fields | `std::map<mem_fetch *, extra_mf_fields>` | `std::unordered_map<const CacheRequest *, ExtraFields>` | 类型差异，功能等价 |
| bandwidth_management | 独立内嵌类，含 `use_data_port()`, `use_fill_port()` | 两个 port 标志位 + `replenish_ports()` | **大幅简化** |
| send_read_request() | 2 个重载（带/不带 writeback） | 1 个重载（不带 writeback 版本） | **简化**，写回逻辑在 write-miss handler 中直接处理 |
| m_gpu | 保存 `gpgpu_sim *` 指针 | 不存在 | **删除** |
| m_miss_queue_status | `enum mem_fetch_status` | 不存在 | **删除** |
| update_cache_parameters() | 存在 | 不存在 | **删除** |
| get_stats() 多态 | 支持获取特定 access_type/status 的统计 | 仅 `get_stats()` 和 `get_sub_stats()` | 简化 |
| clear_pw() / get_sub_stats_pw() | 存在，AerialVision 支持 | 不存在 | **删除** |

#### 7.3 DataCache 对比

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| 策略分派 | **函数指针**：`m_wr_hit`, `m_wr_miss`, `m_rd_hit`, `m_rd_miss` | **switch-case** 在 `process_access()` 中直接分派 | 架构选择差异 |
| 写命中方法 | `wr_hit_wb`, `wr_hit_wt`, `wr_hit_we`, `wr_hit_global_we_local_wb` | `write_hit_writeback`, `write_hit_writethrough`, `write_hit_writeevict` | **缺少** `global_we_local_wb` |
| 写缺失方法 | `wr_miss_no_wa`, `wr_miss_wa_naive`, `wr_miss_wa_fetch_on_write`, `wr_miss_wa_lazy_fetch_on_read`, **`wr_miss_wa_write_validate`** | 前 4 个同左 | **缺少** `write_validate` |
| m_memfetch_creator | 存在，用于创建新的 `mem_fetch` 对象（写分配、写回） | 不存在（通过 `CacheRequest` 值传递） | **删除** |
| m_wr_alloc_type / m_wrbk_type | 存在，指定 L1/L2 写分配类型 | 不存在 | **删除** |
| send_write_request() | 存在，处理 miss queue 和事件生成 | 通过 `m_memport->send_request()` 直接发送 | 简化 |
| process_tag_probe() | 存在，聚合 probe + 策略分派 | `process_access()` 内联处理 | 等价但内联 |
| update_m_readable() | 存在，管理 readable 标志 | 嵌套在 write-hit handler 中 | 简化 |

**关键逻辑差异——write_miss_wa_naive（写分配+读取）：**

- GPGPU-Sim：通过 `m_memfetch_creator->alloc()` 创建新的 `mem_fetch` 读请求对象，然后调用 `send_read_request()` 发送。同时处理 writeback 事件的生成和写请求的发送。
- openCache：直接在 `write_miss_wa_naive()` 中调用 `m_tag_array->access()` 分配 line、`m_tag_array->fill()` 填充、设置 MODIFIED 状态并返回。**不发送**额外的读请求。

**关键逻辑差异——write_miss_wa_fetch_on_write：**

- GPGPU-Sim：区分 "整行写入"（partial write check），整行写入直接标记 MODIFIED 不发送读请求；部分写入通过 `m_memfetch_creator->alloc()` 创建读请求。
- openCache：直接分配+填充+标记 MODIFIED，然后发送一个 READ 请求。**没有**整行写入优化。

**关键逻辑差异——DataCache::access()：**

- GPGPU-Sim：`access()` 包含 WRITE_BACK 类型的特殊处理（直接填充 tag array）、调用 `process_tag_probe()` 分派读写。
- openCache：`process_access()` 以类似逻辑处理 WRITE_BACK、WRITE 和 READ 三种情况，但通过 switch-case 而非函数指针。

#### 7.4 ReadOnlyCache 对比

- GPGPU-Sim：`read_only_cache::access()` 包含完整的 miss 处理逻辑（access 分配、填充、probe mode 处理等）
- openCache：`ReadOnlyCache::access()` 逻辑简化——在 MISS/SECTOR_MISS 时直接 access + fill，不检查 MSHR 或 miss queue

### 8. 统计系统 (`cache_stats.h` vs gpu-cache.h `cache_stats`)

| 特性 | GPGPU-Sim | openCache | 差异 |
|------|-----------|-----------|------|
| 多 stream 支持 | `std::map<unsigned long long, vector<vector<...>>>` | 仅 stream 0 (`m_stats[0]`) | **大幅简化** |
| per-window 统计 | `m_stats_pw` + `clear_pw()` + `inc_stats_pw()` | 不存在 | **删除** AerialVision 支持 |
| inc_stats() | `inc_stats(access_type, access_outcome, streamID)` | `record_access(AccessType, AccessStatus)` | 简化 |
| inc_fail_stats() | `inc_fail_stats(access_type, fail_outcome, streamID)` | `record_fail(AccessType, ReservationFailReason)` | 简化 |
| select_stats_status() | 存在，组合 probe + access 的状态 | 不存在 | **删除** |
| operator() 访问器 | 存在，支持 `stats(type, status, fail, streamID)` 语法 | 不存在 | **删除** |
| print_stats() / print_fail_stats() | 支持 stream ID 过滤 | 不支持 | 简化 |
| port 统计 | `m_cache_port_available_cycles` 等 3 个成员 | `CacheSubStats` 中的 `port_available_cycles` 等 | 等价但结构不同 |
| sample_cache_port_utility() | 同 | `sample_port_utility()` | 命名差异 |
| CacheSubStats | `read_hits/write_hits` **拆分**到 `cache_sub_stats_pw` | `read_hits/write_hits` 在 `CacheSubStats` 中 | openCache 不区分 per-window |
| 命中率计算 | 不直接提供 | `hit_rate()`, `read_hit_rate()`, `write_hit_rate()` | **新增** |

### 9. Memory Interface

**GPGPU-Sim:**
- `mem_fetch_interface` 抽象类（定义于 `mem_fetch.h`）
- `mem_fetch_allocator` 用于创建新的内存请求
- `mem_fetch` 对象携带 warp mask、byte mask、sector mask、chip/partition 地址等 GPU 特定信息
- `enum mem_fetch_status` 控制 miss queue 状态
- 写回请求需要 `alloc()` 新 `mem_fetch` 并设置 chip/partition 地址

**openCache:**
- `CacheMemoryInterface` 抽象类，仅含 `send_request(CacheRequest)`, `can_accept_request()`, `get_fill_latency()`
- `SimpleMemory` 具体实现用于独立测试
- `CacheRequest` 是纯值类型（address, type, size, stream_id, instruction_id）
- 无 allocator 模式，请求通过值传递

### 10. 常量定义

| GPGPU-Sim | openCache | 备注 |
|-----------|-----------|------|
| `SECTOR_CHUNCK_SIZE` (全局宏或常量) | `DEFAULT_SECTOR_CHUNK_SIZE = 4` | 修正拼写 |
| `SECTOR_SIZE` (全局常量) | `DEFAULT_SECTOR_SIZE = 32` | 参数化 |
| `MAX_DEFAULT_CACHE_SIZE_MULTIBLIER = 4` | 无（不使用动态 associativity） | 删除 |
| `#define MAX_WARP_PER_SHADER 64` 等 | 无 | 删除 GPU 特定常量 |
| `MAX_BYTE_MASK_SIZE = 128` | 同 | openCache 新增 |
| `MAX_SECTORS = 8` | 无（gpgpu-sim 硬编码 4） | openCache 允许更大 sector 数 |

### 11. 测试与构建

| 特性 | GPGPU-Sim | openCache |
|------|-----------|-----------|
| 构建系统 | Makefile + CMakeLists.txt (庞大) | CMakeLists.txt + build_msvc.bat |
| 测试 | 集成在 GPU 模拟器全流程中 | `test/test_main.cc` 独立单元测试 |
| 独立运行 | 不可（必须集成在 gpgpu-sim 中） | 可（`SimpleMemory` 接口） |
| MSVC 支持 | 未明确支持 | `build_msvc.bat` 明确支持 Windows |

---

## 缺少的 GPGPU-Sim 特性（有意删除）

以下特性在 openCache 中被有意删除，因为它们与 GPU 模拟器架构紧耦合：

1. **Texture Cache** (`tex_cache` 类) —— 使用 fragment_fifo / request_fifo / rob 等纹理专用 FIFO 管线
2. **L1 / L2 派生类** —— `l1_cache` 和 `l2_cache` 仅修改 `m_wr_alloc_type` 和 `m_wrbk_type`，对独立缓存模拟无意义
3. **`wr_hit_global_we_local_wb`** —— Fermi 特有的 global-write-evict / local-write-back 混合策略
4. **`wr_miss_wa_write_validate`** —— 写验证策略（仅标记脏位，不发读取）
5. **STREAMING 分配策略** —— Pascal/Volta L1 streaming cache 特性
6. **FERMI_HASH_SET_FUNCTION** —— Fermi 架构特有的集合索引哈希
7. **Windowed miss rate 统计** —— `new_window()` / `windowed_miss_rate()`（AerialVision 可视化）
8. **Per-stream / per-window 统计** —— `m_stats_pw`、`m_fail_stats`、多 stream ID 支持
9. **动态 associativity 调整** —— `update_cache_parameters()` / `set_assoc()`（Volta unified cache）
10. **Extra MF fields / pending lines** —— 用于 GPU coalescing 和写回追踪
11. **Bank hashing** —— `l1d_cache_config` 中的 bank 交错哈希

---

## 新增/增强的特性（GPGPU-Sim 中没有）

1. **PLRU (Pseudo-LRU)** 替换策略 —— GPGPU-Sim 仅有 LRU 和 FIFO
2. **RANDOM 替换策略** —— 通过 `rand()` 随机选择 victim way
3. **`to_config_string()`** —— 配置序列化回 GPGPU-Sim 兼容格式
4. **`CacheResult` 独立返回类型** —— 包含 status + latency + is_hit，比裸 enum 更信息丰富
5. **`TagProbeResult` 返回类型** —— 替代输出参数方式，返回 set_index / way_index / sector_mask
6. **简化配置字符串** —— 支持 `nsets:bsize:assoc` 三字段简写格式
7. **`SimpleMemory` 测试桩** —— 可独立运行，不需要任何外部依赖
8. **Configurable sector_size** —— sector 尺寸可配置，而非硬编码为 `SECTOR_SIZE`

---

## 代码质量差异

| 维度 | GPGPU-Sim | openCache |
|------|-----------|-----------|
| 拼写错误 | `CHUNCK_SIZE`, `ENRTY`, `replenish` 等 | 均已修正 |
| const 正确性 | 部分函数标记 const | 更一致的 const 标记 |
| 内存管理 | 裸指针 + `new`/`delete` | `std::vector` + 裸指针（可进一步改进为 `unique_ptr`） |
| 函数长度 | 部分函数 100+ 行 | 每个函数较短，职责单一 |
| 注释 | 较详细（含论文引用） | 较少（可能过度精简） |
| 头文件 guard | `#ifndef GPU_CACHE_H` | `#ifndef OPEN_CACHE_H` |

---

---

## 关键功能缺陷（影响模拟正确性）

以下差异不仅仅是"简化"，而是**当前版本中存在的功能性缺陷**，可能导致模拟结果与 GPGPU-Sim 不一致：

### A. 脏块驱逐时不产生写回请求

GPGPU-Sim 在驱逐被修改的缓存行时，会通过 `m_memfetch_creator->alloc()` 创建写回请求（含正确的 chip/partition 地址），并通过 `send_write_request()` 发送到下级存储。

openCache 的 `TagArray::access()` 中虽然检测到 `wb = true` 并填充了 `EvictedBlockInfo`（block_addr / modified_size / byte_mask / sector_mask），但这些信息**从未被使用**——所有 write-miss 和 read-miss handler 都忽略了驱逐信息。被驱逐的脏数据会**静默丢失**。

**影响范围**：所有 write-back 策略下的驱逐场景。

### B. Miss queue 不会被排空

GPGPU-Sim 的 `baseline_cache::cycle()` 每周期检查 `m_miss_queue` 是否非空，如果可以发送则弹出队首请求并推送到下级互联网络。

openCache 的 `BaselineCache::cycle()` **仅调用 `replenish_ports()`**。`m_miss_queue` 中累积的未决请求永远不会被处理发送。

**影响范围**：所有需要排队的 miss 请求（write-through 的写请求、write-allocate 的读请求等）会被永久阻塞。

### C. Sector cache 的扇区缺失处理存在数据破坏

GPGPU-Sim 的 `sector_cache_block` 有 `allocate_sector()` 方法：当已存在的行中缺少某个扇区时，只分配这一个扇区，其他扇区保持不变。

openCache 的 `SectorCacheBlock` **没有** `allocate_sector()` 方法。`allocate()` 调用 `init()` 会**重置整行的所有扇区**为 INVALID。这意味着：

- 扇区缺失（SECTOR_MISS）时，重新分配会销毁同行的其他有效扇区数据
- `TagArray::access()` 中没有 SECTOR_MISS 的显式处理分支，直接走 MISS 路径

**影响范围**：所有 sector cache 配置下的扇区缺失场景。

### D. MSHR 地址粒度在 sector cache 下不正确

GPGPU-Sim 使用 `m_config.mshr_addr()`（以 `atom_size` 为粒度——sector cache 时为 `SECTOR_SIZE`，line cache 时为 `line_size`）。

openCache 的 `send_read_request()` 使用 `m_config.get_block_addr()`（始终以 `line_size` 为粒度）。这导致在 sector cache 配置下，MSHR 的合并粒度过大，可能将应分别处理的不同扇区请求错误合并。

**影响范围**：SECTOR_ASSOC / SECTOR_TEX_FIFO MSHR 类型下。

### E. SECTOR_ASSOC 的扇区分片重组缺失

GPGPU-Sim 的 `baseline_cache::fill()` 在 MSHR 类型为 SECTOR_ASSOC 时有专门逻辑：跟踪 `pending_read` 计数（line_size / SECTOR_SIZE），等待所有扇区响应到齐后才标记就绪。这是 GPGPU-Sim 处理 "非扇区 L1 → 扇区 L2" 请求的关键机制。

openCache 的 `BaselineCache::fill()` 完全没有此逻辑。

**影响范围**：SECTOR_ASSOC MSHR 配置下。

### F. 带宽管理不完整

GPGPU-Sim 有完整的 `bandwidth_management` 内嵌类，包含 `use_data_port()` 和 `use_fill_port()` 方法（根据 data_size / port_width 计算占用周期），在每次 access 和 fill 时调用。

openCache 只有 `replenish_ports()`（周期性地递减计数器），但**从未调用 `use_data_port()` 或 `use_fill_port()`**。这意味着端口永远不会被标记为 busy，`data_port_free()` 和 `fill_port_free()` 永远返回 `true`。

**影响范围**：所有带宽限制场景——实际上带宽建模完全无效。

### G. LOCAL_WB_GLOBAL_WT 写策略不可达

`CacheConfig::parse_config_string()` 能正确解析 `'L'` 并设置 `WritePolicy::LOCAL_WB_GLOBAL_WT`，但 `DataCache::process_access()` 中的 switch-case 没有对应的分支，会 fall through 到 default，即调用 `write_hit_writeback()`。

**影响范围**：配置了 `LOCAL_WB_GLOBAL_WT` 的缓存行为与预期不符。

---

## 学术归属建议

openCache 中以下部分应当明确标注来源于 GPGPU-Sim 模型：

1. **缓存状态机模型**（INVALID → RESERVED → VALID / MODIFIED）
2. **MSHR 合并与就绪队列算法**
3. **配置字符串格式**及其解析逻辑
4. **写策略分类体系**（write-back / write-through / write-evict / write-allocate 的四种组合）
5. **Sector cache 的扇区粒度管理**模式
6. **LRU / FIFO 替换的实现方式**

建议在 README 或 LICENSE 文件中添加对 GPGPU-Sim 论文的引用：

> Bakhoda, A., Yuan, G. L., Fung, W. W. L., Wong, H., & Aamodt, T. M. (2009). "Analyzing CUDA Workloads Using a Detailed GPU Simulator." *IEEE International Symposium on Performance Analysis of Systems and Software (ISPASS)*.
