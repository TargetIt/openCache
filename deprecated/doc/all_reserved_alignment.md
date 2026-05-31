# openCache all_reserved + dirty_percentage 对齐方案

> 背景: GPGPU-Sim `tag_array::probe()` 中有 `all_reserved` 和 `dirty_percentage` 机制，openCache 当前未实现  
> 目标: 分析差异，给出具体修改方案  
> 日期: 2026-05-26

---

## 一、GPGPU-Sim 的机制是什么

### 1.1 代码位置

`gpu-cache.cc:246-334` — `tag_array::probe()` 中的 victim selection 和 `all_reserved` 检查。

### 1.2 逻辑流程

```
probe(addr, idx, mask, is_write)
  │
  ├── 遍历 set 内所有 way
  │   │
  │   ├── tag 匹配? → HIT/HIT_RESERVED/SECTOR_MISS → 立即返回
  │   │
  │   └── tag 不匹配? → 检查此 way 是否可驱逐:
  │       │
  │       ├── line->is_reserved_line()? → 跳过 (不能驱逐 RESERVED 的行)
  │       │
  │       └── !line->is_reserved_line():
  │           │
  │           ├── 计算 dirty_line_percentage:
  │           │   dirty_pct = (m_dirty / (nsets * assoc)) * 100
  │           │
  │           ├── line 是 MODIFIED 且 dirty_pct < m_wr_percent?
  │           │   → 跳过 (保护脏行，优先驱逐干净行)
  │           │
  │           └── 否则 → 可驱逐:
  │               all_reserved = false
  │               ├── line 是 INVALID? → 记录为 invalid_line (最高优先)
  │               └── line 是 VALID/MODIFIED? → 按 LRU/FIFO 时间戳比较,
  │                   记录最优 victim
  │
  ├── all_reserved == true?
  │   → return RESERVATION_FAIL  ← 整个 set 被锁死
  │
  └── 否则:
      ├── 有 invalid_line? → idx = invalid_line
      └── 否则 → idx = valid_line (最老的可驱逐行)
```

### 1.3 关键设计意图

**`all_reserved`**: 防止一个 set 内所有 way 都处于 RESERVED（等待 fill）时继续分配。如果强行分配，会驱逐一个 RESERVED 的行，导致正在进行的 fill 写错位置。

**`dirty_percentage`**: 保护脏行不被驱逐。写回脏行到 DRAM 开销大。当脏行比例低时，优先驱逐干净行（驱逐成本为 0）。当脏行比例超过阈值（`m_wr_percent`）时，不再保护——因为此时驱逐干净行的可选余地已经很小。

---

## 二、openCache 当前的行为

### 2.1 当前流程

```
access(addr, time, is_write)
  │
  ├── find_matching_way(set, tag) → way >= 0?
  │   └── HIT / HIT_RESERVED / SECTOR_MISS 处理
  │
  └── way < 0 (MISS):
      │
      ├── find_invalid_way(set) → 有 INVALID way?
      │   └── 有: 直接分配
      │
      └── 无 INVALID way:
          │
          └── find_victim(set) → 返回:
              ├── FIFO: m_fifo_next[set]
              ├── RANDOM: rand() % assoc
              ├── PLRU: order.back() (实际是 LRU)
              └── LRU: order.back()
              ⚠️ 从不检查 victim 是否为 RESERVED
              ⚠️ 从不检查 dirty_percentage
              ⚠️ 永远返回一个 way (无失败路径)
```

### 2.2 问题

当前 `find_victim()` 有 3 个缺陷：

1. **不检查 RESERVED**: 如果 set 内所有 way 都是 RESERVED，`find_victim()` 仍然返回一个 way（LRU 的最后一个），`access()` 会驱逐它。被驱逐的行正在等待 fill 数据——fill 到达后会写入一个 "已被新请求占据" 的位置 → **数据错乱**。

2. **不检查 dirty_percentage**: 所有可驱逐的行（VALID/MODIFIED）被平等对待。GPGPU-Sim 的设计是在脏行比例低时保护脏行。openCache 没有这种保护，可能不必要地驱逐脏行导致额外的 writeback 流量。

3. **无失败返回**: 返回值是 `uint32_t`，无 "找不到 victim" 的表示。如果所有 way 都不可驱逐，调用方无法感知。

---

## 三、修改方案

### 3.1 涉及的文件

仅需修改 **1 个文件**：`src/tag_array.cc`（以及对应的头文件 `src/tag_array.h`，仅改 `find_victim` 签名）。

### 3.2 修改 1：`find_victim()` 返回值改为 `int32_t`，增加失败路径

**文件**: `tag_array.h` line 55, `tag_array.cc` lines 100-114

**当前签名**:
```cpp
uint32_t find_victim(uint32_t set_index) const;
```

**修改为**:
```cpp
int32_t find_victim(uint32_t set_index) const;
// 返回 >=0: victim way index
// 返回 -1: 无可驱逐的 way (all_reserved)
```

**当前实现**:
```cpp
uint32_t TagArray::find_victim(uint32_t set_index) const {
    if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
        return m_fifo_next[set_index];
    } else if (m_config.replacement_policy == ReplacementPolicy::RANDOM) {
        return static_cast<uint32_t>(rand()) % m_config.associativity;
    } else if (m_config.replacement_policy == ReplacementPolicy::PLRU) {
        const auto &order = m_lru_order[set_index];
        return order.back();
    } else {
        const auto &order = m_lru_order[set_index];
        return order.back();
    }
}
```

**新实现**:
```cpp
int32_t TagArray::find_victim(uint32_t set_index) const {
    // Compute dirty line percentage for the entire cache
    float dirty_pct = (m_config.num_sets * m_config.associativity > 0)
        ? (static_cast<float>(m_dirty) /
           static_cast<float>(m_config.num_sets * m_config.associativity)) * 100.0f
        : 0.0f;

    int32_t best_victim = -1;
    uint64_t best_timestamp = UINT64_MAX;

    for (uint32_t w = 0; w < m_config.associativity; ++w) {
        uint32_t idx = get_line_index(set_index, w);
        const auto *block = m_lines[idx];

        // [NEW] Cannot evict a RESERVED line — it's waiting for fill data
        if (block->is_reserved()) continue;

        // [NEW] Protect MODIFIED (dirty) lines when dirty percentage is low.
        // Evicting a dirty line requires writeback to lower memory.
        // Prefer clean (VALID/INVALID) victims when possible.
        if (block->is_modified() &&
            dirty_pct < static_cast<float>(m_config.write_percent)) continue;

        // Candidate is evictable — select best by replacement policy
        uint64_t ts;
        if (m_config.replacement_policy == ReplacementPolicy::FIFO) {
            ts = block->get_alloc_time();
        } else {
            // LRU, PLRU, RANDOM all use last access time for ranking
            ts = block->get_last_access_time();
        }

        if (ts < best_timestamp) {
            best_timestamp = ts;
            best_victim = static_cast<int32_t>(w);
        }
    }

    // [NEW] If no evictable way was found, return -1 (= all_reserved)
    // For RANDOM policy with evictable candidates: pick random among valid ones
    if (best_victim >= 0 &&
        m_config.replacement_policy == ReplacementPolicy::RANDOM) {
        // Count evictable ways and pick randomly
        int count = 0;
        for (uint32_t w = 0; w < m_config.associativity; ++w) {
            uint32_t idx = get_line_index(set_index, w);
            const auto *block = m_lines[idx];
            if (block->is_reserved()) continue;
            if (block->is_modified() &&
                dirty_pct < static_cast<float>(m_config.write_percent)) continue;
            count++;
        }
        if (count > 0) {
            int r = rand() % count;
            for (uint32_t w = 0; w < m_config.associativity; ++w) {
                uint32_t idx = get_line_index(set_index, w);
                const auto *block = m_lines[idx];
                if (block->is_reserved()) continue;
                if (block->is_modified() &&
                    dirty_pct < static_cast<float>(m_config.write_percent)) continue;
                if (r == 0) { best_victim = static_cast<int32_t>(w); break; }
                r--;
            }
        }
    }

    return best_victim;
}
```

### 3.3 修改 2：`access()` 中检查 `all_reserved`

**文件**: `tag_array.cc` lines 256-270（miss 路径中的 victim 选择部分）

**当前代码**:
```cpp
// Find a victim or invalid way
int32_t victim_way = find_invalid_way(result.set_index);
if (victim_way < 0) {
    victim_way = static_cast<int32_t>(find_victim(result.set_index));
}

if (victim_way < 0) {
    m_res_fails++;
    result.status = AccessStatus::RESERVATION_FAIL;
    return result;
}
```

**修改为**（实际上这个逻辑已经存在，但 `find_victim` 返回 `uint32_t` 所以 `victim_way < 0` 的检查无意义）:
```cpp
// Find a victim or invalid way
int32_t victim_way = find_invalid_way(result.set_index);
if (victim_way < 0) {
    victim_way = find_victim(result.set_index);  // now returns int32_t
}

// [UPDATED] all_reserved: no evictable way in this set
if (victim_way < 0) {
    m_res_fails++;
    result.status = AccessStatus::RESERVATION_FAIL;
    return result;
}
```

逻辑不变，但 `find_victim()` 现在可能返回 -1，使 `victim_way < 0` 真正起作用。

### 3.4 修改 3（可选）：`m_dirty` 的写百分比配置

当前 `CacheConfig::write_percent` 默认值为 `0`（`cache_config.h` line 50）。

- `write_percent = 0` 意味着 `dirty_pct < 0` 永假 → **所有脏行都可驱逐**（等同于 GPGPU-Sim `m_wr_percent = 0` 的行为）
- GPGPU-Sim 典型配置: L1 `m_wr_percent = 0`（不保护脏行，L1 write-evict 本来就驱逐），L2 `m_wr_percent` 可能更高

默认值 `0` 是合理的保守选择。不需要修改。

---

## 四、改动范围总结

| 文件 | 改动 | 行数 |
|------|------|------|
| `tag_array.h` | `find_victim()` 返回类型 `uint32_t` → `int32_t` | 1 行 |
| `tag_array.cc` | `find_victim()` 完整重写（增加 RESERVED 跳过 + dirty 保护 + 失败返回） | ~45 行 |
| `tag_array.cc` | `access()` 中删去 `static_cast<int32_t>`（不再需要强转） | 1 行 |

**总计**: 2 个文件，~47 行改动。

---

## 五、行为变化分析

### 5.1 正常路径（无 all_reserved）

与当前行为**完全一致**。当 set 内存在非 RESERVED 的行时，victim 选择逻辑（LRU 最旧/FIFO 最旧）不变。

### 5.2 all_reserved 触发时

**当前行为**: 强制驱逐一个 RESERVED 行（LRU 最旧的那个）→ fill 数据到达时写入错误位置 → 数据错乱。

**新行为**: 返回 `RESERVATION_FAIL` → 调用方（`DataCache`）收到失败通知 → 记录 `MSHR_ENTRY_FAIL` 或 `LINE_ALLOC_FAIL` → 请求被拒绝，下周期重试或 MSHR 合并。

这与 GPGPU-Sim 的 `all_reserved → RESERVATION_FAIL` 行为一致。

### 5.3 dirty_percentage 保护触发时

**当前行为**: MODIFIED 行与 VALID 行平等竞争，最旧的行被驱逐（不考虑 writeback 成本）。

**新行为**: 当脏行比例低于 `write_percent` 时，脏行被保护（不被驱逐）。这可能导致：
- 更少的 writeback 流量（干净行替换不产生写回）
- 在极端情况下，脏行过多且 `write_percent` 设为 0 时，行为与当前完全一致

### 5.4 对现有测试的影响

- 现有测试使用的默认 `write_percent = 0` → dirty 保护永不激活 → 行为不变
- `tag_array_lru_replacement` 测试中，set 内有 2 way，不会触发 all_reserved（way 0 和 way 1 交替被驱逐和分配，不会同时 RESERVED）
- 所有现有测试应继续通过

---

## 六、与 GPGPU-Sim 的残余差异

| 差异点 | 说明 | 是否需要对齐 |
|--------|------|------------|
| `probe()` vs `access()` 分离 | GPGPU-Sim 在 probe() 中做 victim 选择；openCache 在 access() 中做。语义等价，不需改 | 否 |
| `valid_timestamp` 计算 | GPGPU-Sim 在 probe() 中内联比较；openCache 用 lru_order/fifo_next 管理。等价 | 否 |
| write_percent 默认值 | GPGPU-Sim 默认 0；openCache 默认 0。一致 | 否 |
| `all_reserved` 仅在 ON_MISS 策略生效 | GPGPU-Sim 有 `assert(m_alloc_policy == ON_MISS)`。openCache 应在 ON_FILL 时跳过 all_reserved 检查（因为 ON_FILL 在 fill() 中分配，不在 access() 中） | 是（见下方） |

### 6.1 ON_FILL 的 all_reserved 处理

GPGPU-Sim 只在 `ON_MISS` 时做 `all_reserved` 检查（`gpu-cache.cc:320` 的 `assert(m_config.m_alloc_policy == ON_MISS)`）。因为在 ON_FILL 策略下，`access()` 不分配行——分配发生在 `fill()` 回调中。

openCache 应保持一致：`find_victim()` 中的 all_reserved 检查仅在 `access()` 的分配路径中触发。ON_FILL 路径（`TagArray::fill()` line 302）如果分配失败，由 `access()` 返回的 RESERVATION_FAIL 自然处理。

当前 openCache 在 `access()` line 259 的 miss 路径已经只在 `find_invalid_way()` 和 `find_victim()` 返回有效值时分配，不需要额外的 ON_MISS guard。但需要确保 `fill()` 中的 ON_FILL 分配路径（line 302-312）也能正确处理 `access()` 返回 RESERVATION_FAIL 的情况。

---

## 七、实施建议

1. **Phase 1**: 修改 `find_victim()` 签名和实现（3.2 节）
2. **Phase 2**: 验证 `access()` 中的 `victim_way < 0` 检查生效（3.3 节）
3. **Phase 3**: 编译 + 运行现有 26 个测试确认无回归
4. **Phase 4**: 添加针对性的测试：构造 all_reserved 场景（4-way set 全部 RESERVED），验证返回 RESERVATION_FAIL
