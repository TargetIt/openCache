# gpgpusim_cache Bug Record

更新时间：2026-06-02

本文档记录 `gpgpusim_cache` 验证过程中发现的缺陷、发现方法、根因、解决方法和验证结果。后续新增 bug 继续追加，已解决问题保留记录。

## BUG-20260602-001 final check 发现 write token 被测试 helper 误当成 fill response

### 状态

已解决。

### 发现方法

在白盒测试中把 final check 从专项用例升级为对象级 RAII guard：每个直接创建的 `baseline_cache` 派生对象、`tex_cache`、`tag_array`、`mshr_table` 在用例退出时自动执行 drain，并在 invalidate 前检查 queue/FIFO/MSHR/pending ref/refcount 是否已经自然清空。

执行命令：

```bash
./run.sh
```

失败出现在 deep whitebox 测试 `sequence_same_line_hit_miss_matrix` 收尾阶段。guard 调用 `drain_one_level()` 继续清理 pending 状态时触发断言：

```text
Assertion failed: (e != m_extra_mf_fields.end()), function fill, file gpu_cache_ref.cc, line 1353.
```

### 具体现象

`sequence_same_line_hit_miss_matrix` 中 write-evict/write-through 类路径会向下游 memory interface 发送 write token。原测试 helper `drain_one_level()` 会把 `simple_mem_interface::queue` 里的所有 token 都当成 cache fill response 调用 `cache.fill(resp, cycle)`。

write token 本身不是 miss fill response，因此它不在 `baseline_cache::m_extra_mf_fields` 中。被误传给 `fill()` 后，`fill()` 查找 `m_extra_mf_fields.find(mf)` 失败并触发断言。

### 根因

测试 helper 对外部 memory queue 中 token 的语义区分不够：

- miss read token 返回时可以作为 fill response 调用 `cache.fill()`；
- write-through/write-evict/writeback 产生的 write token 只是下游发送请求，不是当前 cache 等待的 fill response。

原 helper 缺少 `cache.waiting_for_fill(resp)` 判断，导致把非 fill token 误送入 fill 路径。

### 解决方法

修改 `test/test_cache_whitebox.cc` 的 `drain_one_level()`：

```cpp
while (!mem.queue.empty()) {
    mem_fetch *resp = mem.queue.front();
    mem.queue.pop_front();
    if (cache.waiting_for_fill(resp))
        cache.fill(resp, cycle + step);
}
```

只有 cache 确认正在等待该 token fill 时才调用 `fill()`；否则该 token 被视为已经发送到下游的 write/request token，只从测试 memory queue 中移除，不再误入 fill 路径。

### 验证结果

修复后重新执行：

```bash
./run.sh
```

结果通过：

- unit：`13/13`
- scenario：`86/86 checks`
- whitebox：`41/41`
- death：`16/16`

## BUG-20260602-002 final check 发现 tag_array::invalidate() 早退导致 line 未回初始态

### 状态

已解决。

### 发现方法

同样通过对象级 RAII final guard 发现。`tag_array` 白盒对象在用例退出时执行：

1. `tag_array::no_pending_accesses()`，在 invalidate 前确认无 pending/refcount；
2. `tag_array::invalidate()`，作为测试 cleanup；
3. `tag_array::final_state_clean()`，确认 cleanup 后回到初始态。

执行命令：

```bash
./run.sh
```

失败出现在 deep whitebox 测试 `tag_on_fill_all_reserved` 的收尾阶段：

```text
CHECK_TRUE(m_tags.final_state_clean()) at test/test_cache_whitebox.cc:267
```

### 具体现象

`tag_on_fill_all_reserved` 直接使用 `tag_array::fill()` 构造 line 状态。用例结束时 final guard 在确认无 pending/refcount 后调用 `tag_array::invalidate()` 做 cleanup，随后检查 `final_state_clean()`。

检查失败说明至少有 line 没有回到 `INVALID` 或仍处于非初始状态。

### 根因

`tag_array::invalidate()` 原实现包含早退逻辑：

```cpp
void tag_array::invalidate() {
  if (!is_used) return;
  ...
}
```

部分白盒路径直接调用 `fill()` 改变 line 状态，但没有把 `is_used` 置为 true。此时 `invalidate()` 因 `is_used == false` 直接返回，实际有效 line 没有被 invalid，导致 final state check 失败。

从接口语义看，`invalidate()` 应该无条件让当前 tag array 的所有 line 回到 invalid 初始态，不应该依赖 `is_used` 作为是否执行清理的条件。

### 解决方法

修改 `gpgpu_cache/gpu_cache_ref.cc`，去掉 `tag_array::invalidate()` 的早退：

```cpp
void tag_array::invalidate() {
  for (unsigned i = 0; i < m_config.get_num_lines(); i++)
    for (unsigned j = 0; j < SECTOR_CHUNCK_SIZE; j++)
      m_lines[i]->set_status(INVALID, mem_access_sector_mask_t().set(j));

  m_dirty = 0;
  is_used = false;
}
```

这样即使白盒测试或未来调用方通过非 `access()` 路径修改 line 状态，`invalidate()` 仍能可靠恢复初始状态。

### 验证结果

修复后重新执行：

```bash
./run.sh
./coverage.sh coverage-refcount-watermark
```

结果通过：

- unit：`13/13`
- scenario：`86/86 checks`
- whitebox：`41/41`
- death：`16/16`
- coverage total：Region `57.63%`，Function `75.84%`，Line `71.80%`，Branch `61.41%`
