# OpenCache 项目流程

## 1. 需求进入

收到新需求后，先判断影响范围：

| 影响范围 | 记录位置 |
|----------|----------|
| 项目整体、流程、QA、跨版本能力 | `doc/requirements/requirements.md` |
| gem5 版本 | `gem5_cache/doc/requirements/requirements.md` |
| GPGPU-Sim 版本 | `gpgpusim_cache/doc/requirements/requirements.md` |
| 已废弃代码或历史资料 | `deprecated/` 下对应文档，必要时在项目级需求中说明 |

每条需求必须保留原始需求，并拆解成可执行条目。

## 2. 需求拆解

拆解需求时必须明确：

1. 要改哪些目录或模块。
2. 是否影响两个版本的一致性。
3. 是否需要更新用户文档、接口文档或示例。
4. 需要新增、修改或回归哪些测试。
5. 验收标准是什么。

## 3. 实现流程

1. 先读现有代码和文档，确认当前行为。
2. 小步修改，避免无关重构。
3. 如果移动文档，必须同步更新索引和引用。
4. 如果修改代码，必须同时考虑 gem5 和 GPGPU-Sim 两个版本是否需要同步。
5. 如果发现旧实现已经废弃，移入或保留在 `deprecated/`，不要和当前版本混放。

## 4. 验证流程

交付前至少执行：

```bash
./gem5_cache/run.sh
./gpgpusim_cache/run.sh
```

如果只修改文档，也应运行回归脚本，除非明确记录原因，例如本机缺少编译器或测试环境不可用。

## 5. 交付流程

交付说明必须包含：

1. 本次完成了什么。
2. 修改了哪些关键文件。
3. 运行了哪些验证命令，结果如何。
4. 有哪些未解决问题或风险。

交付格式参考 `qa/templates/delivery-report.md`。

## 6. 需求刷新规则

当用户提出新需求、调整原需求、废弃某需求或改变验收标准时，必须刷新：

1. `doc/requirements/requirements.md`
2. 受影响版本的 `doc/requirements/requirements.md`
3. 必要时刷新 `qa/process.md` 或 `qa/quality-gate.md`

需求文档中的状态必须与实际交付状态一致。

