# OpenCache QA 规范入口

`qa` 目录记录 OpenCache 项目的运作规范、需求管理、文档管理、交付流程和质量门禁。

## 文件说明

| 文件 | 用途 |
|------|------|
| `process.md` | 项目工作流：需求进入、拆解、实现、验证、交付 |
| `quality-gate.md` | 质量门禁：交付前必须满足的检查项 |
| `templates/requirement-entry.md` | 新需求记录模板 |
| `templates/delivery-report.md` | 交付记录模板 |

## 核心原则

1. 需求必须有记录，不能只停留在口头或聊天中。
2. 文档必须进入 `doc` 目录，版本文档进入对应版本的 `doc`。
3. 需求变更必须刷新 requirements 文档。
4. 代码或行为变化必须回归所有相关用例。
5. 交付说明必须写清楚变更范围、验证结果和未完成风险。

