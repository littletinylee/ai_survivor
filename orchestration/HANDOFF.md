# Phase 0 交接

用户要求见 docs/PROJECT_BRIEF.md。新项目位于 E:/AI Survior，远端 git@github.com:littletinylee/ai_survivor.git；远端仅 README，基线 410f406bb05bdf18e1cd22aa105ad21b7af3f9ad。保留 E:/project/survivor_like 旧项目，不迁入其 GDScript 逻辑。

Claude Code 2.1.312 登录状态 true；单次 claude-haiku-5-5 请求返回 HTTP 403 provider Terms Of Service，零 token、cost=0；不重试。尚未创建代码变更。DeepSeek Harness headless 使用默认 deepseek-flash，实际探针返回 READY。CLI 位于用户安装目录 resources/runtime/cli/bin/dsh.cmd。

Codex 已完成架构和开发规范。下一位实现者负责最小 GDExtension、状态/计时核心及测试、SCons、Windows 构建脚本和 CI 草稿；不可提交或推送。结束后 Codex 独立审查并运行测试。

本轮因 Computer Use 无法确认浏览器 URL 强制停止。DeepSeek exec session 58432 已 Ctrl-C，中断退出码 1；core/tests 为部分实现，尚未编译。下轮先检查工作树，再完成构建和集成层。不得视为 Phase 0 完成。100 CNY/日已获用户授权。
