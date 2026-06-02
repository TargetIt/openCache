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
| F-MSHR-001 | MSHR add/probe/ready | 接口 | add、mark_ready、next_access | TC-MSHR-001 |
| F-MSHR-002 | MSHR merge 上限 | 白盒 | full(block) true | TC-MSHR-002 |
| F-MSHR-003 | MSHR entry 上限 | 白盒 | full(new block) true | TC-MSHR-003 |
| F-MSHR-004 | MSHR ready 顺序 | 白盒 | merged request FIFO order | TC-MSHR-004 |
| F-MSHR-005 | RAW pending | 白盒 | write 后 read pending 检测 | TC-MSHR-005 |
| F-RO-001 | read_only miss/hit flow | 用户场景 | access/cycle/fill/ready | TC-RO-001 |
| F-RO-002 | read_only miss queue full | 异常 | MISS_QUEUE_FULL fail stats | TC-RO-002 |
| F-DC-001 | data_cache read miss/hit | 用户场景 | read miss event、fill、hit | TC-DC-001 |
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
