# gpgpusim_cache 覆盖率基线

执行日期：2026-06-01

执行命令：

```bash
./coverage.sh
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 48.33% | 67.71% | 63.13% | 52.36% |
| `gpu_cache_ref.h` | 59.19% | 70.73% | 72.00% | 68.80% |
| Total | 51.49% | 69.62% | 66.27% | 56.15% |

## 结论

当前覆盖率作为新增验证体系的首个基线，尚未达到长期质量目标。已经覆盖默认 unit、scenario 和 deep whitebox 回归；后续迭代应重点提升：

1. 更完整的 SECTOR_TEX_FIFO pending_read 组合。
2. texture cache 多请求返回顺序和 result FIFO 长序列。
3. data/fill port timing 精确断言。
4. 更长、更复杂读写混合的 differential/property trace。
5. coverage 采集方式需继续处理 `llvm-cov` profile mismatch 警告。
