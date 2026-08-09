# Playerbots 战场自动补位（等级贴近玩家）

## 目标

真人玩家排队战场时，随机机器人自动补位，使战场能开打；补位机器人：

1. 覆盖玩家当前所在的任意等级段（不限 10–19）
2. 等级不超过队列中最高真人等级 + 1
3. 装备偏普通（最多绿色 / uncommon）
4. **阵营人数**：敌方人数不得多于我方；可以少于或等于我方

## 非目标

- 不开启「无人时机器人自己狂开全段战场」（`RandomBotAutoJoinBG` 全段）——约 45 个机器人会被空队列拖散
- 不改竞技场（Arena）补位逻辑
- 不改战场内战术 AI
- 不改 `battleground_template` SQL 基线数据

## 背景

- `AiPlayerbot.RandomBotJoinBG = 1` 已启用：有真人排队时，`CheckBgQueue` 将对应 `activeBgQueue` 置位，随机机器人经 `BGJoinAction` 入队
- 现有入队条件只要求**同一 PvP 等级段**（如战歌 10–19），因此 10 级玩家可能配到 19 级机器人
- 战歌峡谷模板：`MinPlayersPerTeam = 5`，`MaxPlayersPerTeam = 10`；当前 `Battleground.Override.LowLevels.MinPlayers = 0`（未覆盖）
- 约 45 个随机机器人分布在 1–80、双阵营，严格「玩家等级～+1」的现成机器人经常不够

## 配置变更

编辑运行中的 `env/dist/etc/worldserver.conf` 与 `env/dist/etc/modules/playerbots.conf`（及对应 `.dist` 若需同步默认值）：

| 配置 | 值 | 说明 |
|---|---|---|
| `Battleground.Override.LowLevels.MinPlayers` | `1` | 低于满级时，每边 1 人即可开打 |
| `AiPlayerbot.RandomBotJoinBG` | `1` | 保持开启（已有） |
| `AiPlayerbot.RandomBotAutoJoinBG` | `0` | 保持关闭，避免全段空开 |
| `AiPlayerbot.RandomGearQualityLimit` | `2` | 随机机器人最多绿色 |
| `AiPlayerbot.BgJoinMaxLevelAbovePlayer` | `1` | **新增**：相对队列真人最高等级的允许上浮 |

装备上限变更后，已有机器人需重随装备才会生效（实现计划中写明操作步骤，例如等待 randomize 或执行文档约定的初始化命令）。

## 代码行为

### 触发条件

沿用现有路径：真人 `InBattlegroundQueue` 且未被邀请入场 → `activeBgQueue = 1` → 随机机器人 `BGJoinAction` 评估入队。

仅当该队列/等级段存在**至少一名真人玩家**时，才施加等级贴近约束。若将来开启 AutoJoin 且段内无真人，则不施加本约束（保持现有段内互打行为）。

### 等级贴近

新增配置 `AiPlayerbot.BgJoinMaxLevelAbovePlayer`（默认 `1`）：

1. 在 `CheckBgQueue` 汇总真人排队信息时，把该 `queueTypeId` + `bracketId` 下真人最高等级写入 `BattlegroundInfo`（例如新增字段 `maxPlayerLevel`；无真人则为 0）
2. 允许入队的机器人等级满足：`botLevel <= maxPlayerLevel + BgJoinMaxLevelAbovePlayer`，且仍落在该战场合法段内，并满足现有 `canJoinBg` / `shouldJoinBg`（人数缺口等）
3. **优先**：已在允许窗口内的空闲随机机器人直接入队（不调级）
4. **不足时**：从同阵营、未在战场/队列、无玩家主人、非战斗、可合法进入该段的随机机器人中选取；目标等级 `targetLevel = urand(maxPlayerLevel, maxPlayerLevel + N)`，夹紧到该段 `[minLevel, maxLevel]` 与服务器等级上限；用 `PlayerbotFactory(bot, targetLevel)` + 现有 `Randomize` 路径调级并刷装备后入队

等级对齐只发生在「该段有真人且需要补位」时，且每个机器人在单次入队决策里最多调级一次。

### 阵营人数（敌方不得多于我方）

仅当该 `queueTypeId` + `bracketId` 存在真人排队时生效。人数按现有 `BattlegroundData` 统计（该段排队中 + 已在场，玩家与机器人合计）。

**「我方」定义：** 该段队列中真人玩家所属阵营。  
- 仅一侧有真人（单人服常态）：`playerTeam` = 该侧  
- 两侧都有真人：退化为「两边始终 `|a-h|` 在补位后仍满足双方都不会让对面超过自己」→ 入队后必须 `a == h` 或保持不扩大劣势方被反超，实现上取更严：入队后必须 `a == h`（只允许把较少侧补齐到相等）

**实现算法（单侧有真人，以此为准）：**

- `a` / `h` = 联盟 / 部落总人数  
- `our` / `enemy` = 相对 `playerTeam` 的本方 / 敌方人数  
- 任意机器人入队前须满足原有本方缺口条件  
- 入队后必须始终 `enemy' <= our'`  
  - 机器人加入**我方**：`enemy <= our + 1` 恒真（我方+1 后更宽松），只要有缺口即可  
  - 机器人加入**敌方**：仅当 `enemy + 1 <= our`（加入后 `enemy' <= our`）

允许：我方 `3`、敌方 `2`（`3v2`）；`3v3`；我方更多。  
禁止：敌方比我方多（如我方联盟时的 `2v3`）。

开打所需：敌方仍须达到 MinPlayers，但不得超过我方。例如联盟真人独排、`MinPlayers=1`：允许部落补到与联盟相等（至少 `1v1`），不允许部落超过联盟。

`CheckBgQueue` 需记录该段是否有真人及 `playerTeam`（若两侧都有真人则标记为「双方均有」，走两侧只补到相等的严规则）。

该约束加在 `BGJoinAction::shouldJoinBg`（战场分支，非 Arena）中，与人数上限条件同时满足才可入队。

### 装备

- 全局：`RandomGearQualityLimit = 2`，工厂生成装备时生效
- 因补位而临时调级的机器人：调级后立刻按新等级与该品质上限重随装备
- 已在窗口内、无需调级的机器人：不强制当场重装；依赖全局品质上限与后续 randomize。若实现时发现大量旧蓝装残留导致碾压，可在入队前对补位机器人做一次 `InitEquipment`（实现计划中作为可选加固步骤）

### 开局人数

仅通过 `Battleground.Override.LowLevels.MinPlayers = 1` 降低门槛；不修改 SQL `battleground_template`。满级段仍走模板默认 `MinPlayersPerTeam`（战歌为 5），除非后续另开需求。

说明：AC 的 LowLevels 覆盖作用于「等级 &lt; MaxPlayerLevel」；满级 80 段若也要 1 人开打，需另配或改模板，本设计不包含。

## 数据流（简图）

```text
真人排队 WSG（任意合法段）
    → CheckBgQueue: activeBgQueue=1, 记录段内真人最高等级
    → 随机机器人 BGJoinAction
        → 本方仍有名额？且入队后对面人数 ≤ 本方人数？
        → 优先：等级 ∈ [段下限, maxPlayerLevel+N] 的机器人入队
        → 否则：选空闲机器人 → 调级到窗口 → 刷普通装备 → 入队
    → 每边达到 MinPlayers（低等级覆盖为 1）→ 开打
```

## 验收标准

1. 角色约 10 级排战歌峡谷：有机器人入队，战场能开打；入场可见机器人等级 ≤ 玩家等级 + 1；装备品质目测不超过绿色为主
2. 换到另一合法段（如 30–39 或 70–79）再排：同样能补位开打，且等级约束相对该段真人成立
3. 单人联盟排队时：开打后联盟人数 ≥ 部落人数（可 `2v1`、`2v2`，不可 `1v2`）；部落独排则对称
4. 关闭 `RandomBotJoinBG` 后，不再出现上述自动补位
5. 不开启 `RandomBotAutoJoinBG` 时，无人排队不会出现全段空战场刷屏

## 风险与限制

- 临时调级会改变该随机机器人的世界等级/装备，直到下次 randomize；可接受为单人服补位代价
- 双阵营都要补人时，45 个机器人可能仍偏紧；`MinPlayers=1` 大幅降低需求
- `RandomGearQualityLimit` 影响全部随机机器人，不仅是战场补位
- 已知上游问题：多段 × 多实例 AutoJoin 易过量排队——本设计刻意不启用全段 AutoJoin
