# gpgpusim_cache Feature 分解与反标

## Feature 分层

| Feature ID | Feature | 角度 | 验证重点 | Testcase ID |
|------------|---------|------|----------|-------------|
| F-CFG-001 | 配置串基础解析 | 接口 | nset、line size、assoc、write policy、write allocate | TC-CFG-001 |
| F-CFG-002 | `none` 禁用配置 | 接口 | `disabled()` | TC-CFG-002 |
| F-CFG-003 | cache type | 接口/白盒 | NORMAL、SECTOR、sector line size 约束 | TC-CFG-003, TC-TAG-004 |
| F-CFG-004 | replacement policy | 白盒 | LRU/FIFO victim 差异 | TC-TAG-005 |
| F-CFG-005 | allocation policy | 白盒 | ON_MISS、ON_FILL、STREAMING | TC-CFG-004, TC-TAG-006 |
| F-CFG-006 | write policy | 白盒 | WB、WT、WE、LOCAL_WB_GLOBAL_WT | TC-WR-001, TC-WR-002, TC-WR-003, TC-WR-008 |
| F-CFG-007 | write allocate policy | 白盒 | N/W/F/L 四种策略 | TC-WR-004, TC-WR-005, TC-WR-007, TC-WR-009 |
| F-CFG-008 | MSHR type | 白盒 | ASSOC、SECTOR_ASSOC、TEX_FIFO、SECTOR_TEX_FIFO | TC-MSHR-001, TC-MSHR-006, TC-TEX-001, TC-TEX-004 |
| F-CFG-009 | set index function | 白盒 | LINEAR、XOR、IPOLY、FERMI、CUSTOM 范围、基本差异、边界/高位地址 golden 值 | TC-ADDR-001 |
| F-CFG-010 | illegal config/assert path | 异常 | parse error、sector line size、writeback/on-fill、lazy/on-fill、port width、Fermi nset | TC-DEATH-001 |
| F-CFG-011 | parameter matrix | 接口/白盒/组合 | 单参数典型值遍历和约束 pairwise 组合，覆盖 nset、assoc/way、line size、cache type、replacement、write policy、allocation、write allocate、MSHR/FIFO、data port、set-index | TC-CFG-011, TC-CFG-012 |
| F-ADDR-001 | block/tag/mshr 地址对齐 | 接口 | line、sector atom、tag 等式 | TC-ADDR-002 |
| F-ADDR-002 | 边界地址 | 接口 | 0、line 末尾、跨 line、较大地址 | TC-ADDR-003 |
| F-BLK-001 | line block 生命周期 | 白盒 | INVALID->RESERVED->VALID->MODIFIED->INVALID | TC-TAG-001, TC-TAG-002 |
| F-BLK-002 | sector block 生命周期 | 白盒 | partial sector valid、SECTOR_MISS | TC-TAG-004 |
| F-BLK-003 | dirty byte/sector mask | 白盒 | partial write、modified size、evicted mask | TC-WR-006 |
| F-BLK-004 | readable 标志 | 白盒 | partial write unreadable read 触发 miss，full-sector write read 命中 | TC-WR-007 |
| F-TAG-001 | probe miss/hit | 接口 | MISS、HIT | TC-TAG-001 |
| F-TAG-002 | hit reserved | 白盒 | pending fill 命中返回 HIT_RESERVED | TC-TAG-003 |
| F-TAG-003 | line allocation fail | 白盒 | all reserved -> RESERVATION_FAIL | TC-TAG-007 |
| F-TAG-004 | fill 行为 | 白盒 | fill 后有效、ON_FILL fill allocate | TC-TAG-002, TC-TAG-006 |
| F-TAG-005 | flush/invalidate | 接口 | flush 只 invalid modified 且保留 clean line；invalidate invalid all | TC-TAG-008 |
| F-TAG-006 | new_window/windowed_miss_rate | 统计 | window snapshot | TC-STATS-003 |
| F-SEQ-001 | same line hit/miss sequence | 白盒/序列 | cold miss -> ready hit -> consecutive hit；write-evict hit -> miss | TC-SEQ-001 |
| F-SEQ-002 | same set/full cache eviction sequence | 白盒/序列 | cache 填满、同 set conflict miss、victim miss、非冲突 set 保持 hit | TC-SEQ-002 |
| F-SEQ-003 | sector partial sequence | 白盒/序列 | sector partial valid、目标 sector 为 SECTOR_MISS、补齐后 hit、冲突后 miss | TC-SEQ-003 |
| F-SEQ-004 | MSHR/pending sequence | 白盒/序列 | same line miss merge、只发一个下游请求、ready FIFO 顺序、merge 上限 backpressure | TC-SEQ-004 |
| F-SEQ-005 | texture hit-reserved sequence | 白盒/序列 | texture miss fill 后 result FIFO pending；后续 hit 返回 HIT_RESERVED 并按 FIFO ready | TC-SEQ-005 |
| F-MSHR-001 | MSHR add/probe/ready | 接口 | add、mark_ready、next_access | TC-MSHR-001 |
| F-MSHR-002 | MSHR merge 上限 | 白盒 | full(block) true | TC-MSHR-002 |
| F-MSHR-003 | MSHR entry 上限 | 白盒 | full(new block) true | TC-MSHR-003 |
| F-MSHR-004 | MSHR ready 顺序 | 白盒 | merged request FIFO order | TC-MSHR-004 |
| F-MSHR-005 | RAW pending | 白盒 | write 后 read pending 检测 | TC-MSHR-005 |
| F-RO-001 | read_only miss/hit flow | 用户场景 | access/cycle/fill/ready | TC-RO-001 |
| F-RO-002 | read_only miss queue full | 异常 | MISS_QUEUE_FULL fail stats | TC-RO-002 |
| F-DC-001 | data_cache read miss/hit | 用户场景 | read miss event、fill、hit | TC-DC-001 |
| F-HITLAT-001 | hit response 配置开关 | 接口/兼容性 | 默认新模式；显式关闭后保留旧 hit 兼容行为 | TC-HITLAT-001 |
| F-HITLAT-002 | read-only hit 延迟返回 | 接口/性能模型 | read-only hit 进入 ready queue，按 data port latency 返回 | TC-HITLAT-002 |
| F-HITLAT-003 | data read hit 延迟返回 | 接口/性能模型 | data/l1/l2 read hit 返回 HIT 但数据经 next_access 返回 | TC-HITLAT-003, TC-HITLAT-009 |
| F-HITLAT-004 | data write hit 延迟返回 | 接口/性能模型 | WB/WT/WE/local-global 写 hit 的完成点和 ready 行为 | TC-HITLAT-004, TC-HITLAT-005 |
| F-HITLAT-005 | hit/miss ready 顺序 | 白盒/序列 | hit response 与 miss fill 同周期 ready 的顺序规则 | TC-HITLAT-006, TC-HITLAT-009 |
| F-HITLAT-006 | 统计和端口一致性 | 统计/性能 | data port busy、hit/miss stats、ready 返回数量一致 | TC-HITLAT-007, TC-HITLAT-010 |
| F-HITLAT-007 | texture cache 兼容 | 回归 | texture hit-reserved/result FIFO 行为不回退 | TC-HITLAT-008 |
| F-HITLAT-008 | hit response backpressure | 异常 | hit response queue 满时的返回状态和 fail reason | TC-HITLAT-011 |
| F-HITLAT-009 | DataStore 可见性 | functional | 本阶段不改变 payload 读取，只验证 timing token ready | TC-HITLAT-012 |
| F-HITLAT-010 | line refcount/pin | 白盒/一致性 | pending hit/miss response 未读走前 line 不可被 replacement 替换 | TC-HITLAT-013, TC-HITLAT-014, TC-HITLAT-015 |
| F-HITLAT-011 | MSHR merge refcount | 白盒/一致性 | merged request 按 accepted mf 数量 pin/unpin | TC-HITLAT-016 |
| F-HITLAT-012 | sector line-level pin | 白盒/一致性 | sector cache 任一 sector pending 时整条 line 不可被替换 | TC-HITLAT-017 |
| F-HITLAT-013 | write-evict pinned invalid | 白盒/风险 | write-evict hit 立即 invalid 但仍 pinned 时不能作为 victim | TC-HITLAT-018 |
| F-HITLAT-014 | bounded hit response queue | 异常/风险 | hit response queue 有限容量和背压 | TC-HITLAT-019 |
| F-HITLAT-015 | DataStore snapshot timing | functional/风险 | hit accepted 时快照语义与延迟 token 返回不混淆 | TC-HITLAT-020 |
| F-FINAL-001 | baseline cache final check | 流程/白盒 | 用例收尾 drain 后，miss queue、MSHR、hit response queue、ready queue、pending ref 与 tag line 均回到初始状态 | TC-FINAL-001 |
| F-FINAL-002 | texture cache final check | 流程/白盒 | 用例收尾 drain 后，fragment/request/result FIFO、ROB、extra fields、tag line 与 texture data block 均回到初始状态 | TC-FINAL-002 |
| F-WR-001 | write-back hit | 白盒 | dirty no lower write | TC-WR-001 |
| F-WR-002 | write-through hit | 白盒 | WRITE_REQUEST_SENT | TC-WR-002 |
| F-WR-003 | write-evict hit | 白盒 | write event + invalidation | TC-WR-003 |
| F-WR-004 | no-write-allocate miss | 白盒 | write event no read allocate | TC-WR-004 |
| F-WR-005 | write-allocate miss | 白盒 | write + read + WRITE_ALLOCATE event | TC-WR-005 |
| F-WR-006 | dirty eviction writeback | 白盒 | WRITE_BACK_REQUEST_SENT + evicted info | TC-WR-006 |
| F-WR-007 | lazy fetch partial write | 白盒 | partial unreadable/readable behavior | TC-WR-007 |
| F-L1L2-001 | L1/L2 class parity | 用户场景 | l1/l2 access common behavior | TC-SCEN-001 |
| F-TEX-001 | texture miss pipeline | 用户场景/白盒 | request fifo、ROB、fill、result fifo、SECTOR_TEX_FIFO pending_read | TC-TEX-001, TC-TEX-004 |
| F-TEX-002 | texture hit reserved | 白盒 | hit 返回 HIT_RESERVED，经 fifo ready | TC-TEX-002 |
| F-TEX-003 | texture backpressure/order | 异常/白盒 | fifo/ROB full -> RESERVATION_FAIL、result FIFO backpressure、ROB 返回顺序 | TC-TEX-003, TC-TEX-004 |
| F-BW-001 | data/fill port | 性能 | port occupied/free | TC-BW-001 |
| F-STATS-001 | cache stats 基础 | 统计 | accesses/misses/pending/res fails | TC-STATS-001 |
| F-STATS-002 | fail stats | 统计 | reservation fail reason 精确查询 | TC-STATS-002 |
| F-STATS-003 | per-window stats | 统计 | clear_pw/get_sub_stats_pw | TC-STATS-003 |
| F-STATS-004 | status string tables | 诊断 | request/fail status 字符串映射 | TC-STATS-004 |
| F-FUNC-001 | DataStore read/write | functional | zero default、partial write preserve | TC-FUNC-001 |
| F-FUNC-002 | timing/functional 双模型 | 用户场景 | miss fill copies data, hit reads L1 | TC-FUNC-002 |
| F-PROP-001 | deterministic trace | 随机/回归 | fixed seed、多 seed、读写混合稳定性和不变量 | TC-PROP-001, TC-PROP-002, TC-PROP-003 |
| F-REG-001 | 一键回归 | 流程 | run.sh 构建所有测试并通过 | TC-REG-001 |
| F-COV-001 | 覆盖率统计 | 流程 | coverage 脚本生成报告或说明不可用 | TC-COV-001 |

## 反标规则

1. 新增 feature 必须补 `Testcase ID`。
2. 新增 testcase 必须回填本表。
3. 如果 testcase 暂未实现，必须在 `testcase_matrix.md` 中标记 `Planned` 并说明原因。
4. 默认回归中未执行的 testcase 不能标记为 `Implemented`。
5. 新增和待实现 feature 必须按配置、输入、流程、输出四个维度补充到“四维 Feature 规划”。

## 四维 Feature 规划

| Feature ID | 配置 | 输入 | 流程 | 输出 |
|------------|------|------|------|------|
| F-HITLAT-001 | 默认新模式；通过 `set_defer_hit_response(false)` 显式关闭 | 同一 hit trace，在新模式和显式旧模式下运行 | 对比 `access()`、`access_ready()`、`next_access()` 行为 | 默认 `HIT` 不代表数据 ready；显式旧模式保持兼容 |
| F-HITLAT-002 | read-only cache；新模式开启；指定 `data_port_width` | warm line 后发起 read hit | hit 入 response queue，cycle 推进到延迟结束 | `access()` 返回 `HIT`；延迟后 `next_access()` 返回原 mf |
| F-HITLAT-003 | data/l1/l2 cache；新模式开启；窄/宽 data port | warm line 后发起 data read hit | tag/LRU 更新，pin line，hit response queue 延迟返回 | read hit exactly-once 返回，延迟符合 `ceil(data_size/data_port_width)` |
| F-HITLAT-004 | WB/WT/WE/local-global 写策略；新模式开启 | 对已 warm line 发起 write hit | 执行写策略事件和 tag/dirty/invalidate 处理，进入 response queue | 写 hit 完成 token 延迟返回，策略事件保持正确 |
| F-HITLAT-005 | 新模式开启；可控制 hit ready 和 miss fill ready 同周期 | pending miss 与 delayed hit 交错输入 | 同周期 ready 时按设计顺序调用 `next_access()` | ready 顺序稳定，不丢不重 |
| F-HITLAT-006 | 新模式开启；启用统计和 port 采样 | hit/miss 混合 trace | accepted、ready、stats、data port busy 全程采样 | hit/miss stats 不变；ready 数等于 accepted 完成数；无双重计数 |
| F-HITLAT-007 | texture cache 原配置 | texture miss/hit-reserved trace | 保持 fragment/result FIFO 路径 | 现有 `HIT_RESERVED -> next_access()` 行为不回退 |
| F-HITLAT-008 | hit response queue 配置有限容量 | 超过容量的连续 hit | queue 满时继续访问 | 返回 `RESERVATION_FAIL`，fail reason 与设计一致 |
| F-HITLAT-009 | DataStore 双模型场景；新模式开启 | read/write functional trace | 只改变 timing token ready，不改变 payload 读取模型 | DataStore 场景通过，文档化“读出即快照” |
| F-HITLAT-010 | line-level refcount/pin 开启 | pending hit/miss 后同 set conflict miss | replacement 选 victim 前检查 refcount | refcount 非 0 的 line 不可替换 |
| F-HITLAT-011 | MSHR merge；新模式开启 | 同 line 多个 merged miss | 每个 accepted mf pin，ready 后逐个 unpin | refcount 按请求数增减，最后归零 |
| F-HITLAT-012 | sector cache；line-level pin | 某 sector pending，另一个请求触发同 line/set 替换 | 任一 sector pending 时保护整条 line | 整条 line 不作为 victim |
| F-HITLAT-013 | write-evict hit；立即 invalid 但保持 pin | write-evict hit 后插入同 set conflict miss | victim 选择先查 refcount，再查 invalid 状态 | pinned invalid line 不能复用 |
| F-HITLAT-014 | 有限 hit response queue；容量边界 | 连续 hit 填满 queue，再发起额外 hit | 触发 backpressure，drain 后再访问 | queue 满时 fail，drain 后恢复接收 |
| F-HITLAT-015 | DataStore 快照语义；未来 payload 场景 | hit accepted 后延迟期间插入同地址写 | 固化读快照时机，token 延迟交付 | 返回 accepted 时刻快照，不误判为 coherence 行为 |
| F-FINAL-001 | baseline/read-only/data cache；默认新模式 | miss/fill/hit 序列完成后收尾 | drain 到 queue/MSHR/pending response 空，随后 invalidate | `final_state_clean()==true`，所有 line invalid/unpinned，外部 mem queue 为空 |
| F-FINAL-002 | texture cache；默认新模式 | texture miss/fill/hit-reserved 序列完成后收尾 | drain 到 FIFO/ROB/extra fields 空，随后 invalidate | `final_state_clean()==true`，tag/data block 回初始态，外部 mem queue 为空 |
