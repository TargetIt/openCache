# gpgpusim_cache 覆盖率

执行日期：2026-06-03

执行命令：

```bash
./coverage.sh coverage
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 55.64% | 75.00% | 69.36% | 58.54% |
| `gpu_cache_ref.h` | 63.13% | 74.46% | 74.57% | 73.44% |
| **Total** | **57.78%** | **74.67%** | **71.16%** | **61.79%** |

Empirical Limits（所有极端测试达成的绝对最大值）：

| 度量 | 最大值 | 测试来源 |
|------|--------|----------|
| line_refcount | 64 | pure miss (MSHR merge) / pure hit (defer) / mixed |
| hit_response_queue | 64 | pure hit (deferred hit response) |
| miss_queue | 64 | pure miss (distinct addrs backpressure) |
| ready_response_queue | 64 | pure hit / mixed (fill → ready drain) |
| fragment_fifo | 64 | pure hit / pure miss / mixed (texture cache) |
| result_fifo | 64 | pure hit (texture cache full drain) |

测试矩阵：7 种结构 x 3 种场景（pure miss / pure hit / mixed）

- 49 个 whitebox 测试全部通过
- 13 个 unit 测试全部通过
- 10 个 scenario 全部通过（86 checks）
- 16 个 death 测试全部通过

离线 coverage.sh 使用 llvm-profdata / llvm-cov 合井 unit、scenario、whitebox 三个 binary 的 profile。llvm-cov 报告 `functions have mismatched data`（来自不同 binary 的函数签名为 normalize 为通用 profile 时的已知合并差异），覆盖率表仍来自三类 profile 合并结果。
