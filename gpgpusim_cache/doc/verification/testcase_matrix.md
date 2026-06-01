# gpgpusim_cache Testcase 说明与反标

## Testcase 矩阵

| Testcase ID | 覆盖 Feature | 类型 | 状态 | 实现位置 | 期望 |
|-------------|--------------|------|------|----------|------|
| TC-CFG-001 | F-CFG-001 | unit | Implemented | `test/test_main.cc` | 基础配置字段精确匹配 |
| TC-CFG-002 | F-CFG-002 | unit | Implemented | `test/test_main.cc` | `none` 配置禁用 |
| TC-CFG-003 | F-CFG-003 | unit | Implemented | `test/test_cache_whitebox.cc` | NORMAL/SECTOR atom size 正确 |
| TC-CFG-004 | F-CFG-005 | unit | Implemented | `test/test_cache_whitebox.cc` | STREAMING 转 ON_FILL 且 `is_streaming()` |
| TC-ADDR-001 | F-CFG-009 | unit | Implemented | `test/test_cache_whitebox.cc` | LINEAR/XOR/IPOLY/FERMI/CUSTOM set-index 在范围内，并校验选定地址 golden 值 |
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
| TC-MSHR-001 | F-MSHR-001 | unit | Implemented | `test/test_main.cc` | add/ready/next_access |
| TC-MSHR-002 | F-MSHR-002 | unit | Implemented | `test/test_cache_whitebox.cc` | merge 达上限 full |
| TC-MSHR-003 | F-MSHR-003 | unit | Implemented | `test/test_cache_whitebox.cc` | entry 达上限 full |
| TC-MSHR-004 | F-MSHR-004 | unit | Implemented | `test/test_cache_whitebox.cc` | merged ready FIFO 顺序 |
| TC-MSHR-005 | F-MSHR-005 | unit | Implemented | `test/test_cache_whitebox.cc` | RAW pending 检测 |
| TC-RO-001 | F-RO-001 | unit | Implemented | `test/test_cache_whitebox.cc` | read_only miss/cycle/fill/ready |
| TC-RO-002 | F-RO-002 | unit | Partial | `test/test_cache_whitebox.cc` | backpressure 行为覆盖；fail reason 精确查询后续补充 |
| TC-DC-001 | F-DC-001 | unit | Implemented | `test/test_cache_whitebox.cc` | data_cache read miss/hit |
| TC-WR-001 | F-WR-001 | unit | Implemented | `test/test_cache_whitebox.cc` | WB hit 不产生 lower write |
| TC-WR-002 | F-WR-002 | unit | Implemented | `test/test_cache_whitebox.cc` | WT hit 产生 WRITE_REQUEST_SENT |
| TC-WR-003 | F-WR-003 | unit | Implemented | `test/test_cache_whitebox.cc` | WE hit 后读同地址 miss |
| TC-WR-004 | F-WR-004 | unit | Implemented | `test/test_cache_whitebox.cc` | NWA miss 只有 write event |
| TC-WR-005 | F-WR-005 | unit | Implemented | `test/test_cache_whitebox.cc` | WA miss 产生 write 和 write-allocate，并向下游发出两个 token |
| TC-WR-006 | F-WR-006, F-BLK-003 | unit | Implemented | `test/test_cache_whitebox.cc` | dirty eviction 产生 writeback event |
| TC-WR-007 | F-WR-007, F-BLK-004 | unit | Implemented | `test/test_cache_whitebox.cc` | lazy fetch partial write 后 read 触发 READ_REQUEST_SENT；full-sector write 后 read 命中 |
| TC-WR-008 | F-CFG-006 | unit | Implemented | `test/test_cache_whitebox.cc` | LOCAL_WB_GLOBAL_WT：global write hit write-through，local write hit write-back |
| TC-WR-009 | F-CFG-007 | unit | Implemented | `test/test_cache_whitebox.cc` | FETCH_ON_WRITE：full-line write 不 fetch，partial write 触发 write-allocate |
| TC-SCEN-001 | F-L1L2-001 | scenario | Implemented | `test/test_scenario.cc` | L1/L2 集成场景通过 |
| TC-TEX-001 | F-TEX-001 | unit | Implemented | `test/test_cache_whitebox.cc` | texture miss fill 后 ready |
| TC-TEX-002 | F-TEX-002 | unit | Implemented | `test/test_cache_whitebox.cc` | texture hit 返回 HIT_RESERVED 并 ready |
| TC-TEX-003 | F-TEX-003 | unit | Implemented | `test/test_cache_whitebox.cc` | texture FIFO/ROB 满 backpressure |
| TC-BW-001 | F-BW-001 | unit | Implemented | `test/test_cache_whitebox.cc` | data/fill port free 状态变化 |
| TC-STATS-001 | F-STATS-001 | unit | Implemented | `test/test_main.cc` | cache_sub_stats 汇总 |
| TC-STATS-002 | F-STATS-002 | unit | Partial | `test/test_cache_whitebox.cc` | res_fails 汇总覆盖；具体 fail reason 查询后续补充 |
| TC-STATS-003 | F-STATS-003, F-TAG-006 | unit | Implemented | `test/test_cache_whitebox.cc` | window/per-window stats |
| TC-FUNC-001 | F-FUNC-001 | unit | Implemented | `test/test_cache_whitebox.cc` | DataStore 默认零、write/read、同 block partial write preserve |
| TC-FUNC-002 | F-FUNC-002 | scenario | Implemented | `test/test_scenario.cc` | dual model 数据一致 |
| TC-PROP-001 | F-PROP-001 | property | Implemented | `test/test_cache_whitebox.cc` | fixed seed trace 不变量 |
| TC-REG-001 | F-REG-001 | regression | Implemented | `run.sh` | 一键回归全部通过 |
| TC-COV-001 | F-COV-001 | coverage | Implemented | `coverage.sh` | 生成覆盖率报告；当前 line 64.41%、function 67.83%、branch 54.36% |

## 多角色检视记录

| 阶段 | 架构 | 设计 | 验证 | 项目经理 |
|------|------|------|------|----------|
| step0 验证方案 | 通过，要求覆盖写策略和 sector/texture | 通过，要求区分用户场景与白盒 | 通过，要求先修测试框架假通过 | 通过，要求 requirements 追踪 |
| step1 feature | 通过，feature 覆盖主要接口和状态机 | 通过，建议后续补 death tests | 通过，要求每条反标 testcase | 通过，要求 planned 不得冒充 implemented |
| step2 testcase | 通过，覆盖架构风险点 | 通过，测试分层明确 | 通过，要求默认回归全部可执行 | 通过，要求 run.sh 和 coverage.sh 纳入交付 |
| step3-step7 | 通过，新增白盒覆盖关键架构风险；覆盖率需后续继续提升 | 通过，run.sh/CMake/coverage.sh 均纳入 | 通过，默认回归 13+10+12 全通过 | 通过，文档和追踪矩阵已刷新 |
