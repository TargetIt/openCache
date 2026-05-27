# GPGPU-Sim Cache 参考实现 — 用户手册

本目录包含从 GPGPU-Sim 提取的 **原始未修改** 缓存代码，可独立编译运行。适用于 GPU 架构师、性能建模工程师快速理解并集成缓存模型。

---

## 目录

1. [快速开始](#1-快速开始)
2. [架构总览](#2-架构总览)
3. [核心接口 API](#3-核心接口-api)
4. [配置系统](#4-配置系统)
5. [延迟配置](#5-延迟配置)
6. [吞吐量/带宽配置](#6-吞吐量带宽配置)
7. [缓存类型](#7-缓存类型)
8. [写策略](#8-写策略)
9. [替换策略](#9-替换策略)
10. [MSHR 系统](#10-mshr-系统)
11. [多级缓存 (L1/L2)](#11-多级缓存-l1l2)
12. [统计信息](#12-统计信息)
13. [完整集成示例](#13-完整集成示例)

---

## 1. 快速开始

```bash
# 一键构建+测试
./run.sh

# 手动构建
g++ -std=c++17 -Wall -g -O0 \
    gpgpu_cache/gpu_cache_ref.cc \
    test/test_main.cc \
    -I gpgpu_cache \
    -o test_gpgpu_cache_ref

./test_gpgpu_cache_ref

# CMake 构建
mkdir build && cd build && cmake .. && make
```

---

## 2. 架构总览

### 类层次结构

```
cache_t (抽象基类, 定义 access() 接口)
  |
  +-- baseline_cache (公共功能: 标签阵列、MSHR、带宽管理、统计)
  |     |
  |     +-- read_only_cache (只读缓存: 指令/常量/纹理类)
  |     |
  |     +-- data_cache (读写缓存: 可配置写/分配策略)
  |           |
  |           +-- l1_cache (L1 数据缓存: GLOBAL_ACC_W 写驱逐, 本地写回)
  |           |
  |           +-- l2_cache (L2 共享缓存: 写回+写分配)
  |
  +-- tex_cache (纹理缓存: FIFO 流水线模型, Igehy 1998)
```

### 核心组件

| 组件 | 类 | 说明 |
|------|-----|------|
| **标签阵列** | `tag_array` | 存储 cache line 的 tag、状态、替换元数据 |
| **MSHR** | `mshr_table` | 跟踪所有未命中请求，支持合并 |
| **配置** | `cache_config` | 所有几何/时序/策略参数 |
| **统计** | `cache_stats` | Hit/miss 计数、延迟分布、端口利用率 |
| **存储块** | `cache_block_t` / `line_cache_block` / `sector_cache_block` | 数据存储抽象 |

---

## 3. 核心接口 API

### 3.1 创建缓存

```cpp
#include "gpu_cache_ref.h"

// 每个缓存构造函数都需要以下参数:
//   name       - 缓存名称 (统计输出用)
//   config     - 缓存配置对象
//   core_id    - 核心/着色器 ID
//   type_id    - 缓存类型 ID
//   memport    - 连接到下级存储的接口 (mem_fetch_interface*)
//   ...        - 各子类特有的参数

// ---- 创建 L1 数据缓存 ----
cache_config l1_cfg;
l1_cfg.m_config_string = (char*)"S:32:128:4,L:B:m:F:X,A:32:4,64";
l1_cfg.init(l1_cfg.m_config_string, FuncCachePreferNone);

simple_mem_interface l2_mem(256);          // 下级存储接口
simple_mf_allocator allocator;             // mem_fetch 分配器
gpgpu_sim gpu;                             // GPU 模拟器存根

l1_cache l1("L1D", l1_cfg, 0, 0, &l2_mem, &allocator,
            IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

// ---- 创建 L2 缓存 ----
cache_config l2_cfg;
l2_cfg.m_config_string = (char*)"N:256:128:16,L:B:m:F:X,A:64:8,128";
l2_cfg.init(l2_cfg.m_config_string, FuncCachePreferNone);

simple_mem_interface dram(512);            // 主存
l2_cache l2("L2", l2_cfg, 0, 1, &dram, &allocator,
            IN_PARTITION_L2_TO_DRAM_QUEUE, &gpu, L2_GPU_CACHE);

// ---- 创建只读缓存 (指令/常量) ----
cache_config ro_cfg;
ro_cfg.m_config_string = (char*)"N:16:64:4,L:R:m:N:L,A:8:2,16";
ro_cfg.init(ro_cfg.m_config_string, FuncCachePreferNone);

read_only_cache ro("I-Cache", ro_cfg, 0, 2, &l2_mem,
                   IN_L1C_MISS_QUEUE, OTHER_GPU_CACHE, &gpu);
```

### 3.2 access() — 发送访问请求

```cpp
// 构建访问请求
mem_access_sector_mask_t smask;
smask.set(0);  // 至少设置一个 sector
mem_access_t access(GLOBAL_ACC_R, 0x1000, 4, false,   // 读, 4B
                    active_mask_t(), mem_access_byte_mask_t(), smask);
warp_inst_t inst;
mem_fetch mf(access, &inst, 0, 0, 0, 0, 0, NULL, 0);  // streamID=0

std::list<cache_event> events;
enum cache_request_status status = cache.access(mf.get_addr(), &mf, 1, events);

// status 可能值:
//   HIT              - 命中, 数据可用
//   HIT_RESERVED     - 命中但块尚未填充完成
//   MISS             - 未命中, 已向下级发送请求
//   RESERVATION_FAIL - 无法接受请求 (MSHR满, miss queue满等)
//   SECTOR_MISS      - Sector 缓存中该 sector 未命中
//   MSHR_HIT         - 该地址已有未完成的 MSHR 条目

switch (status) {
case HIT:
    // 数据立即可用, latency = hit_latency
    break;
case MISS:
    // 请求已入队, 等待 cycle() + fill()
    // 检查 events 是否有写回事件
    if (was_write_sent(events)) { /* 处理写回 */ }
    break;
case RESERVATION_FAIL:
    // 重试或阻塞
    break;
}
```

### 3.3 cycle() — 推进时钟

```cpp
// 每个周期调用, 将 miss queue 中的请求发送到下级存储
cache.cycle();
```

### 3.4 fill() — 处理下级响应

```cpp
// 当数据从下级存储返回时调用
cache.fill(&mf, current_time);
```

### 3.5 带宽/端口查询

```cpp
// 在发送请求前检查端口是否可用
if (cache.data_port_free()) {
    // 数据端口空闲, 可以发出请求
}

if (cache.fill_port_free()) {
    // 填充端口空闲, 可以接收 fill
}
```

### 3.6 access_ready() / next_access() — 读取就绪请求

```cpp
// 检查是否有等待填充完成的请求现在就绪
if (cache.access_ready()) {
    mem_fetch *ready = cache.next_access();
    // 处理已完成的请求
}
```

### 3.7 flush() / invalidate()

```cpp
cache.flush();       // 写回所有脏行
cache.invalidate();  // 直接无效化所有行 (不写回)
```

---

## 4. 配置系统

### 4.1 配置字符串格式 (GPGPU-Sim 兼容)

这是配置缓存最直接的方式:

```
格式: <缓存类型>:<set数>:<行大小>:<关联度>,<替换策略>:<写策略>:<分配策略>:<写分配策略>:<索引函数>,<MSHR类型>:<MSHR条目>:<最大合并>,<miss queue大小>[,<数据端口宽度>]

简写: <set数>:<行大小>:<关联度>     (其余使用默认值)
```

**字段含义:**

| 位置 | 字段 | 可选值 | 说明 |
|------|------|--------|------|
| 1 | 缓存类型 | `N`=Normal, `S`=Sector | Sector 缓存按 32B sector 粒度管理 |
| 2 | set 数 | 任何 2 的幂 | 如 32, 64, 128, 256 |
| 3 | 行大小 (B) | 32, 64, 128, 256 | Sector 缓存要求 line_size = 32×4=128 |
| 4 | 关联度 | 1, 2, 4, 8, 16, 24 | way 数量 |
| 5 | 替换策略 | `L`=LRU, `F`=FIFO | |
| 6 | 写策略 | `R`=ReadOnly, `B`=WriteBack, `T`=WriteThrough, `E`=WriteEvict, `L`=LocalWB+GlobalWT | |
| 7 | 分配策略 | `m`=ON_MISS, `f`=ON_FILL | ON_FILL 适合流式访问 |
| 8 | 写分配策略 | `N`=NoWA, `W`=WriteAlloc, `F`=FetchOnWrite, `L`=LazyFetchOnRead | |
| 9 | 索引函数 | `L`=Linear, `X`=BitwiseXOR, `H`=FermiHash, `P`=IPoly | |
| 10 | MSHR类型 | `A`=Assoc, `S`=SectorAssoc, `F`=TexFIFO, `T`=SectorTexFIFO | |
| 11 | MSHR条目数 | 16, 32, 64 | 未命中状态保持寄存器数量 |
| 12 | MSHR最大合并 | 4, 8 | 同一地址最多合并的请求数 |
| 13 | Miss Queue大小 | 32, 64, 128 | |
| 14 | 数据端口宽度 (选填) | 默认=line_size | |

### 4.2 常用配置示例

```cpp
// === L1 数据缓存 (Fermi 风格): 16KB, sector, write-evict ===
"S:32:128:4,L:E:m:F:X,A:32:4,64"

// === L1 数据缓存 (Volta 风格): 32KB, 局部写回, 全局写驱逐 ===
"S:64:128:4,L:L:m:F:X,A:32:4,64"

// === L2 共享缓存: 512KB, write-back, 16-way ===
"N:256:128:16,L:B:m:F:X,A:64:8,128"

// === L2 共享缓存 (大): 2MB, write-back, 16-way ===
"N:1024:128:16,L:B:m:F:X,A:128:8,256"

// === 只读指令缓存: 4KB ===
"N:16:64:4,L:R:m:N:L,A:8:2,16"

// === 纹理缓存: 32KB, sector ===
"S:64:128:4,L:R:m:N:P,F:128:4,128:2"

// === 写穿透缓存 ===
"N:64:64:8,L:T:m:N:L,A:16:4,32"

// === 流式缓存 (memcpy 优化): ON_FILL + FIFO ===
"N:16:128:4,F:E:f:N:L,A:128:1,256"

// === 高吞吐量缓存 (大 MSHR, 大 miss queue) ===
"N:128:128:8,L:B:m:F:X,A:128:16,256"
```

### 4.3 参数化配置 (代码方式)

```cpp
cache_config cfg;
cfg.m_cache_type         = SECTOR;            // 缓存类型
cfg.m_nset               = 32;                // set 数量
cfg.m_line_sz            = 128;               // 行大小 (B)
cfg.m_assoc              = 4;                 // 关联度
cfg.m_replacement_policy = LRU;               // 替换策略
cfg.m_write_policy       = LOCAL_WB_GLOBAL_WT; // 写策略
cfg.m_alloc_policy       = ON_MISS;           // 分配策略
cfg.m_write_alloc_policy = LAZY_FETCH_ON_READ; // 写分配策略
cfg.m_set_index_function = BITWISE_XORING_FUNCTION; // 索引函数
cfg.m_mshr_type          = SECTOR_ASSOC;      // MSHR 类型
cfg.m_mshr_entries       = 32;                // MSHR 条目数
cfg.m_mshr_max_merge     = 4;                 // 最大合并数
cfg.m_miss_queue_size    = 64;                // Miss queue 大小
cfg.m_data_port_width    = 32;                // 数据端口宽度 (B/cycle)

// 初始化后自动推导: line_sz_log2, nset_log2, atom_sz 等
// 检查: cfg.m_line_sz % cfg.m_data_port_width == 0
```

### 4.4 各参数对性能的影响

| 参数 | 增大效果 | 代价 |
|------|----------|------|
| `m_nset` | 减少冲突 miss | 增大面积 |
| `m_assoc` | 减少冲突 miss | 增加比较器/功耗 |
| `m_line_sz` | 利用空间局部性 | 增大 fill 延迟, 浪费带宽 |
| `m_mshr_entries` | 提升 MLP (并行未命中) | 面积 |
| `m_mshr_max_merge` | 减少对下级的重复请求 | 复杂度 |
| `m_miss_queue_size` | 缓冲更多未命中请求 | 面积 |
| `m_data_port_width` | 更高带宽 (字节/周期) | 布线/功耗 |

---

## 5. 延迟配置

延迟通过两个机制控制:

### 5.1 缓存命中延迟 (m_config 中隐式定义)

命中延迟取决于 `m_data_port_width` 和请求大小:

```
data_cycles = data_size / port_width + ((data_size % port_width > 0) ? 1 : 0)
```

**设置方式:** 通过 `m_data_port_width` 间接控制; 每次 HIT 占用 data port `data_cycles` 个周期。

### 5.2 Fill 延迟 (下级存储接口控制)

填充延迟由连接的下级 `mem_fetch_interface` 决定 — **不在 cache_config 中配置**:

```cpp
// 下级存储的延迟体现在 cycle() 发送请求 -> fill() 回调的时间差
// 调用者在外部控制:

for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {
    cache.cycle();  // 发送 miss queue 中的请求

    // 模拟下级处理延迟...
    if (memory_response_ready) {
        cache.fill(&returned_mf, cycle);
    }
}
```

### 5.3 端口占用延迟

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `m_data_port_width` | `m_line_sz` | 每个周期传输的字节数; 决定端口占用周期 |
| 请求大小 | `mf.get_data_size()` | 实际请求字节数 |
| Fill 端口占用 | `m_atom_sz / m_data_port_width` | 每次 fill 占用的周期数 |

**端口占用周期计算:**
```
// 读命中: 数据端口占用 = ceil(data_size / data_port_width)
// 写回 (脏行驱逐): 数据端口占用 = evicted.modified_size / data_port_width
// Fill: fill 端口占用 = atom_sz / data_port_width
```

### 5.4 典型延迟配置

```cpp
// 低延迟 L1: 宽端口, 快速响应
// data_port_width=128 (一次传输完整 cache line)
cfg.m_data_port_width = 128;  // 1 周期完成 128B 传输 → 命中延迟 1 cycle

// 高带宽 L2: 窄端口, 多周期传输
// data_port_width=32 (分 4 次传输 128B cache line)
cfg.m_data_port_width = 32;   // 4 周期完成 128B 传输 → 命中延迟 4 cycles
```

---

## 6. 吞吐量/带宽配置

### 6.1 关键吞吐量参数

```
Bandwidth = (req_size / data_cycles) × frequency
Throughput = min(data_port_bw, fill_port_bw, miss_queue_drain_rate)
```

| 参数 | 作用 | 吞吐量影响 |
|------|------|-----------|
| `m_data_port_width` | 每周期数据端口可传输字节数 | **直接决定峰值带宽** |
| `m_mshr_entries` | 并行未命中请求数 | **限制 MLP (Memory-Level Parallelism)** |
| `m_miss_queue_size` | 未命中请求缓冲深度 | **防止因反压丢请求** |
| `m_mshr_max_merge` | 同地址多请求合并 | **减少下级带宽消耗** |
| `data_port_free()` | 数据端口是否空闲 | 端口忙时新请求需等待 |
| `fill_port_free()` | 填充端口是否空闲 | fill 端口忙时无法接收响应 |

### 6.2 高吞吐量配置示例

```cpp
// GPU L2: 最大化吞吐量
// 64 MSHR × 8 合并 = 可同时处理 512 个请求
"N:256:128:16,L:B:m:F:X,A:64:8,128,32"

// 流式缓存: 最大化 MSHR
// 128 MSHR × 1 合并 (不合并, 避免 stall)
"N:16:128:4,F:E:f:N:L,A:128:1,256"
```

### 6.3 带宽利用率

```cpp
// 统计端口利用率
cache_sub_stats css;
cache.get_sub_stats(css);

float data_util = (float)css.data_port_busy_cycles / css.port_available_cycles;
float fill_util = (float)css.fill_port_busy_cycles / css.port_available_cycles;

printf("Data port utilization: %.2f%%\n", data_util * 100);
printf("Fill port utilization: %.2f%%\n", fill_util * 100);
```

---

## 7. 缓存类型

### 7.1 各类型对比

| 类型 | 类 | 读写 | 典型用途 |
|------|-----|------|----------|
| **L1 数据缓存** | `l1_cache` | 读写 | GPU SM 私有数据缓存 |
| **L2 缓存** | `l2_cache` | 读写 | GPU 共享 L2 |
| **只读缓存** | `read_only_cache` | 只读 | 指令/常量缓存 |
| **纹理缓存** | `tex_cache` | 只读 (FIFO) | 纹理采样 |
| **通用读写缓存** | `data_cache` | 读写 | 任何可配置场景 |

### 7.2 l1_cache vs l2_cache

```
l1_cache:
  - 继承 data_cache
  - 写分配类型固定为 L1_WR_ALLOC_R
  - 写回类型固定为 L1_WRBK_ACC
  - 典型配置: sector cache, write-evict/write-back

l2_cache:
  - 继承 data_cache
  - 写分配类型固定为 L2_WR_ALLOC_R
  - 写回类型固定为 L2_WRBK_ACC
  - 典型配置: normal cache, write-back, write-allocate
```

**核心事实:** L1 和 L2 使用相同的 `data_cache::access()` 实现 — 行为差异**完全由配置**决定(写策略、写分配策略、MSHR类型等)。

### 7.3 Normal vs Sector Cache

```
NORMAL (N):
  - 以整个 cache line 为最小管理单元
  - 一次 MISS 分配并填充整个 line
  - 简单, 适合: L2, 指令缓存

SECTOR (S):
  - 以 sector (32B) 为最小管理单元
  - 一次 SECTOR_MISS 只分配/填充一个 sector
  - 减少 fill 带宽, 更好的空间利用率
  - 适合: L1 数据缓存
```

---

## 8. 写策略

### 8.1 五种写命中策略

| 策略 | 枚举 | 命中行为 | 适用场景 |
|------|------|----------|----------|
| **READ_ONLY** | `READ_ONLY` | 不支持写 | I-Cache, 纹理缓存 |
| **WRITE_BACK** | `WRITE_BACK` | 标记 MODIFIED, 驱逐时写回 | L2, CPU cache |
| **WRITE_THROUGH** | `WRITE_THROUGH` | 同时写入本级和下级 | 一致性要求高 |
| **WRITE_EVICT** | `WRITE_EVICT` | 写命中后无效化该行 | GPU L1 (全局内存) |
| **LOCAL_WB_GLOBAL_WT** | `LOCAL_WB_GLOBAL_WT` | 本地写回, 全局写驱逐 | Fermi L1 |

### 8.2 四种写未命中(分配)策略

| 策略 | 枚举 | 未命中行为 |
|------|------|-----------|
| **NO_WRITE_ALLOCATE** | `NO_WRITE_ALLOCATE` | 不分配, 直接写穿透到下级 |
| **WRITE_ALLOCATE** | `WRITE_ALLOCATE` | 分配行 + 发送写请求 + 发送读请求 |
| **FETCH_ON_WRITE** | `FETCH_ON_WRITE` | 分配行 + 读请求获取整行 |
| **LAZY_FETCH_ON_READ** | `LAZY_FETCH_ON_READ` | 分配行, 标记 dirty, 仅在后续读时才 fetch |

### 8.3 策略选择指南

```
场景                            | 写策略      | 写分配策略
--------------------------------|-------------|-------------
GPU L1 (全局内存, stream)       | WRITE_EVICT | FETCH_ON_WRITE
GPU L1 (局部内存)               | WRITE_BACK  | LAZY_FETCH_ON_READ
GPU L2 (共享)                   | WRITE_BACK  | FETCH_ON_WRITE
CPU 独占 L1                     | WRITE_BACK  | FETCH_ON_WRITE
CPU 共享 L2                     | WRITE_BACK  | FETCH_ON_WRITE
I/O 一致性                      | WRITE_THROUGH | NO_WRITE_ALLOCATE
Memcpy/Streaming                | WRITE_EVICT | NO_WRITE_ALLOCATE
```

---

## 9. 替换策略

### 9.1 支持的策略

| 策略 | 配置字符 | 算法 |
|------|----------|------|
| **LRU** | `L` | 驱逐最近最少使用的行 (基于 `m_last_access_time`) |
| **FIFO** | `F` | 驱逐最早分配的行 (基于 `m_alloc_time`) |

### 9.2 脏行保护

写百分比参数 `m_wr_percent` 控制允许驱逐脏行的阈值:
- 脏行比例 < `m_wr_percent`% 时: **只驱逐干净行** (减少写回带宽)
- 脏行比例 >= `m_wr_percent`% 时: 允许驱逐脏行

```cpp
// 示例: 最多允许 50% 脏行
cfg.m_wr_percent = 50;  // 当脏行 < 50% 时, 只驱逐干净行
```

---

## 10. MSHR 系统

MSHR (Miss Status Holding Register) 管理所有未完成的未命中请求。

### 10.1 基本操作

```cpp
mshr_table mshr(32, 4);  // 32 条目, 每条目最多合并 4 个请求

// 检查是否有未完成的请求
if (mshr.probe(block_addr)) {
    // MSHR hit: 已有对该地址的未完成请求
}

// 检查是否可以接受新请求
if (!mshr.full(block_addr)) {
    mshr.add(block_addr, &mf);  // 添加或合并请求
}

// 数据返回时标记就绪
bool has_atomic = false;
mshr.mark_ready(block_addr, has_atomic);

// 取出就绪的请求
if (mshr.access_ready()) {
    mem_fetch *ready = mshr.next_access();
}
```

### 10.2 MSHR 类型

| 类型 | 配置字符 | 说明 |
|------|----------|------|
| **ASSOC** | `A` | 全相联 MSHR (普通缓存) |
| **SECTOR_ASSOC** | `S` | Sector 版本: 跟踪 pending_read 计数 |
| **TEX_FIFO** | `F` | 纹理缓存的 FIFO MSHR |
| **SECTOR_TEX_FIFO** | `T` | 纹理缓存的 Sector FIFO MSHR |

### 10.3 MSHR 对性能的影响

```
MLP (Memory-Level Parallelism) ≈ min(MSHR条目数, 实际并行未命中数)

增大 mshr_entries:
  + 更高 MLP → 更好掩盖 fill 延迟
  - 更多面积/功耗

增大 mshr_max_merge:
  + 减少对下级存储的重复请求
  - 合并的请求等待时间变长
```

---

## 11. 多级缓存 (L1/L2)

### 11.1 连接方式

```cpp
// L2 的下级是 DRAM
simple_mem_interface dram(512);
// L1 的下级是 L2 ... 但 L2 也是 cache_t, 不是 mem_fetch_interface!

// 需要适配器将 cache_t 包装为 mem_fetch_interface:
// (参考 openCache 项目的 CacheMemoryInterface 概念)
```

**关键限制:** 当前的 `mem_fetch_interface` 是 FIFO 队列模型。将 L1 连接到 L2 需要将 L2 的 `access()` 包装为 `push()`/`full()` 接口。这不在本参考实现的范围内, 但代码结构已为此预留了接口。

### 11.2 L1→L2 交互流程

```
1. SM 发出请求 → L1.access()
2. L1 Miss → L1 将请求放入 miss_queue
3. L1.cycle() → 从 miss_queue 取出, 发送到 L2
4. L2.access() → 可能 HIT / MISS / RESERVATION_FAIL
5. L2 Miss → L2 将请求放入自己的 miss_queue
6. L2.cycle() → 发送到 DRAM
7. DRAM 返回 → L2.fill() → L2 MSHR 就绪
8. L2.next_access() → 将响应返回给 L1
9. L1.fill() → L1 MSHR 就绪
10. L1.next_access() → 将数据返回给 SM
```

### 11.3 典型 L1+L2 配置

```cpp
// L1: 每个 SM 私有, 16KB, sector, write-evict for global
"S:32:128:4,L:E:m:F:X,A:32:4,64"

// L2: 所有 SM 共享, 512KB, write-back, 16-way
"N:256:128:16,L:B:m:F:X,A:64:8,128"
```

---

## 12. 统计信息

### 12.1 获取统计数据

```cpp
// 方式 1: 获取子统计
cache_sub_stats css;
cache.get_sub_stats(css);

printf("Accesses:      %llu\n", css.accesses);
printf("Misses:        %llu\n", css.misses);
printf("Hit Rate:      %.2f%%\n",
       100.0 * (1.0 - (double)css.misses / css.accesses));
printf("Pending Hits:  %llu\n", css.pending_hits);
printf("Reservation Fails: %llu\n", css.res_fails);
printf("Data Port Util:    %.2f%%\n",
       100.0 * css.data_port_busy_cycles / css.port_available_cycles);
printf("Fill Port Util:    %.2f%%\n",
       100.0 * css.fill_port_busy_cycles / css.port_available_cycles);

// 方式 2: 按类型/状态打印详细信息
cache.get_stats().print_stats(stdout, 0, "L1D");
cache.get_stats().print_fail_stats(stdout, 0, "L1D");
```

### 12.2 统计项说明

| 字段 | 说明 |
|------|------|
| `accesses` | 总访问次数 |
| `misses` | 总未命中次数 (MISS + SECTOR_MISS) |
| `pending_hits` | 命中但尚未填充完成的行 |
| `res_fails` | 因 MSHR/队列满导致的预留失败 |
| `data_port_busy_cycles` | 数据端口繁忙周期数 |
| `fill_port_busy_cycles` | 填充端口繁忙周期数 |
| `port_available_cycles` | 端口统计总周期数 |

---

## 13. 完整集成示例

### 13.1 基本 L1 缓存集成

```cpp
#include "gpu_cache_ref.h"

int main() {
    // 1. 创建下级存储
    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;

    // 2. 配置并创建 L1 缓存
    cache_config cfg;
    cfg.m_config_string = (char*)"S:32:128:4,L:B:m:F:X,A:32:4,64";
    cfg.init(cfg.m_config_string, FuncCachePreferNone);

    l1_cache cache("L1D", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

    // 3. 模拟访问序列
    for (int cycle = 0; cycle < 1000; cycle++) {
        // --- 发送新请求 ---
        if (has_pending_request()) {
            mem_access_sector_mask_t smask; smask.set(0);
            mem_access_t access(GLOBAL_ACC_R, get_addr(), 4, false,
                                active_mask_t(), mem_access_byte_mask_t(), smask);
            warp_inst_t inst;
            mem_fetch *mf = allocator.alloc(
                0, GLOBAL_ACC_R, 4, false, cycle, 0);

            std::list<cache_event> events;
            enum cache_request_status s = cache.access(mf->get_addr(), mf, cycle, events);

            if (s == HIT) {
                record_hit(mf, cycle);
            }
        }

        // --- 处理 fill 响应 ---
        if (mem_has_response()) {
            mem_fetch *resp = get_mem_response();
            cache.fill(resp, cycle);
        }

        // --- 处理就绪请求 ---
        while (cache.access_ready()) {
            mem_fetch *ready = cache.next_access();
            record_complete(ready, cycle);
        }

        // --- 推进缓存时钟 ---
        cache.cycle();
    }

    // 4. 打印统计
    cache_sub_stats css;
    cache.get_sub_stats(css);
    printf("Hit Rate: %.2f%%\n",
           100.0 * (1.0 - (double)css.misses / css.accesses));

    return 0;
}
```

### 13.2 参数扫描 — 寻找最优配置

```cpp
// 扫描 set 数以寻找最佳命中率
for (int nsets : {16, 32, 64, 128, 256}) {
    char cfg_str[64];
    snprintf(cfg_str, sizeof(cfg_str),
             "N:%d:128:4,L:B:m:F:X,A:32:4,64", nsets);

    cache_config cfg;
    cfg.m_config_string = cfg_str;
    cfg.init(cfg_str, FuncCachePreferNone);

    simple_mem_interface mem(256);
    simple_mf_allocator allocator;
    gpgpu_sim gpu;
    l1_cache cache("Sweep", cfg, 0, 0, &mem, &allocator,
                   IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);

    // 运行相同 trace
    run_trace(cache, "traces/benchmark.trace");

    cache_sub_stats css;
    cache.get_sub_stats(css);
    printf("nsets=%d: hit_rate=%.2f%%\n", nsets,
           100.0 * (1.0 - (double)css.misses / css.accesses));
}
```

### 13.3 多缓存并发集成

```cpp
// GPU 有多个 SM, 每个 SM 有私有的 L1
// 所有 L1 共享一个 L2

simple_mem_interface dram(512);
simple_mf_allocator allocator;
gpgpu_sim gpu;

// L2 共享缓存
cache_config l2_cfg;
l2_cfg.m_config_string = (char*)"N:256:128:16,L:B:m:F:X,A:64:8,128";
l2_cfg.init(l2_cfg.m_config_string, FuncCachePreferNone);
l2_cache l2("L2", l2_cfg, 0, 0, &dram, &allocator,
            IN_PARTITION_L2_TO_DRAM_QUEUE, &gpu, L2_GPU_CACHE);

// 每个 SM 的私有 L1
const int NUM_SM = 16;
l1_cache *l1_caches[NUM_SM];
for (int sm = 0; sm < NUM_SM; sm++) {
    cache_config l1_cfg;
    l1_cfg.m_config_string = (char*)"S:32:128:4,L:E:m:F:X,A:32:4,64";
    l1_cfg.init(l1_cfg.m_config_string, FuncCachePreferNone);
    l1_caches[sm] = new l1_cache(
        "L1D", l1_cfg, sm, 0, &l2_mem_adapter, &allocator,
        IN_L1D_MISS_QUEUE, &gpu, L1_GPU_CACHE);
}

// 每个周期: 驱动所有 L1 + L2
for (int cycle = 0; cycle < MAX_CYCLES; cycle++) {
    for (int sm = 0; sm < NUM_SM; sm++) {
        // 处理 SM 的请求...
        l1_caches[sm]->cycle();
    }
    l2.cycle();
}
```

---

## 附录 A: GPGPU-Sim → openCache 对应关系

| GPGPU-Sim (本目录) | openCache (上层项目) |
|-------------------|---------------------|
| `cache_config` | `CacheConfig` |
| `tag_array` | `TagArray` |
| `mshr_table` | `MSHRTable` |
| `cache_stats` | `CacheStats` |
| `baseline_cache` | `BaselineCache` |
| `read_only_cache` | `ReadOnlyCache` |
| `data_cache` | `DataCache` |
| `l1_cache` | (派生类已移除, 用配置区分) |
| `l2_cache` | (派生类已移除, 用配置区分) |
| `mem_fetch` | `CacheRequest` |
| `mem_fetch_interface` | `CacheMemoryInterface` |
| `cache_event` | `CacheEvent` |

## 附录 B: 文件结构

```
reference/
  gpgpu_cache/
    gpu-cache.h           — 原始 GPGPU-Sim 缓存头文件 (未修改)
    gpu-cache.cc          — 原始 GPGPU-Sim 缓存实现 (未修改)
    gpu_cache_ref.h        — 适配后的头文件 (只改了 #include)
    gpu_cache_ref.cc       — 适配后的实现 (只改了 #include + 3 个桩函数)
    gpgpu_stubs.h          — GPGPU-Sim 依赖的桩实现
  test/
    test_main.cc           — 12 个单元测试
    test_scenario.cc       — 场景集成测试 (本手册配套)
  USER_GUIDE.md            — 本手册
  README.md                — 简要说明
  CMakeLists.txt           — CMake 构建
  run.sh                   — 一键构建+测试
```
