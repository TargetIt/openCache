# Hit Response 延迟返回测试方案

## 测试目标

验证 data/read-only cache 在启用 hit response 延迟模式后，`access()` 的 hit 判定与数据返回解耦；上层必须通过 `access_ready()` 和 `next_access()` 获取 hit 完成请求。

## 测试原则

1. 旧模式兼容性必须测试：默认配置下历史行为保持不变。
2. 新模式语义必须测试：hit 不能在 `access()` 同周期通过 `next_access()` 返回。
3. 延迟必须与 `data_port_width` 和 `data_size` 相关。
4. hit ready 与 miss ready 的同周期顺序必须测试并固化。
5. read-only、data read hit、data write hit 都必须覆盖。
6. texture cache 不改行为，但要保留对齐测试，证明现有 texture 模型仍通过。
7. 每个 accepted request 必须 exactly-once 返回：不能丢、不能重复、不能提前。

## Planned Feature

| Feature ID | Feature | 验证重点 |
|------------|---------|----------|
| F-HITLAT-001 | hit response 配置开关 | 默认旧行为；开启后 hit 延迟返回 |
| F-HITLAT-002 | read-only hit 延迟返回 | read-only hit 进入 ready queue，按 data port latency 返回 |
| F-HITLAT-003 | data read hit 延迟返回 | data/l1/l2 read hit 进入 ready queue，`HIT` 不等于数据立即完成 |
| F-HITLAT-004 | data write hit 延迟返回 | WB/WT/WE/local-global 写 hit 的完成点和 ready 行为 |
| F-HITLAT-005 | hit/miss ready 顺序 | hit response 与 miss fill 同周期 ready 的顺序规则 |
| F-HITLAT-006 | 统计和端口一致性 | data port busy、hit/miss stats、ready 返回数量一致 |
| F-HITLAT-007 | texture cache 兼容 | texture hit-reserved/result FIFO 行为不回退 |
| F-HITLAT-008 | hit response backpressure | hit response queue 满时的返回状态和 fail reason |
| F-HITLAT-009 | DataStore 可见性 | 本阶段不改变 payload 读取，只验证 timing token ready |

## Planned Testcase

| Testcase ID | Feature | 类型 | 状态 | 期望 |
|-------------|---------|------|------|------|
| TC-HITLAT-001 | F-HITLAT-001 | unit | Planned | 默认不开启 hit 延迟时，既有 hit 行为和当前回归保持一致 |
| TC-HITLAT-002 | F-HITLAT-002 | unit | Planned | read-only cache hit 返回 `HIT`，同周期 `access_ready()==false`，延迟后 `next_access()` 返回原 mf |
| TC-HITLAT-003 | F-HITLAT-003 | unit | Planned | l1/data read hit 返回 `HIT`，按 `ceil(data_size/data_port_width)` cycle 后 ready |
| TC-HITLAT-004 | F-HITLAT-004 | unit | Planned | write-back hit 延迟返回，并保持 dirty/tag 更新正确 |
| TC-HITLAT-005 | F-HITLAT-004 | unit | Planned | write-through/write-evict hit 发出事件后，按定义进入 hit response ready |
| TC-HITLAT-006 | F-HITLAT-005 | unit | Planned | hit response 和 miss fill 同周期完成时，`next_access()` 顺序与策略文档一致 |
| TC-HITLAT-007 | F-HITLAT-006 | unit | Planned | data port busy cycles、hit stats、ready 返回数量一致，不重复计数 |
| TC-HITLAT-008 | F-HITLAT-007 | regression | Planned | texture cache 现有 `HIT_RESERVED -> result FIFO -> next_access()` 测试继续通过 |
| TC-HITLAT-009 | F-HITLAT-003, F-HITLAT-005 | sequence | Planned | 同一 line `MISS -> fill -> HIT accepted -> delayed ready -> HIT` 序列完整 |
| TC-HITLAT-010 | F-HITLAT-002, F-HITLAT-003, F-HITLAT-006 | property | Planned | 多 seed hit/miss trace 在新模式下 accepted、ready、stats 一致且 exactly-once 返回 |
| TC-HITLAT-011 | F-HITLAT-008 | unit | Planned | hit response queue 容量受限时返回 `RESERVATION_FAIL`，fail reason 与策略文档一致 |
| TC-HITLAT-012 | F-HITLAT-009 | scenario | Planned | DataStore 双模型场景继续通过，证明本阶段只改变 timing token 完成时机 |

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

如果第一阶段使用无界 hit response queue，则 `TC-HITLAT-011` 保持 Planned，并在实现说明中写明原因。

## 回归要求

实现阶段必须通过：

```bash
./run.sh
./coverage.sh
```

交付时同步更新：

1. `doc/requirements/requirements.md`
2. `doc/verification/feature_matrix.md`
3. `doc/verification/testcase_matrix.md`
4. `doc/verification/coverage_baseline.md`
5. `doc/遗留问题.md`

## 多角色评审

| 角色 | 结论 | 意见 |
|------|------|------|
| 项目经理 | 通过 | 当前文档阶段只标记 Planned，不得声称实现完成 |
| 设计 | 通过 | 测试必须约束接口语义，尤其是 `HIT` 与数据 ready 的区别 |
| 验证 | 通过 | 必须覆盖默认旧模式和新模式，防止兼容性回退 |
| 测试专家 | 通过 | 必须有精确 cycle 级断言、ready 顺序断言和统计一致性断言 |
| 独立评审代理 | 通过 | 补充 backpressure、统计口径、DataStore 读取时机、exactly-once property 和破坏性变更风险 |

## 评审限制说明

当前会话已达到可新建子 agent 上限，无法新建独立评审 agent。本文档依据项目流程完成四角色评审表；后续实现前建议补一次独立 agent 复审。
