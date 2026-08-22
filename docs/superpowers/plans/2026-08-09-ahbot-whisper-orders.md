# AH Bot Whisper Orders Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let account `AuctionHouseBot.Account` whisper `[item link] quantity` to the AH Bot character so it lists that item at market-style prices and replies with a Chinese receipt.

**Architecture:** Add a pure parse helper (unit-tested), an `AuctionHouseBot::SellOrderedItem` method that reuses existing create/price/AddAuction logic without the random bins, a `PlayerScript` on `OnPlayerCanUseChat(..., Player* receiver)` for whispers, config `WhisperOrders`, and startup auto-login of the AH Bot GUID via `sRandomPlayerbotMgr.AddPlayerBot` so whispers can be received.

**Tech Stack:** AzerothCore C++20, `mod-ah-bot`, Google Test (`src/test`), Playerbots login (`sRandomPlayerbotMgr`), `PlayerScript` chat hooks.

## Global Constraints

- Spec: `docs/superpowers/specs/2026-08-09-ahbot-whisper-orders-design.md`
- Format: item link + space + quantity only (no name, no raw ID, no custom prices)
- Permission: only `AuctionHouseBot.Account`; other accounts silent-ignore
- Hard quantity cap: `MaxStack * 20` per whisper
- Split stacks into multiple auctions when quantity > MaxStack
- Reject BOP and quest-bound; reject zero BuyPrice and SellPrice
- Do not change random AH restock behavior
- Code style: Allman braces, 4-space indent, `{}` format strings, `auto const&`
- Do not build/install unless the user explicitly asks; unit tests for parse may be run when `BUILD_TESTING=ON` already exists
- Do not commit unless the user explicitly asks (skip git commit steps if not requested)

## File Structure

| File | Responsibility |
|------|----------------|
| `modules/mod-ah-bot/src/AHBotWhisperOrderParse.h` | Header-only parse + copper money format (no WoW runtime deps beyond `<string>` / `<cstdint>`) |
| `src/test/ahbot_whisper_order_parse_test.cpp` | Google Test for parse / format helpers |
| `modules/mod-ah-bot/src/AuctionHouseBot.h` / `.cpp` | New `SellOrderedItem` + result struct |
| `modules/mod-ah-bot/src/AuctionHouseBotPlayerScript.cpp` | Whisper hook + receipt + optional ensure-online helper used from WorldScript |
| `modules/mod-ah-bot/src/AuctionHouseBotWorldScript.cpp` / `.h` | After startup, if WhisperOrders, schedule AH Bot login |
| `modules/mod-ah-bot/src/AuctionHouseBotScript.cpp` | Register the new PlayerScript |
| `modules/mod-ah-bot/conf/mod_ahbot.conf.dist` | Document `AuctionHouseBot.WhisperOrders` |
| `env/dist/etc/modules/mod_ahbot.conf` | Enable WhisperOrders on this install |

---

### Task 1: Parse helper + unit tests

**Files:**
- Create: `modules/mod-ah-bot/src/AHBotWhisperOrderParse.h`
- Create: `src/test/ahbot_whisper_order_parse_test.cpp`

**Interfaces:**
- Consumes: nothing
- Produces:
  - `bool AHBotParseWhisperOrder(std::string const& msg, uint32& outItemId, uint32& outQuantity);`
  - `std::string AHBotFormatCopper(uint64 copper);`

- [ ] **Step 1: Write the failing tests**

Create `src/test/ahbot_whisper_order_parse_test.cpp`:

```cpp
#include "gtest/gtest.h"
#include "../../modules/mod-ah-bot/src/AHBotWhisperOrderParse.h"

TEST(AHBotWhisperOrderParse, ParsesItemLinkAndQuantity)
{
    uint32 itemId = 0;
    uint32 qty = 0;
    std::string const msg = "|cffffffff|Hitem:41599:0:0:0:0:0:0:0:80|h[Frostweave Bag]|h|r 5";
    ASSERT_TRUE(AHBotParseWhisperOrder(msg, itemId, qty));
    EXPECT_EQ(itemId, 41599u);
    EXPECT_EQ(qty, 5u);
}

TEST(AHBotWhisperOrderParse, RejectsMissingQuantity)
{
    uint32 itemId = 0;
    uint32 qty = 0;
    EXPECT_FALSE(AHBotParseWhisperOrder("|Hitem:41599:0:0:0:0:0:0:0:80|h[Bag]|h", itemId, qty));
}

TEST(AHBotWhisperOrderParse, RejectsBareId)
{
    uint32 itemId = 0;
    uint32 qty = 0;
    EXPECT_FALSE(AHBotParseWhisperOrder("41599 5", itemId, qty));
}

TEST(AHBotWhisperOrderParse, FormatsCopper)
{
    EXPECT_EQ(AHBotFormatCopper(0), "0g 0s 0c");
    EXPECT_EQ(AHBotFormatCopper(12345), "1g 23s 45c");
}
```

- [ ] **Step 2: Run tests to verify they fail to compile / link**

Run (from existing build dir with `-DBUILD_TESTING=ON` if available):

```bash
# PowerShell example from build tree
ctest -R ahbot_whisper -V
# or
./src/test/unit_tests --gtest_filter=AHBotWhisperOrderParse.*
```

Expected: FAIL (header / symbols missing)

- [ ] **Step 3: Implement the header**

Create `modules/mod-ah-bot/src/AHBotWhisperOrderParse.h`:

```cpp
#ifndef AH_BOT_WHISPER_ORDER_PARSE_H
#define AH_BOT_WHISPER_ORDER_PARSE_H

#include <cstdint>
#include <cctype>
#include <string>

inline bool AHBotParseWhisperOrder(std::string const& msg, uint32_t& outItemId, uint32_t& outQuantity)
{
    std::size_t const itemTag = msg.find("|Hitem:");
    if (itemTag == std::string::npos)
        return false;

    std::size_t pos = itemTag + 7;
    if (pos >= msg.size() || !std::isdigit(static_cast<unsigned char>(msg[pos])))
        return false;

    uint32_t itemId = 0;
    while (pos < msg.size() && std::isdigit(static_cast<unsigned char>(msg[pos])))
    {
        itemId = itemId * 10u + static_cast<uint32_t>(msg[pos] - '0');
        ++pos;
    }
    if (itemId == 0)
        return false;

    // Link form: |Hitem:...|h[Name]|h
    std::size_t firstH = msg.find("|h", pos);
    if (firstH == std::string::npos)
        return false;
    std::size_t secondH = msg.find("|h", firstH + 2);
    if (secondH == std::string::npos)
        return false;

    pos = secondH + 2;
    while (pos < msg.size() && std::isspace(static_cast<unsigned char>(msg[pos])))
        ++pos;

    if (pos >= msg.size() || !std::isdigit(static_cast<unsigned char>(msg[pos])))
        return false;

    uint32_t quantity = 0;
    while (pos < msg.size() && std::isdigit(static_cast<unsigned char>(msg[pos])))
    {
        quantity = quantity * 10u + static_cast<uint32_t>(msg[pos] - '0');
        ++pos;
    }
    if (quantity == 0)
        return false;

    while (pos < msg.size() && std::isspace(static_cast<unsigned char>(msg[pos])))
        ++pos;
    if (pos != msg.size())
        return false;

    outItemId = itemId;
    outQuantity = quantity;
    return true;
}

inline std::string AHBotFormatCopper(uint64_t copper)
{
    uint64_t g = copper / 10000ull;
    uint64_t s = (copper % 10000ull) / 100ull;
    uint64_t c = copper % 100ull;
    return std::to_string(g) + "g " + std::to_string(s) + "s " + std::to_string(c) + "c";
}

#endif
```

Note: use `uint32_t` in the header so unit_tests need no AzerothCore typedefs. Call sites in the module may cast to `uint32`.

- [ ] **Step 4: Reconfigure if needed and run tests**

```bash
# from build dir with BUILD_TESTING=ON
cmake --build . --target unit_tests -j
./src/test/unit_tests --gtest_filter=AHBotWhisperOrderParse.*
```

Expected: all four tests PASS

- [ ] **Step 5: Commit (only if user asked)**

```bash
git add modules/mod-ah-bot/src/AHBotWhisperOrderParse.h src/test/ahbot_whisper_order_parse_test.cpp
git commit -m "$(cat <<'EOF'
feat(ahbot): add whisper order parse helpers and unit tests

EOF
)"
```

---

### Task 2: `SellOrderedItem` on AuctionHouseBot

**Files:**
- Modify: `modules/mod-ah-bot/src/AuctionHouseBot.h`
- Modify: `modules/mod-ah-bot/src/AuctionHouseBot.cpp` (after `Sell()`, before `Update()`)

**Interfaces:**
- Consumes: existing pricing fields on `AHBConfig` (`GetItemPrice`, `UseBuyPriceForSeller`, `GetMinPrice`/`GetMaxPrice`, `GetMinBidPrice`/`GetMaxBidPrice`, `ElapsingTimeClass`, `GetAHFID`), globals `gAllianceConfig` / `gHordeConfig` / `gNeutralConfig`, private helpers `getElapsedTime`
- Produces:

```cpp
struct AHBotOrderResult
{
    bool success = false;
    std::string error;          // Chinese when failed; empty on full success
    std::string itemName;
    uint32 listedQuantity = 0;  // successfully listed count
    uint32 listedStacks = 0;
    uint64 totalBid = 0;
    uint64 totalBuyout = 0;
};

// Lists itemId x quantity as bot-owned auctions. Does not use random bins.
AHBotOrderResult SellOrderedItem(uint32 itemId, uint32 quantity);
```

- [ ] **Step 1: Declare types/methods in the header**

In `AuctionHouseBot.h`, add `#include <string>` if needed, then inside `class AuctionHouseBot` public section:

```cpp
    struct OrderResult
    {
        bool success = false;
        std::string error;
        std::string itemName;
        uint32 listedQuantity = 0;
        uint32 listedStacks = 0;
        uint64 totalBid = 0;
        uint64 totalBuyout = 0;
    };

    OrderResult SellOrderedItem(uint32 itemId, uint32 quantity);
```

(Use nested `OrderResult` name in code; plan text `AHBotOrderResult` means this nested type.)

- [ ] **Step 2: Implement `SellOrderedItem`**

Implementation sketch (place in `AuctionHouseBot.cpp`; mirror the create/price/AddAuction block from `Sell()` around lines 795–923):

```cpp
AuctionHouseBot::OrderResult AuctionHouseBot::SellOrderedItem(uint32 itemId, uint32 quantity)
{
    OrderResult result;

    ItemTemplate const* prototype = sObjectMgr->GetItemTemplate(itemId);
    if (!prototype)
    {
        result.error = "物品不存在";
        return result;
    }
    result.itemName = prototype->Name1;

    if (prototype->Bonding == BIND_WHEN_PICKED_UP)
    {
        result.error = "拾取绑定物品不能上架";
        return result;
    }
    if (prototype->Bonding == BIND_QUEST_ITEM)
    {
        result.error = "任务物品不能上架";
        return result;
    }
    if (prototype->BuyPrice == 0 && prototype->SellPrice == 0)
    {
        result.error = "物品没有价格，无法上架";
        return result;
    }

    uint32 maxStack = prototype->GetMaxStackSize();
    if (maxStack == 0)
        maxStack = 1;

    if (quantity == 0)
    {
        result.error = "数量无效";
        return result;
    }
    if (quantity > maxStack * 20u)
    {
        result.error = Acore::StringFormat("数量过大（上限 {}）", maxStack * 20u);
        return result;
    }

    // Pick AH config: same rules as Update() — neutral-only when two-side AH is on
    AHBConfig* config = gNeutralConfig;
    if (!sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
    {
        // Temporary player to resolve faction, same pattern as Update()
        std::string accountName = "AuctionHouseBot" + std::to_string(_account);
        WorldSession session(_account, std::move(accountName), 0, nullptr, SEC_PLAYER,
            sWorld->getIntConfig(CONFIG_EXPANSION), 0, LOCALE_enUS, 0, false, false, 0);
        Player tmpPlayer(&session);
        tmpPlayer.Initialize(_id);
        if (tmpPlayer.GetTeamId() == TEAM_ALLIANCE)
            config = gAllianceConfig;
        else
            config = gHordeConfig;
    }

    if (!config)
    {
        result.error = "拍卖行未就绪";
        return result;
    }

    AuctionHouseEntry const* ahEntry = sAuctionMgr->GetAuctionHouseEntryFromFactionTemplate(config->GetAHFID());
    AuctionHouseObject* auctionHouse = sAuctionMgr->GetAuctionsMap(config->GetAHFID());
    if (!ahEntry || !auctionHouse)
    {
        result.error = "拍卖行未就绪";
        return result;
    }

    std::string accountName = "AuctionHouseBot" + std::to_string(_account);
    WorldSession session(_account, std::move(accountName), 0, nullptr, SEC_PLAYER,
        sWorld->getIntConfig(CONFIG_EXPANSION), 0, LOCALE_enUS, 0, false, false, 0);
    Player AHBplayer(&session);
    AHBplayer.Initialize(_id);
    ObjectAccessor::AddObject(&AHBplayer);

    uint32 remaining = quantity;
    while (remaining > 0)
    {
        uint32 stackCount = remaining > maxStack ? maxStack : remaining;

        Item* item = Item::CreateItem(itemId, stackCount, &AHBplayer);
        if (!item)
        {
            result.error = result.listedQuantity
                ? Acore::StringFormat("部分上架失败（已上架 {}）", result.listedQuantity)
                : "创建物品失败";
            break;
        }

        item->AddToUpdateQueueOf(&AHBplayer);
        if (uint32 randomPropertyId = Item::GenerateItemRandomPropertyId(itemId))
            item->SetItemRandomProperties(randomPropertyId);

        uint64 buyoutPrice = config->GetItemPrice(itemId);
        if (buyoutPrice == 0)
            buyoutPrice = config->UseBuyPriceForSeller ? prototype->BuyPrice : prototype->SellPrice;

        buyoutPrice = buyoutPrice * urand(config->GetMinPrice(prototype->Quality), config->GetMaxPrice(prototype->Quality));
        buyoutPrice = buyoutPrice / 100;
        uint64 bidPrice = buyoutPrice * urand(config->GetMinBidPrice(prototype->Quality), config->GetMaxBidPrice(prototype->Quality));
        bidPrice = bidPrice / 100;

        uint32 elapsingTime = getElapsedTime(config->ElapsingTimeClass);
        uint32 deposit = sAuctionMgr->GetAuctionDeposit(ahEntry, elapsingTime, item, stackCount);

        auto trans = CharacterDatabase.BeginTransaction();
        AuctionEntry* auctionEntry = new AuctionEntry();
        auctionEntry->Id = sObjectMgr->GenerateAuctionID();
        auctionEntry->houseId = AuctionHouseId(config->GetAHID());
        auctionEntry->item_guid = item->GetGUID();
        auctionEntry->item_template = item->GetEntry();
        auctionEntry->itemCount = item->GetCount();
        auctionEntry->owner = AHBplayer.GetGUID();
        auctionEntry->startbid = bidPrice * stackCount;
        auctionEntry->buyout = buyoutPrice * stackCount;
        auctionEntry->bid = 0;
        auctionEntry->deposit = deposit;
        auctionEntry->expire_time = (time_t)elapsingTime + time(nullptr);
        auctionEntry->auctionHouseEntry = ahEntry;

        item->SaveToDB(trans);
        item->RemoveFromUpdateQueueOf(&AHBplayer);
        sAuctionMgr->AddAItem(item);
        auctionHouse->AddAuction(auctionEntry);
        auctionEntry->SaveToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        result.listedQuantity += stackCount;
        result.listedStacks += 1;
        result.totalBid += auctionEntry->startbid;
        result.totalBuyout += auctionEntry->buyout;
        remaining -= stackCount;
    }

    ObjectAccessor::RemoveObject(&AHBplayer);

    if (result.listedQuantity == quantity)
        result.success = true;
    else if (result.listedQuantity == 0 && result.error.empty())
        result.error = "上架失败";

    return result;
}
```

Include whatever headers `Sell()` already uses (`Item.h`, `ObjectAccessor.h`, `WorldSession.h`, `AuctionHouseBotCommon.h` for globals). Keep private `getElapsedTime` usable (already a private method).

- [ ] **Step 3: Compile check (when user asks to build)**

```bash
cmake --build . --target modules -j
```

Expected: `SellOrderedItem` compiles; no link errors from missing symbols.

- [ ] **Step 4: Commit (only if user asked)**

```bash
git add modules/mod-ah-bot/src/AuctionHouseBot.h modules/mod-ah-bot/src/AuctionHouseBot.cpp
git commit -m "$(cat <<'EOF'
feat(ahbot): add SellOrderedItem for whisper restock orders

EOF
)"
```

---

### Task 3: Config flag `WhisperOrders`

**Files:**
- Modify: `modules/mod-ah-bot/conf/mod_ahbot.conf.dist`
- Modify: `env/dist/etc/modules/mod_ahbot.conf`
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotCommon.h` (declare `extern bool gWhisperOrders;`)
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotCommon.cpp` (define `bool gWhisperOrders = false;`)
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotWorldScript.cpp` `OnBeforeConfigLoad` to load the flag

**Interfaces:**
- Consumes: `sConfigMgr->GetOption<bool>("AuctionHouseBot.WhisperOrders", true)`
- Produces: global `gWhisperOrders` readable by PlayerScript / WorldScript

- [ ] **Step 1: Add conf keys**

In both `mod_ahbot.conf.dist` and live `mod_ahbot.conf`, after `EnableSeller` block comments, add:

```ini
#
#    AuctionHouseBot.WhisperOrders
#        Allow AuctionHouseBot.Account characters to whisper the bot
#        with "[item link] quantity" to force-list that item.
#    Default 1 (enabled)
#
AuctionHouseBot.WhisperOrders = 1
```

- [ ] **Step 2: Load into global**

`AuctionHouseBotCommon.h`:

```cpp
extern bool gWhisperOrders;
```

`AuctionHouseBotCommon.cpp`:

```cpp
bool gWhisperOrders = false;
```

In `AHBot_WorldScript::OnBeforeConfigLoad`, after reading account/GUID options:

```cpp
gWhisperOrders = sConfigMgr->GetOption<bool>("AuctionHouseBot.WhisperOrders", true);
```

Also reload this on `reload` path (same function already runs on reload).

- [ ] **Step 3: Commit (only if user asked)**

```bash
git add modules/mod-ah-bot/conf/mod_ahbot.conf.dist env/dist/etc/modules/mod_ahbot.conf \
  modules/mod-ah-bot/src/AuctionHouseBotCommon.h modules/mod-ah-bot/src/AuctionHouseBotCommon.cpp \
  modules/mod-ah-bot/src/AuctionHouseBotWorldScript.cpp
git commit -m "$(cat <<'EOF'
feat(ahbot): add WhisperOrders config flag

EOF
)"
```

---

### Task 4: PlayerScript whisper handler + receipts

**Files:**
- Create: `modules/mod-ah-bot/src/AuctionHouseBotPlayerScript.cpp`
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotScript.cpp` (register)
- Modify: `modules/mod-ah-bot/src/ah_bot_loader.cpp` only if a new `Add*` is introduced; prefer registering inside existing `AddAHBotScripts()`

**Interfaces:**
- Consumes: `gWhisperOrders`, `gBots`, `AHBotParseWhisperOrder`, `AHBotFormatCopper`, `AuctionHouseBot::SellOrderedItem`, `AuctionHouseBot::GetAHBplayerGUID`
- Produces: `AHBot_PlayerScript` registered; whisper path end-to-end

- [ ] **Step 1: Implement PlayerScript**

Create `modules/mod-ah-bot/src/AuctionHouseBotPlayerScript.cpp`:

```cpp
#include "ScriptMgr.h"
#include "Player.h"
#include "Chat.h"
#include "WorldSession.h"
#include "Config.h"

#include "AuctionHouseBot.h"
#include "AuctionHouseBotCommon.h"
#include "AHBotWhisperOrderParse.h"

class AHBot_PlayerScript : public PlayerScript
{
public:
    AHBot_PlayerScript() : PlayerScript("AHBot_PlayerScript", {
        PLAYERHOOK_CAN_PLAYER_USE_PRIVATE_CHAT
    }) { }

    bool OnPlayerCanUseChat(Player* player, uint32 type, uint32 /*lang*/, std::string& msg, Player* receiver) override
    {
        if (!gWhisperOrders || type != CHAT_MSG_WHISPER || !player || !receiver)
            return true;

        // Receiver must be an AH bot character
        AuctionHouseBot* bot = nullptr;
        for (AuctionHouseBot* candidate : gBots)
        {
            if (candidate->GetAHBplayerGUID() == receiver->GetGUID().GetCounter())
            {
                bot = candidate;
                break;
            }
        }
        if (!bot)
            return true;

        // Only the configured AH bot account may place orders
        uint32 allowedAccount = sConfigMgr->GetOption<uint32>("AuctionHouseBot.Account", 0);
        if (!allowedAccount || player->GetSession()->GetAccountId() != allowedAccount)
            return true; // silent ignore

        uint32_t itemId = 0;
        uint32_t quantity = 0;
        if (!AHBotParseWhisperOrder(msg, itemId, quantity))
            return true; // not an order; leave whisper alone

        AuctionHouseBot::OrderResult result = bot->SellOrderedItem(itemId, quantity);

        ChatHandler handler(player->GetSession());
        if (result.success)
        {
            if (result.listedStacks > 1)
            {
                handler.PSendSysMessage("已上架：{} x{}（共{}笔），起拍合计 {}，一口价合计 {}",
                    result.itemName, result.listedQuantity, result.listedStacks,
                    AHBotFormatCopper(result.totalBid), AHBotFormatCopper(result.totalBuyout));
            }
            else
            {
                handler.PSendSysMessage("已上架：{} x{}，起拍合计 {}，一口价合计 {}",
                    result.itemName, result.listedQuantity,
                    AHBotFormatCopper(result.totalBid), AHBotFormatCopper(result.totalBuyout));
            }
        }
        else
        {
            if (result.listedQuantity > 0)
            {
                handler.PSendSysMessage("部分上架：{} x{}，起拍合计 {}，一口价合计 {}。{}",
                    result.itemName, result.listedQuantity,
                    AHBotFormatCopper(result.totalBid), AHBotFormatCopper(result.totalBuyout),
                    result.error);
            }
            else
                handler.PSendSysMessage("上架失败：{}", result.error);
        }

        // Consume the whisper as a command (optional: return true to still deliver)
        return true;
    }
};

void AddAHBotPlayerScripts()
{
    new AHBot_PlayerScript();
}
```

Receipts use `ChatHandler::PSendSysMessage` (system message to the orderer). This avoids depending on the bot AI answering whispers. Spec allows Bot whisper; system message is clearer and works even if bot chat AI swallows the message. If you prefer true whisper from `receiver`, use `receiver->Whisper(text, LANG_UNIVERSAL, player)` instead of `ChatHandler`.

**Choose for this plan:** system message via `ChatHandler` (reliable with playerbots whisper hooks). Spec text “私聊回执” is satisfied operationally by an immediate private response channel; if product owner insists on `CHAT_MSG_WHISPER` from bot, swap to `receiver->Whisper`.

- [ ] **Step 2: Register script**

In `AuctionHouseBotScript.cpp`:

```cpp
void AddAHBotPlayerScripts();

void AddAHBotScripts()
{
    new AHBot_WorldScript();
    new AHBot_AuctionHouseScript();
    new AHBot_MailScript();
    AddAHBotPlayerScripts();
}
```

- [ ] **Step 3: Manual logic review checklist (no full rebuild required yet)**

Verify in code review:
- Wrong account → no `SellOrderedItem`, no message
- Bad format → no message
- Good order → `SellOrderedItem` called once

- [ ] **Step 4: Commit (only if user asked)**

```bash
git add modules/mod-ah-bot/src/AuctionHouseBotPlayerScript.cpp \
  modules/mod-ah-bot/src/AuctionHouseBotScript.cpp
git commit -m "$(cat <<'EOF'
feat(ahbot): handle owner whisper orders on AH bot character

EOF
)"
```

---

### Task 5: Ensure AH Bot character is online

**Files:**
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotWorldScript.h`
- Modify: `modules/mod-ah-bot/src/AuctionHouseBotWorldScript.cpp`

**Interfaces:**
- Consumes: `gWhisperOrders`, `gBotsId`, `sRandomPlayerbotMgr.AddPlayerBot`
- Produces: AH Bot GUID logged in after startup so clients can whisper it

- [ ] **Step 1: Extend WorldScript hooks**

In the constructor enabled-hooks list, add `WORLDHOOK_ON_UPDATE` (or keep a one-shot flag from `OnStartup`).

`AuctionHouseBotWorldScript.h` — add:

```cpp
void OnUpdate(uint32 diff) override;
private:
    bool _whisperLoginAttempted = false;
    uint32 _whisperLoginTimer = 0;
```

- [ ] **Step 2: Delayed login via playerbots**

In `.cpp`:

```cpp
#include "RandomPlayerbotMgr.h"
#include "ObjectGuid.h"
#include "ObjectAccessor.h"

void AHBot_WorldScript::OnUpdate(uint32 diff)
{
    if (!gWhisperOrders || _whisperLoginAttempted)
        return;

    _whisperLoginTimer += diff;
    if (_whisperLoginTimer < 10000) // wait 10s for playerbots init
        return;

    _whisperLoginAttempted = true;

    for (uint32 id : gBotsId)
    {
        ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(id);
        if (ObjectAccessor::FindConnectedPlayer(guid))
            continue;

        LOG_INFO("module", "AHBot: WhisperOrders login bot character {}", id);
        sRandomPlayerbotMgr.AddPlayerBot(guid, 0);
    }
}
```

Register `WORLDHOOK_ON_UPDATE` in the constructor hook list.

If `AddPlayerBot` refuses the character (account policy), log an error:

```cpp
LOG_ERROR("module", "AHBot: failed to keep character {} online for WhisperOrders; whispers will not work until it is logged in", id);
```

Note: `AddPlayerBot(..., 0)` treats the character as a random bot login path (allowed). Ensure account 58 / GUID 562 is acceptable to playerbots config (`randomBotAccounts` / character exists). If login fails in practice, fallback documented for the user: manually `.login` / add that character as a playerbot.

- [ ] **Step 3: Commit (only if user asked)**

```bash
git add modules/mod-ah-bot/src/AuctionHouseBotWorldScript.h \
  modules/mod-ah-bot/src/AuctionHouseBotWorldScript.cpp
git commit -m "$(cat <<'EOF'
feat(ahbot): auto-login AH bot character for whisper orders

EOF
)"
```

---

### Task 6: End-to-end verification

**Files:** none (manual)

**Interfaces:**
- Consumes: completed Tasks 1–5, running worldserver with rebuilt `mod-ah-bot`

- [ ] **Step 1: Rebuild / install when user asks**

```bash
cmake --build . --target worldserver -j
# install if that is how this env deploys
```

- [ ] **Step 2: Confirm config**

In `env/dist/etc/modules/mod_ahbot.conf`:

```
AuctionHouseBot.EnableSeller = 1
AuctionHouseBot.Account = 58
AuctionHouseBot.GUID = 562
AuctionHouseBot.WhisperOrders = 1
AuctionHouseBot.VendorItems = 1
AuctionHouseBot.ProfessionItems = 1
```

- [ ] **Step 3: In-game checklist**

1. Restart worldserver; within ~15s AH Bot character GUID 562 is online (who / friends).
2. From any character on account 58, whisper Bot: shift-click Frostweave Bag + ` 1` → system message success; AH shows 1 listing owned by Bot.
3. Whisper stackable material ` 200` with MaxStack 20 → 10 stacks; receipt shows `共10笔`.
4. From a different account whisper the same → no listing, no receipt.
5. Whisper a BOP item link + ` 1` → `上架失败：拾取绑定物品不能上架`.
6. Set `WhisperOrders = 0`, `.reload config` (or restart) → orders ignored.
7. Random AH restock still runs (unrelated white/green items still appear over time).

- [ ] **Step 4: Final commit of any leftover conf/docs (only if user asked)**

```bash
git add docs/superpowers/specs/2026-08-09-ahbot-whisper-orders-design.md \
  docs/superpowers/plans/2026-08-09-ahbot-whisper-orders.md
git commit -m "$(cat <<'EOF'
docs: add AH bot whisper-order spec and plan

EOF
)"
```

---

## Spec coverage check

| Spec requirement | Task |
|------------------|------|
| Format link + qty | Task 1, 4 |
| Account 58 only, silent others | Task 4 |
| Auto market / buy price + quality multipliers | Task 2 |
| Split stacks / cap MaxStack*20 | Task 2 |
| Reject BOP / quest / no price | Task 2 |
| Success / failure receipts | Task 4 |
| `WhisperOrders` config | Task 3 |
| Bot online for whispers | Task 5 |
| No random-bin filter on orders | Task 2 |
| Random restock unchanged | Task 2 (new method only) |
| E2E tests from spec | Task 6 |

## Self-review notes

- No TBD placeholders left.
- Nested type name in code is `AuctionHouseBot::OrderResult` (consistent across Task 2 and 4).
- Receipt channel: system message (documented deviation from literal “Bot whisper”); swap to `receiver->Whisper` in Task 4 if required during review.
- Module↔playerbots dependency for login is intentional on this fork; listing itself uses the existing temporary-Player pattern and does not require the real bot online.
