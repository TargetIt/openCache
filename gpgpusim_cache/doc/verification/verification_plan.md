# gpgpusim_cache 验证方案

## 目标

本验证方案面向 `gpgpusim_cache` 的零缺陷取向验证。目标不是只证明样例能运行，而是系统性覆盖用户集成路径、公开接口、内部状态机、异常路径、统计路径和回归流程。

## 参考方法

调研和借鉴的公开 cache 验证思路：

| 来源 | 可借鉴方法 | 本项目采用方式 |
|------|------------|----------------|
| gem5 Classic Caches: https://www.gem5.org/documentation/general_docs/memory_system/classic_caches/ | 将 cache 分解为 tags、replacement、indexing、MSHR、write queue 等可验证组件 | 把 `tag_array`、replacement、set index、MSHR 拆成独立 feature 和 testcase |
| gem5 Replacement Policies: https://www.gem5.org/documentation/general_docs/memory_system/replacement_policies/ | 将 reset/touch/invalidate/getVictim 语义拆成可观测行为 | 针对 LRU/FIFO、invalid-first、flush/invalidate 构造白盒测试 |
| gem5 Indexing Policies: https://www.gem5.org/documentation/general_docs/memory_system/indexing_policies/ | 将 set index function 作为独立风险点 | 覆盖范围、差异和选定地址 golden 值 |
| GPGPU-Sim cache 模型 | 数据 cache、texture cache、MSHR、sector cache、write policy 与事件模型 | 按 GPGPU-Sim 的配置字符串、写策略、sector/texture 管线构造白盒测试 |
| ChampSim: https://github.com/ChampSim/ChampSim 与 https://champsim.github.io/ChampSim/ | 用固定 trace 和固定 seed 的回归保护替换策略与命中率 | 增加固定 seed 的 deterministic trace/property 测试 |
| 经典 cache 测试方法 | compulsory/capacity/conflict miss、LRU/FIFO victim、write-through/write-back、flush/invalidate、backpressure | 按 feature 建立 directed tests、边界 tests、随机 tests 和场景集成 tests |

## 验证范围

| 层级 | 范围 |
|------|------|
| 配置层 | `cache_config` 配置串、cache type、replacement、write policy、allocation policy、write allocate、MSHR type、set index function、非法配置 |
| 地址映射层 | `tag()`、`block_addr()`、`mshr_addr()`、`set_index()`、linear/xor/ipoly/Fermi/custom 选择 |
| block 层 | `line_cache_block`、`sector_cache_block` 的 `INVALID/RESERVED/VALID/MODIFIED`、dirty byte/sector mask、readable、fill flags |
| tag 层 | `tag_array::probe/access/fill/flush/invalidate/new_window`，包含 LRU/FIFO、all reserved、dirty writeback、sector miss |
| MSHR 层 | entry 上限、merge 上限、ready 顺序、RAW pending、atomic 标记边界 |
| cache 子类层 | `read_only_cache`、`data_cache`、`l1_cache`、`l2_cache`、`tex_cache` |
| pipeline 层 | `access -> cycle -> memport -> fill -> next_access`，data/fill port bandwidth |
| functional 层 | `DataStore` 与 timing cache 分离、fill 后数据一致性 |
| stats 层 | `cache_stats`、fail stats、per-window stats、port utilization |
| 回归层 | 一键构建、运行全部 unit/scenario/whitebox/property 测试、覆盖率统计 |

## 验证策略

1. Directed whitebox tests：对每个状态机和策略路径构造精确输入，断言返回状态、事件、统计和内部 block 状态。
2. Scenario integration tests：保留用户视角的 L1/L2、multi-L1 shared L2、DataStore 双模型示例，但强化断言。
3. Property tests：固定 seed 生成地址和读写序列，校验不变量，例如稳定复现、访问数统计、miss+hit 分类一致。
4. Negative/death tests：对非法配置和 assert 路径使用子进程隔离，纳入默认回归，避免 abort 中断主测试进程。
5. Regression script：默认一键跑全部非 death 测试；覆盖率脚本在工具可用时生成报告。

## 多角色检视结论

| 角色 | 关键意见 | 处理 |
|------|----------|------|
| 架构 | 必须覆盖配置矩阵、tag/sector 状态、MSHR、写策略、texture FIFO、bandwidth、stub 边界 | 纳入 `feature_matrix.md` |
| 验证 | 当前测试框架失败会假通过，必须先修；必须补精确断言、事件测试、随机测试 | 修复 `test_main.cc` 框架并新增专项测试 |
| 项目质量 | 每条需求必须追踪到文档、测试、回归结果；交付前必须更新 requirements 和 QA 证据 | 更新 requirements、feature/testcase 反标 |

## 质量门禁

| 门禁 | 要求 |
|------|------|
| 测试框架 | 任一断言失败必须导致对应测试失败，并导致进程非 0 退出 |
| 默认回归 | `./run.sh` 必须构建并运行全部默认测试，退出码为 0 |
| Feature 追踪 | 每条 feature 必须映射到 testcase；每条 testcase 必须映射到测试文件或标记为 planned |
| 覆盖率 | 如果 `llvm-profdata`/`llvm-cov` 可用，生成行/函数/分支覆盖率报告；若不可用，记录原因 |
| 文档 | 修改测试或需求后必须同步 `requirements`、feature、testcase 文档 |

## 当前覆盖率基线

2026-06-01 执行 `./coverage.sh`，结果：

| 指标 | 覆盖率 |
|------|--------|
| Region | 51.40% |
| Function | 69.62% |
| Line | 66.27% |
| Branch | 55.85% |

当前覆盖率未达到长期目标。原因是本阶段优先补齐默认回归、白盒关键路径、feature/testcase 追踪、death test 和覆盖率基础设施；更多 sector/texture backpressure、更多 hash golden 地址和随机 differential 长跑仍需后续迭代。覆盖率报告由 `gpgpusim_cache/coverage.sh` 生成，提交中固化的基线见 `coverage_baseline.md`。death tests 通过子进程验证 abort/assert 路径，默认不计入 coverage，以避免 abort profile 造成覆盖率文件不稳定。

## 迭代准则

如果发现以下情况，必须回到 step0 到 step7 迭代：

1. 回归失败。
2. feature 无 testcase 映射。
3. testcase 无实现或 planned 原因不充分。
4. 覆盖率工具显示关键路径未覆盖。
5. 新发现 bug 或需求未写入 requirements。
