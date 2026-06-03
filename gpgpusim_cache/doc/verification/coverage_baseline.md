# gpgpusim_cache 覆盖率基线

执行日期：2026-06-02

执行命令：

```bash
./coverage.sh coverage-refcount-watermark
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 55.23% | 75.44% | 69.83% | 58.01% |
| `gpu_cache_ref.h` | 63.58% | 76.09% | 75.54% | 73.44% |
| Total | 57.63% | 75.84% | 71.80% | 61.41% |

## 结论

当前覆盖率在默认新模式、白盒用例 final guard、refcount/queue/FIFO watermark 统计断言实现后继续提升，但尚未达到长期质量目标。已经覆盖默认 unit、scenario 和 deep whitebox 回归；后续迭代应重点提升：

1. 更完整的 SECTOR_TEX_FIFO pending_read 组合。
2. texture cache 更多 result FIFO 长序列和跨请求 backpressure 组合。
3. data/fill port timing 精确断言。
4. 更长、更复杂读写混合的 differential/property trace。
5. 更高 branch coverage 的 directed 异常路径和状态组合。

## 工具诊断

`llvm-cov` 在合并 unit、scenario、whitebox 三个测试 binary 的 profile 时会报告 `functions have mismatched data`。`coverage.sh` 已将原始诊断保存到 `coverage-refcount-watermark/coverage-warnings.txt`，并在 `coverage-summary.txt` 末尾写入说明；覆盖率表仍来自三类测试 profile 的合并结果。
