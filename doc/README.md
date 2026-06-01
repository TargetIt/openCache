# OpenCache 文档中心

本目录是 OpenCache 项目的项目级文档入口。项目下所有说明、需求、流程和交付资料都应放在 `doc` 或对应版本目录的 `doc` 中。

## 文档结构

| 路径 | 用途 |
|------|------|
| `doc/requirements/` | 项目级需求台账，记录原始需求、拆解需求、变更历史和验收标准 |
| `gem5_cache/doc/` | gem5 版本的版本说明、用户手册和版本级需求 |
| `gpgpusim_cache/doc/` | GPGPU-Sim 版本的版本说明、用户手册和版本级需求 |
| `qa/` | 项目运作规范、质量门禁、交付模板和流程要求 |

## 文档维护规则

1. 新需求先写入 `doc/requirements/requirements.md`，保留用户原始表述。
2. 将原始需求拆解为可执行需求，并补充验收标准。
3. 如果需求影响某个版本，在对应版本的 `doc/requirements/requirements.md` 中同步记录。
4. 任何代码或文档交付前，必须按 `qa/quality-gate.md` 执行检查。
5. 文档改名或迁移时必须更新本索引和相关链接。

