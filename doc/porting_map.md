# openCache ↔ GPGPU-Sim 代码移植映射表

> 基准: gpgpu-sim_distribution (dev), `src/gpgpu-sim/gpu-cache.h` (1943 行), `gpu-cache.cc` (2164 行)  
> 目标: openCache `src/` 目录下 10 个文件  
> 更新日期: 2026-05-26

---

## 总体概览

openCache 的 10 个源文件移植自 GPGPU-Sim 的 2 个文件（`gpu-cache.h` + `gpu-cache.cc`，合计 ~4100 行）。移植时做了 C++ 现代化（`namespace opencache`、`enum class`、STL 容器替换 `tr1_hash_map`）。

**未移植的 GPGPU-Sim 代码**（GPU 专属）:
- `l1d_cache_config` / `l2_cache_config` 派生类
- `l1_cache` / `l2_cache` 派生类
- `tex_cache` 纹理缓存类
- `cache_sub_stats_pw` 窗口统计
- `new_window()` / `windowed_miss_rate()` / AerialVision 统计
- `update_cache_parameters()` 动态 associativity
- `add_pending_line()` / `remove_pending_line()` pending 追踪
- `inc_aggregated_stats*()` 层级统计聚合

---

## 文件级映射

### 1. `src/open_cache_types.h` (openCache 232 → 247 行)

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `BlockState` enum | `enum cache_block_state` | gpu-cache.h:47 | 改为 `enum class`, 加 `: uint8_t` |
| `AccessStatus` enum | `enum cache_request_status` | gpu-cache.h:49-56 | 去掉 `CACHE_` 中缀, `NUM_STATUS` 替代 `NUM_CACHE_REQUEST_STATUS` |
| `ReservationFailReason` enum | `enum cache_reservation_fail_reason` | gpu-cache.h:59-64 | 修正拼写 `ENRTY`→`ENTRY` |
| `CacheEventType` enum | `enum cache_event_type` | gpu-cache.h:68-70 | 同值 |
| `CacheLevel` enum | `enum cache_gpu_level` | gpu-cache.h:75-77 | 新增 L3, 去掉 `_GPU` 后缀 |
| `ReplacementPolicy` enum | `enum replacement_policy_t` | gpu-cache.h:514 | **新增** RANDOM, PLRU |
| `WritePolicy` enum | `enum write_policy_t` | gpu-cache.h:516-522 | 同值 |
| `AllocationPolicy` enum | `enum allocation_policy_t` | gpu-cache.h:524 | 去掉 STREAMING（Pascal/Volta 专属） |
| `WriteAllocatePolicy` enum | `enum write_allocate_policy_t` | gpu-cache.h:526-531 | 同值 |
| `MSHRType` enum | `enum mshr_config_t` | gpu-cache.h:533-538 | 同值（TEX_FIFO/SECTOR_TEX_FIFO 未使用） |
| `SetIndexFunction` enum | `enum set_index_function` | gpu-cache.h:540-546 | 去掉 FERMI_HASH_SET_FUNCTION |
| `CacheType` enum | `enum cache_type` | gpu-cache.h:548 | 同值 |
| `AccessType` enum | — | **NEW** | openCache 自创，替代 GPGPU-Sim 的 `mem_access_type`（定义在 `abstract_hardware_model.h`） |
| `CacheRequest` struct | `mem_fetch` (mem_fetch.h) | — | **NEW** 独立结构体，替代 `mem_fetch*` 依赖 |
| `CacheResult` struct | — | **NEW** | 包装返回值 |
| `EvictedBlockInfo` struct | `evicted_block_info` | gpu-cache.h:82-105 | 改名，去掉 `set_info()` 方法 |
| `CacheEvent` struct | `cache_event` | gpu-cache.h:107-123 | 改名 |
| `access_status_str()` | `cache_request_status_str()` | gpu-cache.cc:42-55 | 内联到 .h |
| `was_write_sent()` etc. | `was_write_sent()` etc. | gpu-cache.cc:515-567 | 改用 `std::vector` 替代 `std::list` |

### 2. `src/cache_config.h` + `src/cache_config.cc`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `CacheConfig` 类 | `cache_config` | gpu-cache.h:556-905 | 去掉了 `m_config_string`/`m_valid`/`m_disabled`/`FuncCache`/`m_result_fifo_entries`/`m_is_streaming`/`original_m_assoc` |
| `compute_derived()` | `init()` 中的内联计算 | gpu-cache.h:592-671 | 提取为独立方法 |
| `parse_config_string()` | `init(char*, FuncCache)` | gpu-cache.h:568-905 | 返回 bool 替代 void+abort；新增 3 字段简化格式 |
| `to_config_string()` | — | **NEW** | 配置序列化（GPGPU-Sim 无此功能） |
| `hash_addr()` | `hash_function()` | gpu-cache.h:721-734 | 内联实现替代外部 `hashing.cc` |
| `get_set_index()` | `set_index()` | gpu-cache.h:714-719 | 简化 |
| `get_bank_index()` | `l1d_cache_config::set_bank()` | gpu-cache.h:907-934 | 移植自 L1 派生类 |
| `get_tag()` / `get_block_addr()` | `tag()` / `block_addr()` | gpu-cache.h:700-711 | 两者返回相同值（GPGPU-Sim 也如此） |
| `get_mshr_addr()` | `mshr_addr()` | gpu-cache.h:712-713 | 使用 `atom_size` 粒度 |
| `get_sector_mask()` | **NEW** | — | 从地址计算扇区掩码 |
| `log2_u32()` | `LOGB2()` 宏 | — | constexpr 函数替代宏 |
| `is_streaming()` | `is_streaming()` | gpu-cache.h:684 | 简化：仅检查 `alloc_policy == ON_FILL` |
| `l1d_cache_config` | `l1d_cache_config` | gpu-cache.h:907-934 | **未移植**（GPU 专属） |
| `l2_cache_config` | `l2_cache_config` | gpu-cache.h:936-944 | **未移植**（GPU 专属） |

### 3. `src/cache_block.h`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `CacheBlock` 抽象类 | `cache_block_t` | gpu-cache.h:125-170 | 改名，方法签名去 `_line` 后缀；`set_byte_mask(mem_fetch*)` 重载去掉 |
| `LineCacheBlock` | `line_cache_block` | gpu-cache.h:172-283 | 类型改用 `uint64_t`（原 `unsigned long long`）；方法改名 |
| `SectorCacheBlock` | `sector_cache_block` | gpu-cache.h:285-512 | `MAX_SECTORS=8`（原 `SECTOR_CHUNCK_SIZE=4`）；数组大小可配；修正 `CHUNCK`→`CHUNK` |
| `SectorCacheBlock::allocate_sector()` | `sector_cache_block::allocate_sector()` | gpu-cache.h:360-384 | 独立实现，保持已有效扇区数据 |
| `create_cache_block()` | tag_array 构造函数内联 | gpu-cache.cc:205-219 | 提取为工厂函数 |

### 4. `src/tag_array.h` + `src/tag_array.cc`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `TagArray` 类 | `tag_array` | gpu-cache.h:946-1022 | 去掉 `mem_fetch*` 参数；`TagProbeResult` 替代输出参数 `unsigned &idx` |
| `TagArray::TagArray()` | `tag_array::init()` | gpu-cache.cc:205-219 | `std::vector` 替代裸数组；LRU/FIFO 状态显式初始化 |
| `probe()` (2 重载) | `tag_array::probe()` | gpu-cache.cc:239-334 | 返回 `TagProbeResult` 替代输出参数；去掉 dirty 百分比过滤 |
| `access()` (2 重载) | `tag_array::access()` | gpu-cache.cc:335-400 | SECTOR_MISS 调用 `allocate_sector()`（**修复** v2.4.2） |
| `fill()` (3 重载) | `tag_array::fill()` | gpu-cache.cc:402-448 | ON_FILL 策略自动分配（**新增**） |
| `flush()` | `tag_array::flush()` | gpu-cache.cc:450-462 | Sector 逐扇区刷新（**修复** v2.4.1） |
| `invalidate()` | `tag_array::invalidate()` | gpu-cache.cc:464-483 | 同 flush；重置 LRU/FIFO；清零 `m_dirty` |
| `find_victim()` | 内联在 `probe()` 中 | gpu-cache.cc:271-310 | **提取**为独立方法；新增 RANDOM/PLRU |
| `update_replacement_state()` | 内联在各处 | — | **提取**为独立方法 |
| `lru_promote()` / `fifo_advance()` | 内联在 `probe()` 中 | — | **提取**为独立方法 |
| `find_matching_way()` | 内联在 `probe()` 中 | — | **提取**为独立方法 |
| `find_invalid_way()` | 内联在 `probe()` 中 | — | **提取**为独立方法 |
| `inc_dirty()` | `tag_array::inc_dirty()` | gpu-cache.h:997 | 公开方法 |
| `get_stats()` | `tag_array::get_stats()` | gpu-cache.cc:505-513 | 同逻辑 |
| `print()` | `tag_array::print()` | gpu-cache.cc:492-503 | 简化输出 |
| `pending_lines` / `add_pending_line()` / `remove_pending_line()` | 同 | gpu-cache.cc:221-237 | **未移植** |
| `update_cache_parameters()` | 同 | gpu-cache.cc:184-203 | **未移植**（GPU 动态 associativity） |
| `new_window()` / `windowed_miss_rate()` | 同 | gpu-cache.cc:485-490 | **未移植**（AerialVision） |

### 5. `src/mshr.h`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `MSHRTable` 类 | `mshr_table` | gpu-cache.h:1024-1080 | `std::unordered_map` 替代 `tr1_hash_map`；`std::vector` 替代 `std::list<mem_fetch*>` |
| `probe()` | `mshr_table::probe()` | gpu-cache.h:1039 | 内联 |
| `full()` | `mshr_table::full()` | gpu-cache.h:1042 | 内联 |
| `add()` | `mshr_table::add()` | gpu-cache.cc:569-593 | `CacheRequest` 值语义替代 `mem_fetch*` |
| `mark_ready()` | `mshr_table::mark_ready()` | gpu-cache.cc:595-617 | 去掉 `has_atomic` 参数 |
| `next_ready()` | `mshr_table::next_access()` | 内联 | 返回 `vector` 替代单个指针 |
| `is_read_after_write_pending()` | 同 | gpu-cache.h:1073 | 同逻辑 |
| `print()` | `mshr_table::display()` | gpu-cache.cc:619-639 | 简化输出 |
| `clear()` | — | **NEW** | GPGPU-Sim 无此方法 |
| `busy()` | `mshr_table::busy()` | gpu-cache.h:1050 | **未移植**（始终返回 false） |
| `check_mshr_parameters()` | 同 | gpu-cache.h:1063 | **未移植**（跨 kernel 检查不需要） |
| `has_atomic` 字段 | `m_has_atomic` | gpu-cache.h:1080 | 声明但未使用（原子操作支持待实现） |

### 6. `src/cache_stats.h`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `CacheSubStats` struct | `cache_sub_stats` | gpu-cache.h:1088-1142 | 新增 `read_hits`/`read_misses`/`write_hits`/`write_misses`/`sector_misses`；新增 `hit_rate()`/`read_hit_rate()`/`write_hit_rate()` 方法 |
| `CacheStats` 类 | `cache_stats` | gpu-cache.h:1204-1259 | 简化：仅单 stream (id=0)；`std::map<uint32_t, CacheSubStats>` 替代多维 vector |
| `record_access()` | `inc_stats()` | gpu-cache.cc:662-681 | 自动计算 read/write hit/miss |
| `record_fail()` | `inc_fail_stats()` | gpu-cache.cc:704-720 | 简化：不去掉 type 参数（但当前未使用） |
| `select_stats_status()` | 同 | gpu-cache.cc:722-917 | 同逻辑 |
| `sample_port_utility()` | 同 | gpu-cache.cc:1134-1151 | 同逻辑 |
| `get_sub_stats()` | 同 | gpu-cache.cc:1021-1054 | 简化 |
| `clear_pw()` / `inc_stats_pw()` / `get_sub_stats_pw()` | 同 | gpu-cache.cc:655-660, 683-702, 1055-1132 | **未移植**（AerialVision 窗口统计） |
| `operator()` 索引器 / `operator+` | 同 | gpu-cache.h:1227-1239 | **未移植**（层级聚合不需要） |

### 7. `src/open_cache.h` + `src/open_cache.cc`

| openCache 内容 | GPGPU-Sim 来源 | 行号 | 移植说明 |
|---------------|---------------|------|---------|
| `CacheMemoryInterface` | `mem_fetch_interface` (mem_fetch.h) | — | **NEW** 独立抽象接口，替代 `mem_fetch_interface` + `mem_fetch_allocator` |
| `SimpleMemory` | — | **NEW** | 测试桩，独立运行不需要外部组件 |
| `BaselineCache` 类 | `baseline_cache` | gpu-cache.h:1280-1481 | 去掉 `mem_fetch_status`/`gpgpu_sim*`/`bandwidth_management` 内嵌类；ExtraFields 键改为 `addr_t` 替代 `mem_fetch*`；Config 值语义替代引用 |
| `BaselineCache::BaselineCache()` | `baseline_cache()` 构造函数 | gpu-cache.h:1285-1293 | MSHR 在初始化列表构建 |
| `BaselineCache::cycle()` | `baseline_cache::cycle()` | gpu-cache.cc:1215-1229 | **修复**: 新增 miss queue 排空；不调用 `bandwidth_management` |
| `BaselineCache::fill()` | `baseline_cache::fill()` | gpu-cache.cc:1231-1282 | **修复**: SECTOR_ASSOC pending_read 倒计数；ON_FILL 路径 |
| `send_read_request()` (2 重载) | `baseline_cache::send_read_request()` | gpu-cache.cc:1341-1400 | **修复**: 使用 `mshr_addr` 粒度；`pending_read=1` 替代 `line_size/sector_size`；完整 fail 统计 |
| `use_data_port()` | `bandwidth_management::use_data_port()` | gpu-cache.cc:1153-1183 | 去除 `mem_fetch*` 依赖；仅 HIT 计费 |
| `use_fill_port()` | `bandwidth_management::use_fill_port()` | gpu-cache.cc:1185-1190 | 简化为 `atom_size / port_width` |
| `replenish_ports()` | `bandwidth_management::replenish_port_bandwidth()` | gpu-cache.cc:1192-1203 | 直接嵌入 BaselineCache |
| `ReadOnlyCache` 类 | `read_only_cache` | gpu-cache.h:1483-1507 | **修复**: `access()` 走 `send_read_request` + MSHR |
| `ReadOnlyCache::access()` | `read_only_cache::access()` | gpu-cache.cc:1883-1927 | **修复**: 完整 MSHR + miss queue 集成 |
| `DataCache` 类 | `data_cache` | gpu-cache.h:1509-1700 | 去掉 `mem_fetch_allocator*`/`m_wr_alloc_type`/`m_wrbk_type`/`gpgpu_sim*` |
| `DataCache::init_function_pointers()` | `data_cache::init()` | gpu-cache.h:1520-1580 | **提取**为独立方法；switch 替代 if-else |
| `DataCache::access()` | `data_cache::access()` | gpu-cache.cc:1977-1999 | 去掉 `mem_fetch*` 参数 |
| `process_tag_probe()` | `data_cache::process_tag_probe()` | gpu-cache.cc:1929-1975 | 函数指针分派（架构一致） |
| `send_write_request()` | `data_cache::send_write_request()` | gpu-cache.cc:1402-1408 | 使用 `CacheRequest` 替代 `mem_fetch*` |
| `wr_hit_wb()` | `data_cache::wr_hit_wb()` | gpu-cache.cc:1431-1448 | **修复**: 含 `inc_dirty()` |
| `wr_hit_wt()` | `data_cache::wr_hit_wt()` | gpu-cache.cc:1450-1477 | **修复**: 含 `inc_dirty()` + miss_queue_full 检查 |
| `wr_hit_we()` | `data_cache::wr_hit_we()` | gpu-cache.cc:1479-1499 | 同逻辑 |
| `wr_hit_global_we_local_wb()` | `data_cache::wr_hit_global_we_local_wb()` | gpu-cache.cc:1501-1516 | **修复**: 基于 `is_global_access` 字段分派（原用 `GLOBAL_ACC_W` 枚举值） |
| `wr_miss_no_wa()` | `data_cache::wr_miss_no_wa()` | gpu-cache.cc:1799-1817 | 同逻辑 |
| `wr_miss_wa_naive()` | `data_cache::wr_miss_wa_naive()` | gpu-cache.cc:1518-1598 | **修复**: 含完整 MSHR 检查 + writeback 生成 |
| `wr_miss_wa_fetch_on_write()` | `data_cache::wr_miss_wa_fetch_on_write()` | gpu-cache.cc:1600-1729 | **修复**: 整行写优化 + RAW 防危 + writeback |
| `wr_miss_wa_lazy_fetch_on_read()` | `data_cache::wr_miss_wa_lazy_fetch_on_read()` | gpu-cache.cc:1731-1797 | **修复**: readable 标记 + writeback |
| `rd_hit_base()` | `data_cache::rd_hit_base()` | gpu-cache.cc:1819-1841 | 同逻辑（去掉 atomic 检查） |
| `rd_miss_base()` | `data_cache::rd_miss_base()` | gpu-cache.cc:1843-1881 | **修复**: 含 writeback |
| `l1_cache` / `l2_cache` 类 | 同 | gpu-cache.h:1702-1749 | **未移植**（仅改 `m_wr_alloc_type`/`m_wrbk_type`，用 Config 替代） |
| `tex_cache` 类 | 同 | gpu-cache.h:1751-1943 | **未移植**（GPU 纹理管线专属） |

---

## 关键修改标注

以下是在移植过程中对 GPGPU-Sim 原始逻辑做了**行为变更**的位置：

| 位置 | 原 GPGPU-Sim 行为 | openCache 修改 | 原因 |
|------|------------------|---------------|------|
| `tag_array.cc` flush/invalidate | 逐行设置 INVALID（对 sector cache 可能崩溃） | 逐扇区设置 INVALID | Bug 4.1 修复 |
| `tag_array.cc` SECTOR_MISS dirty | 检查整行 `is_modified()` | 检查扇区 `get_status(smask) == MODIFIED` | Bug 4.2 修复 |
| `open_cache.cc` pending_read | `line_size / sector_size` | `1` | Bug 4.3 修复 |
| `open_cache.cc` wr_hit_global_we_local_wb | 基于 `GLOBAL_ACC_W` vs local | 基于 `CacheRequest::is_global_access` | Bug 4.5 修复 |
| `cache_stats.h` m_fail_stats 初始化 | `{8, 0}` (2 元素) | `std::vector<uint64_t>(8, 0)` (8 元素) | 堆溢出修复 |
| `cache_config.h` ALLOCATION_POLICY | 含 `STREAMING` | 去掉，`'s'` 映射到 `ON_FILL` | Pascal/Volta 专属 |
| `cache_config.h` SET_INDEX_FUNCTION | 含 `FERMI_HASH` | 去掉 | Fermi 专属 |
| `open_cache_types.h` REPLACEMENT_POLICY | 仅 `LRU, FIFO` | 新增 `RANDOM, PLRU` | 通用 cache 增强 |
| `open_cache_types.h` CACHE_LEVEL | `L1_GPU_CACHE, L2_GPU_CACHE, OTHER_GPU_CACHE` | 新增 `L3`，去 `_GPU` 后缀 | 通用化 |
| `tag_array.cc` PLRU 实现 | — | LRU 回退（非真 PLRU） | 已知问题 5.4 |
| `tag_array.cc` find_victim() | 内联在 probe() 中 | 提取为独立方法 | 代码组织 |
| `tag_array.cc` update_replacement_state() | 分散在各处 | 集中管理 | 代码组织 |
