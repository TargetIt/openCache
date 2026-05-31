# openCache 需求文档

## 原始需求

在开源项目中（优先从 GPGPU-Sim 中）寻找一个通用的、高度参数化、可配置的 Cache 模型。该 Cache 需要满足以下要求：

1. **多场景可例化**：能够被例化为 GPU 的 L1 Cache、L2 Cache、Texture Cache、ReadOnly Cache、WriteOnly Cache、WriteThrough Cache 等多种场景
2. **高度参数化**：各项参数均可方便配置，包括但不限于：
   - Cache Line 大小
   - Bank 数量
   - Set 数量（组相联度）
   - Way 数量
   - 容量大小
   - 读写策略（Read/Write/ReadWrite）
   - 写策略（WriteThrough / WriteBack / WriteEvict 等）
   - 替换策略（LRU / FIFO / Random / RRIP 等）
   - 访问延迟参数（hit latency, miss latency）
   - 端口数
   - 是否支持 MSHR（Miss Status Holding Register）
   - 是否支持 Prefetch
3. **性能模型友好**：在 trace-driven 性能模型评估中，可以方便调节任何参数以寻求最优配置
4. **参考来源优先级**：CPU > GPU > NPU 开源项目

## 分解需求

### 功能需求

| 编号 | 需求 | 描述 |
|------|------|------|
| F1 | 可配置的 Cache 几何结构 | 支持配置 set 数、way 数、line 大小、bank 数 |
| F2 | 多级 Cache 支持 | 可例化为 L1/L2/L3 等不同层级的 Cache |
| F3 | 多种 Cache 类型 | 支持 Data Cache、Instruction Cache、Texture Cache、Constant Cache、ReadOnly Cache |
| F4 | 写策略可配 | WriteThrough、WriteBack、WriteEvict、WriteOnce |
| F5 | 替换策略可配 | LRU、FIFO、Random、RRIP、PLRU、LFU 等 |
| F6 | MSHR 支持 | 支持 Miss Status Holding Register，可配置 MSHR 数量 |
| F7 | Bank 冲突建模 | 支持 Bank 级并行访问和冲突检测 |
| F8 | 端口可配置 | 读写端口数量可配置 |

### 接口需求

| 编号 | 需求 | 描述 |
|------|------|------|
| I1 | 统一的请求接口 | 对外提供统一的读写请求接口 |
| I2 | Trace 驱动接口 | 支持从 trace 文件读取访问序列进行性能评估 |
| I3 | 统计信息接口 | 提供 hit/miss rate、bank conflict、延迟分布等统计 |
| I4 | 回调/通知机制 | Miss 时通知上层或下层 Cache/内存 |

### 非功能需求

| 编号 | 需求 | 描述 |
|------|------|------|
| N1 | 高性能 | 性能模型中的 Cache 仿真应高效 |
| N2 | 可扩展 | 方便添加新的替换策略、写策略等 |
| N3 | 文档完善 | 各参数含义清晰，配置方式简单 |
