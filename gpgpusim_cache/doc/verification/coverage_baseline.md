# gpgpusim_cache 覆盖率基线

执行日期：2026-06-01

执行命令：

```bash
./coverage.sh
```

结果：

| 文件 | Region | Function | Line | Branch |
|------|--------|----------|------|--------|
| `gpu_cache_ref.cc` | 43.72% | 64.21% | 55.91% | 46.52% |
| `gpu_cache_ref.h` | 52.93% | 63.19% | 63.16% | 61.11% |
| Total | 46.42% | 63.57% | 58.48% | 49.90% |

## 结论

当前覆盖率作为新增验证体系的首个基线，尚未达到长期质量目标。已经覆盖默认 unit、scenario 和 deep whitebox 回归；后续迭代应重点提升：

1. 非法配置 death tests。
2. 更完整的 sector cache 状态组合。
3. texture cache FIFO/ROB/result FIFO 极限组合。
4. data/fill port timing 精确断言。
5. hash function golden 对照。
6. 更长 seed 集的 differential/property trace。
