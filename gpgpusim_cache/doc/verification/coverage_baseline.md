# gpgpusim_cache 覆盖率基线

执行日期：2026-06-02

执行命令：

```bash
./coverage.sh coverage-final-guards
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 54.17% | 73.83% | 68.60% | 57.08% |
| `gpu_cache_ref.h` | 62.56% | 74.44% | 74.10% | 72.80% |
| Total | 56.79% | 74.48% | 70.64% | 60.57% |

## 结论

当前覆盖率在默认新模式和白盒用例 final guard 实现后继续小幅提升，但尚未达到长期质量目标。已经覆盖默认 unit、scenario 和 deep whitebox 回归；后续迭代应重点提升：

1. 更完整的 SECTOR_TEX_FIFO pending_read 组合。
2. texture cache 更多 result FIFO 长序列和跨请求 backpressure 组合。
3. data/fill port timing 精确断言。
4. 更长、更复杂读写混合的 differential/property trace。
5. 更高 branch coverage 的 directed 异常路径和状态组合。

## 工具诊断

`llvm-cov` 在合并 unit、scenario、whitebox 三个测试 binary 的 profile 时会报告 `functions have mismatched data`。`coverage.sh` 已将原始诊断保存到 `coverage-final-guards/coverage-warnings.txt`，并在 `coverage-summary.txt` 末尾写入说明；覆盖率表仍来自三类测试 profile 的合并结果。
