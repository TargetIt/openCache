# gpgpusim_cache Testcase 说明与反标

## Testcase 矩阵

| Testcase ID | 覆盖 Feature | 类型 | 状态 | 实现位置 | 期望 |
|-------------|--------------|------|------|----------|------|
| TC-CFG-001 | F-CFG-001 | unit | Implemented | `test/test_main.cc` | 基础配置字段精确匹配 |
| TC-CFG-002 | F-CFG-002 | unit | Implemented | `test/test_main.cc` | `none` 配置禁用 |
| TC-CFG-003 | F-CFG-003 | unit | Implemented | `test/test_cache_whitebox.cc` | NORMAL/SECTOR atom size 正确 |
| TC-CFG-004 | F-CFG-005 | unit | Implemented | `test/test_cache_whitebox.cc` | STREAMING 转 ON_FILL 且 `is_streaming()` |
| TC-CFG-011 | F-CFG-011 | unit | Implemented | `test/test_cache_whitebox.cc` | 单参数典型值矩阵解析和派生字段校验 |
| TC-CFG-012 | F-CFG-011 | unit | Implemented | `test/test_cache_whitebox.cc` | 约束 pairwise 参数组合执行 miss/fill/hit 或 texture ready smoke |
| TC-DEATH-001 | F-CFG-010 | death | Implemented | `test/test_death.cc` | 非法配置/assert 路径在子进程中异常退出，主回归不中断；覆盖策略非法组合、texture token 缺失、非法枚举字符、port width、Fermi nset |
| TC-ADDR-001 | F-CFG-009 | unit | Implemented | `test/test_cache_whitebox.cc` | LINEAR/XOR/IPOLY/CUSTOM/FERMI32/FERMI64 set-index 在范围内，并校验 9 个边界/高位地址 golden 值 |
| TC-ADDR-002 | F-ADDR-001 | unit | Implemented | `test/test_cache_whitebox.cc` | block/tag/mshr 地址按 line/sector 对齐 |
| TC-ADDR-003 | F-ADDR-002 | unit | Implemented | `test/test_cache_whitebox.cc` | 边界地址映射不越界 |
| TC-TAG-001 | F-BLK-001, F-TAG-001 | unit | Implemented | `test/test_main.cc` | tag miss 后 fill，再 probe hit |
| TC-TAG-002 | F-BLK-001, F-TAG-004 | unit | Implemented | `test/test_cache_whitebox.cc` | fill 后状态从 reserved 到 valid |
| TC-TAG-003 | F-TAG-002 | unit | Implemented | `test/test_cache_whitebox.cc` | pending fill 命中返回 HIT_RESERVED |
| TC-TAG-004 | F-CFG-003, F-BLK-002 | unit | Implemented | `test/test_cache_whitebox.cc` | sector 局部 fill 后其他 sector 为 SECTOR_MISS |
| TC-TAG-005 | F-CFG-004 | unit | Implemented | `test/test_cache_whitebox.cc` | LRU 与 FIFO victim 可观测 |
| TC-TAG-006 | F-CFG-005, F-TAG-004 | unit | Implemented | `test/test_cache_whitebox.cc` | ON_FILL fill 分配 |
| TC-TAG-007 | F-TAG-003 | unit | Implemented | `test/test_cache_whitebox.cc` | 全 reserved line allocation fail 返回 RESERVATION_FAIL 且统计 res_fails |
| TC-TAG-008 | F-TAG-005 | unit | Implemented | `test/test_cache_whitebox.cc` | flush 后 dirty line miss、clean line hit；invalidate 后多 line miss |
| TC-SEQ-001 | F-SEQ-001 | unit | Implemented | `test/test_cache_whitebox.cc` | 同一 line 覆盖 cold miss -> ready hit -> consecutive hit，以及 write-evict hit 后再次 read miss |
| TC-SEQ-002 | F-SEQ-002 | unit | Implemented | `test/test_cache_whitebox.cc` | direct-mapped cache 填满后，同 set conflict miss 驱逐旧 line，其他 set 保持 hit |
| TC-SEQ-003 | F-SEQ-003 | unit | Implemented | `test/test_cache_whitebox.cc` | sector partial valid 后目标 sector 返回 SECTOR_MISS，补齐后 hit，冲突后旧 line miss |
| TC-SEQ-004 | F-SEQ-004 | unit | Implemented | `test/test_cache_whitebox.cc` | same line miss merge 只发一个下游请求，fill 后按 FIFO ready；merge 上限返回 RESERVATION_FAIL |
| TC-SEQ-005 | F-SEQ-005 | unit | Implemented | `test/test_cache_whitebox.cc` | texture miss fill pending 时后续 hit 返回 HIT_RESERVED，并按 result FIFO 顺序 ready |
| TC-MSHR-001 | F-MSHR-001 | unit | Implemented | `test/test_main.cc` | add/ready/next_access |
| TC-MSHR-002 | F-MSHR-002 | unit | Implemented | `test/test_cache_whitebox.cc` | merge 达上限 full |
| TC-MSHR-003 | F-MSHR-003 | unit | Implemented | `test/test_cache_whitebox.cc` | entry 达上限 full |
| TC-MSHR-004 | F-MSHR-004 | unit | Implemented | `test/test_cache_whitebox.cc` | merged ready FIFO 顺序 |
| TC-MSHR-005 | F-MSHR-005 | unit | Implemented | `test/test_cache_whitebox.cc` | RAW pending 检测 |
| TC-MSHR-006 | F-CFG-008 | unit | Implemented | `test/test_cache_whitebox.cc` | SECTOR_ASSOC 多 sector response 到齐后才 mark ready，并恢复 original request |
| TC-RO-001 | F-RO-001 | unit | Implemented | `test/test_cache_whitebox.cc` | read_only miss/cycle/fill/ready |
| TC-RO-002 | F-RO-002 | unit | Implemented | `test/test_cache_whitebox.cc` | miss queue full backpressure，并精确查询 MISS_QUEUE_FULL |
| TC-DC-001 | F-DC-001 | unit | Implemented | `test/test_cache_whitebox.cc` | data_cache read miss/hit |
| TC-HITLAT-001 | F-HITLAT-001 | unit | Planned | `test/test_cache_whitebox.cc` | 默认不开启 hit 延迟时，既有 hit 行为和当前回归保持一致 |
| TC-HITLAT-002 | F-HITLAT-002 | unit | Planned | `test/test_cache_whitebox.cc` | read-only cache hit 返回 `HIT`，同周期 `access_ready()==false`，延迟后 `next_access()` 返回原 mf |
| TC-HITLAT-003 | F-HITLAT-003 | unit | Planned | `test/test_cache_whitebox.cc` | l1/data read hit 返回 `HIT`，按 `ceil(data_size/data_port_width)` cycle 后 ready |
| TC-HITLAT-004 | F-HITLAT-004 | unit | Planned | `test/test_cache_whitebox.cc` | write-back hit 延迟返回，并保持 dirty/tag 更新正确 |
| TC-HITLAT-005 | F-HITLAT-004 | unit | Planned | `test/test_cache_whitebox.cc` | write-through/write-evict hit 发出事件后，按定义进入 hit response ready |
| TC-HITLAT-006 | F-HITLAT-005 | unit | Planned | `test/test_cache_whitebox.cc` | hit response 和 miss fill 同周期完成时，`next_access()` 顺序与策略文档一致 |
| TC-HITLAT-007 | F-HITLAT-006 | unit | Planned | `test/test_cache_whitebox.cc` | data port busy cycles、hit stats、ready 返回数量一致，不重复计数 |
| TC-HITLAT-008 | F-HITLAT-007 | regression | Planned | `test/test_cache_whitebox.cc` | texture cache 现有 `HIT_RESERVED -> result FIFO -> next_access()` 测试继续通过 |
| TC-HITLAT-009 | F-HITLAT-003, F-HITLAT-005 | sequence | Planned | `test/test_cache_whitebox.cc` | 同一 line `MISS -> fill -> HIT accepted -> delayed ready -> HIT` 序列完整 |
| TC-HITLAT-010 | F-HITLAT-002, F-HITLAT-003, F-HITLAT-006 | property | Planned | `test/test_cache_whitebox.cc` | 多 seed hit/miss trace 在新模式下 accepted、ready、stats 一致且 exactly-once 返回 |
| TC-HITLAT-011 | F-HITLAT-008 | unit | Planned | `test/test_cache_whitebox.cc` | hit response queue 容量受限时返回 `RESERVATION_FAIL`，fail reason 与策略文档一致 |
| TC-HITLAT-012 | F-HITLAT-009 | scenario | Planned | `test/test_scenario.cc` | DataStore 双模型场景继续通过，证明本阶段只改变 timing token 完成时机 |
| TC-WR-001 | F-WR-001 | unit | Implemented | `test/test_cache_whitebox.cc` | WB hit 不产生 lower write |
| TC-WR-002 | F-WR-002 | unit | Implemented | `test/test_cache_whitebox.cc` | WT hit 产生 WRITE_REQUEST_SENT |
| TC-WR-003 | F-WR-003 | unit | Implemented | `test/test_cache_whitebox.cc` | WE hit 后读同地址 miss |
| TC-WR-004 | F-WR-004 | unit | Implemented | `test/test_cache_whitebox.cc` | NWA miss 只有 write event |
| TC-WR-005 | F-WR-005 | unit | Implemented | `test/test_cache_whitebox.cc` | WA miss 产生 write 和 write-allocate，并向下游发出两个 token |
| TC-WR-006 | F-WR-006, F-BLK-003 | unit | Implemented | `test/test_cache_whitebox.cc` | dirty eviction 产生 writeback event |
| TC-WR-007 | F-WR-007, F-BLK-004 | unit | Implemented | `test/test_cache_whitebox.cc` | lazy fetch partial write 后 read 触发 READ_REQUEST_SENT；full-sector write 后 read 命中 |
| TC-WR-008 | F-CFG-006 | unit | Implemented | `test/test_cache_whitebox.cc` | LOCAL_WB_GLOBAL_WT：global write hit write-through，local write hit write-back |
| TC-WR-009 | F-CFG-007 | unit | Implemented | `test/test_cache_whitebox.cc` | FETCH_ON_WRITE：full-line write 不 fetch，partial write 触发 write-allocate |
| TC-WR-010 | F-BLK-003, F-WR-006 | unit | Implemented | `test/test_cache_whitebox.cc` | sector cache 多 dirty sector 驱逐时 writeback size 和 sector mask 正确 |
| TC-SCEN-001 | F-L1L2-001 | scenario | Implemented | `test/test_scenario.cc` | L1/L2 集成场景通过 |
| TC-TEX-001 | F-TEX-001 | unit | Implemented | `test/test_cache_whitebox.cc` | texture miss fill 后 ready |
| TC-TEX-002 | F-TEX-002 | unit | Implemented | `test/test_cache_whitebox.cc` | texture hit 返回 HIT_RESERVED 并 ready |
| TC-TEX-003 | F-TEX-003 | unit | Implemented | `test/test_cache_whitebox.cc` | texture FIFO/ROB 满 backpressure，result FIFO 满时阻塞后续 ready |
| TC-TEX-004 | F-TEX-001, F-TEX-003 | unit | Implemented | `test/test_cache_whitebox.cc` | SECTOR_TEX_FIFO 多 sector response 全部返回后 ready；乱序 fill 按 ROB 顺序返回 |
| TC-BW-001 | F-BW-001 | unit | Implemented | `test/test_cache_whitebox.cc` | data/fill port free 状态变化 |
| TC-STATS-001 | F-STATS-001 | unit | Implemented | `test/test_main.cc` | cache_sub_stats 汇总 |
| TC-STATS-002 | F-STATS-002 | unit | Implemented | `test/test_cache_whitebox.cc` | LINE_ALLOC_FAIL、MISS_QUEUE_FULL、MSHR_ENTRY、MSHR_MERGE、MSHR_RW_PENDING 精确查询 |
| TC-STATS-003 | F-STATS-003, F-TAG-006 | unit | Implemented | `test/test_cache_whitebox.cc` | window/per-window stats |
| TC-STATS-004 | F-STATS-004 | unit | Implemented | `test/test_cache_whitebox.cc` | request/fail status 字符串表完整映射 |
| TC-FUNC-001 | F-FUNC-001 | unit | Implemented | `test/test_cache_whitebox.cc` | DataStore 默认零、write/read、同 block partial write preserve |
| TC-FUNC-002 | F-FUNC-002 | scenario | Implemented | `test/test_scenario.cc` | dual model 数据一致 |
| TC-PROP-001 | F-PROP-001 | property | Implemented | `test/test_cache_whitebox.cc` | fixed seed trace 不变量 |
| TC-PROP-002 | F-PROP-001 | property | Implemented | `test/test_cache_whitebox.cc` | 5-seed read-only 随机 trace 重复运行差分一致，校验 accesses/misses/res_fails 不变量 |
| TC-PROP-003 | F-PROP-001 | property | Implemented | `test/test_cache_whitebox.cc` | 4-seed read/write 混合 trace 与无驱逐 oracle 对照，校验 hit/miss、per-window read/write hit 和重复运行一致性 |
| TC-REG-001 | F-REG-001 | regression | Implemented | `run.sh` | 一键回归 unit/scenario/whitebox/death 全部通过 |
| TC-COV-001 | F-COV-001 | coverage | Implemented | `coverage.sh` | 生成覆盖率报告；当前 line 68.77%、function 71.65%、branch 59.00%；death 子进程 abort 路径不计入 coverage；llvm-cov mismatch 诊断落盘并写入说明 |

## 多角色检视记录

| 阶段 | 架构 | 设计 | 验证 | 项目经理 |
|------|------|------|------|----------|
| step0 验证方案 | 通过，要求覆盖写策略和 sector/texture | 通过，要求区分用户场景与白盒 | 通过，要求先修测试框架假通过 | 通过，要求 requirements 追踪 |
| step1 feature | 通过，feature 覆盖主要接口和状态机 | 通过，建议后续补 death tests | 通过，要求每条反标 testcase | 通过，要求 planned 不得冒充 implemented |
| step2 testcase | 通过，覆盖架构风险点 | 通过，测试分层明确 | 通过，要求默认回归全部可执行 | 通过，要求 run.sh 和 coverage.sh 纳入交付 |
| step3-step7 | 通过，新增白盒和 death 覆盖关键架构风险；参数矩阵和 sequence matrix 已纳入；覆盖率需后续继续提升 | 通过，run.sh/CMake/coverage.sh 均纳入 | 通过，默认回归 13+10+26+16 全通过 | 通过，文档和追踪矩阵已刷新 |
| sequence matrix | 通过，补齐 same line/set/cache/sector/MSHR/texture 序列风险 | 通过，用例以显式状态序列驱动，不依赖随机统计 | 通过，新增 5 条白盒用例并反标 feature | 通过，默认回归 13+10+26+16 全通过，coverage 基线已刷新 |
| hit response strategy | 通过，接口语义变化和受影响范围已列出 | 通过，推荐兼容开关和复用 access_ready/next_access | 通过，planned testcase 覆盖 read-only/data/write/顺序/统计/backpressure | 通过，文档阶段完成，不声称实现完成 |
