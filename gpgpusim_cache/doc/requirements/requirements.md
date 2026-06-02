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

1. `./run.sh` 通过：unit 13/13，scenario 86/86 checks，whitebox 19/19，death 6/6。
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
| Region | 54.10% |
| Function | 71.15% |
| Line | 68.59% |
| Branch | 58.60% |

### 更新时间

2026-06-01
