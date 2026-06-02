# gpgpusim_cache 设计文档

## Hit Response 延迟返回与 Cache Line Pin 设计

### 需求背景

当前 `read_only_cache` 和 `data_cache` 在 tag hit 时，`access()` 直接返回 `HIT`，调用方通常认为数据已经可用。miss 路径则不同：`access()` 返回 `MISS` 只表示请求被接收；下级 `fill()` 完成后，调用方通过 `access_ready()` 和 `next_access()` 获取完成请求。

新需求要求 data/read-only cache 的 hit 也拆成两个阶段：

1. `access()` 完成 tag hit/miss 判定和请求接收。
2. SRAM/data array 读延迟结束后，请求再通过 `access_ready()` / `next_access()` 返回。

这个改变会引入新的生命周期风险：hit 请求已经被接收但数据尚未被上层读走时，该 cache line 不能被 replacement、conflict miss 或其他分配动作替换。

### 核心设计结论

必须引入 cache line pin/refcount 机制。

原因：

1. 现有 `RESERVED` 状态只保护 miss 分配后、fill 之前的 line。
2. hit 延迟返回时，line 已经是 `VALID` 或 `MODIFIED`，不是 `RESERVED`。
3. replacement 逻辑当前只跳过 reserved line，不会跳过“已有未完成 hit response”的 valid line。
4. 如果不 pin，可能出现 hit 已接收但尚未 `next_access()`，随后同 set miss 把该 line 选为 victim 的错误。

### Refcount 语义

每条 cache line 维护一个 pending response refcount。

| 时机 | 操作 |
|------|------|
| hit 请求被 cache 接收并进入 hit response queue | refcount++ |
| miss 请求被 cache 接收并最终需要从该 line 返回 | refcount++ |
| MSHR merge 接收同 line 多个请求 | 每个 accepted request 都 refcount++ |
| `next_access()` 返回一个请求给上层 | 对应 line refcount-- |
| refcount 不为 0 | line 不能被 replacement 选为 victim |

推荐命名：

```cpp
unsigned m_pending_response_refcount;
```

或者在 sector cache 中保持 line-level pin：

```cpp
unsigned m_line_pending_response_refcount;
```

### Line-Level Pin 优先

第一阶段采用 line-level refcount，不做 sector-level refcount。

理由：

1. replacement 替换的是整条 cache line。
2. line-level pin 能安全保护所有 sector 的未完成 response。
3. sector-level pin 虽然更精细，但容易引入 partial sector 生命周期和 dirty/readable 状态交织风险。

sector cache 中，只要任意 sector 有 pending response，整条 line 都不可被 replacement 选中。

### 与 RESERVED 的关系

`RESERVED` 和 refcount 解决不同问题：

| 机制 | 保护阶段 | 当前是否已有 |
|------|----------|--------------|
| `RESERVED` | miss 分配后、fill 前 | 已有 |
| refcount/pin | 请求已接收、数据尚未 `next_access()` 返回前 | 需要新增 |

miss 路径中，fill 前由 `RESERVED` 保护；fill 后如果 response 尚未被上层读走，由 refcount 继续保护。

### Replacement 规则

`tag_array::probe()` 选择 victim 时必须跳过 pinned line。

重要约束：victim 选择必须先判断 `refcount == 0`，再考虑 `INVALID`、`VALID`、`MODIFIED` 等状态。即使某条 line 已被标为 `INVALID`，只要 refcount 不为 0，也不能被复用。

新的 candidate 条件：

```text
line 非 RESERVED
line refcount == 0
line 满足 dirty victim 策略
```

如果同 set 内所有 line 都 reserved 或 pinned，则返回 `RESERVATION_FAIL`。

fail reason 建议新增：

```cpp
LINE_PINNED_FAIL
```

如果短期不新增 fail reason，也必须在测试和文档中说明复用 `LINE_ALLOC_FAIL` 的原因。但从可诊断性看，推荐新增 `LINE_PINNED_FAIL`。

### Hit 路径

read-only hit：

1. `access()` tag probe 得到 `HIT`。
2. 更新 LRU。
3. `pin(cache_index)`。
4. 请求进入 hit response queue。
5. `access()` 返回 `HIT`。
6. 延迟结束后请求进入 ready queue。
7. `next_access()` 返回请求并 `unpin(cache_index)`。

data read hit：

1. 保持 `rd_hit_base()` 的 tag/LRU/atomic 行为。
2. `pin(cache_index)`。
3. 请求进入 hit response queue。
4. `next_access()` 时 unpin。

data write hit：

1. 保持 write policy 的 tag/dirty/event 行为。
2. 如果该写请求需要完成 token，进入 hit response queue 并 pin。
3. `next_access()` 时 unpin。

### Miss 路径

miss accepted：

1. `tag_array::access()` 分配 line 并置 `RESERVED`。
2. 对每个 accepted miss request 记录其目标 cache index 或 block addr。
3. `pin(cache_index)`。
4. 下级 fill 到达后，line 从 `RESERVED` 变为 `VALID/MODIFIED`。
5. 请求进入 ready queue。
6. 每次 `next_access()` 返回一个 merged request 时 `unpin(cache_index)`。

MSHR merge 注意事项：

1. 同一个 MSHR entry 可能有多个 `mem_fetch`。
2. 每个 accepted `mem_fetch` 都代表一个未来 response。
3. refcount 必须按 accepted request 数增加，而不是按 line 或 MSHR entry 只增加一次。

### Ready Queue 设计

推荐统一 ready queue：

```cpp
struct ready_response_entry {
  mem_fetch *mf;
  unsigned cache_index;
};

std::list<ready_response_entry> m_ready_response_queue;
```

`next_access()` 从统一 ready queue 弹出，并执行：

```text
entry = pop_front()
unpin(entry.cache_index)
return entry.mf
```

这样 hit response 和 miss fill response 的 unpin 逻辑一致。

### Flush/Invalidate/Write-Evict 规则

必须定义 pinned line 遇到管理操作时的行为。

第一阶段建议：

| 操作 | pinned line 行为 |
|------|------------------|
| replacement victim | 禁止选择 |
| conflict miss 分配 | 如果候选都 pinned，返回 `RESERVATION_FAIL` |
| write-evict hit | 该请求自身进入 response queue 后，line 状态可按策略 invalidate，但 refcount 检查必须优先于 `INVALID` victim 选择 |
| explicit `invalidate()` | 作为强制管理操作允许执行，但测试需确认不会破坏 pending token exactly-once 返回 |
| explicit `flush()` | dirty 写回语义保持；pinned line 不应被普通 replacement 清掉 |

如果后续要求更严格，可以规定 explicit invalidate/flush 对 pinned line 返回失败或延迟执行；这需要新增接口，第一阶段不建议。

write-evict hit 有两种可选实现：

| 方案 | 说明 | 取舍 |
|------|------|------|
| 立即 invalidate，但保持 pin | write-evict hit 立即将 line 置为 `INVALID`，但 replacement 必须先检查 refcount，pinned invalid line 不可复用 | 保持 write-evict 可观测语义，要求 victim 选择严格 |
| 延迟 invalidate 到 `next_access()` | write-evict hit token 被读走后再 invalidate | 生命周期更直观，但改变 invalidate 可观测时刻 |

推荐第一阶段采用“立即 invalidate，但保持 pin”。该方案要求测试覆盖 pinned invalid line 不可作为 victim。

### Hit Response Queue 容量

hit response queue 不建议长期无界。

无界队列虽然降低第一版实现复杂度，但会掩盖真实背压。如果上层长时间不调用 `next_access()`，accepted hit 会无限堆积，既不符合 GPU cache 时序模型，也可能造成仿真器内存增长。

推荐第一阶段即引入有限容量：

```cpp
unsigned m_hit_response_queue_size;
```

容量满时：

1. 新 hit 返回 `RESERVATION_FAIL`。
2. fail reason 优先使用新增 `HIT_RESPONSE_QUEUE_FULL`。
3. 如果不新增 fail reason，必须在实现说明中明确复用哪个现有 fail reason，并补测试。

如果为了降低第一版风险临时使用无界队列，必须把 bounded queue 作为遗留问题保留，且不能关闭 `TC-HITLAT-011` 的 Planned 状态。

### DataStore 快照语义

第一阶段只改变 timing token 的完成时机，不改变 payload 读取语义。

如果后续实现引入真实数据 payload，需要显式规定读快照时机。推荐语义是：tag hit 被接受时读取 SRAM/DataStore 快照，token 在后续 cycle 交付；即使延迟期间其他写入更新同地址，该已经在 pipeline 中的读请求仍返回旧快照。

该语义符合硬件流水线直觉：

```text
cycle N:   hit 被接受，SRAM 读启动并捕获数据快照
cycle N+k: token 通过 next_access() 返回
```

这不是 cache coherence 模型；如果后续要验证跨核写入可见性，需要单独建立一致性需求和 testcase。

### 评审风险与处理策略

| 风险 | 合理性 | 处理策略 |
|------|--------|----------|
| write-evict hit 后 line 立即 invalid，但 pending response 尚未返回 | 合理，高优先级 | replacement 必须先检查 refcount；pinned invalid line 不可复用；新增测试覆盖 |
| 无界 hit response queue 掩盖背压 | 合理，中高优先级 | 第一阶段建议有限容量；容量满返回 `RESERVATION_FAIL`；新增 backpressure 测试 |
| DataStore 快照与延迟返回存在可见性时差 | 合理，但非阻塞 | 第一阶段只处理 timing token；未来 payload 语义采用“读出即快照”并单独测试 |

### 统计与诊断

`HIT` 仍在 `access()` 接收阶段计入 hit。refcount 不改变 hit/miss 统计口径。

建议新增诊断统计：

| 统计项 | 说明 |
|--------|------|
| `hit_response_pending` | 当前 pending hit response 数 |
| `line_pinned_fail` | 因 line pinned 无 victim 导致的 reservation fail |
| `ready_response_count` | 通过 ready queue 返回的请求数 |

测试至少要断言：

1. refcount 入队加一。
2. `next_access()` 减一。
3. refcount 非 0 时 replacement 不选该 line。
4. MSHR merge 多请求 refcount 计数正确。

### 实施阶段

| 阶段 | 内容 |
|------|------|
| 阶段 1 | 给 cache block 增加 line-level refcount/pin API |
| 阶段 2 | replacement 跳过 pinned line，并补 fail reason |
| 阶段 3 | hit response queue 入队时 pin，`next_access()` 时 unpin |
| 阶段 4 | miss/MSHR ready 路径按 accepted request pin/unpin |
| 阶段 5 | 补充 refcount/pin directed tests 和 property tests |

### 验证要求

必须新增或扩展以下测试：

1. hit pending 时同 set conflict miss 不能 evict 该 line。
2. hit `next_access()` 后 refcount 归零，后续 conflict miss 可以 evict。
3. same line 多个 hit pending，refcount 按数量增加，并逐个 `next_access()` 递减。
4. MSHR merge 多个 miss ready，refcount 逐个释放。
5. sector cache 中某 sector pending 时整条 line 不可被 victim。
6. replacement fail reason 能区分 pinned 和 reserved，或文档说明复用原因。
7. write-evict hit 立即 invalid 但仍 pinned 时，同 set miss 不能复用该 invalid line。
8. bounded hit response queue 满时必须产生 backpressure，drain 后恢复接收。
9. DataStore hit accepted 时快照语义必须用场景测试或文档化断言固定。

所有新增 testcase 必须按配置、输入、流程、输出四个维度记录到 `doc/verification/testcase_matrix.md`。

### 默认交付模式

当前交付模式为默认新模式：`cache_config` 构造时 `m_defer_hit_response=true`。用户如果需要历史同步 hit 行为，必须显式调用：

```cpp
cfg.set_defer_hit_response(false);
```

因此默认调用方不能把 `access()` 返回 `HIT` 理解为数据已经完成返回。`HIT` 只表示 tag hit 且请求已被 cache 接收；完成点必须通过 `access_ready()` 和 `next_access()` 观察。

### Final Check 设计

为支持用例收尾质量检查，当前实现提供只读式状态观测接口：

| 对象 | 接口 | 检查范围 |
|------|------|----------|
| `tag_array` | `final_state_clean()` | pending line 表为空，所有 line invalid 且 unpinned |
| `mshr_table` | `empty()` | MSHR entry 表和 ready response 队列为空 |
| `baseline_cache` | `queues_empty()` / `final_state_clean()` | miss queue、MSHR、extra fields、hit response queue、ready response queue、pending response index、tag line |
| `tex_cache` | `queues_empty()` / `final_state_clean()` | fragment FIFO、request FIFO、ROB、result FIFO、extra fields、tag line、texture data block |

final check 不提供强制清空内部状态的后门。测试必须先通过正常 `cycle()`、`fill()`、`next_access()` drain 到空，再调用 `invalidate()` 将 line/data block 回到初始态，最后断言 `final_state_clean()==true`。

故障注入类用例如果刻意制造下游背压或未完成请求，不应直接套用 final check；它们必须在可收敛路径补 drain，或在 testcase 中说明该场景故意保持 pending。

### 设计状态

已进入代码实现并通过默认回归。2026-06-02 当前结果：`./run.sh` 通过 unit `13/13`、scenario `86/86 checks`、whitebox `40/40`、death `16/16`；`./coverage.sh coverage-final-check` 总覆盖率 Region `56.43%`、Function `73.52%`、Line `70.25%`、Branch `60.21%`。
