# 当前状态（2026-10-29）

- 当前没有阻止常规开发的认证或工具链阻塞。仅验收 Windows；Linux 排障已取消。
- Claude Haiku/Sonnet 官方可用；DeepSeek 文件开发和 Subrouter 备用已验证。GitHub 插件 PR 操作已验证。
- Subrouter 每日 100 CNY 后台硬上限由用户确认；实际花费仍 unknown。
- PR #1、#2、#3 均经 Windows CI 通过后合并。P2-01 本地 100 测试、原生集成、2 个全新目录冷启动通过，等待新 PR 的 CI。
- 三类武器已实现并接入卡片；默认鱼 seed 1 自动策略约 151 秒阵亡，20 分钟平衡尚未证明。
- Sonnet 本次调用达到自设 1 USD 列表估价上限而停止，未重复启动同一调用；Codex 完成现有改动的修复与本地验证。没有证据表明每日 100 CNY 供应商上限耗尽，实际花费仍 unknown。
- 旧独立运行器已以 agent_stop 退出，PID 不存在；当前聊天接管，禁止同时启动另一个工作树写入者。
