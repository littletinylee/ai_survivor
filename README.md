# AI幸存者 / AI Survivors

原创 2D Roguelite 生存游戏。当前支持主菜单、数据驱动的角色选择、WASD/方向键移动、Esc 暂停和重开。战斗与经验系统仍在开发。

正式工作目录：`E:\AI Survior\ai_survivor`。父目录为早期断点副本，后续在本克隆继续。

## Windows 构建与验证

前置环境：Python 3.11、Visual Studio 2026 Build Tools（MSVC 14.50.35717，实测 cl 19.50.35728）、PowerShell 7、Git。依赖版本见 `tools/toolchain-lock.json`，禁止自动升级。

```powershell
./tools/setup.ps1
./tools/build.ps1
./tools/test.ps1
& ./.tools/godot-4.7.2/Godot_v4.7.2-stable_win64.exe --path .
```

Setup 固定子模块和 SCons，并以官方 SHA512 校验 Godot。测试先运行 GoogleTest，再导入项目和启动 Headless 原生场景；超时、错误日志、非零退出码或缺少成功标记均失败。

## 架构与状态

- `src/core`：不依赖 Godot 的状态机和 1200 秒生存计时器。
- `src/gameplay`：原生节点注册、启动 UI 与集成测试。
- `data/characters/*.json`：角色配置，加载时校验字段、属性与唯一 ID；空美术字段表示程序图形占位。
- `tests/unit`、`tests/simulation`：状态、计时和加速 20 分钟计时模拟；完整战斗模拟尚待 Phase 1。
- `.github/workflows/verify.yml`：PR/推送触发 Windows 构建、单测及 Godot Headless；当前只验收 Windows。
- `docs/ARCHITECTURE.md`、`docs/VALIDATION.md`：架构与验收证据。
- `orchestration`：任务队列、路由、预算、阻塞与交接。

预算为北京时间自然日 100 CNY。费用账本及可执行的硬预算控制尚待实现，无人值守循环保持关闭。
