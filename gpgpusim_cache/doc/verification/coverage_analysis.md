# GPGPU-Sim Cache Reference — 覆盖率深度分析

执行日期：2026-06-03 | 基线：71.16% lines / 74.67% functions / 61.79% branches

---

## 1. 当前覆盖率全景

| 指标 | gpu_cache_ref.cc | gpu_cache_ref.h | Total |
|------|:---:|:---:|:---:|
| Regions | 55.64% | 63.13% | 57.78% |
| Functions | 75.00% | 74.46% | 74.67% |
| Lines | 69.36% | 74.57% | 71.16% |
| Branches | 58.54% | 73.44% | 61.79% |

Source: `gpu_cache_ref.cc` (2384 lines) + `gpu_cache_ref.h` (2102 lines) = 4486 total.

---

## 2. 未覆盖代码根因分类

### 类别 A：HIT_RESERVED 写入路径（~120 lines，最关键缺口）

**症状**：三个写 miss 处理函数中，命中 HIT_RESERVED（tag 已分配但 fill 未完成）的代码路径执行次数为 0。

- `wr_miss_wa_fetch_on_write()` — 1748-1762（full-write HIT_RESERVED）、1790-1807（partial-write HIT_RESERVED）
- `wr_miss_wa_lazy_fetch_on_read()` — 1875-1886（HIT_RESERVED in LAZY_FETCH）
- `wr_miss_wa_naive()` — 1642-1660（HIT_RESERVED in naive write-allocate）

**根因**：所有测试都是顺序操作的——发请求 → 等待 fill → 再发下一个。从未在 fill 未完成时向同一地址发第二个请求。

**连带影响**：sector cache 的 `set_ignore_on_fill()`、`set_readable_on_fill()`、`set_byte_mask_on_fill()` 等 flag 设置路径全部未被覆盖。

### 类别 B：脏行驱逐写回（~60 lines）

**症状**：写 miss 触发 dirty eviction → writeback 的代码路径几乎全部未执行。

- `wr_miss_wa_fetch_on_write()` dirty evict 路径（1748-1762、1825-1837）
- `wr_miss_wa_lazy_fetch_on_read()` dirty evict（1893-1905）
- `wr_miss_wa_naive()` dirty evict（1692-1706）

**根因**：测试使用空 cache 或已 clean 的 L1/L2 cache。从未先填满 set 再触发驱逐。

### 类别 C：MSHR_RW_PENDING（~7 lines）

**症状**：读写冲突保护的 MSHR_RW_PENDING 路径从未自然触发。

**根因**：需要三步序列 `write → read → write` 同一地址，且每一步间隔不能有 fill 完成。

### 类别 D：队列背压（~30 lines）

**症状**：`miss_queue_full()` 在写命中/写 miss 处理中返回 true 的路径从未触发。

- `wr_hit_wt()` miss_queue_full（1568-1572）
- `wr_hit_we()` miss_queue_full（1597-1601）
- `wr_miss_no_wa()` miss_queue_full（1915-1919）
- `rd_miss_base()` miss_queue_full（1959）

**根因**：测试使用宽松的 miss_queue_size，从未打满。

### 类别 E：统计聚合操作符（~217 lines，低价值）

**症状**：`cache_stats::operator+`、`operator+=`、`operator()`（mutable/const）完全未调用。

**根因**：这些是 GPGPU-Sim 多 cache 聚合统计用的，standalone 模式下是孤儿代码。Operator+ 和 operator+= 在 standalone 环境下不存在调用方。

### 类别 F：Debug / 打印函数（~120 lines，低价值）

**症状**：`print()`、`display_state()`、`display()`、`print_stats()`、`print_fail_stats()` 从未被调用。

**根因**：测试框架使用 CHECK_TRUE/CHECK_EQ 宏直接断言内部状态，未走 debug 输出路径。

### 类别 G：GPGPU-Sim 集成桩（~53 lines，不可覆盖）

**症状**：以下函数需要完整 GPGPU-Sim 基础设施：
- `l1d_cache_config::set_bank()`（需要 bank interleaving 配置）
- `l2_cache_config::init/set_index()`（需要 address_mapping 对象）
- `tag_array::add_pending_line/remove_pending_line()`（需要 memory_coalescer）
- `tag_array::windowed_miss_rate/new_window()`（需要 AerialVision 可视化器）
- `inc_aggregated_stats*()`（有意置为 no-op 桩，共 17 lines）

**结论**：standalone 模式下不可达。不需要强行覆盖。

### 类别 H：函数零调用（29 个 in .cc，47 个 in .h）

主要包括：
- 未被测试调用的公共/私有方法（event check helpers、config accessors）
- Debug dump 函数
- 仅在特定 cache 类型（l1_cache、l2_cache、tex_cache）中存在的虚函数，但测试未创建该类型的 cache 实例

---

## 3. 覆盖率提升空间估算

| 类别 | 行数 | 可提升？ | 优先级 | 预计提升 |
|------|------|:---:|:---:|:---:|
| A: HIT_RESERVED 写路径 | ~120 | 是 | P0 | +4.5% |
| B: 脏行驱逐写回 | ~60 | 是 | P0 | +2.2% |
| C: MSHR_RW_PENDING | ~7 | 是 | P0 | +0.3% |
| D: 队列背压 | ~30 | 是 | P0 | +1.1% |
| E: 统计聚合 | ~217 | 是 | P2 | +8.1% |
| F: Debug 打印 | ~120 | 是 | P2 | +4.5% |
| G: 集成桩 | ~53 | 否 | — | — |
| H: 零调用函数 | ~150 | 部分 | P1 | +5.6% |

**可达到上限**：~(768 - 53) = 715 lines 可覆盖，当前已覆盖 1895/2663 lines。若 P0+P1 完成，预计可达到 **82-85% 行覆盖率**。若 P2 也完成，可达到 **90%+**。

---

## 4. P0 测试增补清单（核心路径，预计 +8%）

| # | 测试名 | 场景 |
|---|--------|------|
| 1 | `wr_miss_fetch_on_write_hit_reserved` | Write miss → 不等待 fill → 再发同地址写 → HIT_RESERVED |
| 2 | `wr_miss_lazy_fetch_on_read_hit_reserved` | Read miss → 未 fill → 同地址写 → HIT_RESERVED in LAZY_FETCH |
| 3 | `wr_miss_naive_dirty_eviction` | 填满 set → dirty 行 → 换出 writeback |
| 4 | `wr_miss_fetch_on_write_dirty_eviction` | Full/partial write → 脏驱逐写回 |
| 5 | `write_hit_miss_queue_backpressure` | WRITE_THROUGH + miss_queue=1 → backpressure |
| 6 | `read_miss_queue_backpressure` | 多地址 read + miss_queue=1 → RESERVATION_FAIL |
| 7 | `mshr_rw_pending_trigger` | Write → read → write 同一地址，触发 MSHR_RW_PENDING |
| 8 | `atomic_read_hit` | 创建 isatomic() mem_fetch，走 atomic 路径 |

---

## 5. P1 测试增补清单（边界路径，预计 +5%）

| # | 测试名 | 场景 |
|---|--------|------|
| 9 | `tag_fill_on_fill_sector_miss` | Sector cache + ON_FILL → SECTOR_MISS fill |
| 10 | `tag_fill_on_fill_reservation_fail` | Tag array full → RESERVATION_FAIL fill |
| 11 | `sector_flags_integration` | ignore_on_fill / readable_on_fill 联动 |
| 12 | `l1_cache_write_evict` | l1_cache 专有 write-evict 路径 |
| 13 | `l2_cache_writeback_policy` | l2_cache 专有 writeback 处理 |

---

## 6. P2 测试增补清单（兼容/打印，预计 +12%）

| # | 测试名 | 场景 |
|---|--------|------|
| 14 | `print_methods_no_crash_smoke` | 所有 print/display 方法不崩溃 |
| 15 | `stat_merge_operators` | operator+ / operator+= 逻辑正确 |
| 16 | `event_check_helpers` | was_write_sent / was_read_sent / was_writeallocate_sent |
| 17 | `config_edge_cases` | CUSTOM set index / 边界配置组合 |

---

## 7. 不可达代码白名单

以下代码 standalone 模式下不可达，不应计入覆盖率目标：

1. `l1d_cache_config::set_bank()` — 需要 GPGPU-Sim bank 配置
2. `l2_cache_config::init/set_index()` — 需要 `address_mapping` 对象
3. `tag_array::add_pending_line/remove_pending_line()` — 需要 `memory_coalescer`
4. `tag_array::windowed_miss_rate/new_window()` — 需要 AerialVision
5. `inc_aggregated_stats/inc_aggregated_fail_stats/inc_aggregated_stats_pw()` — 有意的 no-op 桩
6. `hash_function()` default abort — 不可能触发（config parsing 已校验）
7. `tag_array::access()` default abort — 不可能触发（probe 返回值已限定）

共计 ~53 lines，扣除后理论覆盖率上限 ~98%。
