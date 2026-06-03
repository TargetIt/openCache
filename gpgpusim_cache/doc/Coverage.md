# GPGPU-Sim Cache 覆盖率与极限水位测试报告 (2026-06-02)

## 测试执行汇总

- **GPGPU-Sim Cache Reference Test Suite**: 13/13 测试通过
- **Scenario Integration Tests**: 10 个集成场景全数通过 (86 checks, 0 failed)
- **Deep Whitebox Test Suite**: 44/44 测试通过 

## 极限拥堵场景压测 (Extreme Congestion Tests)
根据**“拥堵构造法” (Congestion Engineering)** 的方案，我们新增了 4 个极限测试用例，刻意卡死下级 Memory 接口与本级 Ready 取出接口，将各类硬件表项推至极限：
- `extreme_mshr_merge_refcount_spike`: 验证 MSHR 合并 64 项后，在同一周期 Fill 时瞬间带来的极大并发 Ready，确保 `refcount` 安全达到 `64`。
- `extreme_hit_queue_ready_queue_congestion`: 在极低带宽配置下压入 64 个 Hit，使其在 Hit Queue 排队并阻塞在 Ready Queue 中。
- `extreme_miss_queue_backpressure`: 锁死下级 Memory，发射 64 个不同地址的 Miss，确保其打满 64 项 Miss Queue，并在第 65 个请求时成功触发背压机制。
- `extreme_texture_fifo_congestion`: 压入 64 个 Texture Access 并阻塞取出端，验证其内部级联 FIFO 的峰值情况。

### 运行态实测物理最高水位 (Empirical Absolute Maximums)
以下数据为真实运行期探针所抓取到的历史全局最大值：
```text
================================================================
>>> [EMPIRICAL LIMITS] Absolute Maximums Reached Across All Tests:
>>> Max REF Counter (line_refcount): 64
>>> Max Hit Response Queue Size: 64
>>> Max Miss Queue Size: 64
>>> Max Ready Response Queue Size: 64
================================================================
```
各项数据精准对齐配置上限，证明：
1. 底层数据结构不存在潜藏的小数值溢出或截断风险。
2. 拥堵产生时，相关 Backpressure 机制可以有效保护整个模型不崩溃。

## 覆盖率基线数据

| 文件名 | 代码行覆盖率 (Line Cover) | 分支覆盖率 (Branch Cover) | 函数覆盖率 (Func Cover) |
| :--- | :--- | :--- | :--- |
| `gpu_cache_ref.cc` | 69.36% (1209 / 1743) | 58.22% (535 / 919) | 75.00% (87 / 116) |
| `gpu_cache_ref.h` | 74.57% (686 / 920) | 73.44% (188 / 256) | 74.46% (137 / 184) |
| **总计 (TOTAL)** | **71.16%** (1895 / 2663) | **61.53%** (723 / 1175) | **74.67%** (224 / 300) |