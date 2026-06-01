# OpenCache 需求台账

本文件记录 OpenCache 项目的原始需求、拆解需求、验收标准和变更历史。后续所有新需求都追加到本文件，不能只散落在聊天记录、临时文件或代码注释中。

## 记录规范

每条需求必须包含：

| 字段 | 说明 |
|------|------|
| 编号 | 使用 `REQ-YYYYMMDD-NNN` 格式 |
| 原始需求 | 保留用户原话或尽量贴近原话 |
| 拆解需求 | 将原始需求拆成可执行、可验收的条目 |
| 影响范围 | 标注影响 `project`、`gem5_cache`、`gpgpusim_cache`、`qa` 或其他目录 |
| 验收标准 | 明确什么结果算完成 |
| 状态 | `待处理`、`进行中`、`已完成`、`已废弃` |
| 更新时间 | 最近一次更新日期 |

## REQ-20260601-001 文档与 QA 规范化

### 原始需求

用户要求：

> oencache 项目下有两个版本，gem5 和 gpgpusim 的，请帮我规范一下这两个版本的这个文档。在它的目录下创建一个 requirements，记录我的原始需求，以及基于原始需求之后的分解需求，以及将来我有新的需求，也要把这个新的需求填写到这个 requirements 的下面的文档中。里面所有的那个文档都放在 doc 目录下。另外，在这个 OpenCache 下面创建一个目录叫 qa，这个 qa 呢，这里面就记录它的规范。记录整个项目运作的规范，包括文档需求，包括我本次给你说的这个需求。如果有刷新，要刷新那个 require 目录下的 requirements 目录下面的文档之类的。文档的模板，以及我们交付的质量。比如说有修改之后，它要去自动回归所有的用例啊。回归所有的用例。以及交付的模板、交付的质量，还有整个的流程。

### 拆解需求

| 子需求 | 内容 | 影响范围 | 状态 |
|--------|------|----------|------|
| REQ-20260601-001-A | 为 OpenCache 建立项目级需求台账，记录原始需求、拆解需求、验收标准和变更历史 | `doc/requirements/` | 已完成 |
| REQ-20260601-001-B | 为 `gem5_cache` 建立版本级 `doc/requirements` 目录和需求入口 | `gem5_cache/doc/requirements/` | 已完成 |
| REQ-20260601-001-C | 为 `gpgpusim_cache` 建立版本级 `doc/requirements` 目录和需求入口 | `gpgpusim_cache/doc/requirements/` | 已完成 |
| REQ-20260601-001-D | 将现有版本文档统一放入各自 `doc` 目录 | `gem5_cache/doc/`, `gpgpusim_cache/doc/` | 已完成 |
| REQ-20260601-001-E | 在 OpenCache 根目录创建 `qa` 目录，记录项目运作规范、文档规范、交付质量和流程 | `qa/` | 已完成 |
| REQ-20260601-001-F | 定义需求变更时必须刷新 requirements 文档的流程 | `qa/process.md`, `qa/quality-gate.md` | 已完成 |
| REQ-20260601-001-G | 定义交付前必须自动回归所有用例的质量门禁 | `qa/quality-gate.md` | 已完成 |
| REQ-20260601-001-H | 提供需求记录和交付记录模板 | `qa/templates/` | 已完成 |

### 验收标准

1. `doc/requirements/requirements.md` 存在，并包含原始需求、拆解需求和验收标准。
2. `gem5_cache/doc/requirements/requirements.md` 存在。
3. `gpgpusim_cache/doc/requirements/requirements.md` 存在。
4. 版本级 `README.md` 和 `USER_GUIDE.md` 均位于各自 `doc` 目录下。
5. `qa/` 目录包含项目流程、质量门禁和模板。
6. 交付前运行 `gem5_cache/run.sh` 和 `gpgpusim_cache/run.sh`，并记录结果。

### 状态

已完成。

### 更新时间

2026-06-01

## 新需求追加区

后续新增需求从这里继续追加，编号递增。

## REQ-20260601-002 gpgpusim_cache 零缺陷取向验证体系

### 原始需求

用户要求：

> gpgpusim_cache 你现在是一名验证人员，在你看来质量是最重要的，token自由，不考虑成本，想尽一切办法提升质量是你的生存价值；工作任务，给gpgpusim_cache构造用例；可以到网络上参考一下一些cache的开源用例，经典测试方法等等，为你所用。工作建议：step0 doc下写一个验证方案... step1 doc下建立一个feature文档... step2 doc下建立testcase文档... step3 根据feature和testcase文档构造用例... step4 反标到feature和testcase文档... step5 调试所有的用例... step6 构造一键回归的脚本... step7 统计覆盖率... step7 提交与推送。

### 拆解需求

| 子需求 | 内容 | 影响范围 | 状态 |
|--------|------|----------|------|
| REQ-20260601-002-A | 调研公开 cache 验证方法并编写验证方案 | `gpgpusim_cache/doc/verification/` | 已完成 |
| REQ-20260601-002-B | 建立 gpgpusim_cache feature 分解文档 | `gpgpusim_cache/doc/verification/feature_matrix.md` | 已完成 |
| REQ-20260601-002-C | 建立 testcase 说明和 feature 反标矩阵 | `gpgpusim_cache/doc/verification/testcase_matrix.md` | 已完成 |
| REQ-20260601-002-D | 修复原测试框架断言失败假通过问题 | `gpgpusim_cache/test/test_main.cc` | 已完成 |
| REQ-20260601-002-E | 新增 deep whitebox 测试覆盖配置、地址、tag、MSHR、写策略、texture、stats、DataStore、property trace | `gpgpusim_cache/test/test_cache_whitebox.cc` | 已完成 |
| REQ-20260601-002-F | 将新增用例纳入一键回归脚本和 CMake | `gpgpusim_cache/run.sh`, `gpgpusim_cache/CMakeLists.txt` | 已完成 |
| REQ-20260601-002-G | 增加覆盖率统计脚本并记录覆盖率基线 | `gpgpusim_cache/coverage.sh`, `gpgpusim_cache/doc/verification/verification_plan.md` | 已完成 |
| REQ-20260601-002-H | 组织架构、验证、项目质量多 agent 检视并吸收结论 | `gpgpusim_cache/doc/verification/` | 已完成 |

### 验收标准

1. `gpgpusim_cache/doc/verification/verification_plan.md` 存在。
2. `gpgpusim_cache/doc/verification/feature_matrix.md` 存在，feature 反标到 testcase。
3. `gpgpusim_cache/doc/verification/testcase_matrix.md` 存在，testcase 反标到 feature 和测试文件。
4. `gpgpusim_cache/test/test_cache_whitebox.cc` 纳入默认回归。
5. `gpgpusim_cache/run.sh` 一键回归通过。
6. `gpgpusim_cache/coverage.sh` 能生成覆盖率摘要。
7. 回归和覆盖率结果记录在交付说明中。

### 状态

已完成。

### 更新时间

2026-06-01
