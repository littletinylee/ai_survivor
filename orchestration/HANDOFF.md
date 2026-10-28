# 恢复入口

正式工作目录 E:/AI Survior/ai_survivor，用户克隆。父目录保留早期断点。分支 feat/phase0-bootstrap 已推送；读取 docs/VALIDATION.md、PROJECT_STATE.json 和 TASK_QUEUE.json。

当前 Codex gpt-6.1-sol 完成原生场景、SCons、Windows 构建/测试脚本、工具链锁及 Linux CI；修复状态命令来源错误和 Godot 导入编译对象问题。21 项单测、Windows Headless 和截图已验证。Linux 构建通过，集成失败；提交 7ae82cd 为 CI 增加错误注释便于通过公开 API读取。main 尚未合并，PR 尚未创建。

Claude 先前 403，禁止循环重试；DeepSeek deepseek-flash 探针成功但沙箱编译受阻。仅在能力/预算就绪后再路由，不需要重复空探针。本轮没有额外调用付费外部模型，Codex 费用无可获取账本。

下一步定位 CI、完成 PR 验收，然后补预算和有界执行器。不要将计时模拟描述为完整战斗模拟。100 CNY/日，北京时间自然日；无可核验成本时不启动无人值守。
