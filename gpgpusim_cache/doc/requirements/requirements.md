# gpgpusim_cache 版本需求

本文件记录 GPGPU-Sim 版本相关需求。项目级需求先进入 `../../doc/requirements/requirements.md`，如果需求影响 GPGPU-Sim 版本，再同步摘录到这里。

## REQ-20260601-001-C GPGPU-Sim 版本文档规范化

### 原始来源

来自项目级需求 `REQ-20260601-001`。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-DOC-001 | 将 `gpgpusim_cache` 的版本说明文档统一放入 `gpgpusim_cache/doc/` | 已完成 |
| GPGPUSIM-DOC-002 | 保留 `README.md` 作为版本入口文档 | 已完成 |
| GPGPUSIM-DOC-003 | 保留 `USER_GUIDE.md` 作为详细用户手册 | 已完成 |
| GPGPUSIM-DOC-004 | 保留已有 `doc/遗留问题_20260528_230000.md` | 已完成 |
| GPGPUSIM-DOC-005 | 建立 `gpgpusim_cache/doc/requirements/requirements.md` 作为版本级需求入口 | 已完成 |

### 验收标准

1. `gpgpusim_cache/doc/README.md` 存在。
2. `gpgpusim_cache/doc/USER_GUIDE.md` 存在。
3. `gpgpusim_cache/doc/requirements/requirements.md` 存在。
4. 修改 GPGPU-Sim 版本代码或文档后，交付前运行 `gpgpusim_cache/run.sh`。

### 更新时间

2026-06-01

## REQ-20260602-001 gpgpusim_cache 参数矩阵验证

### 原始来源

用户要求调查并补齐 `gpgpusim_cache` 中 set 数、way/assoc、line size、data port 宽度、MSHR 参数、cache type、写策略等可配参数的典型状态遍历，以及参数相互组合遍历。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-PARAM-001 | 在 feature/testcase 文档中新增参数矩阵验证项，并反标到具体测试 | 已完成 |
| GPGPUSIM-PARAM-002 | 覆盖单参数典型值：nset、assoc、line size、cache type、replacement、write policy、allocation、write allocate、MSHR type、MSHR entries/merge、miss queue、texture FIFO、data port width、set-index function | 已完成 |
| GPGPUSIM-PARAM-003 | 覆盖约束后的 pairwise 组合：NORMAL/SECTOR x MSHR type、write policy x write allocate、line size x data port、nset x assoc、replacement x line pressure、texture FIFO 组合 | 已完成 |
| GPGPUSIM-PARAM-004 | 保留非法组合 death tests，并把参数矩阵新增结果同步到遗留问题和覆盖率基线 | 已完成 |

### 验收标准

1. Feature/testcase 文档中有参数矩阵验证条目。
2. 默认 `./run.sh` 执行参数矩阵用例并通过。
3. `./coverage.sh` 通过，覆盖率基线与遗留问题文档同步刷新。
4. 参数矩阵不能只 parse 配置，至少对代表性组合执行一次 miss/fill/hit 或 texture ready 流程。

### 更新时间

2026-06-02

## REQ-20260602-002 gpgpusim_cache hit/miss 序列矩阵验证

### 原始来源

用户要求补齐 hit/miss 细分状态与序列 pattern 覆盖，包括同一 cache line、同一 set、整个 cache、sector partial、pending data、MSHR merge、连续 hit、连续 miss、hit 后 miss、miss 后 hit、容量/冲突驱逐等场景。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-SEQ-001 | 在 feature/testcase 文档中新增 hit/miss 序列矩阵，并反标到具体测试 | 已完成 |
| GPGPUSIM-SEQ-002 | 覆盖同一 cache line 的 cold miss、ready hit、连续 hit、write-evict hit 后 miss | 已完成 |
| GPGPUSIM-SEQ-003 | 覆盖同一 set 与整个 cache 填满后的 conflict/capacity eviction 序列 | 已完成 |
| GPGPUSIM-SEQ-004 | 覆盖 sector partial valid、目标 sector miss、补齐后 hit 的序列 | 已完成 |
| GPGPUSIM-SEQ-005 | 覆盖 MSHR merge miss、pending ready 顺序、merge 上限 backpressure | 已完成 |
| GPGPUSIM-SEQ-006 | 覆盖 texture cache miss 后 hit-reserved，再按 result FIFO 顺序 ready | 已完成 |

### 验收标准

1. Feature/testcase 文档中有 hit/miss sequence matrix 条目。
2. 每条 sequence testcase 映射到明确测试函数。
3. 用例不能只统计 hit/miss 数，必须断言每一步状态、事件、ready 顺序或 eviction 后状态。
4. 默认 `./run.sh` 执行新增 sequence 用例并通过。
5. `./coverage.sh` 通过，覆盖率基线与遗留问题文档同步刷新。

### 更新时间

2026-06-02

## REQ-20260602-003 gpgpusim_cache hit response 延迟返回

### 原始来源

用户提出当前 data/read-only cache 在 hit 后 `access()` 直接完成数据返回，不符合 SRAM/data array 读访问仍需时间的性能模型。需求是将 hit/miss 判定与最终数据返回解耦，使 hit 也能像 miss fill 一样通过 `access_ready()` / `next_access()` 延迟返回。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-HITLAT-001 | 编写 hit response 延迟返回策略方案，明确接口语义、兼容开关、ready 队列和风险 | 已完成 |
| GPGPUSIM-HITLAT-002 | 编写 hit response 延迟返回测试方案，覆盖 read-only/data/read hit/write hit/ready 顺序/统计/backpressure | 已完成 |
| GPGPUSIM-HITLAT-003 | 完成项目经理、设计、验证、测试专家评审，并记录独立评审代理意见 | 已完成 |
| GPGPUSIM-HITLAT-004 | 在 feature/testcase 文档中新增 planned 追踪项 | 已完成 |
| GPGPUSIM-HITLAT-005 | 在 `doc/design.md` 中补充 line-level refcount/pin 设计，确保 pending response 未读走前 line 不可被替换 | 已完成 |
| GPGPUSIM-HITLAT-006 | 实现兼容开关、hit response queue、line pin/refcount 和统一 ready 返回语义 | 已完成 |
| GPGPUSIM-HITLAT-007 | 实现 planned testcase 并刷新覆盖率与遗留问题 | 已完成 |
| GPGPUSIM-HITLAT-008 | 将新增和待实现 feature/testcase 按配置、输入、流程、输出四个维度补齐规划 | 已完成 |
| GPGPUSIM-HITLAT-009 | 为 review 风险补充四维 feature/testcase 规划：write-evict pinned invalid、bounded hit response queue、DataStore 快照语义 | 已完成 |

### 验收标准

1. `doc/verification/hit_response_strategy.md` 存在，并明确 `HIT` 不等于数据 ready 的新语义。
2. `doc/verification/hit_response_test_plan.md` 存在，并列出 planned testcase。
3. `doc/design.md` 存在，并明确 line-level refcount/pin 机制。
4. Feature/testcase 文档已建立 `F-HITLAT-*` 和 `TC-HITLAT-*` 追踪。
5. 新增和待实现 feature/testcase 已按配置、输入、流程、输出四维记录。
6. 默认旧模式保持既有 hit 行为；新模式开启后 hit 经 `access_ready()/next_access()` 延迟返回。
7. `TC-HITLAT-001..020` 已反标到白盒用例，默认 `./run.sh` 全通过。
8. 提交前 `git diff --check` 通过。

### 更新时间

2026-06-02

## 新需求追加区

后续 GPGPU-Sim 版本需求从这里继续追加。

## REQ-20260601-002 gpgpusim_cache 零缺陷取向验证体系

### 原始来源

来自项目级需求 `REQ-20260601-002`。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-VERIFY-001 | 编写验证方案，吸收公开 cache 验证方法和多 agent 检视意见 | 已完成 |
| GPGPUSIM-VERIFY-002 | 建立 feature 分解矩阵，覆盖用户场景、接口、白盒状态和流程 | 已完成 |
| GPGPUSIM-VERIFY-003 | 建立 testcase 矩阵，并把 testcase 反标到 feature | 已完成 |
| GPGPUSIM-VERIFY-004 | 修复 `test_main.cc` 失败假通过风险 | 已完成 |
| GPGPUSIM-VERIFY-005 | 新增 `test_cache_whitebox.cc` 深度白盒测试 | 已完成 |
| GPGPUSIM-VERIFY-006 | 更新 `run.sh`，一键运行 unit、scenario、whitebox 测试 | 已完成 |
| GPGPUSIM-VERIFY-007 | 更新 CMake，纳入 whitebox 测试 | 已完成 |
| GPGPUSIM-VERIFY-008 | 新增 `coverage.sh`，生成覆盖率摘要 | 已完成 |

### 验收标准

1. `./run.sh` 通过：unit 13/13，scenario 86/86 checks，whitebox 38/38，death 16/16。
2. `./coverage.sh` 通过并输出覆盖率摘要。
3. Feature/testcase 文档中每条已实现 testcase 映射到测试文件。
4. 覆盖率基线已记录，低覆盖区域作为后续迭代输入。

### 当前验证结果

| 命令 | 结果 |
|------|------|
| `./run.sh` | 通过 |
| `./coverage.sh` | 通过 |

覆盖率基线：

| 指标 | 覆盖率 |
|------|--------|
| Region | 55.49% |
| Function | 72.86% |
| Line | 69.82% |
| Branch | 60.30% |

### 更新时间

2026-06-01
