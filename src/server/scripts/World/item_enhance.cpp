/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Bag.h"
#include "Chat.h"
#include "Config.h"
#include "CreatureScript.h"
#include "DatabaseEnv.h"
#include "Formulas.h"
#include "Group.h"
#include "Item.h"
#include "ItemScript.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "PlayerScript.h"
#include "Random.h"
#include "ScriptedGossip.h"
#include "StringFormat.h"
#include "WorldScript.h"
#include "WorldSession.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Equipment enhancement. Gems are the only resource. Right-click a gem, or talk to the
// enhancement master, to enhance worn gear, exchange gems, move a level, or see the top list.
// The stat bonus itself is applied by Player::_ApplyItemBonuses from Item::GetEnhanceLevel().
// Menu text is zhCN only.

namespace
{
constexpr uint32 ENHANCE_TEXT = 800001;
constexpr uint32 TOP_LIMIT = 10;
constexpr uint32 TARGET_LIMIT = 20;

enum GemTier : uint8
{
    TIER_LOW  = 0,
    TIER_MID  = 1,
    TIER_HIGH = 2,
    MAX_TIERS = 3
};

constexpr uint32 SENDER_MAIN = 1;
constexpr uint32 SENDER_LIST = 2;
constexpr uint32 SENDER_EXCHANGE = 3;
constexpr uint32 SENDER_TRANSFER = 4;
constexpr uint32 SENDER_TOP = 5;
constexpr uint32 SENDER_ITEM_BASE = 100;
constexpr uint32 SENDER_TRANSFER_BASE = 200;

constexpr uint32 ACT_REFRESH = 1;
constexpr uint32 ACT_BACK = 2;
constexpr uint32 ACT_LIST = 3;
constexpr uint32 ACT_EXCHANGE = 4;
constexpr uint32 ACT_TRANSFER = 5;
constexpr uint32 ACT_TOP = 6;
constexpr uint32 ACT_ATTEMPT = 7;
constexpr uint32 ACT_SLOT_BASE = 100;
constexpr uint32 ACT_EXCHANGE_BASE = 200;
constexpr uint32 ACT_POS_BASE = 0x10000;

struct EnhanceConfig
{
    bool Enable = true;
    uint32 NpcEntry = 550002;
    std::array<uint32, MAX_TIERS> Gem = { 550010, 550011, 550012 };

    uint32 SafeLevel = 3;
    float ChanceBase = 90.0f;
    float ChanceDecay = 0.94f;
    float ChanceFloor = 30.0f;

    uint32 LowUntil = 5;
    uint32 MidUntil = 10;
    uint32 HighStep = 10;

    uint32 KeepUntil = 7;
    uint32 AnchorFrom = 10;
    uint32 AnchorStep = 5;

    uint32 ExchangeRate = 5;
    uint32 TransferMidPerLevel = 1;
    uint32 AnnounceLevel = 15;

    bool DropEnable = true;
    float NormalLow = 2.0f;
    float EliteLow = 20.0f;
    float EliteMid = 5.0f;
    uint32 RareLow = 1;
    float RareMid = 30.0f;
    uint32 DungeonMid = 1;
    float DungeonHigh = 10.0f;
    uint32 HeroicMid = 2;
    float HeroicHigh = 20.0f;
    uint32 Raid10High = 1;
    uint32 Raid25High = 2;
    uint32 WorldBossHigh = 3;
};

EnhanceConfig LoadConfig()
{
    EnhanceConfig cfg;
    cfg.Enable = sConfigMgr->GetOption<bool>("ItemEnhance.Enable", true, false);
    cfg.NpcEntry = sConfigMgr->GetOption<uint32>("ItemEnhance.NpcEntry", 550002, false);
    cfg.Gem[TIER_LOW] = sConfigMgr->GetOption<uint32>("ItemEnhance.Gem.Low", 550010, false);
    cfg.Gem[TIER_MID] = sConfigMgr->GetOption<uint32>("ItemEnhance.Gem.Mid", 550011, false);
    cfg.Gem[TIER_HIGH] = sConfigMgr->GetOption<uint32>("ItemEnhance.Gem.High", 550012, false);

    cfg.SafeLevel = sConfigMgr->GetOption<uint32>("ItemEnhance.Chance.SafeLevel", 3, false);
    cfg.ChanceBase = sConfigMgr->GetOption<float>("ItemEnhance.Chance.Base", 90.0f, false);
    cfg.ChanceDecay = sConfigMgr->GetOption<float>("ItemEnhance.Chance.Decay", 0.94f, false);
    cfg.ChanceFloor = sConfigMgr->GetOption<float>("ItemEnhance.Chance.Floor", 30.0f, false);

    cfg.LowUntil = sConfigMgr->GetOption<uint32>("ItemEnhance.Cost.LowUntil", 5, false);
    cfg.MidUntil = std::max(cfg.LowUntil, sConfigMgr->GetOption<uint32>("ItemEnhance.Cost.MidUntil", 10, false));
    cfg.HighStep = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("ItemEnhance.Cost.HighStep", 10, false));

    cfg.KeepUntil = sConfigMgr->GetOption<uint32>("ItemEnhance.Fail.KeepUntil", 7, false);
    cfg.AnchorFrom = sConfigMgr->GetOption<uint32>("ItemEnhance.Fail.AnchorFrom", 10, false);
    cfg.AnchorStep = sConfigMgr->GetOption<uint32>("ItemEnhance.Fail.AnchorStep", 5, false);

    cfg.ExchangeRate = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("ItemEnhance.Exchange.Rate", 5, false));
    cfg.TransferMidPerLevel = sConfigMgr->GetOption<uint32>("ItemEnhance.Transfer.MidPerLevel", 1, false);
    cfg.AnnounceLevel = sConfigMgr->GetOption<uint32>("ItemEnhance.Announce.MinLevel", 15, false);

    cfg.DropEnable = sConfigMgr->GetOption<bool>("ItemEnhance.Drop.Enable", true, false);
    cfg.NormalLow = sConfigMgr->GetOption<float>("ItemEnhance.Drop.Normal.LowChance", 2.0f, false);
    cfg.EliteLow = sConfigMgr->GetOption<float>("ItemEnhance.Drop.Elite.LowChance", 20.0f, false);
    cfg.EliteMid = sConfigMgr->GetOption<float>("ItemEnhance.Drop.Elite.MidChance", 5.0f, false);
    cfg.RareLow = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.Rare.LowCount", 1, false);
    cfg.RareMid = sConfigMgr->GetOption<float>("ItemEnhance.Drop.Rare.MidChance", 30.0f, false);
    cfg.DungeonMid = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.DungeonBoss.MidCount", 1, false);
    cfg.DungeonHigh = sConfigMgr->GetOption<float>("ItemEnhance.Drop.DungeonBoss.HighChance", 10.0f, false);
    cfg.HeroicMid = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.HeroicBoss.MidCount", 2, false);
    cfg.HeroicHigh = sConfigMgr->GetOption<float>("ItemEnhance.Drop.HeroicBoss.HighChance", 20.0f, false);
    cfg.Raid10High = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.Raid10Boss.HighCount", 1, false);
    cfg.Raid25High = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.Raid25Boss.HighCount", 2, false);
    cfg.WorldBossHigh = sConfigMgr->GetOption<uint32>("ItemEnhance.Drop.WorldBoss.HighCount", 3, false);
    return cfg;
}

EnhanceConfig& Cfg()
{
    static EnhanceConfig cfg = LoadConfig();
    return cfg;
}

char const* const TierName[MAX_TIERS] = { "低阶强化石", "中阶强化石", "高阶强化石" };

char const* SlotName(uint8 slot)
{
    static char const* const names[EQUIPMENT_SLOT_END] =
    {
        "头部", "颈部", "肩部", "衬衣", "胸部", "腰部", "腿部", "脚", "手腕", "手",
        "手指", "手指", "饰品", "饰品", "背部", "主手", "副手", "远程", "战袍"
    };
    return slot < EQUIPMENT_SLOT_END ? names[slot] : "";
}

struct Cost
{
    GemTier Tier = TIER_LOW;
    uint32 Count = 1;
};

// Cost of one attempt to reach target.
Cost CostFor(uint32 target)
{
    EnhanceConfig const& cfg = Cfg();
    if (target <= cfg.LowUntil)
        return { TIER_LOW, 1 };
    if (target <= cfg.MidUntil)
        return { TIER_MID, 1 };
    return { TIER_HIGH, 1 + (target - cfg.MidUntil - 1) / cfg.HighStep };
}

// Success chance in percent for reaching target.
float ChanceFor(uint32 target)
{
    EnhanceConfig const& cfg = Cfg();
    if (target <= cfg.SafeLevel)
        return 100.0f;
    float chance = cfg.ChanceBase * std::pow(cfg.ChanceDecay, float(target - cfg.SafeLevel - 1));
    return std::min(100.0f, std::max(cfg.ChanceFloor, chance));
}

// Level left after a failed attempt from current to current + 1.
uint32 LevelAfterFail(uint32 current)
{
    EnhanceConfig const& cfg = Cfg();
    if (current + 1 <= cfg.KeepUntil || !current)
        return current;
    if (cfg.AnchorStep && current >= cfg.AnchorFrom)
    {
        uint32 anchor = cfg.AnchorFrom + (current - cfg.AnchorFrom) / cfg.AnchorStep * cfg.AnchorStep;
        return std::max(anchor, current - 1);
    }
    return current - 1;
}

std::string ItemName(Player const* player, ItemTemplate const* proto)
{
    std::string name = proto->Name1;
    if (ItemLocale const* locale = sObjectMgr->GetItemLocale(proto->ItemId))
        ObjectMgr::GetLocaleString(locale->Name, player->GetSession()->GetSessionDbcLocale(), name);
    return name;
}

std::string ItemLabel(Player const* player, Item const* item)
{
    std::string label = ItemName(player, item->GetTemplate());
    if (uint32 level = item->GetEnhanceLevel())
        label += Acore::StringFormat(" +{}", level);
    return label;
}

std::string ItemLink(Player const* player, Item const* item)
{
    ItemTemplate const* proto = item->GetTemplate();
    uint32 color = ItemQualityColors[proto->Quality < MAX_ITEM_QUALITY ? proto->Quality : ITEM_QUALITY_POOR];
    return Acore::StringFormat("|c{:08x}|Hitem:{}:0:0:0:0:0:0:0:0:0|h[{}]|h|r", color, proto->ItemId,
        ItemName(player, proto));
}

bool HasScalableStats(ItemTemplate const* proto)
{
    if (proto->Armor || proto->Block || proto->ScalingStatDistribution || proto->Damage[0].DamageMax > 0.0f)
        return true;
    if (proto->HolyRes || proto->FireRes || proto->NatureRes || proto->FrostRes || proto->ShadowRes || proto->ArcaneRes)
        return true;
    for (uint8 i = 0; i < proto->StatsCount && i < MAX_ITEM_PROTO_STATS; ++i)
        if (proto->ItemStat[i].ItemStatValue > 0)
            return true;
    return false;
}

bool CanEnhance(Item const* item)
{
    if (!item)
        return false;
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto || (proto->Class != ITEM_CLASS_WEAPON && proto->Class != ITEM_CLASS_ARMOR))
        return false;
    if (proto->InventoryType == INVTYPE_NON_EQUIP || proto->InventoryType == INVTYPE_BODY
        || proto->InventoryType == INVTYPE_TABARD || proto->InventoryType == INVTYPE_BAG)
        return false;
    return HasScalableStats(proto) && item->CanHoldEnhance();
}

uint8 TransferGroup(uint32 inventoryType)
{
    switch (inventoryType)
    {
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:
            return INVTYPE_CHEST;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
        case INVTYPE_WEAPONOFFHAND:
            return INVTYPE_WEAPON;
        case INVTYPE_RANGED:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_THROWN:
            return INVTYPE_RANGED;
        default:
            return uint8(inventoryType);
    }
}

uint32 GemCount(Player const* player, GemTier tier)
{
    return player->GetItemCount(Cfg().Gem[tier]);
}

std::string GemSummary(Player const* player)
{
    return Acore::StringFormat("持有：低阶 {}，中阶 {}，高阶 {}", GemCount(player, TIER_LOW),
        GemCount(player, TIER_MID), GemCount(player, TIER_HIGH));
}

void Say(Player* player, std::string const& text)
{
    ChatHandler(player->GetSession()).SendSysMessage(text);
}

void AddOption(Player* player, std::string const& text, uint32 sender, uint32 action,
    uint32 icon = GOSSIP_ICON_CHAT)
{
    if (player->PlayerTalkClass->GetGossipMenu().GetMenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;
    AddGossipItemFor(player, icon, text, sender, action);
}

bool IsGem(uint32 entry)
{
    for (uint32 gem : Cfg().Gem)
        if (gem && gem == entry)
            return true;
    return false;
}

// A right-clicked gem stack can be used up by the menu itself. Item gossip needs a live item,
// so move the menu to any gem the player still has.
ObjectGuid LiveSource(Player* player, ObjectGuid const& source)
{
    if (!source.IsItem() || player->GetItemByGuid(source))
        return source;

    for (uint32 gem : Cfg().Gem)
    {
        if (!gem)
            continue;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (item->GetEntry() == gem)
                    return item->GetGUID();
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = player->GetBagByPos(bagSlot))
                for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                    if (Item* item = bag->GetItemByPos(uint8(slot)))
                        if (item->GetEntry() == gem)
                            return item->GetGUID();
    }
    return ObjectGuid::Empty;
}

bool Send(Player* player, ObjectGuid const& source)
{
    ObjectGuid live = LiveSource(player, source);
    if (live.IsEmpty())
    {
        CloseGossipMenuFor(player);
        return false;
    }
    SendGossipMenuFor(player, ENHANCE_TEXT, live);
    return true;
}

void ShowMain(Player* player, ObjectGuid const& source)
{
    AddOption(player, GemSummary(player), SENDER_MAIN, ACT_REFRESH);
    AddOption(player, "强化身上的装备", SENDER_MAIN, ACT_LIST, GOSSIP_ICON_BATTLE);
    AddOption(player, "强化石兑换", SENDER_MAIN, ACT_EXCHANGE, GOSSIP_ICON_MONEY_BAG);
    AddOption(player, "强化转移", SENDER_MAIN, ACT_TRANSFER, GOSSIP_ICON_INTERACT_1);
    AddOption(player, "全服强化榜", SENDER_MAIN, ACT_TOP, GOSSIP_ICON_TABARD);
    Send(player, source);
}

void ShowList(Player* player, ObjectGuid const& source)
{
    bool any = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!CanEnhance(item))
            continue;

        uint32 next = item->GetEnhanceLevel() + 1;
        AddOption(player, Acore::StringFormat("[{}] {} → +{}（{:.0f}%）", SlotName(slot), ItemLabel(player, item), next,
            ChanceFor(next)), SENDER_LIST, ACT_SLOT_BASE + slot, GOSSIP_ICON_BATTLE);
        any = true;
    }
    if (!any)
        AddOption(player, "身上没有可以强化的装备。", SENDER_LIST, ACT_REFRESH);
    AddOption(player, "返回", SENDER_LIST, ACT_BACK);
    Send(player, source);
}

void ShowItem(Player* player, ObjectGuid const& source, uint8 slot)
{
    uint32 sender = SENDER_ITEM_BASE + slot;
    Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!CanEnhance(item))
    {
        AddOption(player, "这个部位没有可以强化的装备。", sender, ACT_REFRESH);
        AddOption(player, "返回", sender, ACT_BACK);
        Send(player, source);
        return;
    }

    uint32 current = item->GetEnhanceLevel();
    uint32 next = current + 1;
    Cost cost = CostFor(next);
    uint32 afterFail = LevelAfterFail(current);

    AddOption(player, Acore::StringFormat("[{}] {}", SlotName(slot), ItemLabel(player, item)), sender, ACT_REFRESH);
    AddOption(player, Acore::StringFormat("当前属性加成 +{}%，+{} 时为 +{}%", Item::GetEnhanceBonusPct(current), next,
        Item::GetEnhanceBonusPct(next)), sender, ACT_REFRESH);
    AddOption(player, Acore::StringFormat("成功率 {:.0f}%，消耗 {} 颗{}", ChanceFor(next), cost.Count, TierName[cost.Tier]),
        sender, ACT_REFRESH);
    if (afterFail == current)
        AddOption(player, "失败：等级不变", sender, ACT_REFRESH);
    else
        AddOption(player, Acore::StringFormat("失败：降为 +{}", afterFail), sender, ACT_REFRESH);
    AddOption(player, GemSummary(player), sender, ACT_REFRESH);
    AddOption(player, Acore::StringFormat("强化到 +{}", next), sender, ACT_ATTEMPT, GOSSIP_ICON_BATTLE);
    AddOption(player, "返回", sender, ACT_BACK);
    Send(player, source);
}

void Attempt(Player* player, uint8 slot)
{
    Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!CanEnhance(item))
    {
        Say(player, "这个部位没有可以强化的装备。");
        return;
    }
    if (player->IsInCombat())
    {
        Say(player, "战斗中不能强化。");
        return;
    }

    uint32 current = item->GetEnhanceLevel();
    uint32 next = current + 1;
    Cost cost = CostFor(next);
    if (GemCount(player, cost.Tier) < cost.Count)
    {
        Say(player, Acore::StringFormat("需要 {} 颗{}。", cost.Count, TierName[cost.Tier]));
        return;
    }

    player->DestroyItemCount(Cfg().Gem[cost.Tier], cost.Count, true);

    if (roll_chance_f(ChanceFor(next)))
    {
        player->SetItemEnhanceLevel(item, next);
        ChatHandler(player->GetSession()).SendNotification(Acore::StringFormat("强化成功！+{}", next));
        Say(player, Acore::StringFormat("|cff00ff00强化成功！|r{} 强化到 +{}。", ItemLink(player, item), next));
        if (Cfg().AnnounceLevel && next >= Cfg().AnnounceLevel)
            ChatHandler(player->GetSession()).SendWorldText("|cffffcc00[强化]|r 恭喜 {} 将 {} 强化到 |cffff8000+{}|r！",
                player->GetName(), ItemLink(player, item), next);
        return;
    }

    uint32 afterFail = LevelAfterFail(current);
    player->SetItemEnhanceLevel(item, afterFail);
    ChatHandler(player->GetSession()).SendNotification(std::string_view("强化失败"));
    if (afterFail == current)
        Say(player, Acore::StringFormat("|cffff2020强化失败。|r{} 保持 +{}。", ItemLink(player, item), current));
    else
        Say(player, Acore::StringFormat("|cffff2020强化失败。|r{} 降为 +{}。", ItemLink(player, item), afterFail));
}

struct Exchange
{
    GemTier From;
    uint32 FromCount;
    GemTier To;
    uint32 ToCount;
};

Exchange ExchangeOf(uint32 kind)
{
    uint32 rate = Cfg().ExchangeRate;
    switch (kind)
    {
        case 0: return { TIER_LOW, rate, TIER_MID, 1 };
        case 1: return { TIER_MID, rate, TIER_HIGH, 1 };
        case 2: return { TIER_MID, 1, TIER_LOW, rate };
        default: return { TIER_HIGH, 1, TIER_MID, rate };
    }
}

void ShowExchange(Player* player, ObjectGuid const& source)
{
    AddOption(player, GemSummary(player), SENDER_EXCHANGE, ACT_REFRESH);
    for (uint32 kind = 0; kind < 4; ++kind)
    {
        Exchange ex = ExchangeOf(kind);
        AddOption(player, Acore::StringFormat("{} 颗{} → {} 颗{}", ex.FromCount, TierName[ex.From], ex.ToCount,
            TierName[ex.To]), SENDER_EXCHANGE, ACT_EXCHANGE_BASE + kind, GOSSIP_ICON_MONEY_BAG);
    }
    AddOption(player, "返回", SENDER_EXCHANGE, ACT_BACK);
    Send(player, source);
}

void DoExchange(Player* player, uint32 kind)
{
    Exchange ex = ExchangeOf(kind);
    EnhanceConfig const& cfg = Cfg();
    if (GemCount(player, ex.From) < ex.FromCount)
    {
        Say(player, Acore::StringFormat("需要 {} 颗{}。", ex.FromCount, TierName[ex.From]));
        return;
    }

    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, cfg.Gem[ex.To], ex.ToCount) != EQUIP_ERR_OK)
    {
        Say(player, "背包空间不足。");
        return;
    }

    player->DestroyItemCount(cfg.Gem[ex.From], ex.FromCount, true);
    if (Item* gem = player->StoreNewItem(dest, cfg.Gem[ex.To], true))
        player->SendNewItem(gem, ex.ToCount, true, false);
}

uint32 TransferCost(uint32 level)
{
    return level * Cfg().TransferMidPerLevel;
}

void ShowTransfer(Player* player, ObjectGuid const& source)
{
    bool any = false;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item || !item->GetEnhanceLevel())
            continue;
        AddOption(player, Acore::StringFormat("从 [{}] {} 转出", SlotName(slot), ItemLabel(player, item)),
            SENDER_TRANSFER, ACT_SLOT_BASE + slot, GOSSIP_ICON_INTERACT_1);
        any = true;
    }
    if (!any)
        AddOption(player, "身上没有强化过的装备。", SENDER_TRANSFER, ACT_REFRESH);
    AddOption(player, Acore::StringFormat("转移把等级搬到背包里同类装备上，原装备归零，每级消耗 {} 颗中阶强化石。",
        Cfg().TransferMidPerLevel), SENDER_TRANSFER, ACT_REFRESH);
    AddOption(player, "返回", SENDER_TRANSFER, ACT_BACK);
    Send(player, source);
}

bool IsTransferTarget(Item const* from, Item const* to)
{
    if (!from || !to || from == to || !CanEnhance(to) || to->GetEnhanceLevel() >= from->GetEnhanceLevel())
        return false;
    return TransferGroup(from->GetTemplate()->InventoryType) == TransferGroup(to->GetTemplate()->InventoryType);
}

template <typename Fn>
void ForEachBagItem(Player* player, Fn&& fn)
{
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            fn(item);
    for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        if (Bag* bag = player->GetBagByPos(bagSlot))
            for (uint32 slot = 0; slot < bag->GetBagSize(); ++slot)
                if (Item* item = bag->GetItemByPos(uint8(slot)))
                    fn(item);
}

void ShowTransferTargets(Player* player, ObjectGuid const& source, uint8 slot)
{
    uint32 sender = SENDER_TRANSFER_BASE + slot;
    Item* from = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!from || !from->GetEnhanceLevel())
    {
        ShowTransfer(player, source);
        return;
    }

    uint32 level = from->GetEnhanceLevel();
    uint32 cost = TransferCost(level);
    AddOption(player, Acore::StringFormat("转出：{}，消耗 {} 颗中阶强化石", ItemLabel(player, from), cost),
        sender, ACT_REFRESH);

    uint32 shown = 0;
    ForEachBagItem(player, [&](Item* item)
    {
        if (shown >= TARGET_LIMIT || !IsTransferTarget(from, item))
            return;
        std::string popup = Acore::StringFormat("把 +{} 转到 {}？{} 将归零。", level, ItemName(player, item->GetTemplate()),
            ItemName(player, from->GetTemplate()));
        if (player->PlayerTalkClass->GetGossipMenu().GetMenuItemCount() < GOSSIP_MAX_MENU_ITEMS)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, Acore::StringFormat("转到 {}", ItemLabel(player, item)),
                sender, ACT_POS_BASE + item->GetPos(), popup, 0, false);
        ++shown;
    });
    if (!shown)
        AddOption(player, "背包里没有同类、等级更低的装备。", sender, ACT_REFRESH);
    AddOption(player, "返回", sender, ACT_BACK);
    Send(player, source);
}

void DoTransfer(Player* player, uint8 slot, uint16 pos)
{
    Item* from = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    Item* to = player->GetItemByPos(pos);
    if (!IsTransferTarget(from, to) || to->IsEquipped())
    {
        Say(player, "转移目标已经变化，请重新选择。");
        return;
    }
    if (player->IsInCombat())
    {
        Say(player, "战斗中不能转移。");
        return;
    }

    uint32 level = from->GetEnhanceLevel();
    uint32 cost = TransferCost(level);
    if (GemCount(player, TIER_MID) < cost)
    {
        Say(player, Acore::StringFormat("需要 {} 颗中阶强化石。", cost));
        return;
    }

    if (cost)
        player->DestroyItemCount(Cfg().Gem[TIER_MID], cost, true);
    player->SetItemEnhanceLevel(to, level);
    player->SetItemEnhanceLevel(from, 0);
    Say(player, Acore::StringFormat("已把 +{} 转到 {}。", level, ItemLink(player, to)));
}

void ShowTop(Player* player, ObjectGuid const& source)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_ITEM_ENHANCE_TOP);
    stmt->SetData(0, ITEM_ENHANCE_ENCHANT_BASE);
    stmt->SetData(1, ITEM_ENHANCE_ENCHANT_BASE + ITEM_ENHANCE_MAX_LEVEL);
    stmt->SetData(2, TOP_LIMIT);

    uint32 rank = 0;
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            Field* fields = result->Fetch();
            std::string owner = fields[0].Get<std::string>();
            uint32 entry = fields[1].Get<uint32>();
            uint64 level = fields[2].Get<uint64>() - ITEM_ENHANCE_ENCHANT_BASE;
            std::string name = Acore::StringFormat("{}", entry);
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry))
                name = ItemName(player, proto);
            AddOption(player, Acore::StringFormat("{}. {}：{} +{}", ++rank, owner, name, level),
                SENDER_TOP, ACT_REFRESH);
        } while (result->NextRow());
    }
    if (!rank)
        AddOption(player, "还没有人强化过装备。", SENDER_TOP, ACT_REFRESH);
    AddOption(player, "返回", SENDER_TOP, ACT_BACK);
    Send(player, source);
}

void ShowForSender(Player* player, ObjectGuid const& source, uint32 sender)
{
    if (sender >= SENDER_TRANSFER_BASE && sender < SENDER_TRANSFER_BASE + EQUIPMENT_SLOT_END)
        ShowTransferTargets(player, source, uint8(sender - SENDER_TRANSFER_BASE));
    else if (sender >= SENDER_ITEM_BASE && sender < SENDER_ITEM_BASE + EQUIPMENT_SLOT_END)
        ShowItem(player, source, uint8(sender - SENDER_ITEM_BASE));
    else if (sender == SENDER_LIST)
        ShowList(player, source);
    else if (sender == SENDER_EXCHANGE)
        ShowExchange(player, source);
    else if (sender == SENDER_TRANSFER)
        ShowTransfer(player, source);
    else if (sender == SENDER_TOP)
        ShowTop(player, source);
    else
        ShowMain(player, source);
}

uint32 ParentOf(uint32 sender)
{
    if (sender >= SENDER_TRANSFER_BASE && sender < SENDER_TRANSFER_BASE + EQUIPMENT_SLOT_END)
        return SENDER_TRANSFER;
    if (sender >= SENDER_ITEM_BASE && sender < SENDER_ITEM_BASE + EQUIPMENT_SLOT_END)
        return SENDER_LIST;
    return SENDER_MAIN;
}

void HandleAction(Player* player, ObjectGuid const& source, uint32 sender, uint32 action)
{
    if (action == ACT_BACK)
    {
        ShowForSender(player, source, ParentOf(sender));
        return;
    }

    if (sender == SENDER_MAIN)
    {
        switch (action)
        {
            case ACT_LIST: ShowList(player, source); return;
            case ACT_EXCHANGE: ShowExchange(player, source); return;
            case ACT_TRANSFER: ShowTransfer(player, source); return;
            case ACT_TOP: ShowTop(player, source); return;
            default: ShowMain(player, source); return;
        }
    }

    if ((sender == SENDER_LIST || sender == SENDER_TRANSFER)
        && action >= ACT_SLOT_BASE && action < ACT_SLOT_BASE + EQUIPMENT_SLOT_END)
    {
        uint8 slot = uint8(action - ACT_SLOT_BASE);
        if (sender == SENDER_LIST)
            ShowItem(player, source, slot);
        else
            ShowTransferTargets(player, source, slot);
        return;
    }

    if (sender >= SENDER_ITEM_BASE && sender < SENDER_ITEM_BASE + EQUIPMENT_SLOT_END && action == ACT_ATTEMPT)
        Attempt(player, uint8(sender - SENDER_ITEM_BASE));
    else if (sender == SENDER_EXCHANGE && action >= ACT_EXCHANGE_BASE && action < ACT_EXCHANGE_BASE + 4)
        DoExchange(player, action - ACT_EXCHANGE_BASE);
    else if (sender >= SENDER_TRANSFER_BASE && sender < SENDER_TRANSFER_BASE + EQUIPMENT_SLOT_END
        && action >= ACT_POS_BASE && action <= ACT_POS_BASE + 0xFFFF)
    {
        DoTransfer(player, uint8(sender - SENDER_TRANSFER_BASE), uint16(action - ACT_POS_BASE));
        ShowTransfer(player, source);
        return;
    }

    ShowForSender(player, source, sender);
}

bool Refuse(Player* player)
{
    if (!player || !player->GetSession())
        return true;
    if (!Cfg().Enable)
    {
        Say(player, "装备强化未启用。");
        return true;
    }
    if (player->GetSession()->IsBot())
    {
        Say(player, "机器人不能使用装备强化。");
        return true;
    }
    return false;
}

struct Drop
{
    GemTier Tier;
    uint32 Count;
    float Chance;
};

std::vector<Drop> DropsFor(Creature const* killed)
{
    EnhanceConfig const& cfg = Cfg();
    Map const* map = killed->GetMap();
    bool boss = killed->IsDungeonBoss() || killed->isWorldBoss();
    if (boss && map->IsRaid())
        return { { TIER_HIGH, map->Is25ManRaid() ? cfg.Raid25High : cfg.Raid10High, 100.0f } };
    if (boss && map->IsDungeon())
    {
        if (map->IsHeroic())
            return { { TIER_MID, cfg.HeroicMid, 100.0f }, { TIER_HIGH, 1, cfg.HeroicHigh } };
        return { { TIER_MID, cfg.DungeonMid, 100.0f }, { TIER_HIGH, 1, cfg.DungeonHigh } };
    }
    if (killed->isWorldBoss())
        return { { TIER_HIGH, cfg.WorldBossHigh, 100.0f } };

    uint32 rank = killed->GetCreatureTemplate()->rank;
    if (rank == CREATURE_ELITE_RARE || rank == CREATURE_ELITE_RAREELITE)
        return { { TIER_LOW, cfg.RareLow, 100.0f }, { TIER_MID, 1, cfg.RareMid } };
    if (killed->isElite())
        return { { TIER_LOW, 1, cfg.EliteLow }, { TIER_MID, 1, cfg.EliteMid } };
    return { { TIER_LOW, 1, cfg.NormalLow } };
}

void GiveGems(Player* player, uint32 entry, uint32 count)
{
    ItemPosCountVec dest;
    if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, entry, count) == EQUIP_ERR_OK)
    {
        if (Item* item = player->StoreNewItem(dest, entry, true))
            player->SendNewItem(item, count, true, false);
        return;
    }

    Item* item = Item::CreateItem(entry, count, player);
    if (!item)
        return;
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    item->SaveToDB(trans);
    MailDraft("强化石", "背包已满，掉落的强化石通过邮件送达。")
        .AddItem(item)
        .SendMailTo(trans, MailReceiver(player), MailSender(MAIL_NORMAL, player->GetGUID().GetCounter(),
            MAIL_STATIONERY_GM));
    CharacterDatabase.CommitTransaction(trans);
    Say(player, "背包已满，强化石已通过邮件送达。");
}

void RewardMember(Player* member, Creature* killed, std::vector<Drop> const& drops)
{
    if (!member || !member->GetSession() || member->GetSession()->IsBot())
        return;
    if (!member->IsAtGroupRewardDistance(killed))
        return;
    if (killed->GetLevel() <= Acore::XP::GetGrayLevel(member->GetLevel()))
        return;

    for (Drop const& drop : drops)
    {
        uint32 entry = Cfg().Gem[drop.Tier];
        if (!entry || !drop.Count || !roll_chance_f(drop.Chance))
            continue;
        GiveGems(member, entry, drop.Count);
    }
}

void RewardKill(Creature* killed)
{
    EnhanceConfig const& cfg = Cfg();
    if (!cfg.Enable || !cfg.DropEnable || !killed)
        return;
    if (killed->IsPet() || killed->IsTotem() || killed->IsCritter() || killed->GetCharmerOrOwnerGUID().IsPlayer())
        return;
    if (killed->GetMap()->IsBattlegroundOrArena() || !killed->IsDamageEnoughForLootingAndReward())
        return;
    bool boss = killed->IsDungeonBoss() || killed->isWorldBoss();
    if (!boss && killed->HasFlagsExtra(CREATURE_FLAG_EXTRA_NO_XP))
        return;

    std::vector<Drop> drops = DropsFor(killed);
    if (Group* group = killed->GetLootRecipientGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            RewardMember(ref->GetSource(), killed, drops);
    }
    else
        RewardMember(killed->GetLootRecipient(), killed, drops);
}
}

class item_enhance_gem : public ItemScript
{
public:
    item_enhance_gem() : ItemScript("item_enhance_gem") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (Refuse(player) || !IsGem(item->GetEntry()))
        {
            player->SendEquipError(EQUIP_ERR_CANT_DO_RIGHT_NOW, item, nullptr);
            return true;
        }
        if (player->IsInCombat())
        {
            player->SendEquipError(EQUIP_ERR_NOT_IN_COMBAT, item, nullptr);
            return true;
        }

        // The item spell only makes the gem usable. It is not cast.
        player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);
        ClearGossipMenuFor(player);
        ShowMain(player, item->GetGUID());
        return true;
    }

    void OnGossipSelect(Player* player, Item* item, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (Refuse(player))
        {
            CloseGossipMenuFor(player);
            return;
        }
        HandleAction(player, item->GetGUID(), sender, action);
    }
};

class npc_enhance_master : public CreatureScript
{
public:
    npc_enhance_master() : CreatureScript("npc_enhance_master") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        ClearGossipMenuFor(player);
        if (Refuse(player) || creature->GetEntry() != Cfg().NpcEntry)
        {
            CloseGossipMenuFor(player);
            return true;
        }
        ShowMain(player, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (Refuse(player) || creature->GetEntry() != Cfg().NpcEntry)
        {
            CloseGossipMenuFor(player);
            return true;
        }
        HandleAction(player, creature->GetGUID(), sender, action);
        return true;
    }
};

class player_enhance_drops : public PlayerScript
{
public:
    player_enhance_drops() : PlayerScript("player_enhance_drops",
        { PLAYERHOOK_ON_CREATURE_KILL, PLAYERHOOK_ON_CREATURE_KILLED_BY_PET }) { }

    void OnPlayerCreatureKill(Player* /*killer*/, Creature* killed) override
    {
        RewardKill(killed);
    }

    void OnPlayerCreatureKilledByPet(Player* /*petOwner*/, Creature* killed) override
    {
        RewardKill(killed);
    }
};

class world_enhance_config : public WorldScript
{
public:
    world_enhance_config() : WorldScript("world_enhance_config", { WORLDHOOK_ON_AFTER_CONFIG_LOAD }) { }

    void OnAfterConfigLoad(bool reload) override
    {
        if (reload)
            Cfg() = LoadConfig();
    }
};

void AddSC_item_enhance()
{
    new item_enhance_gem();
    new npc_enhance_master();
    new player_enhance_drops();
    new world_enhance_config();
}
