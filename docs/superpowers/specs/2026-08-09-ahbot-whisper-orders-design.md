# AH Bot 私聊点单

## 目标

账号所有者向 AH Bot 角色发送私聊：`[物品链接] 数量`，Bot 按行情上架该物品，并私聊回执结果。

## 非目标

- 不支持中文名或纯物品 ID（仅物品链接）
- 不支持自定义起拍价 / 一口价
- 不对全服玩家开放
- 不改变现有随机补货逻辑（点单与随机上架并行）

## 权限

- 允许账号：`AuctionHouseBot.Account`（当前配置为 `58`）
- 该账号下任意角色均可点单
- 其它账号的私聊忽略（不当命令处理，也不回执，避免暴露功能）

## 在线要求

- AH Bot 角色（`AuctionHouseBot.GUID`）必须在线才能收私聊
- 世界启动（或配置加载）后：若该 GUID 对应角色未在线，则自动登录该角色
- 自动登录失败时：写日志；点单功能不可用，直至角色在线

## 消息格式

```
[物品链接] 数量
```

示例（客户端实际文本含 `|Hitem:...|`）：

```
|Hitem:41599:0:0:0:0:0:0:0:80|h[霜纹袋]|h 5
```

解析规则：

1. 从私聊文本中提取第一个 `|Hitem:<id>:...|` 链接，得到 `itemId`
2. 链接后跳过空白，解析正整数 `quantity`
3. 不符合上述格式的私聊：忽略（不回执）

数量与堆叠：

- `quantity` 必须 ≥ 1
- 单笔拍卖数量上限为物品 `MaxStack`；若 `quantity > MaxStack`，拆成多笔拍卖（每笔 ≤ MaxStack），直到凑满请求数量
- 建议硬上限：单次点单总数量不超过 `MaxStack * 20`（防止误输入极大数字）；超出则拒绝并回执说明

## 上架规则

- 卖家：AH Bot 角色（与现有 `Sell()` 归属一致）
- 拍卖行：按 Bot 阵营选择联盟 / 部落拍卖行（复用现有 `GetAuctionHouseEntry` 逻辑）
- 定价：
  1. 若存在市场价（`AHBConfig::GetItemPrice(itemId)` > 0），以其为单价基准
  2. 否则：若 `UseBuyPriceForSeller`，用 `BuyPrice`；否则用 `SellPrice`
  3. 再套用现有品质 `min/max price` 与 `min/max bid` 倍率，得到起拍与一口价（与随机上架同一套公式）
  4. 堆叠拍卖：单价 × 该笔 stackCount
- 拍卖时长：复用 `ElapsingTimeClass` 现有逻辑
- 不扣点单玩家的金币与背包物品（与现有 AH Bot 凭空上架一致）
- 点单**不**经过随机货池 / Vendor / Loot / Profession 过滤（可上架商店包、材料等）
- 拒绝条件并回执：
  - 物品模板不存在
  - 拾取绑定（BOP）
  - 任务绑定物品（`BIND_QUEST_ITEM`）
  - `BuyPrice` 与 `SellPrice` 均为 0（无法定价）
  - Bot 卖家未就绪（角色未在线 / 会话不可用）

## 回执

Bot 私聊回复点单者：

- 成功：`已上架：<物品名> x<总数量>（共N笔），起拍合计 <金币格式>，一口价合计 <金币格式>`；单笔时省略「（共N笔）」
- 失败：简短中文原因（格式不符不回；其它拒绝见上）

## 配置

在 `mod_ahbot.conf` 增加：

```ini
# 是否允许账号所有者私聊点单上架
# Default: 1
AuctionHouseBot.WhisperOrders = 1
```

依赖已有：

- `AuctionHouseBot.Account`
- `AuctionHouseBot.GUID`
- `AuctionHouseBot.UseBuyPriceForSeller`
- `AuctionHouseBot.UseMarketPriceForSeller`（市场价基准；点单定价仍会套品质倍率）
- `AuctionHouseBot.ElapsingTimeClass`

## 架构

模块：`mod-ah-bot`

| 单元 | 职责 |
|------|------|
| `AHBotWhisperOrderScript`（`PlayerScript`） | 监听 `OnPlayerChat`（whisper + receiver）；校验账号；解析链接与数量；调用上架 API；回执 |
| `AuctionHouseBot::SellOrderedItem(...)`（新方法） | 创建物品、定价、写入拍卖行；可拆堆；供私聊脚本调用 |
| 现有 `AHBConfig` / 市场价 | 复用 `GetItemPrice`、品质倍率等 |
| 启动钩子（`WorldScript` 或现有 AHBot 初始化路径） | `WhisperOrders` 开启时确保 Bot 角色在线 |

数据流：

```
玩家私聊 → PlayerScript 校验账号/开关
        → 解析 itemId + quantity
        → SellOrderedItem
        → AuctionHouseMgr 上架
        → 私聊回执
```

## 错误处理

- 格式不符：静默忽略
- 账号不符：静默忽略
- 业务拒绝：私聊说明原因
- 上架中途部分成功（多笔拆分时某一笔失败）：回执已成功数量与失败原因

## 测试计划

1. 账号 58 角色私聊 Bot：`[霜纹袋链接] 1` → AH 出现 1 个，收到成功回执  
2. `[链接] 5` 且 MaxStack=1 → 上架 5 笔，回执总数 5  
3. `[链接] 200` 且 MaxStack=20 → 上架 10 笔 ×20  
4. 非账号 58 私聊同样内容 → 无上架、无回执  
5. BOP 物品链接 → 拒绝回执  
6. `WhisperOrders = 0` → 忽略点单  
7. 重启世界服后 Bot 角色自动在线，点单仍可用  

## 实现备注

- 物品链接解析：扫描 `|Hitem:`，读取随后数字为 `itemId`（与客户端 shift-点击一致）
- 尽量抽取现有 `Sell()` 中「创建 Item → 定价 → 存款/时长 → AddAuction」片段，避免复制整段随机选品逻辑
- 中文回执字符串可直接写死或走简单格式化，无需新 DB 表
