项目目前只有 README，无法构建或启动。本次增加 Godot 4.7.2 原生 C++ GDExtension 启动场景、独立状态/计时核心、SCons 构建及固定提交的 godot-cpp/GoogleTest 依赖。

暂停和升级选择冻结生存时间；到达 1200 秒进入胜利，死亡或胜利后冻结，显式重开清零。无效状态命令及负数/非有限时间被拒绝。

Windows 本地编译、21 项 GoogleTest、加速计时模拟、Godot Headless 原生加载及启动截图均已验证。CI 在 Ubuntu 24.04 上重复构建和无头测试。当前为 Phase 0 工程基线，尚无战斗玩法。

预算策略已记录每日 100 CNY；费用账本与硬预算执行器尚待完成，无人值守关闭。详见 docs/VALIDATION.md 和 orchestration/PROJECT_STATE.json。
