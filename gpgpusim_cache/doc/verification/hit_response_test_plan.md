# Hit Response 延迟返回测试方案

## 测试目标

验证 data/read-only cache 在启用 hit response 延迟模式后，`access()` 的 hit 判定与数据返回解耦；上层必须通过 `access_ready()` 和 `next_access()` 获取 hit 完成请求。

## 测试原则

1. 新模式是交付默认模式：默认配置下 hit 不能在 `access()` 同周期通过 `next_access()` 返回。
2. 旧模式兼容性必须测试：仅在显式 `set_defer_hit_response(false)` 后历史同步 hit 行为保持不变。
3. 延迟必须与 `data_port_width` 和 `data_size` 相关。
4. hit ready 与 miss ready 的同周期顺序必须测试并固化。
5. read-only、data read hit、data write hit 都必须覆盖。
6. texture cache 不改行为，但要保留对齐测试，证明现有 texture 模型仍通过。
7. 每个 accepted request 必须 exactly-once 返回：不能丢、不能重复、不能提前。
8. pending response 对应 cache line 必须被 pin，`next_access()` 前不能被 replacement 选为 victim。
9. 白盒用例收尾必须能通过 final guard：每个直接创建的 cache/tag/MSHR 对象都要自动检查内部 queue/FIFO/MSHR/pending ref 清空，line 或 texture data block 回到初始状态。

## Implemented Feature

| Feature ID | Feature | 验证重点 |
|------------|---------|----------|
| F-HITLAT-001 | hit response 配置开关 | 默认新模式；显式关闭后保留旧 hit 兼容行为 |
| F-HITLAT-002 | read-only hit 延迟返回 | read-only hit 进入 ready queue，按 data port latency 返回 |
| F-HITLAT-003 | data read hit 延迟返回 | data/l1/l2 read hit 进入 ready queue，`HIT` 不等于数据立即完成 |
| F-HITLAT-004 | data write hit 延迟返回 | WB/WT/WE/local-global 写 hit 的完成点和 ready 行为 |
| F-HITLAT-005 | hit/miss ready 顺序 | hit response 与 miss fill 同周期 ready 的顺序规则 |
| F-HITLAT-006 | 统计和端口一致性 | data port busy、hit/miss stats、ready 返回数量一致 |
| F-HITLAT-007 | texture cache 兼容 | texture hit-reserved/result FIFO 行为不回退 |
| F-HITLAT-008 | hit response backpressure | hit response queue 满时的返回状态和 fail reason |
| F-HITLAT-009 | DataStore 可见性 | 本阶段不改变 payload 读取，只验证 timing token ready |
| F-HITLAT-010 | line refcount/pin | pending hit/miss response 未读走前 line 不可被 replacement 替换 |
| F-HITLAT-011 | MSHR merge refcount | merged request 按 accepted mf 数量 pin/unpin |
| F-HITLAT-012 | sector line-level pin | sector cache 任一 sector pending 时整条 line 不可被替换 |
| F-HITLAT-013 | write-evict pinned invalid | write-evict hit 立即 invalid 但仍 pinned 时不能作为 victim |
| F-HITLAT-014 | bounded hit response queue | hit response queue 有限容量和背压 |
| F-HITLAT-015 | DataStore snapshot timing | hit accepted 时快照语义与延迟 token 返回不混淆 |
| F-FINAL-001 | baseline/tag/MSHR final guard | baseline/read-only/data cache、tag_array、mshr_table 收尾状态自动检查并可回到初始态 |
| F-FINAL-002 | texture cache final guard | texture FIFO/ROB/extra fields/tag/data block 收尾状态自动检查并可回到初始态 |
| F-WATERMARK-001 | baseline queue/refcount watermark | baseline queue、pending response index、line refcount 最大水位可统计、可断言 |
| F-WATERMARK-002 | MSHR watermark | MSHR entry、merge、ready 最大水位可统计、可断言 |
| F-WATERMARK-003 | texture FIFO watermark | texture FIFO/ROB/result FIFO/extra fields 最大水位可统计、可断言 |

## Implemented Testcase

| Testcase ID | Feature | 类型 | 状态 | 期望 |
|-------------|---------|------|------|------|
| TC-HITLAT-001 | F-HITLAT-001 | unit | Implemented | 默认新模式开启；显式 `set_defer_hit_response(false)` 时旧 hit 行为保持兼容 |
| TC-HITLAT-002 | F-HITLAT-002 | unit | Implemented | read-only cache hit 返回 `HIT`，同周期 `access_ready()==false`，延迟后 `next_access()` 返回原 mf |
| TC-HITLAT-003 | F-HITLAT-003 | unit | Implemented | l1/data read hit 返回 `HIT`，按 `ceil(data_size/data_port_width)` cycle 后 ready |
| TC-HITLAT-004 | F-HITLAT-004 | unit | Implemented | write-back hit 延迟返回，并保持 dirty/tag 更新正确 |
| TC-HITLAT-005 | F-HITLAT-004 | unit | Implemented | write-through/write-evict hit 发出事件后，按定义进入 hit response ready |
| TC-HITLAT-006 | F-HITLAT-005 | unit | Implemented | hit response 和 miss fill 同周期完成时，`next_access()` 顺序与策略文档一致 |
| TC-HITLAT-007 | F-HITLAT-006 | unit | Implemented | data port busy cycles、hit stats、ready 返回数量一致，不重复计数 |
| TC-HITLAT-008 | F-HITLAT-007 | regression | Implemented | texture cache 现有 `HIT_RESERVED -> result FIFO -> next_access()` 测试继续通过 |
| TC-HITLAT-009 | F-HITLAT-003, F-HITLAT-005 | sequence | Implemented | 同一 line `MISS -> fill -> HIT accepted -> delayed ready -> HIT` 序列完整 |
| TC-HITLAT-010 | F-HITLAT-002, F-HITLAT-003, F-HITLAT-006 | property | Implemented | 多 seed hit/miss trace 在新模式下 accepted、ready、stats 一致且 exactly-once 返回 |
| TC-HITLAT-011 | F-HITLAT-008 | unit | Implemented | hit response queue 容量受限时返回 `RESERVATION_FAIL`，fail reason 与策略文档一致 |
| TC-HITLAT-012 | F-HITLAT-009 | unit | Implemented | DataStore 双模型场景继续通过，证明本阶段只改变 timing token 完成时机 |
| TC-HITLAT-013 | F-HITLAT-010 | unit | Implemented | hit pending 且 refcount 非 0 时，同 set conflict miss 不能 evict 该 line |
| TC-HITLAT-014 | F-HITLAT-010 | unit | Implemented | `next_access()` 读走 hit response 后 refcount 归零，后续 conflict miss 可以 evict |
| TC-HITLAT-015 | F-HITLAT-010 | unit | Implemented | same line 多个 hit pending 时 refcount 按数量增加，并逐个 `next_access()` 递减 |
| TC-HITLAT-016 | F-HITLAT-011 | unit | Implemented | MSHR merge 多个 miss ready 时，每个 accepted mf 都 pin，逐个返回后 unpin |
| TC-HITLAT-017 | F-HITLAT-012 | unit | Implemented | sector cache 中任一 sector pending response 时，整条 line 不可被 replacement 选为 victim |
| TC-HITLAT-018 | F-HITLAT-013 | unit | Implemented | write-evict hit 立即 invalid 但仍 pinned 时，同 set miss 不能复用该 invalid line |
| TC-HITLAT-019 | F-HITLAT-014 | unit | Implemented | hit response queue 达容量上限后返回 `RESERVATION_FAIL`，drain 后恢复接收 |
| TC-HITLAT-020 | F-HITLAT-015 | unit | Implemented | DataStore hit accepted 时快照语义被文档和场景测试固定，不误当成 coherence 行为 |
| TC-FINAL-001 | F-FINAL-001 | unit | Implemented | 每个白盒用例中创建的 baseline cache 派生对象、tag_array、mshr_table 均由 final guard 收尾检查 |
| TC-FINAL-002 | F-FINAL-002 | unit | Implemented | 每个白盒用例中创建的 texture cache 均由 final guard 收尾检查 |
| TC-WATERMARK-001 | F-WATERMARK-001 | unit | Implemented | baseline `queue_watermarks()` 断言 hit/ready response queue、pending response index、line refcount、外部 mem queue 最大水位 |
| TC-WATERMARK-002 | F-WATERMARK-002 | unit | Implemented | MSHR `watermarks()` 断言 entry、merge、ready 最大水位 |
| TC-WATERMARK-003 | F-WATERMARK-003 | unit | Implemented | texture `queue_watermarks()` 断言 fragment/request/result FIFO、ROB、extra fields、外部 mem queue 最大水位 |

## 关键测试场景

### read-only hit 延迟

1. 构造 read-only cache，开启 hit 延迟模式。
2. 第一次读 miss，fill 后通过 `next_access()` 返回。
3. 第二次读同 line，`access()` 返回 `HIT`。
4. 立刻检查 `access_ready()==false`。
5. 推进 `cycle()` 到指定延迟。
6. `access_ready()==true`，`next_access()` 返回第二次 hit 的 mf。

### data read hit 延迟

1. 构造 data/l1 cache，开启 hit 延迟模式，设置窄 `data_port_width`。
2. warm line。
3. 发起 read hit。
4. 断言 data port busy 和 `access_ready()` 时序。
5. 延迟结束后取回 hit mf。

### write hit 延迟

1. write-back hit：断言 dirty 状态已更新，但完成 token 延迟返回。
2. write-through hit：断言 `WRITE_REQUEST_SENT` 事件产生，hit response 仍延迟返回。
3. write-evict hit：断言 invalidate 已发生，后续读为 miss，write hit token 延迟返回。

### hit/miss 同周期 ready 顺序

1. 构造一个 pending miss，控制 fill 在某 cycle 到达。
2. 同时构造一个 hit response 在同 cycle ready。
3. 按策略文档定义顺序调用 `next_access()`。
4. 第二次 `next_access()` 返回另一个 ready 请求。
5. 再次检查 `access_ready()==false`。

### 统计一致性

1. 命中统计仍计为 `HIT`。
2. 不因延迟返回把 hit 误计为 `HIT_RESERVED`。
3. ready 返回数量等于 accepted hit 数加 completed miss 数。
4. data port busy cycles 不因 hit response queue 和旧 `use_data_port()` 双重计数。

### exactly-once property

1. 固定 seed 生成 hit/miss 混合 trace。
2. 记录所有 `access()` 返回非 `RESERVATION_FAIL` 的 accepted mf。
3. 周期推进直到所有 pending 请求完成。
4. 每个 accepted mf 必须恰好通过 `next_access()` 返回一次。
5. 返回集合不得包含未 accepted mf。

### Backpressure

如果实现阶段引入 hit response queue 容量，必须覆盖：

1. queue 未满时 hit 被接收。
2. queue 满时 hit 返回 `RESERVATION_FAIL`。
3. fail stats 与策略文档定义一致。
4. queue drain 后后续 hit 可再次接收。

第一阶段实现使用有限 hit response queue，`TC-HITLAT-011` 和 `TC-HITLAT-019` 已覆盖 queue full fail reason 以及 drain 后恢复接收。

### Final Check

1. baseline/read-only/data cache 提供 `queues_empty()` 和 `final_state_clean()`，覆盖 miss queue、MSHR、hit response queue、ready response queue、pending response index、extra fields、tag pending lines 和 line pin 状态。
2. texture cache 提供 `queues_empty()` 和 `final_state_clean()`，覆盖 fragment FIFO、request FIFO、ROB、result FIFO、extra fields、tag line 和 texture data block。
3. `tag_array` 提供 `final_state_clean()`，`mshr_table` 提供 `empty()`，覆盖裸白盒对象的收尾状态。
4. 白盒测试通过 RAII guard 绑定每个直接创建的对象，用例退出时自动 drain 到内部队列空，在调用 `invalidate()` 之前先断言 `no_pending_accesses()==true` 或 `empty()==true`。
5. invalidate 只作为 final check 通过后的测试 cleanup；cleanup 后再断言 `final_state_clean()==true`，确认 line/data block 回初始态。
6. 故障注入类用例如果刻意制造下游背压，必须在断言完成后恢复可收敛条件，让 guard 通过正常路径完成收尾。

### Refcount / pin

1. hit 被接收并进入 hit response queue 后，对应 line refcount 加一。
2. `next_access()` 返回 hit mf 后，对应 line refcount 减一。
3. refcount 不为 0 时，replacement candidate 选择必须跳过该 line。
4. 如果同 set 所有 line 都 reserved 或 pinned，新的 miss 返回 `RESERVATION_FAIL`。
5. MSHR merge 场景按 accepted mf 个数增加 refcount，而不是按 line 只加一次。
6. sector cache 第一阶段使用 line-level pin，任一 sector pending 会保护整条 line。

### Watermark / coverage

1. `baseline_cache::queue_watermarks()` 上报 miss queue、extra fields、hit response queue、ready response queue、pending response index、MSHR 和 line refcount 最大水位。
2. `mshr_table::watermarks()` 上报 MSHR entry、merge 深度、ready response 队列最大水位。
3. `tex_cache::queue_watermarks()` 上报 fragment FIFO、request FIFO、ROB、result FIFO、extra fields 和 line refcount 最大水位。
4. `simple_mem_interface::max_queue_occupancy` 上报测试侧外部 memory queue 最大水位。
5. 白盒 testcase 必须对相关 watermark 做显式 `CHECK_EQ` 断言；coverage 明细必须能看到这些统计接口被执行。

## 回归要求

实现阶段必须通过：

```bash
./run.sh
./coverage.sh
```

## 实现结果

2026-06-02 已完成第一阶段实现，`TC-HITLAT-001..020` 均反标到 `test/test_cache_whitebox.cc` 的 `hitlat_*` 用例。

2026-06-02 交付模式调整为默认新模式，旧模式仅作为显式兼容开关保留；所有默认回归用例按新模式运行。新增 `TC-FINAL-001/002` final check 专项用例后，`./run.sh` 通过。

2026-06-02 final check 升级为白盒用例对象级 RAII guard：`test_cache_whitebox.cc` 中每个直接创建的 baseline/read-only/data cache、texture cache、tag_array、mshr_table 都在用例退出时自动收尾检查，并纳入默认回归。

2026-06-02 新增 refcount/queue/FIFO 正式 max watermark 统计与断言：`cache_block_t`、`mshr_table`、`baseline_cache`、`tex_cache`、`simple_mem_interface` 均提供可观测最大水位；白盒测试新增 `refcount_block_accessors_track_current_and_max`，并在 hit queue、MSHR merge、sector pin、write-evict pin、texture FIFO/ROB/result FIFO 场景中断言最大水位。`./run.sh` 通过：unit `13/13`、scenario `86/86 checks`、whitebox `41/41`、death `16/16`。覆盖率通过 `./coverage.sh coverage-refcount-watermark` 刷新：Total Region `57.63%`、Function `75.84%`、Line `71.80%`、Branch `61.41%`。

交付时同步更新：

1. `doc/requirements/requirements.md`
2. `doc/verification/feature_matrix.md`
3. `doc/verification/testcase_matrix.md`
4. `doc/verification/coverage_baseline.md`
5. `doc/遗留问题.md`

## 多角色评审

| 角色 | 结论 | 意见 |
|------|------|------|
| 项目经理 | 通过 | 文档阶段只标记 Planned；实现完成后必须刷新为 Implemented |
| 设计 | 通过 | 测试必须约束接口语义，尤其是 `HIT` 与数据 ready 的区别 |
| 验证 | 通过 | 必须覆盖默认新模式和显式旧模式，防止交付语义或兼容开关回退 |
| 测试专家 | 通过 | 必须有精确 cycle 级断言、ready 顺序断言和统计一致性断言 |
| 独立评审代理 | 通过 | 补充 backpressure、统计口径、DataStore 读取时机、exactly-once property 和破坏性变更风险 |

## 评审限制说明

当前会话已达到可新建子 agent 上限，无法新建独立评审 agent。本文档依据项目流程完成四角色评审表；后续实现前建议补一次独立 agent 复审。
