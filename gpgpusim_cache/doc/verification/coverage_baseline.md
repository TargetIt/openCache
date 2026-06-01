# gpgpusim_cache 覆盖率基线

执行日期：2026-06-01

执行命令：

```bash
./coverage.sh
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 49.13% | 67.71% | 64.03% | 53.00% |
| `gpu_cache_ref.h` | 60.49% | 70.73% | 72.35% | 70.94% |
| Total | 52.44% | 69.62% | 66.97% | 57.13% |

## 结论

当前覆盖率作为新增验证体系的首个基线，尚未达到长期质量目标。已经覆盖默认 unit、scenario 和 deep whitebox 回归；后续迭代应重点提升：

1. 更完整的 SECTOR_TEX_FIFO pending_read 组合。
2. texture cache 多请求返回顺序和 result FIFO 长序列。
3. data/fill port timing 精确断言。
4. 更长、更复杂读写混合的 differential/property trace。
5. 更高 branch coverage 的 directed 异常路径和状态组合。

## 工具诊断

`llvm-cov` 在合并 unit、scenario、whitebox 三个测试 binary 的 profile 时会报告 `functions have mismatched data`。`coverage.sh` 已将原始诊断保存到 `coverage/coverage-warnings.txt`，并在 `coverage-summary.txt` 末尾写入说明；覆盖率表仍来自三类测试 profile 的合并结果。
