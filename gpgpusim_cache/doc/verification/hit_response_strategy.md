# Hit Response 延迟返回策略方案

## 背景

当前 `read_only_cache` 和 `data_cache` 的 `access()` 在 tag hit 时直接返回 `HIT`，调用方通常会把 `HIT` 理解为数据已经可用。代码同时会占用 data port 若干 cycle，但这个延迟只影响端口统计，不影响请求完成时刻。

miss 路径已经不同：`access()` 返回 `MISS` 只表示请求被接收并送往下级；下级 `fill()` 后，请求通过 `access_ready()` 和 `next_access()` 返回给上层。`tex_cache` 的 hit 也已经采用类似语义：hit 返回 `HIT_RESERVED`，请求进入 fragment/result FIFO，之后由 `access_ready()` 和 `next_access()` 返回。

新需求要求 data/read-only cache 的 hit 与数据返回解耦：hit 只表示 tag 命中和请求被接收，数据仍需经过 SRAM/data array 读延迟后再返回。

## 目标

1. 将 data/read-only cache 的 hit 判定和 hit 数据返回拆成两个阶段。
2. 复用 `access_ready()` / `next_access()` 作为统一完成接口，避免新增上层调度接口。
3. 保持 miss fill ready 路径原有语义。
4. 支持按 `m_data_port_width` 和请求数据大小建模 hit 返回延迟。
5. 提供兼容开关，避免一次性破坏现有用户和测试对 `HIT` 的旧语义假设。

## 受影响范围

| 模块 | 影响 | 处理策略 |
|------|------|----------|
| `read_only_cache` | read hit 从同步完成变成可选延迟完成 | 新模式下 hit 入 response queue |
| `data_cache` | read/write hit 从同步完成变成可选延迟完成 | 新模式下 read hit 必须延迟；write hit 行为需显式测试 |
| `l1_cache`/`l2_cache` | 继承 `data_cache::access()` 语义 | 通过 data cache 测试覆盖 |
| `tex_cache` | 已经采用 FIFO/ready 返回模型 | 本需求不修改，只保留回归保护 |
| 调用方/场景测试 | 旧代码可能认为 `HIT` 立即完成 | 默认关闭新模式，并在实现阶段迁移测试 |

## 非目标

1. 本阶段不改变 texture cache 的已存在 fragment/result FIFO 模型。
2. 本阶段不实现真实 SRAM bank conflict，只建模 hit response latency 和 data port 占用。
3. 本阶段不修改功能数据模型，不引入真实 payload 返回。
4. 本阶段不改变 `MISS`、`RESERVATION_FAIL`、`SECTOR_MISS` 的枚举含义。

## 当前语义

| cache | hit `access()` 返回 | hit 是否进入 ready 队列 | 数据返回接口 |
|-------|--------------------|--------------------------|--------------|
| `read_only_cache` | `HIT` | 否 | 立即完成 |
| `data_cache`/`l1_cache`/`l2_cache` | `HIT` | 否 | 立即完成 |
| `tex_cache` | `HIT_RESERVED` | 是 | `access_ready()` / `next_access()` |
| miss path | `MISS` | fill 后进入 MSHR ready | `access_ready()` / `next_access()` |

## 推荐设计

### 1. 不新增上层完成接口

保留现有入口：

```cpp
cache_request_status access(new_addr_type addr, mem_fetch *mf, unsigned time,
                            std::list<cache_event> &events);
bool access_ready() const;
mem_fetch *next_access();
```

修改后的语义：

| 接口 | 新语义 |
|------|--------|
| `access()` | 完成 tag probe、策略处理、资源占用判断；返回请求是否被接收及 tag 结果 |
| `access_ready()` | 有任何已完成的 cache access 可以返回上层，包括 hit response 和 miss fill response |
| `next_access()` | 返回下一个已完成请求 |

行为破坏性说明：开启新模式后，`HIT` 不再表示数据已同步返回；它只表示 tag hit 且请求被 cache 接收。调用方必须用 `access_ready()` 和 `next_access()` 观察请求完成。

### 2. 增加兼容开关

新增配置项或运行时字段：

```cpp
bool m_defer_hit_response;
```

建议初始默认值为 `false`，保持历史行为。开启后：

| 模式 | `HIT` 含义 | 上层何时拿到请求 |
|------|------------|------------------|
| 旧模式 | tag hit 且数据立即可用 | `access()` 返回时 |
| 新模式 | tag hit 且请求被接收 | `access_ready()` 为 true 后调用 `next_access()` |

后续如果所有调用方完成迁移，可再评估默认开启。

### 3. 增加 hit response 队列

在 `baseline_cache` 增加 hit response 管线，建议命名：

```cpp
struct hit_response_entry {
  mem_fetch *mf;
  unsigned remaining_cycles;
};

std::list<hit_response_entry> m_hit_response_queue;
std::list<mem_fetch *> m_ready_response_queue;
```

hit response latency 计算：

```cpp
cycles = ceil(mf->get_data_size() / m_config.m_data_port_width);
```

最小延迟建议为 1 cycle。即使数据大小小于 port width，也不能在 `access()` 同周期通过 `next_access()` 返回。

如果 hit response queue 后续设计为有容量限制，则必须定义 backpressure：

| 场景 | 建议行为 |
|------|----------|
| hit response queue 未满 | `access()` 返回 `HIT`，请求入队 |
| hit response queue 已满 | `access()` 返回 `RESERVATION_FAIL` |
| fail reason | 若新增 fail reason 成本低，增加 `HIT_RESPONSE_QUEUE_FULL`；否则暂记入 `MISS_QUEUE_FULL` 不推荐 |

第一阶段可以使用无界内部队列降低实现风险，但测试计划仍保留 backpressure testcase 作为后续扩展项。

### 4. `cycle()` 推进 hit response

`baseline_cache::cycle()` 增加步骤：

1. 发送 miss queue 到下级，保持原逻辑。
2. 推进 hit response queue。
3. 采样 port busy。
4. 释放 data/fill port bandwidth。

hit response 进入 `m_ready_response_queue` 后，`access_ready()` 返回 true。

需要明确顺序策略：

| 策略 | 优点 | 风险 |
|------|------|------|
| 统一 ready FIFO | hit/miss 完成顺序可控，调用方简单 | 需要把 MSHR ready 也搬入统一队列 |
| hit ready 优先于 MSHR ready | 实现简单 | 可能改变 miss fill 与 hit 同周期完成顺序 |
| MSHR ready 优先于 hit ready | 保持 miss 行为优先 | hit latency 模型可能被 fill 挤压 |

推荐先采用统一 ready FIFO。`fill()` 在 mark MSHR ready 后也把 ready 请求转入统一队列，或者 `next_access()` 内部按固定规则从 hit queue 和 MSHR ready 中弹出。若第一阶段要求改动最小，可先采用 `hit ready 优先于 MSHR ready`，但必须在测试中固化该规则。

### 5. hit 路径处理

`read_only_cache` hit：

1. 更新 tag/LRU。
2. 如果旧模式，保持立即完成。
3. 如果新模式，把 `mf` 放入 hit response queue。
4. `access()` 仍返回 `HIT`。

`data_cache` read hit：

1. 保持 `rd_hit_base()` 的 tag 和 atomic 修改逻辑。
2. 如果新模式，把 `mf` 放入 hit response queue。
3. `access()` 仍返回 `HIT`。

`data_cache` write hit：

写命中是否需要上层完成事件必须显式定义。建议：

| 写策略 | 新模式完成点 |
|--------|--------------|
| write-back hit | SRAM/tag 更新完成后进入 hit response queue |
| write-through hit | 本 cache 接收并向下级写请求后，经过 data port latency 进入 hit response queue |
| write-evict hit | 写请求发出并本地 invalidate 后，经过 data port latency 进入 hit response queue |
| local/global mixed | 按实际命中策略进入 hit response queue |

如果上层不需要写完成 token，可以通过配置保留旧行为；但验证文档必须覆盖写 hit 延迟的选择。

### 6. 统计语义

`HIT` 仍计入 hit，不新增 cache_request_status。`HIT_RESERVED` 暂时不复用于 data/read-only cache，避免污染历史统计含义。

统计计数时机建议保持在 `access()` 接收阶段，而不是 `next_access()` 完成阶段。理由是现有统计语义是“cache 访问结果统计”，不是“请求完成统计”。新增 ready 相关统计应单独命名，避免改变历史 hit/miss 口径。

新增可选统计项建议：

| 统计项 | 用途 |
|--------|------|
| hit_response_pending | 观察 hit 已接收但未返回数量 |
| hit_response_latency_cycles | 累计 hit 返回延迟 |
| hit_response_ready | 已通过 ready queue 返回的 hit 数 |

如果短期不加统计项，至少要在测试中断言 data port busy 和 ready 行为。

## Functional DataStore 读取时机

当前 standalone 模型中 `mem_fetch` 主要作为 timing token，真实 payload 由 `DataStore` 场景单独模拟。开启 hit 延迟后，必须明确 functional 可见性：

| 选择 | 说明 | 建议 |
|------|------|------|
| `access()` 时捕获数据 | tag hit 时立即读取 DataStore 快照，之后延迟返回 token | 简单，但与“SRAM 延迟读”抽象不完全一致 |
| `next_access()` 时读取数据 | 完成时才读取 DataStore | 更贴近延迟返回，但可能改变期间写入的可见性 |
| 本阶段不搬运 payload | 只改变 timing token 完成时机 | 推荐第一阶段采用，并在文档中说明 payload 非本阶段范围 |

推荐第一阶段不改变 DataStore payload 读取，只改变 timing token ready 时机；后续如果引入真实数据返回，再单独定义一致性模型。

## 风险与约束

| 风险 | 说明 | 缓解 |
|------|------|------|
| 兼容性破坏 | 现有调用方可能认为 `HIT` 立即完成 | 默认关闭新模式，新增测试只在开启模式下验证 |
| ready 顺序改变 | hit 和 miss 同周期 ready 的顺序会影响上层调度 | 方案文档和 testcase 固化顺序 |
| 写 hit 语义不清 | 写 hit 是否需要返回 token需要明确 | 测试计划单独覆盖读 hit 和写 hit |
| port 模型重复计数 | 现有 `use_data_port()` 已占用端口，新增队列不能重复计算 | 统一由 hit response 入队逻辑计算 latency，并保持 port 统计一致 |
| texture 行为偏离 | texture 已经有独立 FIFO | 不改 texture，只做对齐测试 |
| DataStore 可见性歧义 | hit 延迟期间若发生写入，payload 读取时机会影响结果 | 第一阶段只处理 timing token，不改变 payload |
| 调用方漏改 | 开启新模式后只看 `HIT` 会提前完成 | 默认关闭；实现阶段增加迁移说明和 exactly-once 测试 |

## 分阶段实施建议

| 阶段 | 内容 | 交付 |
|------|------|------|
| 阶段 0 | 文档和评审 | 本文件、测试计划、requirements/feature/testcase planned 追踪 |
| 阶段 1 | 增加兼容开关和 hit response queue | 默认旧行为不变，新模式测试通过 |
| 阶段 2 | 扩展 read-only/data read hit 延迟返回 | 新增 read hit latency tests |
| 阶段 3 | 扩展 write hit 延迟返回 | 新增 write hit latency tests |
| 阶段 4 | 统一 ready ordering 和统计 | 覆盖 hit/miss 同周期 ready 顺序、统计一致性 |
| 阶段 5 | 评估默认开启 | 更新 USER_GUIDE 和迁移说明 |

## 多角色评审

| 角色 | 评审结论 | 必须落实项 |
|------|----------|------------|
| 项目经理 | 通过，前提是先以兼容开关落地，避免影响现有用户 | requirements 必须标明“方案完成、实现待开展” |
| 设计 | 通过，推荐复用 `access_ready()/next_access()`，不新增分裂接口 | 固化 `HIT` 表示 tag hit/accepted，不表示数据返回 |
| 验证 | 通过，要求新增开关默认关闭和开启模式两类测试 | 测试必须断言 hit 后不能同周期 ready、若干 cycle 后 ready |
| 测试专家 | 通过，要求覆盖 read-only/data/read hit/write hit/hit-miss 同周期顺序 | testcase 先标记 Planned，实现阶段不得标记 Implemented |
| 独立评审代理 | 通过，要求补充 backpressure、统计口径、DataStore 读取时机、exactly-once property 和行为破坏性说明 | 已补入本策略和测试计划 |

## 评审限制说明

当前会话已达到可新建子 agent 上限，无法新建独立项目经理、设计、验证、测试专家四个 agent。本文档按四角色检查表完成评审记录；后续如果 agent 资源释放，应补一次独立 agent 复审。
