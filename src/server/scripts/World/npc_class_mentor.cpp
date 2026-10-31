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

#include "Chat.h"
#include "Config.h"
#include "CreatureScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemScript.h"
#include "Language.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "SpellMgr.h"
#include "StringFormat.h"
#include "WorldSession.h"
#ifdef MOD_PLAYERBOTS
#include "PlayerbotsDatabase.h"
#endif

#include <algorithm>
#include <array>
#include <unordered_set>
#include <vector>

// The mastery list is skipped. feat/multiclass-mastery-and-weapon-floor is not merged,
// so this NPC does not offer a third mastery menu.

namespace
{
constexpr uint32 MENTOR_TEXT = 800000;
constexpr uint32 PAGE_SIZE = 8;
constexpr uint32 PAGE_STRIDE = 1000;

constexpr uint32 ACT_BACK = 0xF0000001;
constexpr uint32 ACT_NEXT = 0xF0000003;
constexpr uint32 ACT_PREV = 0xF0000004;
constexpr uint32 ACT_SPELLS = 0xF0000005;
constexpr uint32 ACT_TALENTS = 0xF0000006;
constexpr uint32 ACT_SYNERGY = 0xF0000007;
constexpr uint32 ACT_CONFIRM = 0xF0000008;
constexpr uint32 ACT_REFRESH = 0xF0000009;

constexpr uint32 SENDER_MAIN = 1;
constexpr uint32 SENDER_CONFIRM_BASE = 1000;
constexpr uint32 SENDER_RETAIN = 2000;
constexpr uint32 SENDER_CLASS_BASE = 3000;
constexpr uint32 SENDER_SPELL_BASE = 100000;
constexpr uint32 SENDER_TALENT_BASE = 200000;
constexpr uint32 SENDER_SYNERGY_BASE = 300000;

constexpr uint8 RETAIN_KIND_SPELL = 0;
constexpr uint8 RETAIN_KIND_TALENT = 1;
constexpr uint8 RETAIN_KIND_SYNERGY = 2;

struct ClassRow
{
    uint8 Level = 0;
    uint32 Xp = 0;
    bool Present = false;
};

struct RetainPick
{
    uint8 SourceClass = 0;
    uint32 SpellId = 0;
    uint8 Kind = 0;
};

struct SynergyChoice
{
    uint32 SpellId = 0;
    bool Remove = false;
};

struct MentorView
{
    std::array<ClassRow, MAX_CLASSES> Classes;
    std::array<std::unordered_set<uint32>, MAX_CLASSES> Spells;
    std::array<std::unordered_set<uint32>, MAX_CLASSES> Talents;
    std::vector<RetainPick> Retained;
    uint8 SynergyClasses = 0;
};

bool IsRandomBotGuid(ObjectGuid::LowType guid)
{
#ifdef MOD_PLAYERBOTS
    PlayerbotsDatabasePreparedStatement* stmt =
        PlayerbotsDatabase.GetPreparedStatement(PLAYERBOTS_SEL_RANDOM_BOT_BY_BOT);
    stmt->SetData(0, guid);
    return bool(PlayerbotsDatabase.Query(stmt));
#else
    (void)guid;
    return false;
#endif
}

std::string T(Player const* player, uint32 entry)
{
    return sObjectMgr->GetAcoreString(entry, player->GetSession()->GetSessionDbcLocale());
}

template <typename... Args>
std::string Tf(Player const* player, uint32 entry, Args&&... args)
{
    return Acore::StringFormat(T(player, entry), std::forward<Args>(args)...);
}

std::string Short(std::string text)
{
    constexpr size_t MaxBytes = 90;
    if (text.size() <= MaxBytes)
        return text;

    text.resize(MaxBytes);
    while (!text.empty() && (uint8(text.back()) & 0xC0) == 0x80)
        text.pop_back();
    return text;
}

void AddOption(Player* player, std::string text, uint32 sender, uint32 action)
{
    if (!player->PlayerTalkClass)
        return;
    if (player->PlayerTalkClass->GetGossipMenu().GetMenuItemCount() >= GOSSIP_MAX_MENU_ITEMS)
        return;

    AddGossipItemFor(player, GOSSIP_ICON_CHAT, Short(std::move(text)), sender, action);
}

std::string ClassLabel(Player const* player, uint8 classId)
{
    LocaleConstant locale = player->GetSession()->GetSessionDbcLocale();
    if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId))
    {
        if (locale < TOTAL_LOCALES && entry->name[locale] && entry->name[locale][0])
            return entry->name[locale];
        if (entry->name[LOCALE_enUS] && entry->name[LOCALE_enUS][0])
            return entry->name[LOCALE_enUS];
    }
    return Acore::StringFormat("{}", uint32(classId));
}

char const* LocaleField(std::array<char const*, 16> const& field, LocaleConstant locale)
{
    if (locale < TOTAL_LOCALES && field[locale] && field[locale][0])
        return field[locale];
    return nullptr;
}

// zhCN comes from Spell.dbc / spell_dbc. An empty slot falls back to enUS.
std::string SpellNameText(SpellInfo const* info)
{
    if (!info)
        return {};

    if (char const* name = LocaleField(info->SpellName, LOCALE_zhCN))
        return name;
    if (char const* name = LocaleField(info->SpellName, LOCALE_enUS))
        return name;
    return {};
}

struct SpellOrder
{
    std::string Name;
    uint8 Rank = 1;
    uint32 SpellId = 0;
};

SpellOrder OrderOf(uint32 spellId)
{
    SpellOrder order;
    order.SpellId = spellId;
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return order;

    order.Name = SpellNameText(info);
    order.Rank = info->GetRank();
    if (order.Name.empty())
        order.Name = Acore::StringFormat("{}", spellId);
    return order;
}

bool SpellOrderLess(SpellOrder const& left, SpellOrder const& right)
{
    if (left.Name != right.Name)
        return left.Name < right.Name;
    if (left.Rank != right.Rank)
        return left.Rank < right.Rank;
    return left.SpellId < right.SpellId;
}

void SortSpellIds(std::vector<uint32>& spellIds)
{
    std::vector<SpellOrder> orders;
    orders.reserve(spellIds.size());
    for (uint32 spellId : spellIds)
        orders.push_back(OrderOf(spellId));

    std::sort(orders.begin(), orders.end(), SpellOrderLess);
    for (size_t index = 0; index < spellIds.size(); ++index)
        spellIds[index] = orders[index].SpellId;
}

void FitUtf8(std::string& text, size_t maxBytes)
{
    if (text.size() <= maxBytes)
        return;

    size_t end = 0;
    while (end < text.size())
    {
        uint8 lead = uint8(text[end]);
        size_t width = 1;
        if (lead >= 0x80)
        {
            if ((lead & 0xE0) == 0xC0)
                width = 2;
            else if ((lead & 0xF0) == 0xE0)
                width = 3;
            else if ((lead & 0xF8) == 0xF0)
                width = 4;
            else
                break;
        }

        if (end + width > maxBytes || end + width > text.size())
            break;
        end += width;
    }
    text.resize(end);
}

// Session locale is ignored. The retain list is always zhCN, with enUS as fallback.
std::string SpellLabel(Player const* /*player*/, uint32 spellId)
{
    if (!sSpellMgr->GetSpellInfo(spellId))
        return Acore::StringFormat("{}", spellId);

    SpellOrder order = OrderOf(spellId);
    // Rank is SpellInfo::GetRank() (spell_ranks, or the talent chain). Unranked spells are 1.
    std::string suffix = Acore::StringFormat("（等级 {}）", uint32(order.Rank));
    // Leave room for the "保留：" / "Remove: " prefix inside Short()'s 90-byte cap.
    constexpr size_t MaxLabelBytes = 78;
    if (suffix.size() < MaxLabelBytes && order.Name.size() + suffix.size() > MaxLabelBytes)
        FitUtf8(order.Name, MaxLabelBytes - suffix.size());
    return order.Name + suffix;
}

uint8 SlotsForLevel(uint8 level)
{
    uint8 count = 0;
    if (level >= sConfigMgr->GetOption<uint32>("Multiclass.Retain.Level1", 20, false))
        ++count;
    if (level >= sConfigMgr->GetOption<uint32>("Multiclass.Retain.Level2", 40, false))
        ++count;
    if (level >= sConfigMgr->GetOption<uint32>("Multiclass.Retain.Level3", 60, false))
        ++count;
    return count;
}

uint32 ErrorEntry(ClassChangeRequestResult result)
{
    switch (result)
    {
        case CLASS_CHANGE_DISABLED:
            return LANG_MULTICLASS_ERR_DISABLED;
        case CLASS_CHANGE_SAME:
            return LANG_MULTICLASS_ERR_SAME;
        case CLASS_CHANGE_RACE:
            return LANG_MULTICLASS_ERR_RACE;
        case CLASS_CHANGE_DEATH_KNIGHT:
            return LANG_MULTICLASS_ERR_DK;
        case CLASS_CHANGE_BOT:
            return LANG_MULTICLASS_ERR_BOT;
        case CLASS_CHANGE_PENDING:
            return LANG_MULTICLASS_ERR_PENDING;
        case CLASS_CHANGE_COOLDOWN:
            return LANG_MULTICLASS_ERR_COOLDOWN;
        case CLASS_CHANGE_COMBAT:
            return LANG_MULTICLASS_ERR_COMBAT;
        case CLASS_CHANGE_BATTLEGROUND:
            return LANG_MULTICLASS_ERR_BG;
        case CLASS_CHANGE_CITY:
            return LANG_MULTICLASS_ERR_CITY;
        case CLASS_CHANGE_MONEY:
            return LANG_MULTICLASS_ERR_MONEY;
        default:
            return LANG_MULTICLASS_ERR_BAD;
    }
}

void LoadProgress(Player const* player, std::array<ClassRow, MAX_CLASSES>& rows)
{
    rows = {};
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_PROGRESS_MENU);
    stmt->SetData(0, player->GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId >= MAX_CLASSES)
                continue;
            rows[classId].Level = fields[1].Get<uint8>();
            rows[classId].Xp = fields[2].Get<uint32>();
            rows[classId].Present = true;
        } while (result->NextRow());
    }

    uint8 current = player->getClass();
    if (current < MAX_CLASSES)
    {
        rows[current].Level = player->GetLevel();
        rows[current].Xp = player->GetUInt32Value(PLAYER_XP);
        rows[current].Present = true;
    }
}

void LoadSnapshots(Player const* player, MentorView& view)
{
    LoadProgress(player, view.Classes);

    CharacterDatabasePreparedStatement* spells = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_SPELL_BY_GUID);
    spells->SetData(0, player->GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(spells))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId < MAX_CLASSES)
                view.Spells[classId].insert(fields[1].Get<uint32>());
        } while (result->NextRow());
    }

    CharacterDatabasePreparedStatement* talents = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_TALENT_BY_GUID);
    talents->SetData(0, player->GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(talents))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId < MAX_CLASSES)
                view.Talents[classId].insert(fields[1].Get<uint32>());
        } while (result->NextRow());
    }

    uint8 current = player->getClass();
    if (current < MAX_CLASSES)
    {
        view.Spells[current].clear();
        view.Talents[current].clear();
        for (auto const& itr : player->GetSpellMap())
        {
            if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED || itr.second->State == PLAYERSPELL_TEMPORARY)
                continue;
            view.Spells[current].insert(itr.first);
        }
        for (auto const& itr : player->GetTalentMap())
        {
            if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED)
                continue;
            view.Talents[current].insert(itr.first);
        }
    }

    CharacterDatabasePreparedStatement* rows = CharacterDatabase.GetPreparedStatement(CHAR_SEL_RETAINED_SPELL);
    rows->SetData(0, player->GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(rows))
    {
        do
        {
            Field* fields = result->Fetch();
            RetainPick row;
            row.SourceClass = fields[0].Get<uint8>();
            row.SpellId = fields[1].Get<uint32>();
            row.Kind = fields[2].Get<uint8>();
            view.Retained.push_back(row);
        } while (result->NextRow());
    }

    uint32 synergyLevel = sConfigMgr->GetOption<uint32>("Multiclass.SynergyLevel", 60, false);
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
        if (view.Classes[classId].Present && view.Classes[classId].Level >= synergyLevel)
            ++view.SynergyClasses;
}

uint32 CountKind(MentorView const& view, uint8 classId, uint8 kind)
{
    uint32 count = 0;
    for (RetainPick const& row : view.Retained)
    {
        if (row.Kind != kind)
            continue;
        if (kind == RETAIN_KIND_SYNERGY || row.SourceClass == classId)
            ++count;
    }
    return count;
}

void AppendSwitchStatus(Player* player, uint32 sender)
{
    uint32 now = uint32(GameTime::GetGameTime().count());
    uint32 cooldown = sConfigMgr->GetOption<uint32>("Multiclass.SwitchCooldown", 0, false);
    uint32 cost = sConfigMgr->GetOption<uint32>("Multiclass.SwitchCost", 0, false);
    uint8 pending = 0;
    uint32 lastSwitch = 0;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_SWITCH);
    stmt->SetData(0, player->GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        Field* fields = result->Fetch();
        pending = fields[0].Get<uint8>();
        lastSwitch = fields[2].Get<uint32>();
    }

    if (cost)
        AddOption(player, Tf(player, LANG_MULTICLASS_COST, cost), sender, ACT_REFRESH);
    else
        AddOption(player, T(player, LANG_MULTICLASS_FREE), sender, ACT_REFRESH);

    if (cooldown && lastSwitch && uint64(lastSwitch) + cooldown > now)
    {
        uint32 remain = uint32(uint64(lastSwitch) + cooldown - now);
        AddOption(player, Tf(player, LANG_MULTICLASS_WAIT, remain), sender, ACT_REFRESH);
    }
    else
        AddOption(player, T(player, LANG_MULTICLASS_READY), sender, ACT_REFRESH);

    if (pending)
        AddOption(player, T(player, LANG_MULTICLASS_PENDING), sender, ACT_REFRESH);
}

// The menu is the same for the mentor and the bag item. source is that creature or item.
// An item source skips the capital-city check when the switch is confirmed.
void ShowMain(Player* player, ObjectGuid const& source);
void ShowConfirm(Player* player, ObjectGuid const& source, uint8 classId);
void ShowRetain(Player* player, ObjectGuid const& source);
void ShowClassRetain(Player* player, ObjectGuid const& source, uint8 classId);
void ShowSpellPage(Player* player, ObjectGuid const& source, uint32 sender, bool talents);
void ShowSynergy(Player* player, ObjectGuid const& source, uint32 page);

void ShowForSender(Player* player, ObjectGuid const& source, uint32 sender)
{
    if (sender >= SENDER_SYNERGY_BASE && sender < SENDER_SYNERGY_BASE + PAGE_STRIDE)
        ShowSynergy(player, source, sender - SENDER_SYNERGY_BASE);
    else if (sender >= SENDER_TALENT_BASE && sender < SENDER_SYNERGY_BASE)
        ShowSpellPage(player, source, sender, true);
    else if (sender >= SENDER_SPELL_BASE && sender < SENDER_TALENT_BASE)
        ShowSpellPage(player, source, sender, false);
    else if (sender >= SENDER_CLASS_BASE && sender < SENDER_CLASS_BASE + MAX_CLASSES)
        ShowClassRetain(player, source, uint8(sender - SENDER_CLASS_BASE));
    else if (sender == SENDER_RETAIN)
        ShowRetain(player, source);
    else if (sender >= SENDER_CONFIRM_BASE && sender < SENDER_CONFIRM_BASE + MAX_CLASSES)
        ShowConfirm(player, source, uint8(sender - SENDER_CONFIRM_BASE));
    else
        ShowMain(player, source);
}

uint32 ParentSender(uint32 sender)
{
    if (sender >= SENDER_SYNERGY_BASE && sender < SENDER_SYNERGY_BASE + PAGE_STRIDE)
        return SENDER_RETAIN;

    if (sender >= SENDER_SPELL_BASE && sender < SENDER_SYNERGY_BASE)
    {
        uint32 base = sender >= SENDER_TALENT_BASE ? SENDER_TALENT_BASE : SENDER_SPELL_BASE;
        uint8 classId = uint8((sender - base) / PAGE_STRIDE);
        return SENDER_CLASS_BASE + classId;
    }

    if (sender >= SENDER_CLASS_BASE && sender < SENDER_CLASS_BASE + MAX_CLASSES)
        return SENDER_RETAIN;

    return SENDER_MAIN;
}

uint32 ShiftPage(uint32 sender, int delta)
{
    if (sender >= SENDER_SYNERGY_BASE && sender < SENDER_SYNERGY_BASE + PAGE_STRIDE)
    {
        uint32 page = sender - SENDER_SYNERGY_BASE;
        if (delta < 0 && !page)
            return sender;
        page = uint32(int32(page) + delta);
        if (page > 200)
            page = 200;
        return SENDER_SYNERGY_BASE + page;
    }

    if (sender < SENDER_SPELL_BASE || sender >= SENDER_SYNERGY_BASE)
        return sender;

    uint32 base = sender >= SENDER_TALENT_BASE ? SENDER_TALENT_BASE : SENDER_SPELL_BASE;
    uint32 rem = sender - base;
    uint32 classId = rem / PAGE_STRIDE;
    uint32 page = rem % PAGE_STRIDE;
    if (delta < 0 && !page)
        return sender;
    page = uint32(int32(page) + delta);
    if (page > 200)
        page = 200;
    return base + classId * PAGE_STRIDE + page;
}

void ShowMain(Player* player, ObjectGuid const& source)
{
    AppendSwitchStatus(player, SENDER_MAIN);

    std::array<ClassRow, MAX_CLASSES> rows;
    LoadProgress(player, rows);
    uint8 freshLevel = player->GetFreshClassLevel();
    bool allowDeathKnight = sConfigMgr->GetOption<bool>("Multiclass.AllowDeathKnight", true, false);
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
    {
        if (!sChrClassesStore.LookupEntry(classId))
            continue;

        std::string name = ClassLabel(player, classId);
        if (classId == player->getClass())
        {
            std::string line = Tf(player, LANG_MULTICLASS_CLASS_OPEN, name,
                uint32(rows[classId].Level), rows[classId].Xp);
            line += T(player, LANG_MULTICLASS_CURRENT);
            AddOption(player, std::move(line), SENDER_MAIN, ACT_REFRESH);
            continue;
        }

        bool blocked = !sObjectMgr->GetPlayerInfo(player->getRace(true), classId);
        if (classId == CLASS_DEATH_KNIGHT && !allowDeathKnight)
            blocked = true;
        if (blocked)
        {
            AddOption(player, Tf(player, LANG_MULTICLASS_UNAVAILABLE, name), SENDER_MAIN, ACT_REFRESH);
            continue;
        }

        uint32 sender = SENDER_CONFIRM_BASE + classId;
        if (rows[classId].Present)
            AddOption(player, Tf(player, LANG_MULTICLASS_CLASS_OPEN, name,
                uint32(rows[classId].Level), rows[classId].Xp), sender, ACT_REFRESH);
        else
            AddOption(player, Tf(player, LANG_MULTICLASS_CLASS_FRESH, name, uint32(freshLevel)),
                sender, ACT_REFRESH);
    }

    AddOption(player, T(player, LANG_MULTICLASS_MENU_RETAIN), SENDER_RETAIN, ACT_REFRESH);
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

void ShowConfirm(Player* player, ObjectGuid const& source, uint8 classId)
{
    if (!classId || classId >= MAX_CLASSES || classId == player->getClass())
    {
        ShowMain(player, source);
        return;
    }

    uint32 sender = SENDER_CONFIRM_BASE + classId;
    AppendSwitchStatus(player, sender);
    AddOption(player, Tf(player, LANG_MULTICLASS_MENU_CONFIRM, ClassLabel(player, classId)), sender, ACT_CONFIRM);
    AddOption(player, T(player, LANG_MULTICLASS_MENU_BACK), sender, ACT_BACK);
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

void ShowRetain(Player* player, ObjectGuid const& source)
{
    MentorView view;
    LoadSnapshots(player, view);
    uint32 synergyLevel = sConfigMgr->GetOption<uint32>("Multiclass.SynergyLevel", 60, false);

    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
    {
        if (!view.Classes[classId].Present)
            continue;

        uint32 slots = SlotsForLevel(view.Classes[classId].Level);
        std::string line = Tf(player, LANG_MULTICLASS_CLASS_OPEN, ClassLabel(player, classId),
            uint32(view.Classes[classId].Level), view.Classes[classId].Xp);
        line += " ";
        line += Tf(player, LANG_MULTICLASS_SLOTS,
            CountKind(view, classId, RETAIN_KIND_SPELL), slots,
            CountKind(view, classId, RETAIN_KIND_TALENT), slots);
        AddOption(player, std::move(line), SENDER_CLASS_BASE + classId, ACT_REFRESH);
    }

    if (view.SynergyClasses < 2)
        AddOption(player, Tf(player, LANG_MULTICLASS_SYNERGY_LOCKED, synergyLevel), SENDER_RETAIN, ACT_REFRESH);
    else
        AddOption(player, T(player, LANG_MULTICLASS_MENU_SYNERGY), SENDER_SYNERGY_BASE, ACT_REFRESH);

    AddOption(player, T(player, LANG_MULTICLASS_MENU_BACK), SENDER_RETAIN, ACT_BACK);
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

void ShowClassRetain(Player* player, ObjectGuid const& source, uint8 classId)
{
    MentorView view;
    LoadSnapshots(player, view);
    if (!classId || classId >= MAX_CLASSES || !view.Classes[classId].Present)
    {
        ShowRetain(player, source);
        return;
    }

    uint32 slots = SlotsForLevel(view.Classes[classId].Level);
    uint32 spells = CountKind(view, classId, RETAIN_KIND_SPELL);
    uint32 talents = CountKind(view, classId, RETAIN_KIND_TALENT);
    uint32 sender = SENDER_CLASS_BASE + classId;
    AddOption(player, Tf(player, LANG_MULTICLASS_SLOTS, spells, slots, talents, slots), sender, ACT_REFRESH);
    AddOption(player, Tf(player, LANG_MULTICLASS_MENU_SPELLS, spells, slots), sender, ACT_SPELLS);
    AddOption(player, Tf(player, LANG_MULTICLASS_MENU_TALENTS, talents, slots), sender, ACT_TALENTS);

    std::vector<RetainPick> picks;
    for (RetainPick const& row : view.Retained)
    {
        if (row.SourceClass != classId || (row.Kind != RETAIN_KIND_SPELL && row.Kind != RETAIN_KIND_TALENT))
            continue;
        picks.push_back(row);
    }
    std::sort(picks.begin(), picks.end(), [](RetainPick const& left, RetainPick const& right)
    {
        return SpellOrderLess(OrderOf(left.SpellId), OrderOf(right.SpellId));
    });
    for (RetainPick const& row : picks)
        AddOption(player, Tf(player, LANG_MULTICLASS_PICK_REMOVE, SpellLabel(player, row.SpellId)),
            sender, row.SpellId);

    AddOption(player, T(player, LANG_MULTICLASS_MENU_BACK), sender, ACT_BACK);
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

std::vector<uint32> Candidates(MentorView const& view, uint8 classId, bool talents)
{
    std::unordered_set<uint32> retained;
    for (RetainPick const& row : view.Retained)
        retained.insert(row.SpellId);

    std::vector<uint32> ids;
    if (classId >= MAX_CLASSES)
        return ids;

    if (talents)
    {
        for (uint32 spellId : view.Talents[classId])
            if (!retained.contains(spellId))
                ids.push_back(spellId);
    }
    else
    {
        for (uint32 spellId : view.Spells[classId])
            if (!view.Talents[classId].contains(spellId) && !retained.contains(spellId))
                ids.push_back(spellId);
    }

    SortSpellIds(ids);
    return ids;
}

void AddPageNav(Player* player, uint32 sender, uint32 page, size_t count)
{
    if ((page + 1) * PAGE_SIZE < count)
        AddOption(player, T(player, LANG_MULTICLASS_MENU_NEXT), sender, ACT_NEXT);
    if (page)
        AddOption(player, T(player, LANG_MULTICLASS_MENU_PREV), sender, ACT_PREV);
    AddOption(player, T(player, LANG_MULTICLASS_MENU_BACK), sender, ACT_BACK);
}

void ShowSpellPage(Player* player, ObjectGuid const& source, uint32 sender, bool talents)
{
    uint32 base = talents ? SENDER_TALENT_BASE : SENDER_SPELL_BASE;
    uint32 rem = sender >= base ? sender - base : 0;
    uint8 classId = uint8(rem / PAGE_STRIDE);
    uint32 page = rem % PAGE_STRIDE;

    MentorView view;
    LoadSnapshots(player, view);
    if (!classId || classId >= MAX_CLASSES || !view.Classes[classId].Present)
    {
        ShowRetain(player, source);
        return;
    }

    std::vector<uint32> ids = Candidates(view, classId, talents);
    if (page && page * PAGE_SIZE >= ids.size())
        page = ids.empty() ? 0 : uint32((ids.size() - 1) / PAGE_SIZE);
    sender = base + uint32(classId) * PAGE_STRIDE + page;

    uint32 start = page * PAGE_SIZE;
    if (start >= ids.size())
        AddOption(player, T(player, LANG_MULTICLASS_NO_SPELLS), sender, ACT_REFRESH);
    else
    {
        uint32 end = start + PAGE_SIZE;
        if (end > ids.size())
            end = uint32(ids.size());
        for (uint32 index = start; index < end; ++index)
            AddOption(player, Tf(player, LANG_MULTICLASS_PICK_ADD, SpellLabel(player, ids[index])),
                sender, ids[index]);
    }

    AddPageNav(player, sender, page, ids.size());
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

std::vector<SynergyChoice> SynergyChoices(MentorView const& view)
{
    std::unordered_set<uint32> retained;
    std::vector<SynergyChoice> choices;
    for (RetainPick const& row : view.Retained)
    {
        retained.insert(row.SpellId);
        if (row.Kind == RETAIN_KIND_SYNERGY)
            choices.push_back({ row.SpellId, true });
    }
    std::sort(choices.begin(), choices.end(), [](SynergyChoice const& left, SynergyChoice const& right)
    {
        return SpellOrderLess(OrderOf(left.SpellId), OrderOf(right.SpellId));
    });

    std::vector<uint32> adds;
    std::unordered_set<uint32> seen;
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
    {
        if (!view.Classes[classId].Present)
            continue;

        auto consider = [&](uint32 spellId)
        {
            if (!seen.insert(spellId).second || retained.contains(spellId))
                return;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
            if (!info || !info->IsPassive())
                return;
            adds.push_back(spellId);
        };
        for (uint32 spellId : view.Spells[classId])
            consider(spellId);
        for (uint32 spellId : view.Talents[classId])
            consider(spellId);
    }
    std::sort(adds.begin(), adds.end(), [](uint32 left, uint32 right)
    {
        return SpellOrderLess(OrderOf(left), OrderOf(right));
    });
    for (uint32 spellId : adds)
        choices.push_back({ spellId, false });
    return choices;
}

void ShowSynergy(Player* player, ObjectGuid const& source, uint32 page)
{
    MentorView view;
    LoadSnapshots(player, view);
    if (view.SynergyClasses < 2)
    {
        ShowRetain(player, source);
        return;
    }

    std::vector<SynergyChoice> choices = SynergyChoices(view);
    if (page > 200)
        page = 200;
    if (page && page * PAGE_SIZE >= choices.size())
        page = choices.empty() ? 0 : uint32((choices.size() - 1) / PAGE_SIZE);

    uint32 sender = SENDER_SYNERGY_BASE + page;
    uint32 start = page * PAGE_SIZE;
    if (start >= choices.size())
        AddOption(player, T(player, LANG_MULTICLASS_NO_SPELLS), sender, ACT_REFRESH);
    else
    {
        uint32 end = start + PAGE_SIZE;
        if (end > choices.size())
            end = uint32(choices.size());
        for (uint32 index = start; index < end; ++index)
        {
            uint32 entry = choices[index].Remove ? LANG_MULTICLASS_PICK_REMOVE : LANG_MULTICLASS_PICK_ADD;
            AddOption(player, Tf(player, entry, SpellLabel(player, choices[index].SpellId)),
                sender, choices[index].SpellId);
        }
    }

    AddPageNav(player, sender, page, choices.size());
    SendGossipMenuFor(player, MENTOR_TEXT, source);
}

void ConfirmSwitch(Player* player, ObjectGuid const& source, uint32 sender)
{
    if (sender < SENDER_CONFIRM_BASE || sender >= SENDER_CONFIRM_BASE + MAX_CLASSES)
    {
        ShowMain(player, source);
        return;
    }

    uint8 classId = uint8(sender - SENDER_CONFIRM_BASE);
    // The mentor keeps Multiclass.RequireCity. The bag item does not.
    bool requireCity = !source.IsItem();
    ClassChangeRequestStatus status = player->RequestClassChange(classId, requireCity);
    if (status.Result == CLASS_CHANGE_OK)
    {
        std::string text = Tf(player, LANG_MULTICLASS_QUEUED, ClassLabel(player, classId));
        CloseGossipMenuFor(player);
        ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
        player->GetSession()->LogoutPlayer(true);
        return;
    }

    std::string text = status.Result == CLASS_CHANGE_COOLDOWN
        ? Tf(player, LANG_MULTICLASS_ERR_COOLDOWN, status.Extra)
        : T(player, ErrorEntry(status.Result));
    ChatHandler(player->GetSession()).SendSysMessage(text.c_str());
    ShowConfirm(player, source, classId);
}

void HandleSpellAction(Player* player, ObjectGuid const& source, uint32 sender, uint32 spellId)
{
    ChatHandler handler(player->GetSession());
    if (sender >= SENDER_CLASS_BASE && sender < SENDER_CLASS_BASE + MAX_CLASSES)
        player->RemoveRetainedSpell(spellId, &handler);
    else if (sender >= SENDER_SYNERGY_BASE && sender < SENDER_SYNERGY_BASE + PAGE_STRIDE)
    {
        MentorView view;
        LoadSnapshots(player, view);
        bool removing = false;
        for (RetainPick const& row : view.Retained)
            if (row.SpellId == spellId && row.Kind == RETAIN_KIND_SYNERGY)
                removing = true;
        if (removing)
            player->RemoveRetainedSpell(spellId, &handler);
        else
            player->AddRetainedSpell(spellId, true, -1, &handler);
    }
    else if (sender >= SENDER_SPELL_BASE && sender < SENDER_SYNERGY_BASE)
        player->AddRetainedSpell(spellId, false, -1, &handler);
    else
    {
        ShowMain(player, source);
        return;
    }

    ShowForSender(player, source, sender);
}

bool Refuse(Player* player, Creature* creature)
{
    if (!player || !player->GetSession())
        return true;

    uint32 entry = sConfigMgr->GetOption<uint32>("Multiclass.MentorNpcEntry", 550000, false);
    if (!entry || !creature || creature->GetEntry() != entry)
    {
        ChatHandler(player->GetSession()).SendSysMessage(T(player, LANG_MULTICLASS_NPC_DISABLED).c_str());
        CloseGossipMenuFor(player);
        return true;
    }

    if (IsRandomBotGuid(player->GetGUID().GetCounter()) || player->GetSession()->IsBot())
    {
        ChatHandler(player->GetSession()).SendSysMessage(T(player, LANG_MULTICLASS_BOT).c_str());
        CloseGossipMenuFor(player);
        return true;
    }

    return false;
}

void HandleMenuAction(Player* player, ObjectGuid const& source, uint32 sender, uint32 action)
{
    if (action == ACT_BACK)
    {
        ShowForSender(player, source, ParentSender(sender));
        return;
    }

    if (action == ACT_NEXT || action == ACT_PREV)
    {
        ShowForSender(player, source, ShiftPage(sender, action == ACT_NEXT ? 1 : -1));
        return;
    }

    if ((action == ACT_SPELLS || action == ACT_TALENTS)
        && sender >= SENDER_CLASS_BASE && sender < SENDER_CLASS_BASE + MAX_CLASSES)
    {
        uint32 base = action == ACT_TALENTS ? SENDER_TALENT_BASE : SENDER_SPELL_BASE;
        ShowForSender(player, source, base + (sender - SENDER_CLASS_BASE) * PAGE_STRIDE);
        return;
    }

    if (action == ACT_CONFIRM)
    {
        ConfirmSwitch(player, source, sender);
        return;
    }

    if (action == ACT_SYNERGY)
    {
        ShowSynergy(player, source, 0);
        return;
    }

    if (!action || action >= 0xF0000000)
    {
        ShowForSender(player, source, sender);
        return;
    }

    HandleSpellAction(player, source, sender, action);
}

// fromUse sends an inventory error so the client releases the item. A menu click just closes.
bool RefuseItem(Player* player, Item* item, bool fromUse)
{
    auto stop = [&](uint32 text, InventoryResult err)
    {
        if (player && player->GetSession())
            ChatHandler(player->GetSession()).SendSysMessage(T(player, text).c_str());
        if (fromUse && player && item)
            player->SendEquipError(err, item, nullptr);
        else if (player)
            CloseGossipMenuFor(player);
        return true;
    };

    if (!player || !player->GetSession() || !item)
        return true;

    uint32 entry = sConfigMgr->GetOption<uint32>("Multiclass.MentorItemEntry", 550001, false);
    if (!entry || item->GetEntry() != entry)
        return stop(LANG_MULTICLASS_NPC_DISABLED, EQUIP_ERR_CANT_DO_RIGHT_NOW);

    if (player->IsInCombat())
        return stop(LANG_MULTICLASS_ERR_COMBAT, EQUIP_ERR_NOT_IN_COMBAT);

    if (IsRandomBotGuid(player->GetGUID().GetCounter()) || player->GetSession()->IsBot())
        return stop(LANG_MULTICLASS_BOT, EQUIP_ERR_CANT_DO_RIGHT_NOW);

    return false;
}
}

class npc_class_mentor : public CreatureScript
{
public:
    npc_class_mentor() : CreatureScript("npc_class_mentor") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        if (Refuse(player, creature))
            return true;

        ShowMain(player, creature->GetGUID());
        return true;
    }

    bool OnGossipSelect(Player* player, Creature* creature, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (Refuse(player, creature))
            return true;

        HandleMenuAction(player, creature->GetGUID(), sender, action);
        return true;
    }
};

class item_class_mentor : public ItemScript
{
public:
    item_class_mentor() : ItemScript("item_class_mentor") { }

    bool OnUse(Player* player, Item* item, SpellCastTargets const& /*targets*/) override
    {
        if (RefuseItem(player, item, true))
            return true;

        // Spell 836 is not cast. Its effect is Spawn. Release the use, then open the menu.
        player->SendEquipError(EQUIP_ERR_NONE, item, nullptr);
        ClearGossipMenuFor(player);
        ShowMain(player, item->GetGUID());
        return true;
    }

    void OnGossipSelect(Player* player, Item* item, uint32 sender, uint32 action) override
    {
        ClearGossipMenuFor(player);
        if (RefuseItem(player, item, false))
            return;

        HandleMenuAction(player, item->GetGUID(), sender, action);
    }
};

void AddSC_npc_class_mentor()
{
    new npc_class_mentor();
    new item_class_mentor();
}
