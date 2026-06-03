# GPGPU-Sim Cache Reference — 覆盖率深度分析 v2

审查日期：2026-06-03 | 审查视角：验证架构师 | 基线：74.05% lines / 77.33% funcs / 63.74% branches

---

## 执行摘要

当前 74% 的行覆盖率不足以证明缓存正确性。问题不在于数字不够高，而在于以下四个维度的**状态转换正确性**目前完全未被验证：

1. **Dirty 计数器（m_dirty）**：在 5 处递增/递减点中，没有任何测试在操作序列后断言 m_dirty 的值。一个差一错误（off-by-one）会静默损坏统计数据。
2. **Atomic 操作流水线**：fill-time atomic 处理和 data_cache 的 atomic read hit 路径完全未被覆盖。
3. **写策略的失败原因路由**：wr_miss_wa_naive 和 wr_miss_wa_fetch_on_write 中的背压/满队列分支从未执行。
4. **Sector 缓存混合状态**：同一行内 MODIFIED + RESERVED sector 的混合状态从未被测试。

这四个维度跨越约 **180 行 correctness-critical 代码**，但当前没有任何断言验证其正确性。

---

## 1. Tier 1 关键缺口：状态正确性（7 项，~80 lines）

### 1.1 tag_array::probe() — dirty_line_percentage 阈值（行 321-331）

**覆盖**：0% | **严重性**：CRITICAL

```cpp
float dirty_line_percentage = ((float)m_dirty / (m_config.m_nset * m_config.m_assoc)) * 100;
if (!line->is_modified_line() || dirty_line_percentage >= m_config.m_wr_percent) {
```

当 `m_wr_percent > 0` 且 dirty_line_percentage 低于阈值时，MODIFIED 行应被保护不被驱逐。这条路径从未被测试。

**根因**：所有测试使用 `wr_percent=0`（默认值）。

**修复**：设置 `wr_percent=50`，先制造 dirty 行，将 dirty_percentage 推到 <50%，再尝试驱逐 — 应被阻止。

---

### 1.2 sector_cache_block::allocate_sector() — MODIFIED 覆盖（行 379-381）

**覆盖**：0% | **严重性**：CRITICAL

```cpp
if (m_status[sidx] == MODIFIED)
    m_set_modified_on_fill[sidx] = true;
```

当 sector 当前为 MODIFIED 状态并被重新分配时，标记 `m_set_modified_on_fill` 使 fill 后恢复 MODIFIED。此路径从未触发。

**根因**：所有测试仅在 INVALID 或 RESERVED sector 上调用 allocate_sector。

**修复**：先 write hit 让 sector 变为 MODIFIED，再通过 MISS 同一 sector 触发重新分配。

---

### 1.3 tag_array::access() — SECTOR_MISS 的 dirty 递减（行 415-420）

**覆盖**：0% | **严重性**：CRITICAL

```cpp
bool before = m_lines[idx]->is_modified_line();
((sector_cache_block *)m_lines[idx])->allocate_sector(...);
if (before && !m_lines[idx]->is_modified_line()) { m_dirty--; }
```

当 MODIFIED sector 被分配为 RESERVED 后整行不再是 MODIFIED 时，m_dirty 应减 1。从未被测试。

**根因**：需要先制造 MODIFIED sector（write hit），再在同一行的不同 sector 上触发 SECTOR_MISS。

**修复**：write hit sector 0 → dirty → SECTOR_MISS sector 1 → allocate_sector 覆盖 sector 0 → 验证 m_dirty 减 1。

---

### 1.4 tag_array::fill() — dirty 递减驱逐路径（行 454-466）

**覆盖**：部分 | **严重性**：CRITICAL

当 fill 到一个已有 MODIFIED sector 的行时，被替换的 MODIFIED sector 状态改变后 m_dirty 的递减逻辑。

**根因**：所有 fill 测试都是从空行开始。没有 fill 到已存在其他 MODIFIED sector 的行的场景。

**修复**：先制造 MODIFIED sector → 另一个 tag fill 到同一 set → 验证 dirty 计数。

---

### 1.5 tag_array::probe() — 混合 MODIFIED+RESERVED 状态（行 294-295）

**覆盖**：0% | **严重性**：CRITICAL

```cpp
if (line->get_status(mask) == RESERVED) return HIT_RESERVED;
```

当同一行同时存在 MODIFIED sector（已填充并写入）和 RESERVED sector（新分配但未填充）时，probe 行为是否正确？

**根因**：没有测试先制造 MODIFIED sector，再触发 SECTOR_MISS 使同一行的另一个 sector 变为 RESERVED。

**修复**：两步操作：write hit sector 0 → MODIFIED，然后 SECTOR_MISS sector 1 → RESERVED。Probe 应针对不同 mask 返回不同结果。

---

### 1.6 baseline_cache::fill() — atomic 标记 MODIFIED（行 1402-1412）

**覆盖**：0% | **严重性**：CRITICAL

```cpp
if (has_atomic) {
    // mark line as modified after atomic fill
    ...
}
```

Atomic miss 的 fill 路径从未被执行。FTC-07 只测试了 atomic read HIT（不经过 fill）。

**根因**：没有创建 atomic miss 场景（atomic mem_fetch + MISS 状态 + fill 完成）。

**修复**：创建 isatomic()=true 的 mem_fetch，发 MISS → fill → 验证 line 被标记为 MODIFIED。

---

### 1.7 data_cache::rd_hit_base() — atomic read hit 通过 data_cache（行 1939-1948）

**覆盖**：0% | **严重性**：CRITICAL

FTC-07 通过 read_only_cache 测试 atomic hit，但 read_only_cache 绕过了 data_cache::process_tag_probe()。通过 data_cache 的 atomic hit 路径完全未覆盖。

**根因**：FTC-07 使用了 read_only_cache（更简单的接口）。从未创建 data_cache + atomic 的测试。

**修复**：用 l1_cache 或 data_cache 创建 atomic mem_fetch，发送 HIT → 验证块标记为 MODIFIED。

---

## 2. Tier 2 高缺口：性能正确性（5 项，~60 lines）

### 2.1 wr_miss_wa_naive() 失败原因分支（行 1647-1655）

三个失败子分支（miss_queue_full、mshr_hit && !mshr_avail、!mshr_hit && !mshr_avail）从未被触发。

**修复**：配置 miss_queue=1, MSHR entry=1，分三次发 WA_NAIVE write miss 触发三个不同失败路径。

### 2.2 wr_miss_wa_fetch_on_write() 背压分支（行 1772-1780）

fetch_on_write partial-write 的三个失败路径类似未测试。

**修复**：同上，但用 FETCH_ON_WRITE 策略。

### 2.3 send_read_request() defer+wa+wb 组合（行 1494, 1501）

defer_hit_response + write_allocate + writeback 的组合标志路径从未被同时触发。

**修复**：打开 defer_hit_response=true，用一个包含 write-allocate 和 eviction 的写 miss 场景测试。

### 2.4 baseline_cache::cycle() 背压暂停（行 1357）

miss queue 因 memory interface 满而暂停的 cycle-level 路径。

**修复**：已有 FTC-04/05 测试了 access 时的背压，需要增加 cycle 级别的暂停测试。

### 2.5 ON_FILL + writable data_cache fill 路径（行 1396-1399）

FTC-10 用 read_only_cache 测试了 ON_FILL，但 data_cache + ON_FILL 路径未测试。

**修复**：l1_cache + 'f' 配置 + write hit 场景。

---

## 3. Tier 3 中缺口：错误处理和边界（7 项，~50 lines）

### 3.1-3.7

包括 process_tag_probe NWA+RESERVATION_FAIL、l1d_cache_config set_bank、l2_cache_config 实例化、wr_hit 完整路径覆盖、config 边界值等。详见 Feature Test Case 文档。

---

## 4. 覆盖率提升策略（修正版）

| 优先级 | 类别 | 预计新增测试 | 预计行覆盖提升 | 关键价值 |
|--------|------|:----------:|:------------:|----------|
| **Tier 1** | 状态正确性 | 7 个 | +3-5% | 防止 dirty counter、atomic、sector 混合状态的静默错误 |
| **Tier 2** | 性能正确性 | 5 个 | +2-3% | 防止失败原因路由、背压暂停的错误 |
| **Tier 3** | 边界和错误处理 | 5 个 | +1-2% | 提高 fail stats 和 config 边缘覆盖 |
| **不可达** | l1/l2 config 集成桩 | 0 | — | 依赖完整 GPGPU-Sim 基础设施 |

---

## 5. 理论覆盖率上限重估

- **可达到上限**（完成 Tier 1+2+3）：~85-87% lines, ~78-80% branches
- **不可达白名单**（~53 lines）：l1d_cache_config::set_bank、l2_cache_config::init/set_index、tag_array add_pending_line/remove_pending_line/windowed_miss_rate/new_window、inc_aggregated_stats 桩
- **低价值**（~120 lines）：debug 打印函数（已通过 FTC-14 smoke 覆盖）、部分统计聚合操作符
- **编译器生成的默认分支**（~20 lines）：switch/default abort 路径

扣除不可达和低价值代码后，实际可达到覆盖率上限约 **93% lines / 85% branches**，足以提供高置信度的正确性保证。
