# 极限水位压力测试方案 (Extreme Stress Test Plan)

## 测试目标
目前的白盒测试虽然达到了 100% 通过且核心分支全覆盖，但如覆盖率报告所示，`line_refcount` 和各项 FIFO Queue 的水位峰值（Watermark）大多停留在 1 或 2。
本方案旨在通过**“拥堵构造法”（Congestion Engineering）**，故意阻塞缓存的上下游流水线，将 REF Counter、MSHR 并发度以及各级 Queue / FIFO 的水位推至硬件配置的极限物理上限（例如 64 或 128），并使用 `queue_watermarks()` 探针进行严格断言。

## 拥堵构造策略 (Congestion Strategy)

要让流水线的驻留项达到最大值，核心思路是“只进不出”：
1. **堵住下游 (Block Downstream)**：停止调用 `mem.queue.pop_front()` 与 `cache.fill()`。这会导致 `m_miss_queue` 被迅速塞满。
2. **堵住上游 (Block Upstream/Drain)**：停止调用 `cache.next_access()`。这会导致所有的命中响应和填充响应全部积压在 `m_ready_response_queue` 中，同时导致对应 Cache Line 的 `refcount` 只增不减。
3. **制造极慢的流水线 (Slow Port Pipeline)**：通过配置极窄的 `data_port_width` (例如 1 字节) 和极大的 Payload (例如 128 字节)，让单个 Hit 请求在 `hit_response_queue` 中驻留 128 个 Cycle，从而给我们足够的时间把后续的 63 个请求全部塞入队列。

## 极限用例规划 (Planned Test Cases)

### TC-MAX-001: 极限 MSHR 合并与瞬态 REF 峰值 (Max MSHR & Refcount Spike)
- **前置条件**：配置 MSHR 大小为 64，`max_merged` 为 64。
- **执行序列**：
  1. 向 `0x1000` 地址发起 1 次 Read Miss。
  2. 堵住下级内存，不返回 Fill。
  3. 在接下来的若干周期内，向同一个地址 `0x1000` 连续发起 63 次 Read 请求。
  4. 检查 `m_mshrs.watermarks().merged` 必须达到 **63**。
  5. 释放下级内存，执行 `cache.fill()`。
  6. **断言极限**：此时 64 个请求同时变为 ready，对应 Cache Line 的 `refcount` 瞬间飙升至 **64**。检查 `watermarks.line_refcount == 64`。

### TC-MAX-002: 极限 Hit Response 队列拥堵 (Max Hit Queue & Ready Queue)
- **前置条件**：配置 `hit_response_queue_size = 64`，数据端口宽度极窄（故意制造巨大 hit latency）。预热 Cache Line (确保全命中)。
- **执行序列**：
  1. 连续发起 64 个针对该 Line 的 Hit 请求。
  2. 由于不调用 `next_access()`，这些请求会在 Hit Queue 中排队并逐渐流入 Ready Queue。
  3. **断言极限**：`watermarks.hit_response_queue` 必须达到配置的峰值上限（受限于注入速度和流出速度，可控在较高水位）。当全部请求流完后，`watermarks.ready_response_queue` 达到 **64**。
  4. **断言极限**：由于 64 个请求都在 Ready 队列中等待，`line_refcount` 将达到 **64**。

### TC-MAX-003: 极限 Miss 队列背压 (Max Miss Queue Full)
- **前置条件**：配置 `miss_queue_size = 64`。
- **执行序列**：
  1. 不断向 64 个完全不同的地址发起请求（保证映射到不同的 Cache Line 以避免 MSHR 合并）。
  2. 完全堵住下级内存。
  3. **断言极限**：检查 `watermarks.miss_queue` 达到 **64**。
  4. 第 65 个请求必须被正确弹回并返回 `RESERVATION_FAIL` 或 `MISS_QUEUE_FULL`。

### TC-MAX-004: Texture Cache 全流水线爆仓 (Max Texture FIFOs)
- **前置条件**：构造 `tex_cache`，配置 `F:64:4,64:2`（即 Fragment FIFO 为 64，Result FIFO 为 64）。
- **执行序列**：
  1. 连续发射 64 个发往 Texture Cache 的有效请求。
  2. 不进行出队操作。
  3. 推进 cycle。
  4. **断言极限**：验证 `watermarks.fragment_fifo == 64` 和 `watermarks.result_fifo == 64`。

## 验收标准 (Acceptance Criteria)
当这批用例在 `test_cache_whitebox.cc` 中实现后，执行：
```bash
./run.sh
```
在控制台打印的 `[EMPIRICAL LIMITS] Absolute Maximums` 必须显示：
- `Max REF Counter` >= 64
- `Max Hit Response Queue` >= N (逼近配置上限)
- `Max Miss Queue` >= 64
- `Max Ready Response Queue` >= 64
同时，用例自身的 `CHECK_EQ` 断言全部通过，证明覆盖率不仅仅走过了代码分支，而且在运行态真正地压测到了数据结构的极限水位。