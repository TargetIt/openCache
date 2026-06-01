# gem5_cache 版本需求

本文件记录 gem5 版本相关需求。项目级需求先进入 `../../doc/requirements/requirements.md`，如果需求影响 gem5 版本，再同步摘录到这里。

## REQ-20260601-001-B gem5 版本文档规范化

### 原始来源

来自项目级需求 `REQ-20260601-001`。

### 拆解需求

| 子需求 | 内容 | 状态 |
|--------|------|------|
| GEM5-DOC-001 | 将 `gem5_cache` 的版本说明文档统一放入 `gem5_cache/doc/` | 已完成 |
| GEM5-DOC-002 | 保留 `README.md` 作为版本入口文档 | 已完成 |
| GEM5-DOC-003 | 保留 `USER_GUIDE.md` 作为详细用户手册 | 已完成 |
| GEM5-DOC-004 | 建立 `gem5_cache/doc/requirements/requirements.md` 作为版本级需求入口 | 已完成 |

### 验收标准

1. `gem5_cache/doc/README.md` 存在。
2. `gem5_cache/doc/USER_GUIDE.md` 存在。
3. `gem5_cache/doc/requirements/requirements.md` 存在。
4. 修改 gem5 版本代码或文档后，交付前运行 `gem5_cache/run.sh`。

### 更新时间

2026-06-01

## 新需求追加区

后续 gem5 版本需求从这里继续追加。

