# Individual Progression（Plan B）接入设计

## 目标

在现有 playerbots + AHBot 服上接入 [mod-individual-progression](https://github.com/ZhengPeiRu21/mod-individual-progression)，让**每个角色**从 Vanilla 走完整旅程：

- 等级帽按角色进度：Vanilla 60 → 打完纳克萨 40 才能出外域到 70 → 打完太阳井才能进诺森德到 80
- 团本和开门事件按 IP 档解锁：奥妮钥匙链、AQ 战争准备/敲锣开门、40 人 60 级纳克萨，以及之后的 TBC / WLK 档
- 5 人本不按全服月份开，角色等级够就进（和当年一样）
- 随机机器人先锁在同时代：最高 60、只在东部/卡利、不出现后期种族/职业；AH 只上架当前时代能用的装备和材料
- 进 TBC / WLK 后用运营脚本按阈值表一次放宽机器人、AH，并回填 RNDBOT 进度、刷新禁用物品表
- 战斗和专业尽量偏原版：削弱 1–60 / 61–70 输出与治疗，服务器 + 客户端 DBC 补丁（并进现有 `patch-zhCN-X`）

`acore_world`、`acore_characters`、`acore_playerbots` 清空重建。`acore_auth` 保留。

## 非目标

- 不上 ChromieCraft 全服阶段（Plan A），不按 1–19 / 20–29 锁 5 人本
- 不把 IP 和 `mod-progression-system` 装在一起
- 不按角色进度自动改机器人/AH（不做 `mod-rndbot-sync` 一类模块）
- 不改战场内战术 AI，不改竞技场补人
- 不整表替换 `patch-zhCN-X` 里已有的 `Spell.dbc`（火球等自定义会丢）
- 不清 `acore_auth`，不重建账号
- 不在本设计里做 AQ 战争准备的机器人代交自动化（可用 GM / `.ip set`）

## 背景

IP 把进度存成**隐藏任务** `66001`–`66018`（落在 `character_queststatus_rewarded`），当前版本不碰 `character_settings`。这批任务由模块 world SQL（`data/sql/world/base/player_progression.sql`）建；SQL 没导进去时 `UpdateProgressionState` 取不到 quest template 会静默跳过，**进度永远停在 0 且不报错**，所以验收必须先查这 18 条任务在不在。装备数值覆盖必须 `DBC.EnforceItemAttributes = 0`。`EnablePlayerSettings` 已不参与进度存储，仍按 `1` 写死（其它模块和 `SimpleConfigOverride` 沿用老约定）。World SQL 单向写入，关掉模块不会回滚。

等级帽和地图门都按「已完成档 ≥ N」判定：进度 < 8 时 60 级停 XP，< 13 时 70 级停 XP；外域门 = 8，诺森德门 = 13，奎尔丹纳斯岛与祖阿曼 = 12。

随机机器人账号默认 `BotAccountsRegex = "^RNDBOT.*"`，不跑完整人进度，只在入队时被队长同步（同步会真写隐藏任务并持久化）。由此有三件事要各自处理：

- **建号不受 IP 限制**：IP 的建号检查对 bot 账号直接放行，而 playerbots 判定可用种族/职业只看核心 `Expansion`。所以 `TbcRacesUnlockProgression` 挡得住真人、挡不住机器人造血精灵/德莱尼。要挡得用核心 `CharacterCreating.Disabled.RaceMask` / `ClassMask`（playerbots 建号时尊重这两个）。
- **野外观感靠 playerbots 自己的等级/地图上限**锁在当前时代，不能指望 IP 替你锁机器人。
- **削弱系数按各自进度算**：没组过队的机器人进度恒为 0，`1 - (1 - 0.55) × (等级 - 10) / 50` 在 70 级是 0.46、80 级只剩 0.37。切时代放宽等级后必须回填进度，见「机器人削弱与进度回填」。

AHBot 的 `DisableItemsAboveReqLevel` / `DisableItemsAboveLevel`（及商业材料对应项）只看 `RequiredLevel` 和 `ItemLevel`，**拦装备够用，拦不住材料**：TBC/WLK 的矿石、布、草药 `RequiredLevel = 0` 且 `ItemLevel` 远低于 92，宝石、雕文、配方同理。材料要真锁时代只能走 `mod_auctionhousebot_disabled_items`。

两个模块的配置生效方式不同：AHBot 支持 `.reload config`（会重建候选物品表并重启卖家），playerbots 的 `AiPlayerbot.*` 只在世界初始化时读一次，**改机器人键必须重启 worldserver**。

`acore_playerbots` 不受 `Updates.EnableDatabases` 管——那个掩码只有 login / characters / world 三位。它由 playerbots 模块自己的 loader 按 `Playerbots.Updates.EnableDatabases` + `Updates.AutoSetup` 在 **worldserver 启动时**建，`ac-db-import` 不会创建它。

本仓库用 Docker 编世界服；模块目录 `modules/` 被 gitignore，和 `mod-playerbots`、`mod-ah-bot` 一样 clone 进去再 `docker compose build`。核心是 playerbots fork（约 2026-08-21）。IP 跟官方 AC 更紧，HEAD 可能编不过，失败则 pin 到 8 月中下旬附近的 commit。

客户端是 `C:\games\World of Warcraft_HD`，自定义只进 `patch-zhCN-X`。IP 可选 `Spell.dbc` 若整表丢进 X，会盖掉已有法术改动。

## 进度模型（角色）

新号 `StartingProgression = 0`。过关条件是打完该档终极大 Boss 或指定任务。存档值是**已完成的最高档**。

| 档 | 内容 | 等级帽 | 过关 |
|---|---|---|---|
| 0–1 | 熔火之心、40 人奥妮（钥匙链还原） | 60 | 拉格纳罗斯、奥妮 |
| 2 | 黑翼之巢 | 60 | 奈法利安 |
| 3 | AQ 战争准备、祖尔格拉布（默认可改） | 60 | 敲锣 |
| 4–5 | 开门、AQ20/AQ40 | 60 | 克苏恩 |
| 6 | 40 人纳克萨、天灾入侵 | 60 | 克尔苏加德 |
| 7 | 黑暗之门前夕 | 60 | 指定任务 |
| 8–12 | 外域团本线，太阳井在 12 | 70 | 基尔加丹等 |
| 13–17 | 诺森德：WLK 纳克萨 → 奥杜尔 → 十字军 → ICC → 红玉 | 80 | 各档尾王 |

档 11 在模块里是空的（`PROGRESSION_TBC_TIER_3` 未启用），祖阿曼另走 `RequiredZulAmanProgression = 12`、祖尔格拉布走 `RequiredZulGurubProgression = 3`，两个都用默认值。

战场：等级帽自然挡住后期图。Vanilla 打战歌/阿拉希/原版奥山；TBC 加风暴；WLK 加海滩、征服之岛。奥山在 Vanilla 是 IP 的 1.12 版，进 WLK 后仍可打 3.3.5 版。

5 人本：当前等级帽内，按角色等级进出，不另做禁用表。IP 自带的几处进度门保留——祖尔格拉布(3)、AQ20/AQ40(4)、奎尔丹纳斯岛与祖阿曼(12)、北裂境(13)，外加 `RequireNaxxStrathEntrance`、`AllowEarlyDungeonSet2`、`AllowEarlyScourgeBosses` 三个开关按默认。

## 时代阈值表（机器人 + AH）

运营脚本只认三个时代。角色 IP 档可以比时代细；**放宽机器人和 AH 只在跨资料片时做一次**。

| 时代 | 何时切 | 机器人最高级 | `RandomBotMaps` | 飞行坐骑最低级 | AH `ReqLevel` 上限 | AH `ItemLevel` 上限 | DK 物品 / 登录 |
|---|---|---|---|---|---|---|---|
| `vanilla` | 开服默认 | 60 | `0,1` | 80（等于禁飞） | 60 | 92 | 关 |
| `tbc` | 角色进度 ≥ 8（出外域） | 70 | `0,1,530` | 70 | 70 | 164 | 关登录，AH 仍禁 DK 装 |
| `wotlk` | 角色进度 ≥ 13（进诺森德） | 80 | `0,1,530,571` | 60 | 0（不限） | 0（不限） | 开 |

还有四个键也随时代切，它们不在上表的两份 conf 里：

| 键 | 文件 | vanilla | tbc | wotlk |
|---|---|---|---|---|
| `IndividualProgression.DisableRDF` | individualProgression.conf | `1` | `1` | `0` |
| `AiPlayerbot.RandomBotJoinLfg` | playerbots.conf | `0` | `0` | `1` |
| `CharacterCreating.Disabled.RaceMask` | worldserver.conf | `1536` | `0` | `0` |
| `CharacterCreating.Disabled.ClassMask` | worldserver.conf | `32` | `32` | `0` |

`DisableRDF` 在启动时一次性写 `CONFIG_LFG_OPTIONSMASK`，不按角色进度走——不切它的话进 wotlk 后 RDF 一直是关的。`RaceMask = 1536` 是血精灵(512) + 德莱尼(1024)，`ClassMask = 32` 是 DK。vanilla 期真人和机器人都建不了这两族、建不了 DK；进 tbc 清 `RaceMask`，进 wotlk 再清 `ClassMask`。

触发不自动侦测。你确认已经过关后执行：

```text
set-era.ps1 vanilla
set-era.ps1 tbc
set-era.ps1 wotlk
```

脚本改 `worldserver.conf`、`env/dist/etc/modules/playerbots.conf`、`env/dist/etc/modules/mod_ahbot.conf`、`env/dist/etc/modules/individualProgression.conf` 四份运行中 conf 的上表键，并用 `-WriteDb` 写库（见「运营脚本」）。

AH 的两道过滤只覆盖装备：`ItemLevel` 92 + `ReqLevel` 60 拦住绝大多数外域装，但 `RequiredLevel` ≤ 60 且 `ItemLevel` ≤ 92 的地狱火/赞加沼泽绿蓝装会漏，材料、宝石、雕文、配方基本全漏（详见「背景」）。164 覆盖太阳井、挡住 WLK 装。材料与漏网装备一律按 entry 段灌进 `mod_auctionhousebot_disabled_items`（vanilla 基本 < 23000，TBC 23000–35000，WLK > 35000）。灌之前先量一下规模：

```sql
SELECT COUNT(*) FROM item_template
WHERE class = 7 AND RequiredLevel <= 60 AND ItemLevel <= 92 AND entry > 23000;
```

切时代后：

- **必须重启 worldserver**，`AiPlayerbot.*` 不吃 `.reload config`；AH 单独改可以只 `.reload config`
- 已上架的后期拍品要等过期或重启卖方
- 已有 60 级机器人不会立刻变成 70，等 periodic randomize 或对机器人做一次初始化
- 机器人进度仍是 0，等级放宽后削弱会加深，必须按「机器人削弱与进度回填」写库

## 机器人削弱与进度回填

`BotOnlyAdjustments = 0` 时输出/治疗钩子对所有人生效，各自按自己的进度取系数。Vanilla 档系数随等级线性加深：

```text
系数 = 1 - (1 - VanillaPowerAdjustment) × (等级 - 10) / 50
```

`0.55` 的配置值下：60 级 0.55、70 级 0.46、80 级 0.37。真人碰不到 60 以上的 vanilla 档（等级帽挡着），**机器人会**：它们进度恒为 0，切到 tbc/wotlk 放宽等级后就落进公式的尾段。

组过队的机器人没问题（队长同步会真写隐藏任务），但池子里几百个没组过队的不会自己好。规模化只有一条路——切时代时给 RNDBOT 角色批量补隐藏任务：

```sql
-- 以 tbc（档 8）为例；wotlk 用 66013
INSERT IGNORE INTO character_queststatus_rewarded (guid, quest)
SELECT c.guid, q.id
FROM characters c
JOIN acore_auth.account a ON a.id = c.account
JOIN (SELECT 66001 AS id UNION SELECT 66002 UNION SELECT 66003 UNION SELECT 66004
      UNION SELECT 66005 UNION SELECT 66006 UNION SELECT 66007 UNION SELECT 66008) q
WHERE a.username LIKE 'RNDBOT%';
```

在 `acore_characters` 上执行。IP 读进度只看已奖励的最高编号，插满 `66001..N` 是为了和模块自己写入的形状一致（`UpdateProgressionState` 总是补齐到目标档）。机器人离线时执行，执行后重启 worldserver。切到 `tbc` / `wotlk` 时由 `set-era.ps1 -WriteDb` 做这件事，不改真人角色的 IP 档。

## 配置

### 核心 `env/dist/etc/worldserver.conf`

| 键 | 值 |
|---|---|
| `Updates.EnableDatabases` | `7`（只管 login/characters/world，playerbots 库另算） |
| `Updates.AutoSetup` | `1`（空库自动建，`acore_playerbots` 靠它） |
| `EnablePlayerSettings` | `1` |
| `DBC.EnforceItemAttributes` | `0` |
| `MaxPlayerLevel` | `80`（角色帽由 IP 管，不要收成 60） |
| `Battleground.Override.LowLevels.MinPlayers` | `1`（沿用现有补人） |
| `CharacterCreating.Disabled.RaceMask` | `1536`（vanilla 期挡血精灵/德莱尼，跨资料片清 0） |
| `CharacterCreating.Disabled.ClassMask` | `32`（vanilla/tbc 期挡 DK，进 wotlk 清 0） |

`IndividualProgression.SimpleConfigOverride = 1` 可代写 `EnablePlayerSettings`、`DBC.EnforceItemAttributes` 和水下呼吸 1 分钟；仍在 worldserver.conf 里写死，避免只靠模块覆盖。

`Playerbots.Updates.EnableDatabases`（在 playerbots.conf 里）保持 `1`，`acore_playerbots` 靠它加 `Updates.AutoSetup` 在 worldserver 启动时建。

### `env/dist/etc/modules/individualProgression.conf`

| 键 | 值 |
|---|---|
| `Enable` | `1` |
| `StartingProgression` | `0` |
| `ProgressionLimit` | `0` |
| `VanillaPowerAdjustment` | `0.55` |
| `VanillaHealingAdjustment` | `0.5` |
| `TBCPowerAdjustment` | `0.55` |
| `TBCHealingAdjustment` | `0.5` |
| `BotOnlyAdjustments` | `0`（真人也吃削弱） |
| `QuestXPFix` | `1` |
| `FishingFix` | `1` |
| `DisableQuestMarkers` | `1` |
| `DisableRDF` | `1`（vanilla/tbc；进 wotlk 改 0） |
| `MaxMonsterSight` | `0`（默认 1 会把怪物视野拉到 80 码，机器人密集时先别开） |
| `EnforceGroupRules` | `0` |
| `TbcRacesUnlockProgression` | `8`（只约束真人，机器人另靠 `RaceMask`） |
| `DeathKnightUnlockProgression` | `13` |
| `DeathKnightStartingProgression` | `13` |
| `MoltenCore.ManualRuneHandling` | `1` |
| `doableNaxx40Bosses_*` | `1`（四骑士/古鲁斯/帕奇维克/拉祖维斯，方便带机器人） |
| `BotAccountsRegex` | `^RNDBOT.*` |
| `BotAccountsMaxLevel` | 与当前时代机器人最高级相同（脚本一并改） |

`BotAccountsMaxLevel` 只是把机器人的 XP 归零，而 playerbots 是直接 `GiveLevel` 拉级的，所以它是冗余保险，真上限是 `AiPlayerbot.RandomBotMaxLevel`。`tbcRacesStartingProgression` 保持默认 `0`（血精灵/德莱尼新号从 0 起步，只能待在自己的起始区）。

### `env/dist/etc/modules/playerbots.conf`（vanilla 起步）

| 键 | 值 |
|---|---|
| `AiPlayerbot.RandomBotMinLevel` | `1` |
| `AiPlayerbot.RandomBotMaxLevel` | `60` |
| `AiPlayerbot.RandomBotMaps` | `0,1` |
| `AiPlayerbot.UseFlyMountAtMinLevel` | `80` |
| `AiPlayerbot.DisableDeathKnightLogin` | `1` |
| `AiPlayerbot.RandomBotJoinLfg` | `0`（RDF 已关；进 WLK 再开） |
| `AiPlayerbot.RandomBotJoinBG` | `1` |
| `AiPlayerbot.RandomBotAutoJoinBG` | `0` |
| `AiPlayerbot.BgJoinMaxLevelAbovePlayer` | `1` |
| `AiPlayerbot.ApplyInstanceStrategies` | `0`（40 人纳克萨的战术脚本未验证，先保守关掉） |
| `AiPlayerbot.RandomGearQualityLimit` | `2` |

无会职业池沿用现有设计。Vanilla 期 DK 不登录、血精灵/德莱尼被 `RaceMask` 挡在建号之前，所以每边池子是 8 个职业（联盟无萨满、部落无圣骑士）；进 tbc 清掉 `RaceMask` 后变 9，进 wotlk 开 DK 后 10。

机器人挑装备也按 `RequiredLevel` 走，和 AH 一样会漏进 58–60 级的外域绿装，这属于同一笔取舍。

### `env/dist/etc/modules/mod_ahbot.conf`（vanilla 起步）

| 键 | 值 |
|---|---|
| `AuctionHouseBot.EnableSeller` | `1` |
| `AuctionHouseBot.DisableItemsAboveReqLevel` | `60` |
| `AuctionHouseBot.DisableTGsAboveReqLevel` | `60` |
| `AuctionHouseBot.DisableItemsAboveLevel` | `92` |
| `AuctionHouseBot.DisableTGsAboveLevel` | `92` |
| `AuctionHouseBot.DisableDKItems` | `1` |

密语下单等现有 AH 改动保留；过滤只限制卖方上架，且只对装备有效。材料/宝石/雕文/配方以及漏网装备由 `set-era.ps1 -WriteDb` 按 entry 段写入 `mod_auctionhousebot_disabled_items`。

## 清库与编译顺序

1. clone `https://github.com/ZhengPeiRu21/mod-individual-progression.git` 到 `modules/mod-individual-progression`（目录名不能带 `-master`）
2. `docker compose build` worldserver / db-import。编不过则把 IP 退到 2026-08 中下旬 commit 再编
3. 停 `ac-worldserver`、`ac-authserver`、`ac-db-import`
4. 在 `ac-database` 上 `DROP DATABASE`：`acore_world`、`acore_characters`、`acore_playerbots`。**不要 drop `acore_auth`**
5. 配置落位：`individualProgression.conf.dist` 复制成 `individualProgression.conf`，连同改过的 `playerbots.conf`、`mod_ahbot.conf` 放到 `env/dist/etc/modules/`，并把上面核心表的键写进 `env/dist/etc/worldserver.conf`
6. 服务器 DBC：把 `dbc.7z` 里的 `SkillLine.dbc`、`SkillLineAbility.dbc`、`SkillRaceClassInfo.dbc`、`SpellItemEnchantment.dbc` 写进服务器数据目录（Docker 的 `ac-client-data` / `env/dist/data/dbc`）。先备份现有 DBC
7. 客户端：IP `patch-V`（原版法力消耗）按 `merging-hd-client-x-patch` **增量**并进 `patch-zhCN-X`。X 已有 `Spell.dbc` 时只合并 IP 改过的行，整表覆盖禁止。更新 `_tools/wowpatch/CONTENTS.md`
8. 服务器 `Spell.dbc` 用第 7 步的**同一份合并结果**，不是 patch-V 里的原始文件——两边不同源会导致法力消耗和法术表现不一致
9. `docker compose up`。db-import / worldserver 按 `Updates.EnableDatabases = 7` 重建 world / characters / auth 更新；`acore_playerbots` 由 worldserver 启动时的 playerbots loader 单独建，光跑 db-import 不会有这个库
10. **重建 AH 卖家角色**：清 characters 库会连带删掉 AHBot 的卖家和密语客服角色（现在是账号 58 的 GUID 562、账号 59 的 GUID 588）。在这两个账号上重新建号，把新 GUID 写回 `AuctionHouseBot.GUID` 与 `AuctionHouseBot.WhisperOrdersReceiverGUID`，再 `.reload config`。不做这一步 AHBot 只会在日志里报 `no characters registered for account`，然后一件不上架
11. `set-era.ps1 vanilla -WriteDb`：灌 vanilla 期 `mod_auctionhousebot_disabled_items`。此时还没有 RNDBOT 角色要回填进度
12. 验收，见下

第一次启动 IP SQL 很多，world 导入会比平时久。

### 验收

1. `SELECT COUNT(*) FROM acore_world.quest_template WHERE ID BETWEEN 66001 AND 66018` = 18。**这条不过后面全是假的**：隐藏任务缺失时进度会永远停在 0 而且不报错
2. `acore_playerbots` 存在且有表
3. 登录新号，`SELECT * FROM character_queststatus_rewarded WHERE guid = <新号> AND quest BETWEEN 66001 AND 66018` 为空（= 进度 0），`.ip` 命令读出的档位也是 0
4. 61 级上不去（60 级后 XP 不涨），黑暗之门进不去
5. 打完熔火之心后 `66001` 出现，等级帽仍是 60
6. 日志里没有 `AHBot: no characters registered for account`，AH 有新拍品
7. AH 上架列表里没有 60 级以上需求的装备，也没有外域/诺森德材料（vanilla 起步已灌 `disabled_items`）
8. 随机机器人里没有 DK、血精灵、德莱尼

## 运营脚本

新建 `apps/progression-era/set-era.ps1`：

- 参数：`vanilla` | `tbc` | `wotlk`（只接受这三个），以及必选的 `-WriteDb`（切 `tbc` / `wotlk` 时必须带；开服灌 vanilla 禁用表也带）
- 读写四份运行中 conf 的已知键，不碰其它行：`worldserver.conf`、`playerbots.conf`、`mod_ahbot.conf`、`individualProgression.conf`
- 覆盖两张表的全部键，包括 `BotAccountsMaxLevel`、`DisableRDF`、`RandomBotJoinLfg`、`CharacterCreating.Disabled.RaceMask` / `ClassMask`
- `-WriteDb`：连 `acore_characters` 给 RNDBOT 回填隐藏任务到目标档，并按 entry 段刷 `mod_auctionhousebot_disabled_items`。不改真人 IP 档。执行前打印将影响的行数，要一次确认
- 标准输出打印已写的键值，并明确区分：**机器人键必须重启 worldserver**，AH 键可 `.reload config`；另提示 AH 旧货待过期、机器人待 randomize
- 不执行 docker

阈值数字只写在脚本一处，本 spec 表格与脚本必须一致。

## DBC / 客户端

- 用 IP 的 `patch-V`（把法力消耗改回原版），不用 `patch-S`（保留 WLK 法力消耗），也不要同时装两份
- 两个 patch 都同时提供客户端 `.mpq` 和服务器 `Spell.dbc`，**必须成对使用且同源**；客户端那份要增量并进 X，服务器那份直接用合并后的结果
- `SkillLine.dbc` / `SkillLineAbility.dbc` / `SkillRaceClassInfo.dbc` / `SpellItemEnchantment.dbc` 是纯服务器侧文件，来自 `dbc.7z`
- 登录画面（`patch-J`）/ 读条（`patch-U`）/ 隐藏商人价签插件可选，与进度无关，不做硬性要求
- 服务器 DBC 与客户端不一致时，配方材料会对不上；两边都要落到同一套 optional 文件
- zhCN HD：只改 X；`HashTableSize` 4096；安装前关 Wow

## 可选 SQL 包

IP `optional/sql/world/` 里三个直接命中本设计痛点的包，要明确表态用不用：

| 包 | 作用 | 建议 |
|---|---|---|
| `zz_optional_small_group_adjustments.sql` | 小队伍打大团本的数值调整 | 用。正对「40 人纳克萨/奥妮凑不齐机器人」 |
| `zz_optional_phasing.sql` | 把后期加进旧世界的低级内容按补丁分相 | 用。正对「同时代观感」 |
| `zz_optional_limit_spells_to_expansion.sql` | 法术按资料片限制 | 待定，和 playerbots 的战术脚本有交互风险 |

其余可选包（弹药堆叠、药水 CD、盗贼毒药还原等）与本设计无关，按口味决定。

## 与现有功能

| 现有 | 处理 |
|---|---|
| 战场补人 | 保留。Vanilla 只有战歌/阿拉希/奥山可排 |
| LFG 补人 | 代码保留，Vanilla/TBC 关 RDF 与 `RandomBotJoinLfg` |
| 无会申请书池 | 保留。Vanilla 每边 8 职业（无 DK、无血精灵/德莱尼） |
| AH 密语下单 | 保留。卖方受时代过滤，材料走 `disabled_items`；卖家/客服角色随 characters 库重建，GUID 要回填 |
| zhCN 任务文本 pending SQL | `pending_db_world/` 里的会在重建后自动重放；要手工补的是 `data/sql/custom/`（gitignored）和你手动执行过的部分 |
| 自有 world 数据改动 | IP 的 `00_cleanup.sql` 加上百个 `base/` 文件会大改 vanilla 生物/任务，与自有改动有覆盖顺序风险，重建后要抽查关键行 |

## 风险

- IP 在 playerbots fork 上可能编不过：pin commit，不要同时升级核心和 IP HEAD
- **性能**：IP 的 `isBotAccount` 每次调用都同步查一次 auth 库并现场编译正则，而它是施法钩子的第一道判断——等于**每个机器人每次施法一条同步 SQL**，给经验时是两条。本设计把 `BotOnlyAdjustments` 设 0，正好绕开伤害/治疗钩子里的同一条路径；上线后仍要盯世界线程耗时，必要时打个按 accountId 缓存的小补丁（或直接用 `GET_PLAYERBOT_AI` 判定）
- **隐藏任务缺失会静默失败**：模块 world SQL 没成功导入时进度恒为 0 且无任何报错，验收第 1 条就是为它设的
- 40 人纳克萨 / 奥妮要大量机器人；`ApplyInstanceStrategies = 0` 换稳定、换无专精战术
- AQ 战争准备每个角色都要交物资
- 野外仍可能看到「同时代但不是你进度」的 60 级机器人（他们不跑 IP 档），这是刻意取舍
- 机器人在 tbc/wotlk 期的削弱系数会随等级加深；切时代必须 `-WriteDb` 回填，漏做就会明显偏弱
- AH 材料市场只靠两道过滤会串味；切时代必须 `-WriteDb` 刷 `disabled_items`
- 清 characters 库会打断 AHBot：卖家和密语客服角色的 GUID 失效，忘了回填就整个 AH 静默停摆
- SQL 污染：三个库已约定可丢；不要在未备份的情况下往现网 world 上试 IP

## 已定决策

| 决策 | 做法 |
|---|---|
| 机器人削弱 | 切 `tbc` / `wotlk` 时 `set-era.ps1 -WriteDb` 给 RNDBOT 回填隐藏任务到目标档。真人进度不动。 |
| 血精灵 / 德莱尼 | vanilla 期 `CharacterCreating.Disabled.RaceMask = 1536`，真人和机器人都建不了这两族；进 tbc 清 0。vanilla 每边 8 职业（联盟无萨满、部落无圣骑士）。 |
| AH 材料 | 按 entry 段灌 `mod_auctionhousebot_disabled_items`。开服和每次切时代都走 `-WriteDb`。 |

`-WriteDb` 是切时代的实际必需通道，不是可选加分项。执行前仍打印行数并确认，避免误伤。
