# Feature Test Cases — GPGPU-Sim Cache Reference

编辑日期：2026-06-03 | 范围：atomic / backpressure / dirty-evict / HIT_RESERVED / ON_FILL / MSHR_RW_PENDING / sector-flag / debug-print / stat-merge / event-helper

---

## 概述

本文档记录全部待增补 Feature Test Cases，按 P0/P1/P2 优先级排列。每个测试包含：场景、先决条件、操作序列、断言目标。

---

## P0：核心路径（预计 +8% 行覆盖率）

### FTC-01: `wr_miss_fetch_on_write_hit_reserved`

**场景**：Write miss 后不等待 fill，对同一地址再发一个写请求，触发 HIT_RESERVED 路径

**先决条件**：
- data_cache，配置 `write_alloc_policy=FETCH_ON_WRITE`，`write_policy=WRITE_BACK`
- 1-way set，line_sz=64

**操作序列**：
1. 发 write miss 到地址 A（starts fill，line 状态 RESERVED）
2. 不调用 `cycle()`，直接再发一个 write miss 到地址 A
3. probe() 应返回 HIT_RESERVED（tag matched, line is RESERVED）
4. HIT_RESERVED 处理：设置 `set_ignore_on_fill(true)`，`set_readable_on_fill(true)`

**断言**：
- 第二次 access 返回 HIT_RESERVED 或通过 MSHR merge
- `ignore_on_fill` 和 `readable_on_fill` 标志被设置

**覆盖目标**：`wr_miss_wa_fetch_on_write.cc:1748-1762`

---

### FTC-02: `wr_miss_lazy_fetch_on_read_hit_reserved`

**场景**：Read miss → 未 fill → 同地址 write miss，触发 LAZY_FETCH 的 HIT_RESERVED 路径

**先决条件**：
- data_cache，`write_alloc_policy=LAZY_FETCH_ON_READ`
- 1-way set

**操作序列**：
1. 发 read miss 到地址 A（starts fill, line RESERVED）
2. 不等待 fill，发 write miss 到地址 A
3. probe 返回 HIT_RESERVED
4. LAZY_FETCH HIT_RESERVED 处理：`set_readable_on_fill(false)`，`set_ignore_on_fill(true)`

**断言**：
- 第二次 access 返回（通过 MSHR merge 或 HIT）
- `readable_on_fill` 被设为 false
- `ignore_on_fill` 被设为 true

**覆盖目标**：`wr_miss_wa_lazy_fetch_on_read.cc:1875-1886`

---

### FTC-03: `wr_miss_naive_dirty_eviction_writeback`

**场景**：填满 set → dirty 行 → 换出触发 writeback

**先决条件**：
- data_cache，`write_alloc_policy=WA_NAIVE`，`write_policy=WRITE_BACK`
- 1-way, 2 sets → 每个 set 只能存 1 行

**操作序列**：
1. Write miss set=0 地址 A → line A dirty
2. Write miss set=0 地址 B（与 A 同 set）→ evict dirty A → writeback

**断言**：
- 第二次 access 产生 writeback event
- evicted block 是脏的
- `was_writeback_sent()` 或等效检查通过

**覆盖目标**：`wr_miss_wa_naive.cc:1692-1706`

---

### FTC-04: `write_hit_miss_queue_backpressure`

**场景**：WRITE_THROUGH 策略 + miss_queue=1 + 连续 write hit → miss_queue 满 → RESERVATION_FAIL

**先决条件**：
- data_cache，`write_policy=WRITE_THROUGH`，`miss_queue_size=1`
- 预先 fill 一个 block 到 cache

**操作序列**：
1. 发 write hit（走 WRITE_THROUGH → push to miss_queue）
2. 再发 write hit → miss_queue 满 → RESERVATION_FAIL

**断言**：
- 第二次 access 返回 RESERVATION_FAIL
- fail reason = MISS_QUEUE_FULL

**覆盖目标**：`wr_hit_wt.cc:1568-1572`

---

### FTC-05: `read_miss_queue_backpressure`

**场景**：read miss + miss_queue=1 + 多地址 → RESERVATION_FAIL

**先决条件**：
- read_only_cache，`miss_queue_size=1`，`mshr_entries=1`
- memory interface 满（不接受新请求）

**操作序列**：
1. Read miss 地址 A → miss_queue 进入
2. cycle() → miss 被 push 到 mem（mem 拒绝）
3. Read miss 地址 B → miss_queue 已满 → RESERVATION_FAIL

**断言**：
- 第二步返回 RESERVATION_FAIL
- watermark 显示 miss_queue 峰值为 1

**覆盖目标**：`rd_miss_base.cc:1959`

---

### FTC-06: `mshr_rw_pending_trigger`

**场景**：Write → read → write 三步序列触发 MSHR_RW_PENDING

**先决条件**：
- data_cache，small MSHR（entry=4, merge=4）

**操作序列**：
1. Write miss 地址 A（MSHR 记录 write pending）
2. Read miss 地址 A（probe MSHR → 合并，设置 read-after-write pending）
3. Write miss 地址 A（probe MSHR → rw_pending 为 true → RESERVATION_FAIL）

**断言**：
- 第三步 access 返回 RESERVATION_FAIL
- fail reason = MSHR_RW_PENDING

**覆盖目标**：`wr_miss_wa_fetch_on_write.cc:1790-1796`

---

### FTC-07: `atomic_read_hit`

**场景**：创建 isatomic()=true 的 mem_fetch，走 atomic 路径

**先决条件**：
- read_only_cache 或 data_cache，预 fill block

**操作序列**：
1. 创建 isatomic()=true 的 mem_fetch（使用 mem_fetch ALU 参数设置）
2. 发送 read hit → rd_hit_base → isatomic() 分支 → 不算 refcount

**断言**：
- access 返回 HIT
- line refcount 不增加（atomic 不计入引用计数）

**覆盖目标**：`rd_hit_base.cc:1939`

---

### FTC-08: `wr_miss_fetch_on_write_full_write_dirty_eviction`

**场景**：Full-line write miss → set 满 → dirty eviction → writeback

**先决条件**：
- data_cache，`write_alloc_policy=FETCH_ON_WRITE`，1-way set
- 预先 fill dirty block

**操作序列**：
1. Write hit → block dirty
2. Write miss 到同 set 不同 block（full-line write，data_size=line_sz）
3. 驱逐 dirty block → writeback

**断言**：
- 产生 writeback event
- 新 block 为 MODIFIED 状态

**覆盖目标**：`wr_miss_wa_fetch_on_write.cc:1748-1762`

---

## P1：边界路径（预计 +5% 行覆盖率）

### FTC-09: `tag_fill_on_fill_sector_miss`

**场景**：Sector cache + ON_FILL 分配 → fill 触发 SECTOR_MISS

**先决条件**：
- sector cache，`alloc_policy=ON_FILL`，1-way, 2 sectors per line

**操作序列**：
1. 发送一次 miss（分配整行，但只 fill 部分 sector）
2. fill 到达 → tag_array::fill() → 识别为 SECTOR_MISS → 只 fill 对应 sector

**断言**：
- 该行状态为 VALID
- 未 fill 的 sector 状态仍为 INVALID
- `get_status(sector_mask)` 区分已 fill / 未 fill sector

**覆盖目标**：`tag_array::fill() .cc:454-490`

---

### FTC-10: `tag_fill_on_fill_reservation_fail`

**场景**：Tag array 满 + ON_FILL → fill 返回 RESERVATION_FAIL 早退出

**先决条件**：
- cache_config，`alloc_policy=ON_FILL`，1-way, 1 set（只有 1 行）
- 该行已被填满（VALID），且 set 没有 INVALID 或可驱逐行

**操作序列**：
1. Fill block A（占满唯一行）
2. 发送 fill block B（不同 block_addr）→ tag_array::fill() → find_victim 失败 → RESERVATION_FAIL

**断言**：
- fill 不修改任何行状态

**覆盖目标**：`tag_array::fill() .cc:443`

---

### FTC-11: `sector_flags_integration`

**场景**：组合 HIT_RESERVED 触发的 sector flag（ignore_on_fill + readable_on_fill + byte_mask_on_fill）

**先决条件**：
- sector cache，`write_alloc_policy=FETCH_ON_WRITE`
- 1-way, 4 sectors per line

**操作序列**：
1. Write miss → tag allocated (RESERVED)，ignore_on_fill=true，readable_on_fill=false
2. 不等待 fill → write miss 同 block → HIT_RESERVED → byte_mask_on_fill 设置
3. fill 到达 → sector_cache_block::fill() 读取这些 flags → 正确地仅填充/忽略对应 sector

**断言**：
- fill 后 sector flags 行为正确

**覆盖目标**：`sector_cache_block::fill() .h:405-409`，`set_ignore_on_fill/set_readable_on_fill/set_byte_mask_on_fill`

---

### FTC-12: `l1_cache_write_evict_path`

**场景**：l1_cache（Fermi write-evict）完整写入流程

**先决条件**：
- l1_cache（继承 data_cache，write_evict 策略）
- 默认 L1 配置（write-back + write-evict）

**操作序列**：
1. Write miss → L1 分配行（write-back）
2. Read miss → hit（L1 命中）
3. Write miss 到同一 set → evict 时 write-evict 处理

**断言**：
- L1 的 write-evict 行为不同于普通 data_cache

**覆盖目标**：`l1_cache::access()` 相关路径，`wr_hit_we`

---

### FTC-13: `l2_cache_dedicated_path`

**场景**：l2_cache（shared L2）独立测试

**先决条件**：
- l2_cache（继承 data_cache，默认 write-back + write-allocate）
- 配置为 L2_GPU_CACHE level

**操作序列**：
1. Read miss → MSHR → fill → ready
2. Write miss → write-allocate → fill → writeback on eviction

**断言**：
- l2_cache 构造和基本流程不崩溃
- L2 层级的统计数据正确累加

**覆盖目标**：`l2_cache` 虚函数表 + 构造路径

---

## P2：辅助路径（预计 +12% 行覆盖率）

### FTC-14: `print_methods_smoke_test`

**场景**：所有 print/display 方法不崩溃

**操作序列**：
1. 创建各种 cache 实例（read_only、data_cache、l1_cache、l2_cache、tex_cache）
2. 每个实例执行若干操作（access + cycle + fill）
3. 调用 print()、display_state()、print_stats()、print_fail_stats()
4. 创建 tag_array 实例，调用 print()
5. 创建 mshr_table 实例，填充后调用 display()

**断言**：所有调用不崩溃（无需验证输出内容）

**覆盖目标**：~120 lines（debug/print 函数集）

---

### FTC-15: `stat_merge_operators`

**场景**：cache_stats operator+ / operator+= 逻辑正确

**操作序列**：
1. 创建两个 cache_stats 对象：A.accumulate(HIT) × 10 + A.accumulate(MISS) × 5；B.accumulate(HIT) × 3
2. C = A + B → 验证 C.hits = 13, C.misses = 5
3. A += B → 验证 A.hits = 13

**断言**：operator+ 和 operator+= 合并结果正确

**覆盖目标**：~217 lines（统计聚合操作符）

---

### FTC-16: `event_check_helpers`

**场景**：was_write_sent / was_read_sent / was_writeallocate_sent 正确性

**操作序列**：
1. Write hit → 检查 was_write_sent()
2. Read miss → 检查 was_read_sent()
3. Write miss (write-allocate) → 检查 was_writeallocate_sent()

**断言**：三种 helper 返回预期布尔值

**覆盖目标**~21 lines（event check helpers）

---

### FTC-17: `config_edge_cases`

**场景**：CUSTOM set_index + 边界配置

**操作序列**：
1. 创建 CUSTOM set_index 的 cache_config → set_index 应返回 0
2. 创建 FuncCachePreferL1 / FuncCachePreferShared 配置
3. 验证配置处理不崩溃

**断言**：CUSTOM set_index 返回 0；prefer 配置不崩溃

**覆盖目标**：~15 lines（config edge cases）

---

## 实施跟踪

| FTC | 优先级 | 状态 | 预计行覆盖 | 实施日期 |
|-----|:---:|------|:---:|----------|
| FTC-01 | P0 | pending | +15 | — |
| FTC-02 | P0 | pending | +15 | — |
| FTC-03 | P0 | pending | +15 | — |
| FTC-04 | P0 | pending | +5 | — |
| FTC-05 | P0 | pending | +5 | — |
| FTC-06 | P0 | pending | +7 | — |
| FTC-07 | P0 | pending | +3 | — |
| FTC-08 | P0 | pending | +15 | — |
| FTC-09 | P1 | pending | +37 | — |
| FTC-10 | P1 | pending | +3 | — |
| FTC-11 | P1 | pending | +15 | — |
| FTC-12 | P1 | pending | +5 | — |
| FTC-13 | P1 | pending | +5 | — |
| FTC-14 | P2 | pending | +120 | — |
| FTC-15 | P2 | pending | +217 | — |
| FTC-16 | P2 | pending | +21 | — |
| FTC-17 | P2 | pending | +15 | — |
