# openCache 替换 GPGPU-Sim Cache 的策略方案

> 目标: 用 openCache 作为基类/基础，替换 GPGPU-Sim 中全部 6 种 GPU 缓存  
> 类型: 思想实验 / 策略文档（不涉及编码实施）  
> 日期: 2026-05-26

---

## 零、核心挑战总览

GPGPU-Sim 的缓存与模拟器之间存在**6 个紧耦合点**。替换的核心工作不是重写缓存逻辑（openCache 已经足够），而是**在这 6 个耦合点上建立适配层**。

```
                    ┌─────────────────────────┐
                    │     GPGPU-Sim 外围       │
                    │  (shader_core / icnt /   │
                    │   mem_partition / DRAM)  │
                    └───────────┬─────────────┘
                                │
              ┌─────────────────┼─────────────────┐
              │   耦合点 1: mem_fetch ↔ CacheRequest │
              │   耦合点 2: cache_t 接口适配        │
              │   耦合点 3: 时钟 / cycle 驱动       │
              │   耦合点 4: 请求拆分 (sector split) │
              │   耦合点 5: fill 响应回注           │
              │   耦合点 6: 延迟注入 (L1 latency)   │
              └─────────────────┼─────────────────┘
                                │
                    ┌───────────▼─────────────┐
                    │      openCache           │
                    │  (BaselineCache /        │
                    │   DataCache /            │
                    │   ReadOnlyCache)         │
                    └─────────────────────────┘
```

---

## 一、接口适配层：mem_fetch ↔ CacheRequest

### 1.1 现状差异

| 维度 | GPGPU-Sim (`mem_fetch*`) | openCache (`CacheRequest`) |
|------|--------------------------|---------------------------|
| 传递方式 | 指针，全生命周期追踪 | 值类型，每次 access 拷贝 |
| 地址 | `get_addr()` | `address` 字段 |
| 类型 | `mem_access_type` (12 种) | `AccessType` (6 种) |
| 大小 | `get_data_size()` | `size` 字段 |
| 字节掩码 | `get_access_byte_mask()` | `byte_mask` 字段 |
| 扇区掩码 | `get_access_sector_mask()` | `sector_mask` 字段 |
| 状态追踪 | `set_status()/get_status()` (标记当前所在队列) | 无（通过 `ExtraFields` 间接追踪） |
| 原始请求 | `get_original_mf()` (SECTOR_TEX_FIFO 用) | 无 |
| warp 信息 | `get_inst()`, `get_wid()`, `get_sid()` | `stream_id`, `instruction_id` |
| 生命周期 | alloc → use → delete | 栈上创建，拷贝传递 |

### 1.2 策略：创建 `CacheRequestAdapter`

**方案 A（推荐）：双向适配器**

```cpp
// 在 GPGPU-Sim 侧创建一个适配器，包裹 mem_fetch 和 CacheRequest
struct MemFetchCacheRequest {
    mem_fetch *mf;           // 原始 GPGPU-Sim 请求（保留给外围使用）
    CacheRequest creq;       // openCache 请求（传给 openCache）
    addr_t mshr_addr;        // 从 addr 计算，SECTOR_ASSOC 时区别于 block_addr
};
```

在 `access()` 调用点：
```cpp
// 原 GPGPU-Sim:
//   cache->access(mf->get_addr(), mf, time, events);

// 替换后:
MemFetchCacheRequest adapter{mf};
adapter.creq.address = mf->get_addr();
adapter.creq.type = map_access_type(mf->get_access_type());  // 12→6 映射
adapter.creq.size = mf->get_data_size();
adapter.creq.byte_mask = mf->get_access_byte_mask();
adapter.creq.sector_mask = mf->get_access_sector_mask();

CacheResult result = cache->access(adapter.creq);
// 将 CacheResult 映射回 cache_request_status:
//   HIT→HIT, MISS→MISS, RESERVATION_FAIL→RESERVATION_FAIL, ...
// 从 CacheResult.events 重建 std::list<cache_event>
```

**mem_access_type → AccessType 映射表**:

| GPGPU-Sim | openCache |
|-----------|-----------|
| `GLOBAL_ACC_R`, `LOCAL_ACC_R`, `CONST_ACC_R`, `TEXTURE_ACC_R`, `INST_ACC_R` | `READ` |
| `GLOBAL_ACC_W`, `LOCAL_ACC_W` | `WRITE` |
| `L1_WRBK_ACC`, `L2_WRBK_ACC` | `WRITE_BACK` |
| `L1_WR_ALLOC_R`, `L2_WR_ALLOC_R` | `WRITE_ALLOCATE` |

**方向映射注意事项**:
- `GLOBAL_ACC_W` vs `LOCAL_ACC_W` 的区分在 `wr_hit_global_we_local_wb` 中需要用到。openCache 已通过 `CacheRequest::is_global_access` 字段支持。适配器需要根据原始 `mem_access_type` 设置此字段。
- `L1_WRBK_ACC` vs `L2_WRBK_ACC` 的路由区分：这个区分在 GPGPU-Sim 中用于互联网络路由，与缓存本身无关。适配器不需要传递给 openCache——路由在外部处理。

---

## 二、cache_t 接口适配

### 2.1 现状差异

GPGPU-Sim 的 `cache_t` 接口：
```cpp
virtual enum cache_request_status access(
    new_addr_type addr, mem_fetch *mf, unsigned time,
    std::list<cache_event> &events) = 0;
virtual bool data_port_free() const = 0;
virtual bool fill_port_free() const = 0;
```

openCache 的 `BaselineCache` 接口：
```cpp
virtual CacheResult access(const CacheRequest &req);
bool data_port_free() const;
bool fill_port_free() const;
```

### 2.2 策略：创建继承适配器

```cpp
// openCache + cache_t 接口的适配器
class OpenCacheAdapter : public cache_t {
    std::unique_ptr<BaselineCache> m_cache;  // 实际的 openCache 实例
    std::unordered_map<addr_t, mem_fetch*> m_mf_map;  // MSHR地址→原始mem_fetch

public:
    enum cache_request_status access(
        new_addr_type addr, mem_fetch *mf, unsigned time,
        std::list<cache_event> &events) override
    {
        CacheRequest creq = build_request(mf);
        CacheResult result = m_cache->access(creq);

        // 重建 events（openCache 用 vector，GPGPU-Sim 用 list）
        // 从 m_cache->m_miss_queue 或 ExtraFields 提取...
        events = convert_events(m_cache);

        // 保存 mem_fetch 映射供 fill() 使用
        if (result.status == AccessStatus::MISS) {
            addr_t mshr = m_cache->get_config().get_mshr_addr(addr);
            m_mf_map[mshr] = mf;
        }

        return convert_status(result.status);
    }

    bool data_port_free() const override { return m_cache->data_port_free(); }
    bool fill_port_free() const override { return m_cache->fill_port_free(); }

    // 适配 fill(): GPGPU-Sim 调 cache->fill(mf, time)
    // openCache 需要 fill(CacheRequest, time)
    void fill(mem_fetch *mf, unsigned time) {
        CacheRequest creq = build_request(mf);
        m_cache->fill(creq, time);
        // fill 后检查 MSHR ready → m_cache->access_ready() → next_ready()
    }

    // 适配 cycle(): GPGPU-Sim 直接在 shader.cc 中调 cache->cycle()
    void cycle() { m_cache->cycle(); }
};
```

---

## 三、逐个缓存替换方案

### 3.1 L1 指令缓存 (m_L1I)

**原实现**: `read_only_cache`，继承 `baseline_cache`

**替换方案**: 直接使用 openCache 的 `ReadOnlyCache`

```cpp
// 原代码 (shader.cc:182):
m_L1I = new read_only_cache(name, m_config->m_L1I_config, m_sid,
    get_shader_instruction_cache_id(), m_icnt, IN_L1I_MISS_QUEUE,
    OTHER_GPU_CACHE, m_gpu);

// 替换后:
m_L1I = new OpenCacheAdapter(
    new ReadOnlyCache(name, convert_config(m_config->m_L1I_config),
                       m_sid, get_shader_instruction_cache_id(),
                       new IcntMemoryInterface(m_icnt, IN_L1I_MISS_QUEUE),
                       CacheLevel::L1));
```

**接入要点**:
- 指令获取在 `shader_core_ctx::fetch()` 中（line ~1001），逐个 warp 逐条指令访问
- `access()` 返回后，HIT→直接使用，MISS→等待 `access_ready()` → `next_access()`
- `fill()` 在 `shader_core_ctx::fill()` 中（line 4024），来自 `m_icnt` 的响应
- `cycle()` 在 fetch 阶段末尾（line 1025）

**风险**: 低。`ReadOnlyCache` 是最简单的缓存类型，openCache 的实现与 GPGPU-Sim 高度一致。

### 3.2 L1 常量缓存 (m_L1C)

**原实现**: `read_only_cache`，继承 `baseline_cache`

**替换方案**: 同 L1I，使用 `OpenCacheAdapter<ReadOnlyCache>`

**接入要点**:
- 在 `ldst_unit::constant_cycle()` 中调用（line ~2247）
- 如果 `perfect_inst_const_cache` 开启则完全绕过
- `fill()` 在 `ldst_unit::cycle()` 的 response_fifo 处理中（line 2850）

**风险**: 低。与 L1I 完全相同的模式。

### 3.3 L1 数据缓存 (m_L1D) — 最复杂

**原实现**: `l1_cache` → `data_cache` → `baseline_cache`

**替换方案**: 使用 `OpenCacheAdapter<DataCache>`

**复杂点 1: L1 Bank 延迟队列**

GPGPU-Sim 的 L1D 有一个**多级延迟队列**（`l1_latency_queue[bank][stage]`）：
```cpp
// 请求进入延迟队列尾部:
l1_latency_queue[bank][l1_latency - 1] = mf;
// 每周期队列向前滑动:
l1_latency_queue[j][stage] = l1_latency_queue[j][stage + 1];
// 延迟后从队列头部出队，调用 access():
mf_next = l1_latency_queue[j][0];
cache->access(mf_next->get_addr(), mf_next, time, events);
```

这个延迟队列模拟的是 **L1 访问的流水线延迟**（数据从 bank 读取需要 N 个周期，与 hit/miss 无关）。

**策略**: 延迟队列**保留在适配器外部**（属于 shader_core 的行为模型，不是缓存本身的逻辑）。`OpenCacheAdapter` 不做任何改动——延迟队列照常运作，只是在队列头部改为调用 `adapter->access()`。

**复杂点 2: SECTOR_ASSOC 的 store_ack 计数**

当 MSHR 类型为 SECTOR_ASSOC 时，store 请求的 `inc_ack` 按扇区数计算：
```cpp
unsigned inc_ack = (mshr_type == SECTOR_ASSOC)
    ? (mf->get_data_size() / SECTOR_SIZE) : 1;
for (unsigned i = 0; i < inc_ack; ++i)
    m_core->inc_store_req(inst.warp_id());
```

**策略**: 同样保留在适配器外部。`OpenCacheAdapter` 的 `access()` 返回的 `CacheResult` 中含有请求大小信息，外围据此计算 ack 数量。

**复杂点 3: WRITE_BACK 绕过 L1D**

当 `CACHE_GLOBAL == inst.cache_op` 或 `gmem_skip_L1D` 时，请求直接发送到互联网络，绕过 L1D：
```cpp
if (bypassL1D) {
    mf->set_status(IN_SHADER_FETCHED, time);
    m_next_global = mf;  // 直接排队到互联网络
}
```

**策略**: 外围逻辑完全不变。Bypass 决策在 `ldst_unit::cycle()` 中，不涉及缓存本身。

### 3.4 L2 缓存 (m_L2cache)

**原实现**: `l2_cache` → `data_cache` → `baseline_cache`

**替换方案**: 使用 `OpenCacheAdapter<DataCache>`

**与 L1D 的区别**:
- L2 没有 bank 延迟队列
- L2 位于 `memory_sub_partition` 内，通过 FIFO 管线连接

**接入要点**:
```
m_icnt_L2_queue (FIFO) → pop() → adapter->access() 
    → HIT: m_L2_icnt_queue.push(mf)
    → MISS: m_L2_dram_queue.push(mf) → DRAM → m_dram_L2_queue 
           → adapter->fill() → m_L2_icnt_queue.push(mf)
```

**策略**: 
- `access()` 返回 HIT → `mf` 推入 `m_L2_icnt_queue`
- `access()` 返回 MISS → `mf` 推入 `m_L2_dram_queue`（经由 `send_read_request` 的事件机制）
- 来自 DRAM 的填充响应经过 `m_dram_L2_queue` 到达后，调用 `adapter->fill(mf, time)`
- 需要将 openCache 的 `CacheRequest` 与 `mem_fetch` 绑定（通过 `m_mf_map` 或 `ExtraFields`），以便 `fill()` 找到原始请求

**L2 独特功能: 请求拆分 (breakdown_request_to_sector_requests)**

当 L1D（sector cache）向 L2 发送行请求时，L2 需要将行请求拆分为扇区请求发送给 DRAM：
```cpp
std::vector<mem_fetch*> memory_sub_partition::breakdown_request_to_sector_requests(mem_fetch *mf);
```

**策略**: 拆分逻辑保留在 `memory_sub_partition` 中，不进入 openCache。拆分后的子请求逐个调用 `adapter->access()`。对应的 `fill()` 需要支持扇区级重组——openCache 已有的 SECTOR_ASSOC `pending_read` 机制（`BaselineCache::fill()` line 53-62）可以处理此场景。

### 3.5 纹理缓存 (m_L1T) — 特殊对待

**原实现**: `tex_cache` → `cache_t`（不继承 baseline_cache），使用 FIFO 管线

**替换方案**: **不建议直接替换**。理由：

1. `tex_cache` 的微架构与 `baseline_cache` 完全不同（FIFO 管线 vs MSHR + miss_queue）
2. `tex_cache` 的 `access()` **永远不返回 HIT**——tag 命中后数据仍需经过 `fragment_fifo`
3. `tex_cache` 的数据存储是 `data_block`（仅 valid bit），不是 `CacheBlock`（四状态机）

**替代策略: 保持 tex_cache 独立，仅复用 openCache 的组件**

```
openCache::TagArray    ← 替换 tex_cache 的 m_tags
openCache::CacheStats  ← 替换 tex_cache 的 m_stats
openCache::CacheConfig ← 替换 tex_cache 的 m_config
```

FIFO 管线（`fragment_fifo`, `request_fifo`, `rob`, `result_fifo`）保留 GPGPU-Sim 原有实现。

**如果一定要统一**: 需要为 openCache 新增一个 `TextureCache` 类，不继承 `BaselineCache`，而是直接继承 `CacheMemoryInterface`，内部包含：
- 一个 `TagArray`（共用）
- 5 个 `FIFO`（独立实现）
- 简化的 `data_block` 数组（不用 `CacheBlock`）
- 自己实现 `access()` / `cycle()` / `fill()` / `access_ready()` / `next_access()`

**工作量评估**: 中等（~400 行新代码），但收益有限——纹理缓存的当前实现已经足够好，且论文依据清晰。

---

## 四、时钟与性能仿真策略

### 4.1 时钟驱动模型

GPGPU-Sim 的时钟系统：
```cpp
// 全局时钟
gpu_sim_cycle + gpu_tot_sim_cycle

// 每个 shader_core 每周期调用:
m_L1I->cycle();
m_L1T->cycle();
m_L1C->cycle();
m_L1D->cycle();

// 每个 memory_partition 每周期调用:
m_sub_partition[i]->cache_cycle(cycle);
```

**策略**: openCache 的 `cycle()` 已正确实现（排空 miss_queue、补充带宽）。适配器中只需转发：
```cpp
void OpenCacheAdapter::cycle() {
    m_cache->cycle();
    // 检查 access_ready() → 将就绪请求推入 shader_core 的响应队列
    if (m_cache->access_ready()) {
        auto ready = m_cache->next_ready();
        for (auto &creq : ready) {
            mem_fetch *mf = lookup_mf(creq);
            mf->set_status(IN_SHADER_FETCHED, current_time);
            push_to_response_fifo(mf);
        }
    }
}
```

### 4.2 延迟注入

GPGPU-Sim 的延迟模型分布在多个层级：

| 延迟类型 | GPGPU-Sim 位置 | openCache 如何处理 |
|----------|---------------|-------------------|
| L1 hit 延迟 | `l1_latency_queue` (外部) | 通过外围延迟队列模拟，不进缓存 |
| L1 fill 延迟 | `m_memport->get_fill_latency()` | `CacheMemoryInterface::get_fill_latency()` |
| L2 hit 延迟 | `cache_config.hit_latency` (隐式) | `CacheConfig::hit_latency` (显式) |
| L2 fill 延迟 | DRAM 模型 (外部) | 通过 `fill()` 调用时机控制 |
| 互联网络延迟 | `icnt` 模型 (外部) | 不进缓存，外围处理 |
| MSHR 等待延迟 | `m_mshrs` 内部 | 内部处理（access_ready 机制） |

**关键**: openCache 的 `CacheResult::latency` 字段返回值在 GPGPU-Sim 中没有对应物——GPGPU-Sim 将所有延迟建模为显式的 queue 延迟或 `fill()` 回调时机。适配器可以**忽略** `CacheResult::latency`，或者用它来驱动一个显式的延迟计数器。

### 4.3 带宽管理

GPGPU-Sim 的 `bandwidth_management` 在每次 `access()` 时调用 `use_data_port()` 和 `use_fill_port()`。`data_port_free()` 用于 stall 流水线。

**策略**: openCache 的 `replenish_ports()` / `use_data_port()` / `use_fill_port()` 已经正确实现。适配器直接映射：
```cpp
bool data_port_free() const override { return m_cache->data_port_free(); }
bool fill_port_free() const override { return m_cache->fill_port_free(); }
```

`use_data_port()` 在 openCache 的 `DataCache::access()` 结束时自动调用——与 GPGPU-Sim 一致。

---

## 五、请求生命周期适配

### 5.1 GPGPU-Sim 的 mem_fetch 生命周期

```
1. shader_core_mem_fetch_allocator::alloc()
   → 创建 mem_fetch 对象
   → 包含完整的 warp_inst, addr, type, size, byte_mask, sector_mask
   
2. cache->access(addr, mf, time, events)
   → HIT: mf 直接返回给 shader_core
   → MISS: mf 被推入 miss_queue，状态设为 IN_L1D_MISS_QUEUE
   
3. icnt->push(mf)
   → mf 通过互联网络发送到 L2/memory_partition
   → mf 状态变更: IN_L1D_MISS_QUEUE → IN_NETWORK → ...
   
4. L2/DRAM 处理完成后，mf 回到 shader_core 的 m_response_fifo
   
5. cache->fill(mf, time)
   → 数据写入 tag_array
   → MSHR 标记 ready
   
6. cache->access_ready() → next_access()
   → 原始 mf 返回，状态设为 IN_SHADER_FETCHED
   
7. shader_core 处理完成 → delete mf
```

### 5.2 openCache 下的生命周期

```
1. shader_core_mem_fetch_allocator::alloc()
   → 创建 mem_fetch 对象 (不变)
   
2. adapter->access(addr, mf, time, events)
   → 内部: build_request(mf) → m_cache->access(creq)
   → HIT: 返回 HIT，mf 直接返回 (不变)
   → MISS: creq 进入 m_miss_queue (由 send_read_request 内部处理)
   → 保存 mf↔mshr_addr 映射
   
3. adapter->cycle()
   → m_cache->cycle() 排空 miss_queue
   → 对于出队的请求: 查找对应的 mf，推入 icnt
   
4. 响应回到 m_response_fifo (不变)
   
5. adapter->fill(mf, time)
   → m_cache->fill(creq, time)
   → SECTOR_ASSOC: pending_read--, 等待所有扇区到齐
   → mark_ready(mshr_addr)
   
6. adapter->cycle()
   → m_cache->access_ready() → next_ready()
   → 从 m_mf_map 查找就绪的 mf
   → 推入响应队列
   
7. shader_core 处理完成 → delete mf, 清除映射
```

---

## 六、配置迁移策略

### 6.1 cache_config → CacheConfig

GPGPU-Sim 的 `cache_config` 与 openCache 的 `CacheConfig` 已经**格式兼容**（配置字符串格式相同）。迁移只需要：

```cpp
CacheConfig convert_config(const cache_config &src) {
    CacheConfig dst;
    dst.cache_type = (src.m_cache_type == SECTOR) ? CacheType::SECTOR : CacheType::NORMAL;
    dst.num_sets = src.m_nset;
    dst.line_size = src.m_line_sz;
    dst.associativity = src.m_assoc;
    dst.replacement_policy = convert_rp(src.m_replacement_policy);
    dst.write_policy = convert_wp(src.m_write_policy);
    dst.alloc_policy = convert_ap(src.m_alloc_policy);
    dst.write_alloc_policy = convert_wap(src.m_write_alloc_policy);
    dst.set_index_func = convert_sif(src.m_set_index_function);
    dst.mshr_type = convert_mshr(src.m_mshr_type);
    dst.mshr_entries = src.m_mshr_entries;
    dst.mshr_max_merge = src.m_mshr_max_merge;
    dst.miss_queue_size = src.m_miss_queue_size;
    dst.num_banks = /* 从 l1d_cache_config 获取，如果是 L1D */;
    dst.compute_derived();
    return dst;
}
```

**需要特殊处理的配置**:
- `l1d_cache_config` 的 bank 参数（`l1_banks`, `l1_banks_byte_interleaving`）→ openCache 的 `num_banks`（当前仅支持 bank 计数，不支持字节交错粒度）
- `l2_cache_config` 的 `m_address_mapping` → openCache 当前不支持地址翻译，可能需要扩展 `SetIndexFunction::CUSTOM`
- `m_wr_percent` → openCache 的 `write_percent`

### 6.2 不支持的配置字符

| GPGPU-Sim | openCache 支持 | 处理方式 |
|-----------|---------------|---------|
| `'H'` (FERMI_HASH) | 不支持 | 映射到 `HASH_IPOLY` |
| `'s'` (STREAMING) | 映射到 `ON_FILL` | 已内置处理 |

---

## 七、实施路线图

### Phase 1: 基础设施（预计工作量: 中）

1. 实现 `MemFetchCacheRequest` 适配器结构体
2. 实现 `map_access_type()` 12→6 映射函数
3. 实现 `convert_config()` 配置转换函数
4. 实现 `OpenCacheAdapter` 类（继承 `cache_t`，内部持有 `BaselineCache`）
5. 实现 `IcntMemoryInterface`（实现 `CacheMemoryInterface`，内部调用 `icnt->push()`）

### Phase 2: 逐个替换 L1 缓存（预计工作量: 低→中）

1. **L1I / L1C**（最低风险）: 替换 `read_only_cache` → `OpenCacheAdapter<ReadOnlyCache>`
2. **L1D**（中等风险）: 替换 `l1_cache` → `OpenCacheAdapter<DataCache>`，保留外部 `l1_latency_queue`
3. **L1T**（可选）: 保持 `tex_cache` 不变，仅替换内部的 `m_tags` 为 openCache `TagArray`

### Phase 3: 替换 L2（预计工作量: 中）

1. 替换 `l2_cache` → `OpenCacheAdapter<DataCache>`
2. 保留 `breakdown_request_to_sector_requests()` 在 `memory_sub_partition` 中
3. 配置 `mshr_type = SECTOR_ASSOC` 以启用扇区分片重组

### Phase 4: 验证（预计工作量: 中）

1. 用 GPGPU-Sim 现有回归测试套件运行
2. 对比 IPC、hit rate、DRAM 流量等关键指标
3. 逐缓存 A/B 测试（一次只替换一个缓存，其余保持原样）

---

## 八、风险与缓解

| 风险 | 等级 | 缓解措施 |
|------|------|---------|
| `mem_fetch*` 生命周期管理 | 高 | `m_mf_map` 严格匹配 alloc/delete，Phase 1 重点测试 |
| SECTOR_ASSOC 扇区拆分/重组 | 中 | 保留 GPGPU-Sim 的拆分逻辑，openCache 仅处理重组（已有 pending_read） |
| L1D bank 延迟队列兼容 | 低 | 延迟队列在外部，不进入 openCache |
| 纹理缓存的 FIFO 管线 | 高（如果替换） | 建议不替换 tex_cache，仅复用 TagArray/CacheStats |
| 配置字符串兼容性 | 低 | 两者格式已兼容 |
| 性能差异（hit rate 漂移） | 中 | A/B 测试 + 逐个替换策略 |

---

## 九、结论

用 openCache 替换 GPGPU-Sim 的缓存**是可行的**。核心工作是建立 `OpenCacheAdapter` 适配层，处理 `mem_fetch*` ↔ `CacheRequest` 的双向映射。数据缓存线（L1I/L1C/L1D/L2）的替换是直接的——它们共享 `baseline_cache` 基类，与 openCache 的 `BaselineCache`/`DataCache`/`ReadOnlyCache` 架构对应。纹理缓存建议保持独立，仅替换共用组件（TagArray/CacheStats）。

最大工作量在适配器的 `mem_fetch` 生命周期管理（创建/追踪/释放），而不是缓存逻辑本身。按 Phase 1→2→3→4 顺序推进，逐个缓存替换并验证，可以将风险控制在可接受范围。
