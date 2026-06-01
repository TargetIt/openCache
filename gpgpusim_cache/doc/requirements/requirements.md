# gpgpusim_cache 版本需求

本文件记录 GPGPU-Sim 版本相关需求。项目级需求先进入 `../../doc/requirements/requirements.md`，如果需求影响 GPGPU-Sim 版本，再同步摘录到这里。

## REQ-20260601-001-C GPGPU-Sim 版本文档规范化

### 原始来源

来自项目级需求 `REQ-20260601-001`。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GPGPUSIM-DOC-001 | 将 `gpgpusim_cache` 的版本说明文档统一放入 `gpgpusim_cache/doc/` | 已完成 |
| GPGPUSIM-DOC-002 | 保留 `README.md` 作为版本入口文档 | 已完成 |
| GPGPUSIM-DOC-003 | 保留 `USER_GUIDE.md` 作为详细用户手册 | 已完成 |
| GPGPUSIM-DOC-004 | 保留已有 `doc/遗留问题_20260528_230000.md` | 已完成 |
| GPGPUSIM-DOC-005 | 建立 `gpgpusim_cache/doc/requirements/requirements.md` 作为版本级需求入口 | 已完成 |

### 验收标准

1. `gpgpusim_cache/doc/README.md` 存在。
2. `gpgpusim_cache/doc/USER_GUIDE.md` 存在。
3. `gpgpusim_cache/doc/requirements/requirements.md` 存在。
4. 修改 GPGPU-Sim 版本代码或文档后，交付前运行 `gpgpusim_cache/run.sh`。

### 更新时间

2026-06-01

## 新需求追加区

后续 GPGPU-Sim 版本需求从这里继续追加。

