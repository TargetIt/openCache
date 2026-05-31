# openCache v2 — GPGPU-Sim Cache 差异审计报告

> **审计基准**: gpgpu-sim_distribution (dev), `src/gpgpu-sim/gpu-cache.h` / `gpu-cache.cc`  
> **审计目标**: openCache `src/` 全部文件（重构后）  
> **审计日期**: 2026-05-26  
> **审计方法**: 逐行对比，逐函数对比，逐算法对比

---

## 执行摘要

openCache 经过重构后，与 GPGPU-Sim cache 内核的**行为对齐度显著提高**。7 个关键缺陷（A~G）已全部修复。当前主要差距集中在以下几类：

| 类别 | 数量 | 严重程度 |
|------|------|----------|
| 行为完全一致 | ~30 项 | — |
| 仅命名/风格差异（无行为影响） | ~15 项 | 无 |
| 有意删除的 GPU 专属特性 | ~6 项 | 无（刻意为之） |
| **需要修复的 Bug** | **5 项** | 高 |
| 值得评审的开放问题 | ~12 项 | 中/低 |

---

## 第一章：行为一致性确认（~30 项）

以下核心机制与 GPGPU-Sim **完全一致**，无需任何修改：

### 1.1 缓存状态机
- INVALID → RESERVED（allocate 时） → VALID（fill 时） → MODIFIED（write hit 时）
- 状态转换时机、触发条件与 GPGPU-Sim 完全一致
- 文件: `cache_block.h`, `tag_array.cc`

### 1.2 Tag Array 核心操作
- `probe()`: 查找匹配 tag → 返回 HIT / SECTOR_MISS / MISS / HIT_RESERVED / RESERVATION_FAIL
- `access()`: 命中路径（更新 LRU、设置状态）、缺失路径（找 victim → 驱逐 → 分配）
- `fill()`: 按索引填充（ON_MISS）或按地址自动分配后填充（ON_FILL）
- SECTOR_MISS 路径: 调用 `allocate_sector()` 而非 `allocate()`，保护同行其他扇区
- 文件: `tag_array.cc` lines 36-313

### 1.3 MSHR 核心操作
- `probe()` / `full()` / `add()` / `mark_ready()` / `next_ready()` / `is_read_after_write_pending()`
- 合并限制（max_merge）、条目限制（num_entries）、就绪队列
- 与 GPGPU-Sim 行为完全一致
- 文件: `mshr.h`

### 1.4 BaselineCache 核心流程
- `cycle()`: 排空 miss queue（每周期发送一个请求到 lower memory）
- `fill()`: ON_MISS 按索引 fill、ON_FILL 按地址 fill，含 SECTOR_ASSOC 的 pending_read 倒计数
- `send_read_request()`: MSHR 命中合并 / 新缺失分配+入队 / 失败统计，使用 mshr_addr 粒度
- `replenish_ports()`: 逐周期递减端口占用计数
- 文件: `open_cache.cc` lines 38-217

### 1.5 DataCache 写命中处理器
- `wr_hit_wb`: 标记 MODIFIED → 设置 dirty byte mask → 更新 LRU
- `wr_hit_wt`: 同上 + miss_queue_full 检查 + 发送写请求到 miss queue
- `wr_hit_we`: 失效化 block + 发送写请求
- 逻辑与 GPGPU-Sim 完全一致
- 文件: `open_cache.cc` lines 384-446

### 1.6 DataCache 写缺失处理器
- `wr_miss_no_wa`: 仅发送写请求, 不分配
- `wr_miss_wa_naive`: 发送写请求 + 发送读请求（含 MSHR 检查 + writeback）
- `wr_miss_wa_fetch_on_write`: 整行写优化（>=atom_size 不发送读请求）+ 部分写走 MSHR + RAW 防危
- `wr_miss_wa_lazy_fetch_on_read`: 分配 + 标记 MODIFIED + 按字节数设置 readable
- 四个 handler 均包含 dirty eviction writeback 处理
- 文件: `open_cache.cc` lines 461-660

### 1.7 DataCache 读处理器
- `rd_hit_base`: 更新 last_access_time + 更新 LRU
- `rd_miss_base`: 通过 send_read_request 走 MSHR + 驱逐脏块写回
- 文件: `open_cache.cc` lines 664-703

### 1.8 函数指针分派
- `init_function_pointers()` 根据 config 设置 `m_wr_hit` / `m_wr_miss` / `m_rd_hit` / `m_rd_miss`
- `process_tag_probe()` 通过函数指针分派
- 架构与 GPGPU-Sim 的 `m_wr_hit ` 等成员函数指针模式一致
- 文件: `open_cache.cc` lines 288-373

### 1.9 替换策略
- LRU: move-to-front 算法, victim = order.back()
- FIFO: 模计数器, victim = fifo_next[set]
- 文件: `tag_array.cc` lines 57-97

### 1.10 配置字符串格式
- 与 GPGPU-Sim 兼容的 `N:nsets:bsize:assoc,policy,...` 格式
- 字符映射完全一致: N/S, L/F, R/B/T/E/L, m/f/s, N/W/F/L, L/X/P/C, A/S/F/T
- 文件: `cache_config.cc` lines 72-163

---

## 第二章：仅命名/风格差异（无行为影响，~15 项）

| # | 差异 | GPGPU-Sim | openCache | 影响 |
|---|------|-----------|-----------|------|
| 1 | 命名空间 | 全局 `::` | `namespace opencache` | 无 |
| 2 | 枚举类型 | C `enum` | C++11 `enum class : uint8_t` | 无（类型更安全） |
| 3 | 地址类型 | `new_addr_type` | `addr_t = uint64_t` | 无 |
| 4 | Block 方法名 | `is_invalid_line()` | `is_invalid()` | 无 |
| 5 | Block 方法名 | `print_status()` | `print()` | 无 |
| 6 | MSHR 容器 | `tr1_hash_map` | `std::unordered_map` | 无 |
| 7 | MSHR 请求存储 | `std::list<mem_fetch*>` | `std::vector<CacheRequest>` | 无 |
| 8 | 返回类型 | 裸 `enum` + 输出参数 `unsigned &idx` | `TagProbeResult` 结构体 | 无（信息更丰富） |
| 9 | Bandwidth 管理 | 内嵌 `bandwidth_management` 类 | 直接在 BaselineCache 内 | 无 |
| 10 | CacheStats 容器 | 多维 vector | `std::map<uint32_t, CacheSubStats>` | 无（支持多 stream） |
| 11 | Config 所有权 | `cache_config &` 引用 | `CacheConfig m_config` 值 | 无（更安全） |
| 12 | `get_tag()` = `get_block_addr()` | GPGPU-Sim 也如此 | 两者返回相同值 | 无（但 API 混淆，见 5.14） |
| 13 | 文件拆分 | 2 个文件 | 10 个文件 | 无 |
| 14 | `fill_latency` 参数 | 隐式（通过 memory subsystem） | 显式配置参数 | 无 |
| 15 | `hit_latency` 参数 | 隐式 | 显式配置参数 | 无 |

---

## 第三章：有意删除的 GPU 专属特性（~6 项，刻意为之）

这些特性与 GPU 模拟器紧耦合，openCache 作为通用 cache 模型不应包含。

| # | GPGPU-Sim 特性 | 删除原因 |
|---|---------------|----------|
| 1 | `tex_cache` 类（fragment_fifo / request_fifo / rob） | 纹理管线专属，通用 cache 不需要 |
| 2 | `l1_cache` / `l2_cache` 派生类 | 仅改变 `m_wr_alloc_type` / `m_wrbk_type`，只需 DataCache + 不同 Config |
| 3 | FERMI_HASH_SET_FUNCTION | Fermi 架构专属哈希，通用 cache 不需要 |
| 4 | `update_cache_parameters()` 动态 associativity | Volta unified cache 专属 |
| 5 | AerialVision / windowed stats / per-stream per-window 统计 | 可视化工具专属 |
| 6 | `mem_fetch` / `mem_fetch_allocator` / `gpgpu_sim *` 依赖 | GPU 模拟器耦合 |

---

## 第四章：需要修复的 Bug（5 项，严重程度：高）

### Bug 4.1 — `flush()` 和 `invalidate()` 对 SectorCacheBlock 崩溃

**位置**: `tag_array.cc` lines 315-335

```cpp
void TagArray::flush() {
    for (auto *line : m_lines) {
        if (line->is_modified()) {
            line->set_status(BlockState::INVALID, sector_mask_t()); // ← 空 bitset!
        }
    }
}
```

**问题**: `sector_mask_t()` 构造空 bitset（所有位为 0）。`SectorCacheBlock::set_status()` 调用 `get_sector_index(mask)` → `assert(mask.count() == 1)` → **崩溃**。

`LineCacheBlock::set_status()` 忽略 mask 参数所以无问题。

**修复方案**: 对 sector cache 逐扇区设置 INVALID:
```cpp
void TagArray::flush() {
    for (auto *line : m_lines) {
        if (line->is_modified()) {
            if (m_config.cache_type == CacheType::SECTOR) {
                for (uint32_t i = 0; i < m_config.sector_chunk_size; i++) {
                    sector_mask_t mask;
                    mask.set(i);
                    line->set_status(BlockState::INVALID, mask);
                }
            } else {
                line->set_status(BlockState::INVALID, sector_mask_t());
            }
        }
    }
}
```

`invalidate()` 同理。

**影响**: 任何 sector cache 调用 `flush()` 或 `invalidate()` 即刻崩溃。

### Bug 4.2 — SECTOR_MISS 路径脏计数器漂移

**位置**: `tag_array.cc` lines 199-210 + `tag_array.cc` lines 296-297

**问题**: SECTOR_MISS 时 `allocate_sector()` 将一个 MODIFIED 扇区转为 RESERVED（`m_set_modified_on_fill=true`），此时 `is_modified()` 返回 false → `m_dirty--`。

之后 `fill()` 调用时，检查 `was_modified = is_modified()` 为 false → 填充后 `is_modified()` 变为 true（扇区恢复 MODIFIED），但 `was_modified` 为 false，所以 `m_dirty++` 会执行。**这部分是正确的。**

但问题是：如果扇区被 `allocate_sector()` 处理时，另一个扇区也是 MODIFIED，则 `was_modified = block->is_modified()` 返回 true（另一个扇区仍是 MODIFIED），`m_dirty--` 不执行。但 `allocate_sector()` 重置的那个 MODIFIED 扇区确实失去了 dirty 状态。后续 `fill()` 中 `was_modified` 为 true，`m_dirty` 不增加。**净效果：m_dirty 少计数了 1。**

**修复方案**: 在 `allocate_sector()` 中只检查**该扇区**是否 MODIFIED，而非整行:
```cpp
bool sector_was_modified = (block->get_status(smask) == BlockState::MODIFIED);
static_cast<SectorCacheBlock*>(block)->allocate_sector(time, smask);
if (sector_was_modified) m_dirty--;
```

### Bug 4.3 — SECTOR_ASSOC pending_read 可能导致挂起

**位置**: `open_cache.cc` lines 201-202

```cpp
ef.pending_read = (m_config.mshr_type == MSHRType::SECTOR_ASSOC)
                  ? (m_config.line_size / m_config.sector_size) : 0;
```

**问题**: `pending_read` 被设为行内扇区总数。但如果只有部分扇区需要填充（例如只有 2/4 个扇区发了读请求），`fill()` 会等待全部 4 个 fill 响应才标记就绪，其余 2 个响应可能永远不会来 → **MSHR 条目永远挂起**。

**修复方案**: `pending_read` 应设为**实际请求的扇区数**而非扇区总数。在已知请求范围时设置为实际值；在无法确定时保守地设为 0（不做扇区分片重组）。

### Bug 4.4 — `TagArray::access()` 声明了 probe_mode 但未实现

**位置**: `tag_array.h` lines 33-35 vs `tag_array.cc` line 157

**问题**: 头文件声明了 `access(addr_t addr, uint64_t time, bool is_write, bool probe_mode)`（4 参数版本），但 .cc 文件中只有 3 参数和 5 参数版本，没有带 `probe_mode` 的实现。

`probe()` 有 `probe_mode` 参数（默认 false），但 `access()` 的 `probe_mode` 重载不存在。

**修复方案**: 从 `tag_array.h` 中删除未实现的 4 参数 `access()` 声明，或在 `tag_array.cc` 中补充实现。

### Bug 4.5 — `wr_hit_global_we_local_wb` 全部回退到 write-back

**位置**: `open_cache.cc` lines 448-457

```cpp
CacheResult DataCache::wr_hit_global_we_local_wb(...) {
    // Since openCache simplifies access types, default to write-back (conservative).
    return wr_hit_wb(req, time, set_index, way_index, flat_idx, events);
}
```

**问题**: GPGPU-Sim 根据 `mf->get_access_type()` 判断是 GLOBAL 还是 LOCAL——global 走 write-evict（立即失效+写回），local 走 write-back（保留 MODIFIED）。openCache 始终走 write-back，意味着 global store 永远不会被 evict，改变了一致性语义。

**修复方案**: 在 `CacheRequest` 或 `AccessType` 中添加地址空间信息（GLOBAL / LOCAL），在 handler 中据此分派。

---

## 第五章：值得评审的开放问题（~12 项）

### 5.1 `m_ignore_on_fill` — 死代码

`set_ignore_on_fill(true)` 在 `wr_miss_wa_fetch_on_write` 的 HIT_RESERVED 路径被设置（open_cache.cc line 550），但 `fill()` 方法中**从未检查** `m_ignore_on_fill`。在 GPGPU-Sim 中，`m_ignore_on_fill` 用于跳过 HIT_RESERVED 块的 fill 操作。openCache 未实现此逻辑。

**影响**: 被标记 ignore 的块仍会被 fill 覆盖。在特定时序下可能导致数据不一致。

### 5.2 `m_set_byte_mask_on_fill` — 死代码

`set_byte_mask_on_fill(true)` 从未被调用。`fill()` 中的 `if (m_set_byte_mask_on_fill)` 分支永远不会执行。

**影响**: 在 GPGPU-Sim 中此标志用于 write-allocate 场景中将 fill 数据合并到已有 dirty byte mask。openCache 不需要此功能因为 write-hit 直接设置了 dirty byte mask，但死代码应在清理时移除或补充实现。

### 5.3 `has_atomic` — MSHR 死字段

`mshr.h` line 128: `bool has_atomic;` 声明但从未设为 true，从未读取。

**影响**: 无运行影响，但表明原子操作支持不完整。

### 5.4 PLRU 实现实际是 LRU

`tag_array.cc` lines 77-80:
```cpp
} else if (m_config.replacement_policy == ReplacementPolicy::PLRU) {
    const auto &order = m_lru_order[set_index];
    return order.back(); // LRU at the back
}
```
注释本身承认 "Simple tree-PLRU approximation"。真正的 PLRU 使用二叉树逐位决策，O(1) 复杂度。

**影响**: 标记为 PLRU 的配置实际行为与 LRU 完全相同。要么实现真正的 PLRU，要么移除这个伪选项。

### 5.5 sector_mask_t 仅 4 位 — 大扇区数场景截断

`sector_mask_t = std::bitset<DEFAULT_SECTOR_CHUNK_SIZE>` = `bitset<4>`。如果 `line_size / sector_size > 4`（例如 256B 行 / 32B 扇区 = 8 扇区），`sector_mask_t` 只能表示 4 个扇区。扇区 4-7 的位会**静默截断**。

`SectorCacheBlock::MAX_SECTORS = 8`，但外部接口用 `bitset<4>` 传递扇区号。

**影响**: 当前所有测试使用 128B 行 / 32B 扇区 = 4 扇区，无问题。但配置 256B 行时会出现不可预期的行为。

### 5.6 `get_tag()` 与 `get_block_addr()` 返回相同值

`cache_config.h` lines 101-108: 两者都返回 `addr & ~(line_size - 1)`。两个函数名暗示不同功能（tag 应排除 set index 位），但实现完全相同。

**影响**: 无运行问题（tag 匹配在 set 范围内进行），但 API 具有误导性。

### 5.7 配置解析的降级路径脆弱

`cache_config.cc` line 87: `if (ntok < 12)` 触发简化格式解析。如果完整格式的前 11 个字段成功匹配但第 12 个失败，会静默降级到简化格式（可能用错误参数）。

**影响**: 配置错误时不会给出明确报错，可能被误用。

### 5.8 `wr_miss_wa_naive` 先写后读的请求顺序

open_cache.cc lines 488-496: 先 `send_write_request` 再 `send_read_request`。GPGPU-Sim 通常是先读后写。

**影响**: 可能影响内存一致性模型。对大多数 GPU 工作负载无实质影响（写请求和读请求到不同地址或同一缓存行）。

### 5.9 `m_dirty` 计数器可能下溢

`m_dirty` 是 `uint32_t`（tag_array.h line 70）。`m_dirty--` 在 `access()` line 250 无前置检查。如果 `flush()` 或 `invalidate()` 被调用但不清零 `m_dirty`，后续 eviction 会下溢。

**影响**: 统计计数器不可靠。GPGPU-Sim 的 `m_dirty` 也有类似问题。

### 5.10 `HIT_RESERVED` 读请求未合并入 MSHR

`process_tag_probe()` lines 365-367: 读 HIT_RESERVED 直接返回而不加入 MSHR。调用方需要自行轮询 `access_ready()`。

**影响**: 调用方必须正确实现轮询循环（`cycle()` → `access_ready()` → `next_ready()`），否则请求永久挂起。GPGPU-Sim 在 HIT_RESERVED 时也加入 MSHR。

### 5.11 `use_data_port` 仅在 HIT 时计费

open_cache.cc lines 111-126: MISS 路径不占用 data port。GPGPU-Sim 有时在 fill 数据传给核心时也占用 data port。

**影响**: 带宽建模简化。对大多数场景影响小（fill 通过 fill_port 计费）。

### 5.12 `WRITE_VALIDATE` 枚举存在但无处理路径

`AccessType::WRITE_VALIDATE` 在 `open_cache_types.h` line 128 定义，但所有 `access()` / `process_tag_probe()` 路径均不处理它。

**影响**: 如果调用方发送 WRITE_VALIDATE 请求，行为未定义。GPGPU-Sim 用于 LAZY_FETCH_ON_READ 的整行写优化。

---

## 第六章：修复优先级排序

| 优先级 | Bug/Issue | 影响 | 修复难度 |
|--------|-----------|------|----------|
| **P0** | 4.1 flush/invalidate 崩溃 | sector cache 不可用 | 低 |
| **P0** | 4.2 脏计数器漂移 | 统计错误，可能影响写回决策 | 低 |
| **P1** | 4.3 SECTOR_ASSOC 挂起 | sector L2 场景可能死锁 | 中 |
| **P1** | 4.4 probe_mode 声明未实现 | 接口不一致 | 低 |
| **P2** | 4.5 global_we_local_wb | L1 Fermi 行为不正确 | 中 |
| **P3** | 5.1 m_ignore_on_fill 死代码 | HIT_RESERVED 时序边缘情况 | 低 |
| **P3** | 5.4 PLRU = LRU | 误导性配置选项 | 中 |
| **P3** | 5.5 sector_mask 4位限制 | 大扇区配置不可用 | 中 |

---

## 附录：修订历史

| 版本 | 日期 | 说明 |
|------|------|------|
| v1 | 2026-05-26 | 初始对比（重构前），发现 7 个关键缺陷 A~G |
| v2 | 2026-05-26 | 重构后对比，7 个关键缺陷已修复，发现 5 个新 Bug + 12 个开放问题 |
