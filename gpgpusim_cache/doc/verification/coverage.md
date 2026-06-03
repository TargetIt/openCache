# gpgpusim_cache 覆盖率

执行日期：2026-06-03

执行命令：

```bash
./coverage.sh coverage
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 62.25% | 82.76% | 79.92% | 66.27% |
| `gpu_cache_ref.h` | 64.18% | 78.26% | 79.21% | 73.44% |
| **Total** | **62.80%** | **80.00%** | **79.68%** | **67.83%** |

Empirical Limits（所有极端测试达成的绝对最大值）：

| 度量 | 最大值 | 测试来源 |
|------|--------|----------|
| line_refcount | 64 | pure miss (MSHR merge) / pure hit (defer) / mixed |
| hit_response_queue | 64 | pure hit (deferred hit response) |
| miss_queue | 64 | pure miss (distinct addrs backpressure) |
| ready_response_queue | 64 | pure hit / mixed (fill → ready drain) |
| fragment_fifo | 64 | pure hit / pure miss / mixed (texture cache) |
| result_fifo | 64 | pure hit (texture cache full drain) |

测试矩阵：极端覆盖率测试 10 个（3 pure miss + 2 pure hit + 4 mixed + 1 texture）

- 116 个 whitebox 测试全部通过
- 13 个 unit 测试全部通过
- 10 个 scenario 全部通过（86 checks）
- 16 个 death 测试全部通过

离线 coverage.sh 使用 llvm-profdata / llvm-cov 合并 unit、scenario、whitebox 三个 binary 的 profile。llvm-cov 报告 `functions have mismatched data`（来自不同 binary 的函数签名为 normalize 为通用 profile 时的已知合并差异），覆盖率表仍来自三类 profile 合并结果。
