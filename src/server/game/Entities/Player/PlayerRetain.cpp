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

#include "Player.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Log.h"
#include "SpellMgr.h"
#include "StringConvert.h"
#include "StringFormat.h"
#include "Tokenize.h"
#include <array>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace
{
constexpr uint8 RETAIN_KIND_SPELL = 0;
constexpr uint8 RETAIN_KIND_TALENT = 1;
constexpr uint8 RETAIN_KIND_SYNERGY = 2;
constexpr uint8 SYNERGY_SLOT_COUNT = 2;
constexpr uint32 SPELL_DEATH_GATE = 50977;

enum class RetainDeny : uint8
{
    None = 0,
    Missing,
    Denied,
    Form,
    Summon,
    Rune,
    Combo,
    Stealth,
    Stance,
    Skill,
    Mount,
    Teleport,
    Power,
    ActiveSynergy
};

struct RetainRow
{
    uint8 SourceClass = 0;
    uint32 SpellId = 0;
    uint8 Kind = 0;
    uint8 Slot = 0;
};

struct RetainContext
{
    std::array<uint8, MAX_CLASSES> Level{};
    std::array<bool, MAX_CLASSES> Present{};
    std::array<std::unordered_set<uint32>, MAX_CLASSES> Spells;
    std::array<std::unordered_set<uint32>, MAX_CLASSES> Talents;
    std::vector<RetainRow> Rows;
    uint8 SynergyClasses = 0;
};

struct SourceChoice
{
    uint8 ClassId = 0;
    uint8 Kind = RETAIN_KIND_SPELL;
    bool Found = false;
};

struct ApplyPlan
{
    uint32 SpellId = 0;
    std::string Reason;
    bool Quiet = false;
};

std::string_view TrimView(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

bool RetainEnabled()
{
    return sConfigMgr->GetOption<bool>("Multiclass.Enable", true, false)
        && sConfigMgr->GetOption<bool>("Multiclass.Retain.Enable", true, false);
}

template <typename T>
T RetainOption(char const* key, T def)
{
    return sConfigMgr->GetOption<T>(key, def, false);
}

std::unordered_set<uint32> ConfigSpellIds(char const* key)
{
    std::unordered_set<uint32> ids;
    std::string raw = sConfigMgr->GetOption<std::string>(key, "", false);
    for (std::string_view token : Acore::Tokenize(raw, ',', false))
    {
        if (Optional<uint32> id = Acore::StringTo<uint32>(TrimView(token)))
            if (*id)
                ids.insert(*id);
    }
    return ids;
}

uint8 SlotsForLevel(uint8 level)
{
    uint8 count = 0;
    if (level >= RetainOption<uint32>("Multiclass.Retain.Level1", 20))
        ++count;
    if (level >= RetainOption<uint32>("Multiclass.Retain.Level2", 40))
        ++count;
    if (level >= RetainOption<uint32>("Multiclass.Retain.Level3", 60))
        ++count;
    return count;
}

uint32 SpellFamilyForClass(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
            return SPELLFAMILY_WARRIOR;
        case CLASS_PALADIN:
            return SPELLFAMILY_PALADIN;
        case CLASS_HUNTER:
            return SPELLFAMILY_HUNTER;
        case CLASS_ROGUE:
            return SPELLFAMILY_ROGUE;
        case CLASS_PRIEST:
            return SPELLFAMILY_PRIEST;
        case CLASS_DEATH_KNIGHT:
            return SPELLFAMILY_DEATHKNIGHT;
        case CLASS_SHAMAN:
            return SPELLFAMILY_SHAMAN;
        case CLASS_MAGE:
            return SPELLFAMILY_MAGE;
        case CLASS_WARLOCK:
            return SPELLFAMILY_WARLOCK;
        case CLASS_DRUID:
            return SPELLFAMILY_DRUID;
        default:
            return SPELLFAMILY_GENERIC;
    }
}

char const* ClassName(uint8 classId)
{
    switch (classId)
    {
        case CLASS_WARRIOR:
            return "Warrior";
        case CLASS_PALADIN:
            return "Paladin";
        case CLASS_HUNTER:
            return "Hunter";
        case CLASS_ROGUE:
            return "Rogue";
        case CLASS_PRIEST:
            return "Priest";
        case CLASS_DEATH_KNIGHT:
            return "Death Knight";
        case CLASS_SHAMAN:
            return "Shaman";
        case CLASS_MAGE:
            return "Mage";
        case CLASS_WARLOCK:
            return "Warlock";
        case CLASS_DRUID:
            return "Druid";
        default:
            return "Unknown";
    }
}

char const* KindName(uint8 kind)
{
    switch (kind)
    {
        case RETAIN_KIND_TALENT:
            return "talent";
        case RETAIN_KIND_SYNERGY:
            return "synergy";
        default:
            return "spell";
    }
}

std::string SpellLabel(SpellInfo const* info)
{
    if (!info)
        return "unknown spell";
    if (info->SpellName[0] && *info->SpellName[0])
        return Acore::StringFormat("{} ({})", info->SpellName[0], info->Id);
    return Acore::StringFormat("spell {}", info->Id);
}

void Tell(Player const* player, ChatHandler* handler, std::string const& text)
{
    if (handler)
    {
        handler->SendSysMessage(text);
        return;
    }

    if (player->GetSession() && !player->isBeingLoaded())
    {
        ChatHandler(player->GetSession()).SendSysMessage(text);
        return;
    }

    LOG_INFO("entities.player", "Player {} ({}): {}", player->GetName(), player->GetGUID().ToString(), text);
}

char const* DenyText(RetainDeny deny)
{
    switch (deny)
    {
        case RetainDeny::Missing:
            return "that spell does not exist";
        case RetainDeny::Denied:
            return "that spell is denied";
        case RetainDeny::Form:
            return "form, stance, presence, and aura spells cannot be retained";
        case RetainDeny::Summon:
            return "pet, totem, and summon spells cannot be retained";
        case RetainDeny::Rune:
            return "rune and runic power spells cannot be retained";
        case RetainDeny::Combo:
            return "combo-point finishers cannot be retained";
        case RetainDeny::Stealth:
            return "stealth-only spells cannot be retained";
        case RetainDeny::Stance:
            return "spells that require a stance cannot be retained";
        case RetainDeny::Skill:
            return "spells that teach a skill cannot be retained";
        case RetainDeny::Mount:
            return "class mounts cannot be retained";
        case RetainDeny::Teleport:
            return "portals, teleports, and Death Gate cannot be retained";
        case RetainDeny::Power:
            return "that spell's power type does not match the current class";
        case RetainDeny::ActiveSynergy:
            return "synergy slots accept passive spells only";
        default:
            return "that spell cannot be retained";
    }
}

bool IsFormSpell(SpellInfo const* info)
{
    if (info->HasAura(SPELL_AURA_MOD_SHAPESHIFT))
        return true;

    SpellSpecificType specific = info->GetSpellSpecific();
    return specific == SPELL_SPECIFIC_AURA || specific == SPELL_SPECIFIC_PRESENCE || specific == SPELL_SPECIFIC_ASPECT;
}

bool IsSummonSpell(SpellInfo const* info)
{
    return info->HasEffect(SPELL_EFFECT_SUMMON)
        || info->HasEffect(SPELL_EFFECT_SUMMON_PET)
        || info->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_WILD)
        || info->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_SLOT1)
        || info->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_SLOT2)
        || info->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_SLOT3)
        || info->HasEffect(SPELL_EFFECT_SUMMON_OBJECT_SLOT4);
}

bool IsSkillTeacher(SpellInfo const* info)
{
    if (sSpellMgr->GetSpellLearnSkill(info->Id))
        return true;

    return info->HasEffect(SPELL_EFFECT_SKILL_STEP)
        || info->HasEffect(SPELL_EFFECT_TRADE_SKILL)
        || info->HasEffect(SPELL_EFFECT_LEARN_SPELL);
}

bool IsClassMount(SpellInfo const* info)
{
    return info->HasAura(SPELL_AURA_MOUNTED) && info->SpellFamilyName != SPELLFAMILY_GENERIC;
}

bool IsTeleportSpell(SpellInfo const* info)
{
    if (info->Id == SPELL_DEATH_GATE)
        return true;

    return info->HasEffect(SPELL_EFFECT_TELEPORT_UNITS)
        || info->HasEffect(SPELL_EFFECT_TELEPORT_UNITS_FACE_CASTER)
        || info->HasEffect(SPELL_EFFECT_TELEPORT_GRAVEYARD)
        || info->HasEffect(SPELL_EFFECT_TRANS_DOOR)
        || info->HasEffect(SPELL_EFFECT_PORTAL)
        || info->HasEffect(SPELL_EFFECT_PORTAL_TELEPORT);
}

bool HasRuneCost(SpellInfo const* info)
{
    return info->RuneCostID != 0 || info->PowerType == POWER_RUNE || info->PowerType == POWER_RUNIC_POWER;
}

bool PowerMatches(SpellInfo const* info, uint8 currentClass)
{
    if (info->IsPassive() || HasRuneCost(info))
        return !HasRuneCost(info);

    bool free = !info->ManaCost && !info->ManaCostPercentage && !info->ManaPerSecond && !info->ManaPerSecondPerLevel;
    if (free || info->PowerType == POWER_HEALTH)
        return true;

    ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(currentClass);
    return entry && info->PowerType == entry->powerType;
}

RetainDeny ValidateRules(SpellInfo const* info, uint8 currentClass, bool requirePassive)
{
    if (!info)
        return RetainDeny::Missing;

    if (ConfigSpellIds("Multiclass.Retain.DenySpells").contains(info->Id))
        return RetainDeny::Denied;

    bool bypass = ConfigSpellIds("Multiclass.Retain.AllowSpells").contains(info->Id);
    if (!bypass && RetainOption<bool>("Multiclass.Retain.EnforceRules", true))
    {
        if (IsFormSpell(info))
            return RetainDeny::Form;
        if (IsSummonSpell(info))
            return RetainDeny::Summon;
        if (HasRuneCost(info))
            return RetainDeny::Rune;
        if (info->NeedsComboPoints())
            return RetainDeny::Combo;
        if (info->HasAttribute(SPELL_ATTR0_ONLY_STEALTHED))
            return RetainDeny::Stealth;
        if (info->Stances && info->CheckShapeshift(0) != SPELL_CAST_OK)
            return RetainDeny::Stance;
        if (IsSkillTeacher(info))
            return RetainDeny::Skill;
        if (IsClassMount(info))
            return RetainDeny::Mount;
        if (IsTeleportSpell(info))
            return RetainDeny::Teleport;
        if (!PowerMatches(info, currentClass))
            return RetainDeny::Power;
    }

    if (requirePassive && !info->IsPassive())
        return RetainDeny::ActiveSynergy;

    return RetainDeny::None;
}

uint32 SelectActiveRank(SpellInfo const* picked, uint32 levelLimit)
{
    uint32 first = sSpellMgr->GetFirstSpellInChain(picked->Id);
    SpellInfo const* info = sSpellMgr->GetSpellInfo(first ? first : picked->Id);
    uint32 best = 0;
    bool sawPicked = false;
    for (uint8 guard = 0; info && guard < 32; ++guard)
    {
        if (info->SpellLevel <= levelLimit)
            best = info->Id;
        if (info->Id == picked->Id)
        {
            sawPicked = true;
            break;
        }
        info = info->GetNextRankSpell();
    }
    return sawPicked ? best : 0;
}

uint32 RetainLevelLimit(Player const* player)
{
    uint64 limit = uint64(player->GetLevel()) + RetainOption<uint32>("Multiclass.RetainLevelGrace", 10);
    if (limit > std::numeric_limits<uint32>::max())
        return std::numeric_limits<uint32>::max();
    return uint32(limit);
}

bool ClassOwnsChain(Player const* player, uint32 spellId)
{
    uint32 first = sSpellMgr->GetFirstSpellInChain(spellId);
    SpellInfo const* info = sSpellMgr->GetSpellInfo(first ? first : spellId);
    for (uint8 guard = 0; info && guard < 32; ++guard)
    {
        auto itr = player->GetSpellMap().find(info->Id);
        if (itr != player->GetSpellMap().end() && itr->second && itr->second->State != PLAYERSPELL_REMOVED
            && itr->second->State != PLAYERSPELL_TEMPORARY)
            return true;
        info = info->GetNextRankSpell();
    }
    return false;
}

bool MarkedRemoved(Player const* player, uint32 spellId)
{
    auto itr = player->GetSpellMap().find(spellId);
    return itr != player->GetSpellMap().end() && itr->second && itr->second->State == PLAYERSPELL_REMOVED;
}

bool KnownByClass(RetainContext const& ctx, uint8 classId, uint32 spellId, bool talent)
{
    if (classId >= MAX_CLASSES)
        return false;
    if (talent)
        return ctx.Talents[classId].contains(spellId);
    return ctx.Spells[classId].contains(spellId) || ctx.Talents[classId].contains(spellId);
}

std::unordered_set<uint8> UsedSlots(RetainContext const& ctx, uint8 sourceClass, uint8 kind)
{
    std::unordered_set<uint8> used;
    for (RetainRow const& row : ctx.Rows)
    {
        if (row.Kind != kind)
            continue;
        if (kind == RETAIN_KIND_SYNERGY || row.SourceClass == sourceClass)
            used.insert(row.Slot);
    }
    return used;
}

void LoadContext(Player const* player, RetainContext& ctx)
{
    ObjectGuid::LowType guid = player->GetGUID().GetCounter();

    CharacterDatabasePreparedStatement* progress =
        CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_PROGRESS_BY_GUID);
    progress->SetData(0, guid);
    if (PreparedQueryResult result = CharacterDatabase.Query(progress))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId >= MAX_CLASSES)
                continue;
            ctx.Level[classId] = fields[1].Get<uint8>();
            ctx.Present[classId] = true;
        } while (result->NextRow());
    }

    CharacterDatabasePreparedStatement* spells = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_SPELL_BY_GUID);
    spells->SetData(0, guid);
    if (PreparedQueryResult result = CharacterDatabase.Query(spells))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId < MAX_CLASSES)
                ctx.Spells[classId].insert(fields[1].Get<uint32>());
        } while (result->NextRow());
    }

    CharacterDatabasePreparedStatement* talents = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_TALENT_BY_GUID);
    talents->SetData(0, guid);
    if (PreparedQueryResult result = CharacterDatabase.Query(talents))
    {
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            if (classId < MAX_CLASSES)
                ctx.Talents[classId].insert(fields[1].Get<uint32>());
        } while (result->NextRow());
    }

    uint8 current = player->getClass();
    if (current < MAX_CLASSES)
    {
        ctx.Spells[current].clear();
        ctx.Talents[current].clear();
        for (auto const& itr : player->GetSpellMap())
        {
            if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED || itr.second->State == PLAYERSPELL_TEMPORARY)
                continue;
            ctx.Spells[current].insert(itr.first);
        }
        for (auto const& itr : player->GetTalentMap())
        {
            if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED)
                continue;
            ctx.Talents[current].insert(itr.first);
        }
        ctx.Level[current] = player->GetLevel();
        ctx.Present[current] = true;
    }

    CharacterDatabasePreparedStatement* rows = CharacterDatabase.GetPreparedStatement(CHAR_SEL_RETAINED_SPELL);
    rows->SetData(0, guid);
    if (PreparedQueryResult result = CharacterDatabase.Query(rows))
    {
        do
        {
            Field* fields = result->Fetch();
            RetainRow row;
            row.SourceClass = fields[0].Get<uint8>();
            row.SpellId = fields[1].Get<uint32>();
            row.Kind = fields[2].Get<uint8>();
            row.Slot = fields[3].Get<uint8>();
            ctx.Rows.push_back(row);
            if (row.SourceClass < MAX_CLASSES)
                ctx.Present[row.SourceClass] = true;
        } while (result->NextRow());
    }

    uint32 synergyLevel = RetainOption<uint32>("Multiclass.SynergyLevel", 60);
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
        if (ctx.Present[classId] && ctx.Level[classId] >= synergyLevel)
            ++ctx.SynergyClasses;
}

SourceChoice ChooseSource(SpellInfo const* info, RetainContext const& ctx, bool synergy)
{
    struct Option
    {
        uint8 ClassId = 0;
        uint8 Kind = RETAIN_KIND_SPELL;
        uint8 Level = 0;
        bool Family = false;
        bool Free = false;
    };

    std::vector<Option> options;
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
    {
        bool talent = ctx.Talents[classId].contains(info->Id);
        bool spell = ctx.Spells[classId].contains(info->Id);
        if (!talent && !spell)
            continue;

        uint8 kind = synergy ? RETAIN_KIND_SYNERGY : (talent ? RETAIN_KIND_TALENT : RETAIN_KIND_SPELL);
        uint8 slots = synergy ? SYNERGY_SLOT_COUNT : SlotsForLevel(ctx.Level[classId]);
        bool free = synergy ? ctx.SynergyClasses >= 2 && UsedSlots(ctx, classId, kind).size() < slots
                            : UsedSlots(ctx, classId, kind).size() < slots;
        bool family = info->SpellFamilyName && info->SpellFamilyName == SpellFamilyForClass(classId);
        options.push_back({ classId, kind, ctx.Level[classId], family, free });
    }

    if (options.empty())
        return {};

    std::vector<Option> pool;
    for (Option const& option : options)
        if (option.Family)
            pool.push_back(option);
    if (pool.empty())
        pool = options;

    std::vector<Option> open;
    for (Option const& option : pool)
        if (option.Free)
            open.push_back(option);
    if (!open.empty())
        pool = open;

    Option best = pool.front();
    for (Option const& option : pool)
        if (option.Level > best.Level)
            best = option;

    return { best.ClassId, best.Kind, true };
}

ApplyPlan PlanRetain(Player const* player, RetainContext const& ctx, RetainRow const& row, bool suppressed)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(row.SpellId);
    if (!info)
        return { 0, "that spell does not exist", false };

    if (row.Kind != RETAIN_KIND_SPELL && row.Kind != RETAIN_KIND_TALENT && row.Kind != RETAIN_KIND_SYNERGY)
        return { 0, "that retained row is invalid", false };

    bool talentKind = row.Kind == RETAIN_KIND_TALENT;
    if (!KnownByClass(ctx, row.SourceClass, row.SpellId, talentKind))
        return { 0, "that class no longer has this spell", false };

    if (row.Kind == RETAIN_KIND_SYNERGY)
    {
        if (ctx.SynergyClasses < 2)
            return { 0, "synergy slots are locked", false };
        if (row.Slot >= SYNERGY_SLOT_COUNT)
            return { 0, "that synergy slot is closed", false };
    }
    else if (row.Slot >= SlotsForLevel(row.SourceClass < MAX_CLASSES ? ctx.Level[row.SourceClass] : 0))
        return { 0, "that class no longer has this slot", false };

    RetainDeny deny = ValidateRules(info, player->getClass(), row.Kind == RETAIN_KIND_SYNERGY);
    if (deny != RetainDeny::None)
        return { 0, DenyText(deny), false };

    if (suppressed && (!info->IsPassive() || !RetainOption<bool>("Multiclass.RetainPassivesInPvP", true)))
        return { 0, "disabled in a battleground or arena", true };

    uint32 applyId = info->Id;
    if (!info->IsPassive())
    {
        applyId = SelectActiveRank(info, RetainLevelLimit(player));
        if (!applyId)
            return { 0, "no rank is low enough for this level", false };
    }

    if (ClassOwnsChain(player, applyId))
        return { 0, "the current class already knows it", true };
    if (MarkedRemoved(player, applyId))
        return { 0, "waiting for the previous class spell to finish saving", true };

    return { applyId, {}, false };
}

uint8 CountKind(RetainContext const& ctx, uint8 classId, uint8 kind)
{
    uint8 count = 0;
    for (RetainRow const& row : ctx.Rows)
        if (row.Kind == kind && (kind == RETAIN_KIND_SYNERGY || row.SourceClass == classId))
            ++count;
    return count;
}
}

void Player::SetRetainActivesSuppressed(bool suppressed)
{
    m_retainActivesSuppressed = suppressed;
    ApplyRetainedSpells(nullptr, false);
}

void Player::ApplyRetainedSpells(ChatHandler* handler, bool announce)
{
    if (GetSession() && GetSession()->IsBot())
        return;

    auto dropTemporary = [this](uint32 spellId)
    {
        auto itr = m_spells.find(spellId);
        if (itr == m_spells.end() || !itr->second || itr->second->State != PLAYERSPELL_TEMPORARY)
            return;
        removeSpell(spellId, SPEC_MASK_ALL, true);
    };

    if (InBattleground())
        m_retainActivesSuppressed = true;

    if (!RetainEnabled())
    {
        std::vector<uint32> applied(m_retainedApplied.begin(), m_retainedApplied.end());
        m_retainedApplied.clear();
        for (uint32 spellId : applied)
            dropTemporary(spellId);
        return;
    }

    RetainContext ctx;
    LoadContext(this, ctx);

    std::unordered_set<uint32> desired;
    for (RetainRow const& row : ctx.Rows)
    {
        ApplyPlan plan = PlanRetain(this, ctx, row, m_retainActivesSuppressed);
        if (!plan.SpellId)
        {
            if (announce && !plan.Quiet)
                Tell(this, handler, Acore::StringFormat("Retained spell {} is inactive: {}.",
                    row.SpellId, plan.Reason));
            continue;
        }
        desired.insert(plan.SpellId);
    }

    std::vector<uint32> stale;
    for (uint32 spellId : m_retainedApplied)
        if (!desired.contains(spellId))
            stale.push_back(spellId);
    for (uint32 spellId : stale)
    {
        m_retainedApplied.erase(spellId);
        dropTemporary(spellId);
    }

    for (uint32 spellId : desired)
    {
        if (m_retainedApplied.contains(spellId))
            continue;

        auto existing = m_spells.find(spellId);
        if (existing != m_spells.end() && existing->second && existing->second->State == PLAYERSPELL_REMOVED)
            continue;

        if (!addSpell(spellId, SPEC_MASK_ALL, true, true))
        {
            existing = m_spells.find(spellId);
            bool kept = existing != m_spells.end() && existing->second
                && existing->second->State == PLAYERSPELL_TEMPORARY && existing->second->Active;
            if (!kept)
            {
                if (existing != m_spells.end() && existing->second && existing->second->State == PLAYERSPELL_TEMPORARY)
                    removeSpell(spellId, SPEC_MASK_ALL, true);
                continue;
            }
        }
        m_retainedApplied.insert(spellId);
    }

    PlaceRetainedActionButtons();
}

void Player::PlaceRetainedActionButtons()
{
    for (uint32 spellId : m_retainedApplied)
        EnsureRetainedButton(spellId);
}

void Player::EnsureRetainedButton(uint32 spellId)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info || info->IsPassive() || !HasSpell(spellId))
        return;

    uint32 chain = sSpellMgr->GetFirstSpellInChain(spellId);
    if (!chain)
        chain = spellId;

    bool found = false;
    for (auto& itr : m_actionButtons)
    {
        ActionButton& button = itr.second;
        if (button.uState == ACTIONBUTTON_DELETED || button.GetType() != ACTION_BUTTON_SPELL)
            continue;

        uint32 actionChain = sSpellMgr->GetFirstSpellInChain(button.GetAction());
        if (!actionChain)
            actionChain = button.GetAction();
        if (actionChain != chain)
            continue;

        found = true;
        if (button.GetAction() != spellId)
            button.SetActionAndType(spellId, ACTION_BUTTON_SPELL);
    }
    if (found)
        return;

    for (uint16 button = 0; button < MAX_ACTION_BUTTONS; ++button)
    {
        if (GetActionButton(uint8(button)))
            continue;
        addActionButton(uint8(button), spellId, ACTION_BUTTON_SPELL);
        return;
    }

    LOG_DEBUG("entities.player", "Player {} ({}) has no free action button for retained spell {}",
        GetName(), GetGUID().ToString(), spellId);
}

void Player::StripRetainedButtons(uint32 spellId)
{
    uint32 chain = sSpellMgr->GetFirstSpellInChain(spellId);
    if (!chain)
        chain = spellId;

    std::vector<uint8> drop;
    for (auto const& itr : m_actionButtons)
    {
        if (itr.second.uState == ACTIONBUTTON_DELETED || itr.second.GetType() != ACTION_BUTTON_SPELL)
            continue;
        uint32 actionChain = sSpellMgr->GetFirstSpellInChain(itr.second.GetAction());
        if (!actionChain)
            actionChain = itr.second.GetAction();
        if (actionChain != chain)
            continue;
        if (!HasSpell(itr.second.GetAction()))
            drop.push_back(itr.first);
    }
    for (uint8 button : drop)
        removeActionButton(button);
}

bool Player::ChargeRetainChange(ChatHandler* handler)
{
    uint32 now = uint32(GameTime::GetGameTime().count());
    uint32 cooldown = RetainOption<uint32>("Multiclass.Retain.Cooldown", 0);
    if (cooldown)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_RETAINED_CHANGE);
        stmt->SetData(0, GetGUID().GetCounter());
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            uint32 changedAt = result->Fetch()[0].Get<uint32>();
            if (changedAt && uint64(changedAt) + cooldown > now)
            {
                uint32 wait = uint32(uint64(changedAt) + cooldown - now);
                Tell(this, handler, Acore::StringFormat(
                    "You must wait {} seconds before changing retained spells.", wait));
                return false;
            }
        }
    }

    uint32 cost = RetainOption<uint32>("Multiclass.Retain.Cost", 0);
    if (cost > uint32(std::numeric_limits<int32>::max()))
        cost = uint32(std::numeric_limits<int32>::max());
    if (cost && !HasEnoughMoney(cost))
    {
        Tell(this, handler, Acore::StringFormat("You need {} copper to change retained spells.", cost));
        return false;
    }
    if (cost)
        ModifyMoney(-int32(cost));

    if (cost || cooldown)
    {
        CharacterDatabasePreparedStatement* stamp = CharacterDatabase.GetPreparedStatement(CHAR_REP_RETAINED_CHANGE);
        stamp->SetData(0, GetGUID().GetCounter());
        stamp->SetData(1, now);
        CharacterDatabase.DirectExecute(stamp);
    }
    return true;
}

bool Player::AddRetainedSpell(uint32 spellId, bool synergy, int32 slot, ChatHandler* handler)
{
    if (GetSession() && GetSession()->IsBot())
    {
        Tell(this, handler, "Bots cannot use retained spells.");
        return false;
    }
    if (!RetainEnabled())
    {
        Tell(this, handler, "Retained spells are disabled.");
        return false;
    }

    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
    {
        Tell(this, handler, "That spell does not exist.");
        return false;
    }

    RetainContext ctx;
    LoadContext(this, ctx);
    for (RetainRow const& row : ctx.Rows)
    {
        if (row.SpellId != spellId)
            continue;
        Tell(this, handler, "That spell is already retained.");
        return false;
    }

    SourceChoice source = ChooseSource(info, ctx, synergy);
    if (!source.Found)
    {
        Tell(this, handler, "No class on this character has learned that spell.");
        return false;
    }

    RetainDeny deny = ValidateRules(info, getClass(), synergy);
    if (deny != RetainDeny::None)
    {
        Tell(this, handler, Acore::StringFormat("Cannot retain {}: {}.", SpellLabel(info), DenyText(deny)));
        return false;
    }

    uint8 maxSlots = 0;
    if (synergy)
    {
        if (ctx.SynergyClasses < 2)
        {
            Tell(this, handler, Acore::StringFormat(
                "Synergy slots unlock when two classes reach level {}.",
                RetainOption<uint32>("Multiclass.SynergyLevel", 60)));
            return false;
        }
        maxSlots = SYNERGY_SLOT_COUNT;
    }
    else
        maxSlots = SlotsForLevel(ctx.Level[source.ClassId]);

    if (!maxSlots)
    {
        Tell(this, handler, Acore::StringFormat("{} has no retained slots at level {}.",
            ClassName(source.ClassId), uint32(ctx.Level[source.ClassId])));
        return false;
    }

    std::unordered_set<uint8> used = UsedSlots(ctx, source.ClassId, source.Kind);
    int32 chosen = slot;
    if (chosen < 0)
    {
        chosen = -1;
        for (uint8 index = 0; index < maxSlots; ++index)
        {
            if (used.contains(index))
                continue;
            chosen = index;
            break;
        }
    }
    if (chosen < 0)
    {
        Tell(this, handler, "There is no free slot.");
        return false;
    }
    if (chosen >= int32(maxSlots) || used.contains(uint8(chosen)))
    {
        Tell(this, handler, "That slot is not open.");
        return false;
    }

    if (!ChargeRetainChange(handler))
        return false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_RETAINED_SPELL);
    stmt->SetData(0, GetGUID().GetCounter());
    stmt->SetData(1, source.ClassId);
    stmt->SetData(2, spellId);
    stmt->SetData(3, source.Kind);
    stmt->SetData(4, uint8(chosen));
    CharacterDatabase.DirectExecute(stmt);

    Tell(this, handler, Acore::StringFormat("Retained {} {} in {} slot {}.",
        ClassName(source.ClassId), SpellLabel(info), KindName(source.Kind), uint32(chosen)));
    ApplyRetainedSpells(handler, true);
    return true;
}

bool Player::RemoveRetainedSpell(uint32 spellId, ChatHandler* handler)
{
    if (GetSession() && GetSession()->IsBot())
    {
        Tell(this, handler, "Bots cannot use retained spells.");
        return false;
    }
    if (!RetainEnabled())
    {
        Tell(this, handler, "Retained spells are disabled.");
        return false;
    }

    RetainContext ctx;
    LoadContext(this, ctx);
    bool found = false;
    for (RetainRow const& row : ctx.Rows)
        if (row.SpellId == spellId)
            found = true;
    if (!found)
    {
        Tell(this, handler, "That spell is not retained.");
        return false;
    }
    if (!ChargeRetainChange(handler))
        return false;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RETAINED_SPELL);
    stmt->SetData(0, GetGUID().GetCounter());
    stmt->SetData(1, spellId);
    CharacterDatabase.DirectExecute(stmt);
    ApplyRetainedSpells(handler, true);
    StripRetainedButtons(spellId);
    Tell(this, handler, Acore::StringFormat("Removed retained spell {}.", spellId));
    return true;
}

bool Player::ClearRetainedSpells(int32 sourceClass, ChatHandler* handler)
{
    if (GetSession() && GetSession()->IsBot())
    {
        Tell(this, handler, "Bots cannot use retained spells.");
        return false;
    }
    if (!RetainEnabled())
    {
        Tell(this, handler, "Retained spells are disabled.");
        return false;
    }

    RetainContext ctx;
    LoadContext(this, ctx);
    std::vector<uint32> spells;
    for (RetainRow const& row : ctx.Rows)
        if (sourceClass < 0 || (row.SourceClass == uint8(sourceClass)))
            spells.push_back(row.SpellId);
    if (spells.empty())
    {
        Tell(this, handler, "There is nothing to clear.");
        return false;
    }
    if (!ChargeRetainChange(handler))
        return false;

    CharacterDatabasePreparedStatement* stmt = nullptr;
    if (sourceClass < 0)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RETAINED_BY_GUID);
        stmt->SetData(0, GetGUID().GetCounter());
    }
    else
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_RETAINED_BY_CLASS);
        stmt->SetData(0, GetGUID().GetCounter());
        stmt->SetData(1, uint8(sourceClass));
    }
    CharacterDatabase.DirectExecute(stmt);
    ApplyRetainedSpells(handler, true);
    for (uint32 spellId : spells)
        StripRetainedButtons(spellId);

    if (sourceClass < 0)
        Tell(this, handler, "Cleared every retained spell.");
    else
        Tell(this, handler, Acore::StringFormat("Cleared retained spells for {}.", ClassName(uint8(sourceClass))));
    return true;
}

void Player::ListRetainedSpells(ChatHandler* handler)
{
    if (!handler)
        return;
    if (GetSession() && GetSession()->IsBot())
    {
        Tell(this, handler, "Bots cannot use retained spells.");
        return;
    }
    if (!RetainEnabled())
    {
        Tell(this, handler, "Retained spells are disabled.");
        return;
    }

    RetainContext ctx;
    LoadContext(this, ctx);
    bool any = false;
    for (uint8 classId = CLASS_WARRIOR; classId < MAX_CLASSES; ++classId)
    {
        if (!ctx.Present[classId])
            continue;
        any = true;
        uint8 slots = SlotsForLevel(ctx.Level[classId]);
        handler->PSendSysMessage("{} level {}: spell slots {}/{}, talent slots {}/{}",
            ClassName(classId), uint32(ctx.Level[classId]),
            uint32(CountKind(ctx, classId, RETAIN_KIND_SPELL)), uint32(slots),
            uint32(CountKind(ctx, classId, RETAIN_KIND_TALENT)), uint32(slots));

        for (RetainRow const& row : ctx.Rows)
        {
            if (row.SourceClass != classId || row.Kind == RETAIN_KIND_SYNERGY)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(row.SpellId);
            ApplyPlan plan = PlanRetain(this, ctx, row, m_retainActivesSuppressed);
            if (plan.SpellId && plan.SpellId != row.SpellId)
                handler->PSendSysMessage("  {} {} slot {} active as {}",
                    KindName(row.Kind), SpellLabel(info), uint32(row.Slot), plan.SpellId);
            else if (plan.SpellId)
                handler->PSendSysMessage("  {} {} slot {} active",
                    KindName(row.Kind), SpellLabel(info), uint32(row.Slot));
            else
                handler->PSendSysMessage("  {} {} slot {} inactive: {}",
                    KindName(row.Kind), SpellLabel(info), uint32(row.Slot), plan.Reason);
        }
    }
    if (!any)
        handler->SendSysMessage("This character has no class progress yet.");

    uint32 synergyLevel = RetainOption<uint32>("Multiclass.SynergyLevel", 60);
    if (ctx.SynergyClasses < 2)
        handler->PSendSysMessage("Synergy passives are locked until two classes reach level {}.", synergyLevel);
    else
        handler->PSendSysMessage("Synergy passives {}/{} ({} classes at level {}).",
            uint32(CountKind(ctx, 0, RETAIN_KIND_SYNERGY)), uint32(SYNERGY_SLOT_COUNT),
            uint32(ctx.SynergyClasses), synergyLevel);

    for (RetainRow const& row : ctx.Rows)
    {
        if (row.Kind != RETAIN_KIND_SYNERGY)
            continue;
        SpellInfo const* info = sSpellMgr->GetSpellInfo(row.SpellId);
        ApplyPlan plan = PlanRetain(this, ctx, row, m_retainActivesSuppressed);
        if (plan.SpellId)
            handler->PSendSysMessage("  synergy {} from {} slot {} active",
                SpellLabel(info), ClassName(row.SourceClass), uint32(row.Slot));
        else
            handler->PSendSysMessage("  synergy {} from {} slot {} inactive: {}",
                SpellLabel(info), ClassName(row.SourceClass), uint32(row.Slot), plan.Reason);
    }
}
