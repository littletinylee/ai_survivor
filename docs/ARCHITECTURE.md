# 架构基线（Phase 0）

## 边界

`src/core` 为无引擎 C++17 确定性领域模型。GameStateManager 控制菜单、战斗、升级选择、暂停、胜利、失败；GameTimer 只接受战斗状态下的有限非负增量，累计上限 1200 秒。游戏结束后不可继续累计。首个可独立验收任务是状态和计时规则。

`src/gameplay` 为 Godot 适配层：C++ Node 注册、输入映射、物理帧固定步长驱动、数据快照显示。初始最小场景仅证明真实扩展加载和调用，不宣称实现游戏。Headless 测试必须由 C++ 场景发出唯一成功标记并主动退出；加载失败/超时必须失败。

后续 PlayerController、EnemyManager、WeaponManager、ProjectileSystem、DamageSystem、ExperienceSystem、LevelUpSystem、UpgradeCardSystem、WaveDirector 为纯逻辑组件；数据配置与图形资源通过 ID 引用。CharacterDefinition 包含总控要求的所有属性；加载时校验。UI 发送命令而非直接改核心状态。

## 后续确定性和容量

固定种子 RNG，随机抽卡用加权不放回抽样。先过滤持有武器、六武器容量、等级上限和条件；不足三张返回实际数量。连续升级进入奖励队列，全部选择完成才恢复计时。模拟使用固定步长、最大实体容量和有界生成策略，死亡优先级需在 Phase 1 明确并测试。

## 工具链

引擎固定 4.7.2.stable.official.ed1daf0bf。godot-cpp 已采用独立版本体系，不能猜测不存在的 godot-4.7.2 tag；固定 commit 1b089f20a959e67e79e32e352a57065b3058dbfd，并使用该引擎实际导出的 tools/extension_api-4.7.2.json。兼容性以实际编译和加载结果为准。

MSVC 14.50.35717，Python 3.11，SCons 4.11.1；GoogleTest v1.17.0 commit 52eb8108c5bdec04579160ae17225d66034bd723。SCons 构建扩展和 GoogleTest 程序，避免另建 CMake 构建链。

## 自动化与恢复

同步单任务有界执行：读取状态→检查预算和凭据→路由→独占写入→构建测试→Codex 审查→PR→通过才合并。预算未知不得宣称能持续无人值守。初期仅进行当前监督周期，不配置重复唤醒。保存 provider、模型请求值、实际响应、任务 ID、分支、测试及失败原因，切换不重置工作树。
