# gem5 Cache 参考实现 — 用户手册

本目录包含从 gem5 提取的**原始未修改**缓存代码，配合 stub 层可独立编译运行。适用于 GPU/CPU 架构师、性能建模工程师快速理解 gem5 缓存模型。

---

## 目录

1. [架构总览](#1-架构总览)
2. [核心组件接口](#2-核心组件接口)
   - 2.1 [CacheBlk — 缓存块](#21-cacheblk--缓存块)
   - 2.2 [BaseCache — 缓存基类](#22-basecache--缓存基类)
   - 2.3 [MSHR — Miss Status Holding Register](#23-mshr--miss-status-holding-register)
   - 2.4 [Tag Storage — 标签存储](#24-tag-storage--标签存储)
   - 2.5 [Replacement Policies — 替换策略](#25-replacement-policies--替换策略)
3. [类层次结构](#3-类层次结构)
4. [使用示例](#4-使用示例)
5. [文件结构](#5-文件结构)

---

## 1. 架构总览

gem5 缓存模型采用**模块化分层架构**，每个组件都是独立的、可替换的 SimObject：

```
Cache / NoncoherentCache
  ├── BaseCache
  │     ├── MSHRQueue (miss handling)
  │     ├── WriteQueue  (write buffering)
  │     ├── compressor   (optional compression)
  │     ├── prefetcher   (optional prefetching)
  │     └── tags
  │           ├── indexing_policy     (address → set mapping)
  │           ├── replacement_policy  (victim selection)
  │           └── partitioning_policy (way partitioning, optional)
  └── [inherits SimObject → ClockedObject]
```

### 与 GPGPU-Sim Cache 的关键差异

| 维度 | gem5 Cache | GPGPU-Sim Cache |
|------|-----------|-----------------|
| 缓存块状态 | INVALID + coherence bits (MOESI) | INVALID/RESERVED/VALID/MODIFIED |
| 标签查找 | 分离 insert/probe/fill 接口 | 一体化 tag_array::probe() |
| MSHR 设计 | 独立 MSHR + MSHRQueue 类 | 内嵌在 baseline_cache 中 |
| 替换策略 | 独立可插拔 Policy 对象 | 硬编码 LRU/FIFO (tag_array) |
| 写策略 | WriteAllocator 分离 | 内嵌函数指针分派 |
| 端口模型 | RequestPort/ResponsePort 连接 | mem_fetch_interface 抽象 |
| 压缩 | 完整压缩框架 | 不支持 |

---

## 2. 核心组件接口

### 2.1 CacheBlk — 缓存块

```cpp
class CacheBlk : public TaggedEntry {
    // === 状态查询 ===
    bool isValid() const;
    bool isDirty() const;
    bool isSet(CoherenceBits bit) const;

    // === 生命周期 ===
    void insert(const KeyType &tag, int srcMasterId, uint32_t taskId, uint64_t partitionId);
    void invalidate();

    // === Coherence ===
    // Bits: WritableBit, DirtyBit, ReadableBit, SecureBit, etc.
    void setCoherenceBits(unsigned bits);
    unsigned getCoherenceBits() const;

    // === 引用计数 (并发访问追踪) ===
    unsigned getRefCount() const;
    void increaseRefCount();

    // === 访问追踪 ===
    void setPosition(uint32_t set, uint32_t way);
    uint32_t getSet() const;
    uint32_t getWay() const;
};
```

**关键设计点**：
- **Coherence Bits** 使用位掩码而非枚举状态机（INVALID/VALID/MODIFIED），支持 MOESI 全部状态
- **RefCount** 追踪有多少并发请求正在使用该块，用于安全驱逐检查
- `extractTag` 函数指针在 insert 前必须通过 `registerTagExtractor()` 设置

### 2.2 BaseCache — 缓存基类

```cpp
class BaseCache : public ClockedObject {
    // === 核心接口 ===
    virtual bool access(PacketPtr pkt, CacheBlk *&blk, Cycles &lat, PacketList &writebacks);
    virtual void recvTimingReq(PacketPtr pkt);

    // === 流水线推进 ===
    virtual void memWriteback() override;
    virtual void memInvalidate() override;

    // === 统计信息 ===
    CacheStats &getStats();

    // === 配置参数 (通过 BaseCacheParams) ===
    //   tagLatency, dataLatency, responseLatency
    //   mshrs, writeBuffers, tgtsPerMSHR
    //   clusivity (mostly_incl / mostly_excl)
    //   sequentialAccess
};
```

### 2.3 MSHR — Miss Status Holding Register

```cpp
class MSHR : public QueueEntry {
    // 核心功能: 合并对同一地址的多个请求
    // - 维护待完成 miss 的目标列表 (Target list)
    // - 支持读后写冲突保护
    // - 支持多级缓存的 pending 信号传递
};
```

### 2.4 Tag Storage — 标签存储

```cpp
class BaseTags : public ClockedObject {
    // === 查找接口 ===
    virtual CacheBlk* findBlock(const KeyType &key) const = 0;

    // === 修改接口 ===
    virtual CacheBlk* findVictim(const KeyType &key, ...) = 0;
    virtual void insertBlock(PacketPtr pkt, CacheBlk *blk) = 0;
    virtual void invalidate(CacheBlk *blk);
    virtual void accessBlock(CacheBlk *blk, ...);
};
```

**派生类**：
- `BaseSetAssoc` — 标准组相联 (set_idx → ways → victim)
- `FALRU` — 全相联 LRU 追踪（维护全局访问时间戳）
- `SectorTags` — Sector 缓存（一行 = 多个 sector 块）
- `CompressedTags` — 压缩感知的标签存储

### 2.5 Replacement Policies — 替换策略

所有替换策略继承自 `replacement_policy::Base` (它本身是 SimObject)：

```cpp
class Base : public SimObject {
    virtual void touch(const std::shared_ptr<ReplacementData> &data) = 0;
    virtual void invalidate(const std::shared_ptr<ReplacementData> &data) = 0;
    virtual void reset(const std::shared_ptr<ReplacementData> &data) = 0;
    virtual ReplaceableEntry* getVictim(const ReplacementCandidates &cands) = 0;
    virtual std::shared_ptr<ReplacementData> instantiateEntry() = 0;
};
```

**支持的替换策略**：
| 策略 | 说明 | 复杂度 |
|------|------|--------|
| LRU | 最近最少使用 | O(1) per touch, O(n) getVictim |
| FIFO | 先进先出 | O(1) |
| MRU | 最近最常使用 | O(1) |
| Random | 随机替换 | O(1) |
| TreePLRU | 树型伪 LRU | O(log n) |
| BIP | 双模插入策略 (LRU + MRU 混合) | O(1) |
| BRRIP | 基于 RRPV 的抗抖动策略 | O(1) |
| SHiP | 基于签名的命中预测 | O(1) + lookup |
| LFU | 最不经常使用 | O(1) |
| SecondChance | 二次机会 (FIFO扩展) | O(1) |
| WeightedLRU | 加权 LRU | O(1) |
| Dueling | 自适应策略对决 | O(1) + 统计 |

---

## 3. 类层次结构

```
SimObject
  ├── ClockedObject
  │     ├── BaseCache
  │     │     ├── Cache
  │     │     └── NoncoherentCache
  │     └── BaseTags
  │           ├── BaseSetAssoc
  │           ├── FALRU
  │           └── SectorTags
  │                 └── CompressedTags
  ├── BaseIndexingPolicy
  │     ├── SetAssociative
  │     └── SkewedAssociative
  └── replacement_policy::Base
        ├── LRU
        ├── FIFO ─── SecondChance
        ├── MRU
        ├── Random
        ├── TreePLRU
        ├── BRRIP
        │     ├── BIP
        │     └── SHiP
        ├── LFU
        ├── WeightedLRU
        └── Dueling
```

---

## 4. 使用示例

### 创建替换策略

```cpp
LRURPParams p;
replacement_policy::LRU lru(p);

auto entry = lru.instantiateEntry();
lru.touch(entry);
lru.invalidate(entry);
lru.reset(entry);
```

### 创建索引策略

```cpp
SetAssociativeParams p;
p.size = 4096;     // cache size in bytes
p.entry_size = 64; // block size
p.assoc = 8;       // associativity
SetAssociative sa(p);

auto entries = sa.getPossibleEntries(/* addr */ 0x1000);
```

### 创建缓存块

```cpp
CacheBlk blk;
blk.registerTagExtractor([](Addr a) { return a; });
blk.insert(CacheBlk::KeyType{0x1000, false}, 0, 0, 0);

assert(blk.isValid());
blk.increaseRefCount();
blk.invalidate();
```

### 创建全相联标签存储

```cpp
FALRUParams p;
p.block_size = 64;
p.size = 4096;
p.tag_latency = Cycles(1);
p.min_tracked_cache_size = 0;
FALRU falru(p);

CacheBlk::KeyType key{0x1000, false};
CacheBlk *blk = falru.findBlock(key);
if (!blk) {
    auto result = falru.accessBlock(key, false);
    blk = result.first;
}
```

---

## 5. 文件结构

```
gem5_cache/
├── src/                          # UNMODIFIED gem5 source files
│   └── mem/cache/
│       ├── base.hh, base.cc                 (BaseCache — ~104KB / 50KB)
│       ├── cache.hh, cache.cc               (Cache — ~6KB / 62KB)
│       ├── cache_blk.hh, cache_blk.cc       (CacheBlk — ~19KB / 3KB)
│       ├── noncoherent_cache.hh/.cc         (NonCoherentCache)
│       ├── mshr.hh, mshr.cc                 (MSHR — ~18KB / 28KB)
│       ├── mshr_queue.hh/.cc                (MSHRQueue)
│       ├── write_queue.hh/.cc               (WriteQueue)
│       ├── write_queue_entry.hh/.cc         (WriteQueueEntry)
│       ├── queue.hh, queue_entry.hh         (Queue infrastructure)
│       ├── cache_probe_arg.hh               (CacheProbeArg)
│       ├── tags/
│       │   ├── base.hh/.cc                  (BaseTags)
│       │   ├── base_set_assoc.hh/.cc        (BaseSetAssoc)
│       │   ├── fa_lru.hh/.cc                (FALRU)
│       │   ├── sector_tags.hh/.cc           (SectorTags)
│       │   ├── sector_blk.hh/.cc            (SectorCacheBlk)
│       │   ├── super_blk.hh/.cc             (SuperBlk)
│       │   ├── compressed_tags.hh/.cc       (CompressedTags)
│       │   ├── dueling.hh/.cc               (Tag Dueling)
│       │   ├── tagged_entry.hh              (TaggedEntry — base)
│       │   ├── indexing_policies/
│       │   │   ├── base.hh                  (BaseIndexingPolicy)
│       │   │   ├── set_associative.hh/.cc   (SetAssociative)
│       │   │   └── skewed_associative.hh/.cc(SkewedAssociative)
│       │   └── partitioning_policies/
│       │       ├── base_pp.hh/.cc           (BasePartitioningPolicy)
│       │       ├── way_pp.hh/.cc            (WayPartitioningPolicy)
│       │       ├── max_capacity_pp.hh/.cc   (MaxCapacityPartitioningPolicy)
│       │       ├── way_allocation.hh/.cc    (WayAllocation)
│       │       └── partition_manager.hh/.cc (PartitionManager)
│       └── replacement_policies/
│           ├── base.hh                      (Base RP)
│           ├── replaceable_entry.hh         (ReplaceableEntry)
│           ├── lru_rp.hh/.cc, fifo_rp.hh/.cc
│           ├── mru_rp.hh/.cc, random_rp.hh/.cc
│           ├── tree_plru_rp.hh/.cc
│           ├── bip_rp.hh/.cc, brrip_rp.hh/.cc
│           ├── ship_rp.hh/.cc
│           ├── lfu_rp.hh/.cc, second_chance_rp.hh/.cc
│           ├── weighted_lru_rp.hh/.cc
│           └── dueling_rp.hh/.cc
├── gem5_stubs/                   # Stub layer for standalone compilation
│   ├── stubs.cc                  (implementation of stub symbols)
│   ├── base/                     (types, stats, logging, bitfield, ...)
│   ├── sim/                      (SimObject, ClockedObject, Event, ...)
│   ├── mem/                      (Packet, Request, Port, ...)
│   ├── params/                   (73 auto-generated param stubs)
│   ├── debug/                    (Debug flag stubs)
│   ├── enums/                    (Enum stubs)
│   ├── cpu/                      (CPU stubs)
│   └── arch/                     (Architecture stubs)
├── test/
│   └── test_main.cc              (13 unit/integration tests)
├── CMakeLists.txt                (CMake build)
├── run.sh                        (One-click build & test)
├── README.md                     (This overview)
└── USER_GUIDE.md                 (This manual)
```
