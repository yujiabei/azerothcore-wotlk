# Playerbots 主城无会职业池（公会申请书签名）

## 目标

真人玩家在主城能用 `/who` 找到无工会的随机 bot，当面递交公会申请书并凑齐签名。

每个阵营在对应主城保持：

- **每种可登录职业各 1 名**在线、无工会的随机 bot
- WotLK 默认可登录 10 个职业，因此每边 10 人（战士、圣骑士、猎人、盗贼、牧师、死亡骑士、萨满、法师、术士、德鲁伊）
- 联盟待在暴风城，部落待在奥格瑞玛

玩家把这些人拉进**自己的工会**后，同职业会再补一个无会 bot 到主城，便于继续招人壮大工会。

`MinPetitionSigns` 仍为 9。每边 10 个无会 bot，足够签满申请书。

## 非目标

- 不改申请书签名人数，不让「已入会」角色在会内直接签字
- 不从玩家工会（含会长是玩家、或有真人会员的工会）里拆人
- 不让 bot 工会会长或最后一名会员退会来凑池子
- 不改竞技场申请书
- 不改 bot 在公会内的职级、银行、任务等玩法
- 不把无会池做成固定角色名单；按「阵营 + 职业」名额维持即可

## 背景

随机 bot 登录时 `RandomPlayerbotMgr::OnBotLoginInternal` 会调用 `PlayerbotFactory::InitGuild()`。`InitGuild` 对所有无会 bot 执行 `PlayerbotGuildMgr::AssignToGuild`，把人塞进纯 bot 工会。

当前运行配置大约 80 个在线随机 bot、阵营 50/50。两边工会装满后，主城几乎找不到能签申请书的人。`PetitionSignAction` 对已入会 bot 会拒绝签字，核心 `HandlePetitionSignOpcode` 同样检查 `GetGuildId()`。

## 配置

编辑 `modules/mod-playerbots/conf/playerbots.conf.dist` 与运行中的 `env/dist/etc/modules/playerbots.conf`：

| 配置 | 值 | 说明 |
|---|---|---|
| `AiPlayerbot.RandomBotUnguildedPerClass` | `1` | **新增**。`1`：每阵营每种可登录职业在主城保留 1 名无会随机 bot。`0`：关闭，行为与现在相同 |

可登录职业：按核心 `CLASSMASK_ALL_PLAYABLE` 枚举。若 `AiPlayerbot.DisableDeathKnightLogin = 1`，死亡骑士不进入名额。

本功能不改 `AiPlayerbot.MaxRandomBots`（当前运行为 80）。在线上限与无会池是两件事：池子占用其中一部分在线名额，不额外突破上限。

## 代码行为

逻辑集中在 `PlayerbotGuildMgr`（查询、补人、是否占用名额），由登录入会、定期检查、邀请/买申请书、战场入队调用。

### 名额定义

对阵营 `team`、职业 `cls`，**持有人（holder）**是一名同时满足下列条件的 bot（不要求已经站在主城，否则开服时人都在新手区，同职业会被全部留成无会）：

1. 在线随机 bot（`sRandomPlayerbotMgr.IsRandomBot`）
2. 阵营与职业匹配
3. 无工会
4. 不是玩家正在操控的小号（`HasActivePlayerMaster()` 为假）

每个 `(team, cls)` 只需要 **1** 名持有人。`HasHolder(team, cls)` 为持有人数量 ≥ 1。`NeedsHolder(team, cls)`：可登录职业且持有人数量为 0。

**可签到（在主城）**：该持有人当前区域为联盟 `AREA_STORMWIND_CITY`（1519）或部落 `AREA_ORGRIMMAR`（1637），且不在战场、不在副本。持有人若不在主城，由驻留逻辑传送过去，不要为此再拆第二个同职业会员。

同职业若因竞态出现多名无会持有人，多余的人可以照常被 `InitGuild` / bot 工会收走，只保留 1 人。

### 哪些工会可以拆人

仅从**纯 bot 工会**拆非会长、且拆完后工会仍至少剩 1 人。

纯 bot 工会：所有会员账号都在随机 bot 账号列表中。会长是随机 bot 但已有真人会员的工会视为玩家占用，不拆。

为此在 `PlayerbotGuildMgr` 增加（或加强）会员扫描：不能只看会长账号（现有 `IsRealGuild` 只看会长）。拆人前用完整会员判断。

### 登录时不入会

`PlayerbotFactory::InitGuild` 在 `AssignToGuild` 之前：

- 已有工会：保持现有逻辑（补战袍等）
- 无工会且 `RandomBotUnguildedPerClass = 1`：统计同阵营同职业的在线无会随机 bot（含自己）。若数量 ≤ 1，则**不分配工会**（自己成为该格持有人），随后由驻留逻辑送去主城
- 否则照旧 `AssignToGuild`

开服时每个阵营每种职业只会留 1 名无会 bot，其余人进 bot 工会。这 1 人可能还在新手区，不算缺编。

### 定期补人

在 `RandomPlayerbotMgr` 的更新循环中调用 `PlayerbotGuildMgr::EnsureUnguildedPool()`（间隔约 30 秒，单次工作量要小，避免一帧拆光）。

对每个阵营、每个可登录职业：

- 已有持有人但不在对应主城（或在战场/副本）：只传送，不拆第二人。
- `NeedsHolder` 时按顺序补 1 人：

1. **在线拆会**：同阵营同职业、在纯 bot 工会、非会长、非最后一人、非战斗；优先不在战场/副本的。退会后传送到主城。
2. **上线离线角色**：同阵营同职业、不在玩家占用工会中的离线随机 bot。若当前在线人数已达 `maxRandomBots`，先下线一名「可让路」的在线随机 bot，再上线目标：可让路 = 在纯 bot 工会中、不是会长、不是某格持有人、不是玩家小号。
3. **新建角色**：仅当不存在可用的同阵营同职业随机角色（在线+离线，且不在玩家占用工会），并且本轮能立刻上线（未达上限，或有可让路的人）时，才走现有 `RandomPlayerbotFactory::CreateRandomBot` 创建并上线。达不到上线条件则不建角色，避免角色库无限涨。
4. **仍无法补**：该格暂时空着，下一轮再试。永不从玩家占用工会拆人。

退会走现有 `HandleGuildLeaveOpcode` / `Guild::DeleteMember` 路径，并 `OnGuildUpdate`。

### 拦住回流

当 bot 是其 `(阵营, 职业)` 的持有人（该格无会人数 ≤ 1 的那名无会随机 bot）时：

- `BuyPetitionAction::canBuyPetition`：返回 false（不自己建会）
- `GuildManageNearbyAction` / `GuildInviteAction`：纯 bot 工会不得邀请他
- `GuildAcceptAction`：邀请者属于纯 bot 工会则拒绝；邀请者是真人（或玩家占用工会）则接受

同职业无会人数 ≥ 2 时，多余的人可以被 bot 工会收走。

真人邀请成功后该格空出，由 `EnsureUnguildedPool` 按上面 1→2→3 补同职业。

### 留在主城

持有人（或刚为补格而退会/上线的 bot）：

- 若不在对应主城，或在战场/副本中：传送到该主城公共区（使用现有随机 bot 传送辅助，目标为暴风城 / 奥格瑞玛的固定安全坐标，不新写跨大陆走路）
- `BGJoinAction`：身为持有人时不入战场队列
- 允许在主城内 RPG（走动、商人、表情），不强制站桩

### 申请书签字

无会 bot 沿用现有 `PetitionSignAction`（无工会则签字）。本功能不改签名 opcode，只保证主城有人可签。

真人递交的公会申请书、以及真人发出的入会邀请，不走组队用的等级 / 装等检查。竞技场申请书和组队邀请仍受该检查。进会之后，同工会组队仍按现有逻辑放开等级差。

同账号限制已对 bot 放开（`OnPlayerbotCheckPetitionAccount`）。阵营限制仍有效。

## 错误与边界

- 某职业在该阵营角色库为 0（例如关了 DK 登录）：该职业不进入名额，不报错循环创建。
- 补人候选全是会长或最后一人：跳过该工会，找下一家；没有下一家则走离线/新建。
- 传送失败：本轮记日志，下轮重试，不因此给 bot 分配工会。
- `RandomBotUnguildedPerClass = 0`：不跑 `EnsureUnguildedPool`，`InitGuild` 不跳过。

## 测试

可单测的纯逻辑（不启动 worldserver）：

- 给定一批「在线 / 阵营 / 职业 / 是否有会 / 是否在主城」快照，计算哪些 `(team, cls)` 需要补人
- 拆人过滤：会长、最后一人、玩家占用工会不得入选
- `DisableDeathKnightLogin` 时名额集合不含 DK

行为验收（需跑服）：

- 开服稳定后，暴风城、奥格瑞玛 `/who` 各能看到约 10 名无会随机 bot，职业不重复
- 对其中 9 人递申请书可建会
- 把这 10 人拉进玩家工会后，主城会再出现同职业无会 bot（在 bot 工会或离线库仍有人的前提下）
- 玩家工会成员不会被拆走

## 主要改动文件

- `modules/mod-playerbots/conf/playerbots.conf.dist`
- `env/dist/etc/modules/playerbots.conf`
- `modules/mod-playerbots/src/PlayerbotAIConfig.h` / `.cpp`
- `modules/mod-playerbots/src/Mgr/Guild/PlayerbotGuildMgr.h` / `.cpp`
- `modules/mod-playerbots/src/Bot/Factory/PlayerbotFactory.cpp`（`InitGuild`）
- `modules/mod-playerbots/src/Bot/RandomPlayerbotMgr.cpp`（定期 `EnsureUnguildedPool`、按需上线）
- `modules/mod-playerbots/src/Ai/Base/Actions/GuildCreateActions.cpp`
- `modules/mod-playerbots/src/Ai/Base/Actions/GuildManagementActions.cpp`
- `modules/mod-playerbots/src/Ai/Base/Actions/GuildAcceptAction.cpp`
- `modules/mod-playerbots/src/Ai/Base/Actions/BattleGroundJoinAction.cpp`
- `src/test/` 下新增名额计算 / 拆人过滤测试
