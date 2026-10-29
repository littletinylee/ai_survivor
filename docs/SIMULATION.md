# 无头完整局模拟

本文说明如何通过 Godot Headless 运行纯 C++ 核心 `run_simulation`，并读取其报告。

## 运行

先用 `tools/test.ps1` 完成导入（本脚本不做导入）。然后：

```powershell
pwsh tools/simulate.ps1                               # 默认：seed 1, kite, deepseek_big_fish
pwsh tools/simulate.ps1 -Seed 42 -Policy stationary
pwsh tools/simulate.ps1 -CharacterId deepseek_big_fish -RequireVictory
```

参数：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `-Seed` | `1` | 0 到 4294967295 的整数 |
| `-Policy` | `kite` | `kite` 或 `stationary` |
| `-CharacterId` | `deepseek_big_fish` | 必须是 `data/characters` 中已加载的角色 id |
| `-Godot` | `.tools/godot-4.7.2/...console.exe` | 与 `test.ps1` 相同，版本必须为 4.7.2 |
| `-RequireVictory` | 关闭 | 开启时，结果不是 `Won` 则失败 |

脚本会在 `build/test-results/simulation-<seed>-<policy>.out.log` 与 `.err.log` 写入日志，180 秒超时后强制结束进程。以下情况都会以失败退出：进程非零退出、输出含 `ERROR:` / `SCRIPT ERROR:` / `FATAL:`、缺少或重复报告、报告字段缺失或不合法。

直接运行 Godot 也可以（需先导入）：

```text
Godot --headless --path <项目目录> -- --simulate-run --seed 42 --policy kite --character deepseek_big_fish
```

## 命令行参数

- `--simulate-run`：启用模拟模式。模拟模式下不进入正常战斗场景，也不进入帧循环。
- `--seed <n>`：非负 32 位整数，默认 `1`。
- `--policy kite|stationary`：默认 `kite`。
- `--character <id>`：默认 `deepseek_big_fish`。角色来自已加载的 JSON 数据，不使用硬编码默认值。

参数错误处理：

- 缺少值、值无效（非法 seed、未知 policy、空角色 id）、重复参数：输出错误并以非零退出。
- 在模拟模式中出现未知参数，或与 `--integration-test` / `--visual-*` 同时出现：以非零退出。
- 未加 `--simulate-run` 却使用 `--seed` / `--policy` / `--character`：以非零退出。
- 角色数据加载失败：以非零退出，不会回退到默认角色。

不带这些参数的正常启动与 `--integration-test` 行为不变。

## 报告

进程输出恰好一行，前缀为 `AI_SURVIVORS_SIMULATION_REPORT `，后接 JSON 对象（由 `JSON::stringify` 生成，键按字母排序）。

| 字段 | 含义 |
| --- | --- |
| `requested_seed` | 请求的种子 |
| `character_id` / `policy` | 角色与策略 |
| `normal_character` | 恒为 `true`，表示角色来自正常数据加载 |
| `final_state` | 结束状态：`Won` 或 `Lost` |
| `survival_seconds` | 生存时间（游戏计时器） |
| `hp` / `maxhp` | 结束时生命值 / 最大生命值 |
| `kills` / `level` / `xp` / `cards` | 击杀数、等级、累计经验、选择的卡牌数 |
| `invalid_selections` / `peak_pending_levels` | 无效选择次数 / 待处理升级峰值 |
| `peak_enemies` / `peak_projectiles` / `peak_orbs` / `peak_weapons` | 各项峰值数量 |
| `frames` | 推进的 Playing 帧数 |
| `trace_hash` | 16 位十六进制字符串（FNV-1a，每帧定点采样）；用字符串是为避免 uint64 在 JSON 中丢精度 |
| `error` | 非空表示模拟未能开始 |
| `iteration_limit` / `stalled` | 是否触达迭代上限 / 是否有卡牌池无可用卡牌 |
| `frozen_violations` / `terminal_violations` | 升级期间时钟推进 / 终局后时钟或状态变化的次数 |

**通过条件（退出码 0）**：结束状态为 `Won` 或 `Lost`，`error` 为空，无迭代上限、无 stalled，无效选择和两个违规计数均为 0，且峰值满足 `enemies ≤ 256`、`projectiles ≤ 512`、`orbs ≤ 512`、`weapons ≤ 6`。`Won` 要求生存时间恰为 1200 秒且 HP 大于 0；`Lost` 要求生存时间小于 1200 秒且 HP 为 0。

**`Lost` 是合法结果，不代表通过正常流程获胜。** 脚本和程序都不会强制胜利，也不会修改角色数值或使用作弊参数。

## 共享的帧与升级逻辑

模拟与正常场景使用同一套核心逻辑，没有单独的模拟规则：

- 每帧以固定步长 1/60 秒调用 `advance_run`，依次处理移动、战斗、计时器、经验与升级提示。
- 升级时进入 Upgrade 状态，游戏时间与战斗时间冻结；`choose_run_upgrade` 应用选择，并返回下一步状态。
- 模拟器替玩家自动选择卡牌（核心层决定，不影响时间）。
- 策略：`stationary` 从不移动；`kite` 远离附近敌人，否则走向最近的经验宝珠。
- 单局逻辑时间为 1200 秒。迭代上限为 100000 步，超限会在报告中标记。

## 受控单元测试夹具（仅用于验证胜利流程）

`tests/simulation/full_run_test.cpp` 中有一个受控夹具：把角色的 `max_hp` 设为 `1e6`，`pickup_radius` 设为 `1000`，使局面几乎必然走到 `Won`。

它**只证明胜利流程（时间、状态迁移、终局判定）能够走通**，不代表默认角色的平衡数值，也不应用于宣传或平衡评估。普通 CLI 模拟始终加载 `data/characters` 中的真实数据。

## 2026-10-29 Windows 实测基线

以下均为实际 JSON 角色，无数值覆盖。当前策略简单，结果只反映该自动策略，不能推断真人最佳表现。

| seed | 策略 | 结果 | 生存秒数 | 击杀 | 等级 | 卡片 | trace_hash |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | kite | Lost | 35.766667382 | 23 | 5 | 4 | bbceee1db25fcc3e |
| 2 | kite | Lost | 47.216667611 | 28 | 5 | 4 | 30e88ee657bae058 |
| 2 | stationary | Lost | 26.133333856 | 16 | 3 | 2 | 93ab356bedda63cb |

seed 1 重复运行报告完全一致。六项非法参数检查均退出 1；`-RequireVictory` 正确拒绝 Lost。普通鱼的 20 分钟可玩性仍待平衡工作，CI 当前只检验流程及不变量。
