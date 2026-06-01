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

