# 验证记录

## 2026-10-29：Windows 原生启动与计时核心

目录 `E:/AI Survior/ai_survivor`，PowerShell 7，Python 3.11，SCons 4.11.1，MSVC 工具集 14.50.35717（cl 19.50.35728）。引擎 `4.7.2.stable.official.ed1daf0bf`，下载经官方 SHA512 校验；子模块提交见 tools/toolchain-lock.json。

- `./tools/build.ps1`：扩展 DLL、纯 C++ GoogleTest 程序编译链接成功。
- `./tools/test.ps1`：21 项测试通过；覆盖状态表、无效转移、终局锁定、负数/非有限时间、暂停/升级冻结、精确 1200 秒、超大步长、死亡冻结及重开；两项模拟测试包含每次 240000 步的 20 分钟计时模拟及重复一致性。
- 同一脚本运行 Godot Headless 导入及原生场景，输出 `AI_SURVIVORS_INTEGRATION_OK`，退出码 0，无 ERROR 日志。
- 自动录帧：Godot `--write-movie build/preview.png --quit-after 2 --fixed-fps 30`。检查 `build/preview00000001.png`（1100×680），文字完整居中、无裁切，正常窗口保持开启的场景可用。
- 测试曾发现菜单调用 resume 错误进入 Playing，已按命令语义限制来源状态；原有回归测试通过。
- 测试脚本曾捕获 Godot 对编译对象文件的错误导入（引擎退出 0 仍被判失败），已用 .gdignore 隔离构建和源码目录。

CI 结果待补充。当前模拟仅验证计时/状态，尚不验证战斗、敌人、经验或资源容量；Phase 1 实现后补充。
