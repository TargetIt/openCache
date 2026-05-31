# openCache 调研报告

## 调研目标

从 CPU/GPU/NPU 开源项目中寻找一个高度参数化、可配置的通用 Cache 模型，用于 trace-driven 性能模型评估。

---

## 候选方案总览

| 方案 | 来源 | 语言 | 成熟度 | GPU Cache 类型 | Bank 建模 | Sector Cache | MSHR | Trace 驱动 |
|------|------|------|--------|---------------|-----------|-------------|------|-----------|
| **GPGPU-Sim cache** | GPGPU-Sim 4.x | C++ | ★★★★★ | L1/L2/Tex/RO | ✅ | ✅ | ✅ | ✅ |
| **ChampSim CACHE** | ChampSim | C++ | ★★★★ | L1I/L1D/L2/LLC | ❌ | ❌ | ✅ | ✅ |
| **Octopus** | OctopusSim | C++ | ★★★ | CPU L1/L2/L3 | ✅ | ❌ | ✅ | ❌ |
| **CacheSimulator** | muditbhargava66 | C++20 | ★★ | L1/L2/L3 | ❌ | ❌ | ❌ | ✅ |
| **libCacheSim** | 1a1a11a | C | ★★★ | 通用 | ❌ | ❌ | ❌ | ✅ |

---

## 1. GPGPU-Sim Cache 模型（强烈推荐）

### 仓库信息
- **主仓库**: https://github.com/gpgpu-sim/gpgpu-sim_distribution
- **Accel-Sim 框架**: https://github.com/accel-sim/accel-sim-framework
- **关键源文件**: `src/gpgpu-sim/gpu-cache.cc`, `src/gpgpu-sim/gpu-cache.h`
- **地址解码**: `src/gpgpu-sim/addrdec.cc`, `src/gpgpu-sim/addrdec.h`

### Cache 类层次结构

```
cache_t  (抽象基类)
  ├── baseline_cache          ← 通用 cache 基类，管理 tag/MSHR/带宽
  │     ├── read_only_cache   ← 只读 cache（如 constant cache）
  │     └── data_cache        ← 读写 cache，支持多种写策略
  │           ├── l1_cache    ← L1 特化
  │           └── l2_cache    ← L2 特化
  └── tex_cache               ← Texture cache（独立实现，含 FIFO pipeline + ROB）
```

### 核心参数配置

GPGPU-Sim 的 cache 通过配置字符串格式定义，格式如下：

```
<sector>:<nsets>:<bsize>:<assoc>:<rep>:<wr>:<alloc>:<wr_alloc>:<set_index_fn>,<mshr>:<N>:<merge>,<mq>
```

#### 可配置参数详细列表

| 参数类别 | 参数名 | 可选值 | 说明 |
|---------|--------|--------|------|
| Cache 类型 | `m_cache_type` | `NORMAL`, `SECTOR` | 普通 cache 或 sector cache |
| Set 数 | `m_nset` | 任意正整数 | Cache 组数 |
| Line 大小 | `m_line_sz` | 任意正整数(字节) | Cache line 大小 |
| 相联度 | `m_assoc` | 任意正整数 | 每组 way 数 |
| 替换策略 | `m_replacement_policy` | `LRU`, `FIFO` | 替换策略 |
| 写策略 | `m_write_policy` | `READ_ONLY`, `WRITE_BACK`, `WRITE_THROUGH`, `WRITE_EVICT` | 写策略 |
| 分配策略 | `m_alloc_policy` | `ON_MISS`, `ON_FILL`, `STREAMING` | 分配时机 |
| 写分配 | `m_write_alloc_policy` | `NO_WRITE_ALLOCATE`, `WRITE_ALLOCATE`, `FETCH_ON_WRITE` | 写分配策略 |
| 索引哈希 | `m_set_index_function` | `LINEAR`, `BITWISE_XOR`, `POLYNOMIAL` | set index 映射 |
| MSHR 数量 | `m_mshr_entries` | 任意正整数 | Miss 状态保持寄存器 |
| MSHR 合并 | `m_mshr_max_merge` | 任意正整数 | 合并深度 |
| Bank 数 | `m_nbanks` | 任意正整数 | Bank 并行度 |
| Bank 哈希 | `m_bank_index_function` | `LINEAR`, `BITWISE_XOR`, `POLYNOMIAL` | Bank 映射 |
| 数据端口带宽 | `m_data_port_bandwidth` | 任意正整数 | 每周期数据端口带宽 |
| Fill 端口带宽 | `m_fill_port_bandwidth` | 任意正整数 | 每周期 fill 端口带宽 |
| Sector 大小 | `m_sector_sz` | 32B (固定) | Sector 子块大小 |
| 延迟配置 | `m_hit_latency`, `m_fill_latency` | 可配 | Hit/Fill 延迟 |

### Cache 可例化的多种类型对应关系

| 目标场景 | GPGPU-Sim 对应实现 | 配置方式 |
|---------|-------------------|---------|
| GPU L1 Data Cache | `l1_cache` (继承自 `data_cache`) | `S:64:128:4,L:L:m:W:X,32:2:4,128` |
| GPU L2 Cache | `l2_cache` (继承自 `data_cache`) | `N:256:128:16,L:B:m:F:X,64:4:6,128` |
| Texture Cache | `tex_cache` | 独立实现，含 FIFO/ROB |
| ReadOnly Cache | `read_only_cache` | `m_write_policy = READ_ONLY` |
| WriteThrough Cache | `data_cache` | `m_write_policy = WRITE_THROUGH` |
| WriteBack Cache | `data_cache` | `m_write_policy = WRITE_BACK` |
| WriteEvict Cache | `data_cache` | `m_write_policy = WRITE_EVICT` |
| Constant Cache | `read_only_cache` | ReadOnly + 小容量配置 |
| Instruction Cache | `read_only_cache` | ReadOnly 配置 |

### 关键内部组件

```
baseline_cache 内部结构:
├── m_tag_array          ← Tag 存储阵列 (tag, valid, dirty, lru state)
├── m_mshrs              ← Miss Status Holding Registers
├── m_bandwidth_management ← 带宽管理 (data_port + fill_port)
├── m_cache_miss_buffer  ← 等待发射的未命中请求队列
└── m_cache_fill_buffer  ← 等待填充的请求队列
```

### 优势
1. **最完整**：原生支持所有 GPU 需要的 Cache 类型和策略
2. **高度参数化**：从 bank 数量到 sector 大小，几乎所有维度可配
3. **经过验证**：ISCA 2020 论文发表，与 NVIDIA 真实 GPU 进行了校准
4. **Trace 兼容**：GPGPU-Sim 本身支持 trace-driven 仿真模式

### 劣势
1. **耦合度高**：与 GPGPU-Sim 框架紧密耦合，需要较大精力解耦
2. **代码量大**：单个 gpu-cache.cc 文件数千行
3. **依赖多**：依赖 GPGPU-Sim 的地址解码、内存接口等模块

---

## 2. ChampSim CACHE 模型（备选方案）

### 仓库信息
- **仓库**: https://github.com/ChampSim/ChampSim
- **关键源文件**: `src/cache.cc`, `inc/cache.h`
- **替换策略目录**: `replacement/<policy_name>/` (lru, srrip, drrip, ship, fifo 等)

### 核心参数

| 参数 | 说明 |
|------|------|
| `NUM_SET` | Set 数量 |
| `NUM_WAY` | Way 数量(相联度) |
| `HIT_LATENCY` | 命中延迟(周期) |
| `FILL_LATENCY` | 填充延迟(周期) |
| `MSHR_SIZE` | MSHR 大小 |
| `PQ_SIZE` | 预取队列大小 |
| `OFFSET_BITS` | Block Offset 位数 |

### 内部结构

```cpp
// 简化的核心结构
class CACHE {
    uint32_t NUM_SET, NUM_WAY;
    uint32_t HIT_LATENCY, FILL_LATENCY;
    std::vector<BLOCK> block;  // NUM_SET * NUM_WAY 个 block

    // 处理流水线
    std::vector<PACKET_QUEUE> RQ, WQ, PQ;  // 请求队列
    // MSHR
    std::vector<MSHR> MSHR;  // 大小 = MSHR_SIZE
};
```

### 替换策略模块化接口

```cpp
// 每个替换策略需实现的钩子函数
uint32_t find_victim(cpu, instr_id, set, current_set, ip, addr, type);
void update_replacement_state(cpu, set, way, addr, ip, victim_addr, type, hit);
```

### 优势
1. **代码清晰**：比 GPGPU-Sim 更现代、模块化的 C++ 设计
2. **替换策略丰富**：LRU, SRRIP, DRRIP, SHIP, FIFO 等多个策略
3. **Trace-driven 天然支持**：ChampSim 核心就是 trace 驱动的
4. **社区活跃**：Texas A&M 维护，持续更新

### 劣势
1. **缺少 GPU 特性**：无 Bank 建模、无 Sector Cache、无 Texture Cache
2. **缺少多种写策略**：默认只有 WriteBack，无 WriteThrough/WriteEvict 等
3. **无 Bank 冲突建模**
4. **纯 CPU 导向**

---

## 3. Octopus Cache 模型

### 仓库信息
- **仓库**: https://github.com/FanosResearch/OctopusSimulator
- **论文**: IEEE 2024
- **配置**: CSV 文件驱动，通过 `-p` CLI 覆盖参数

### 特点
- 2024 年新项目，C++ 模块化设计
- CSV 配置 = 改变参数不需要重新编译
- 支持 Bank、互联拓扑、一致性协议

### 对比评价
- 较新，社区小，缺少 GPU Cache 类型的原生支持

---

## 4. muditbhargava66/CacheSimulator

### 仓库信息
- **仓库**: https://github.com/muditbhargava66/CacheSimulator
- **语言**: C++20

### 特点
- 支持 MESI 一致性协议
- 支持预取、Victim Cache
- 支持 L1/L2/L3 多级
- 含 power/area 建模

### 对比评价
- 较新项目，功能较全但不成熟，无 GPU 特性

---

## 推荐方案

### 首选：提取 GPGPU-Sim Cache 模型并重构

**推荐理由：**
1. GPGPU-Sim 的 cache 模型是唯一一个**同时满足所有需求**的开源实现
2. 原生支持 L1/L2/Texture/ReadOnly/WriteThrough/WriteBack/WriteEvict 等多种 Cache 类型
3. 参数化程度最高：set/way/line/bank/sector/MSHR/replacement/write_policy/allocation 全覆盖
4. 经过真实 GPU 验证（ISCA 2020 Accel-Sim 论文）
5. 支持 trace-driven 仿真

**实施路径建议：**

```
提取目标文件:
  src/gpgpu-sim/gpu-cache.h        ← cache_t, baseline_cache, data_cache, read_only_cache, l1_cache, l2_cache
  src/gpgpu-sim/gpu-cache.cc       ← 实现
  src/gpgpu-sim/addrdec.h          ← 地址解码 (set index / bank index 计算)
  src/gpgpu-sim/addrdec.cc         ← 实现
  src/abstract_hardware_model.h    ← cache_config 定义 (去除 GPGPU-Sim 特定依赖)
  src/gpgpu-sim/mem_fetch.h        ← 内存请求数据结构
  src/gpgpu-sim/mem_fetch.cc       ← 实现

重构方向:
  1. 解耦 cache_config → 独立成 openCache 自己的配置系统
  2. 解耦 mem_fetch → 简化为统一的 cache_request 结构体
  3. 去除 gpgpu_sim 全局指针 → 通过构造函数注入依赖
  4. 保留: tag_array, MSHR, bandwidth_management, 替换/写策略
  5. 添加: 更多替换策略 (RRIP, PLRU, LFU)
  6. 添加: 统一的 trace 接口
```

### 备选：基于 ChampSim 扩展 GPU 特性

如果 GPGPU-Sim 的解耦成本过高，可以基于 ChampSim 添加：
1. Bank 建模（在 tag check 之前添加 bank hashing）
2. 多种写策略（WriteThrough, WriteEvict, ReadOnly 等）
3. Sector Cache 支持
4. Texture Cache 类型标记

此方案代码更干净，但需要添加较多 GPU 特性。

### 备选 2：libCacheSim 作为轻量替代

如果只需要功能性 trace 分析（不需要 cycle-accurate）：
- **仓库**: https://github.com/1a1a11a/libCacheSim
- 纯 C 库，超轻量，专注 trace 分析
- 支持 10+ 种替换策略
- 但不支持 Bank、Sector、各种写策略等 GPU 特性
