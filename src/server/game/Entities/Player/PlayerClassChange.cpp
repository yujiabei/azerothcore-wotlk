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
#include "CharacterCache.h"
#include "Chat.h"
#include "Config.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Guild.h"
#include "GuildMgr.h"
#include "Item.h"
#include "Language.h"
#include "Log.h"
#include "Mail.h"
#include "ObjectMgr.h"
#include "AreaDefines.h"
#include "MapMgr.h"
#include "Pet.h"
#include "QuestDef.h"
#include "Spell.h"
#include "SpellAuras.h"
#include "SpellMgr.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <vector>

namespace
{
// Quest 12801 "The Light of Dawn" teaches 50977. Its paired cast, 53821, teleports into the
// Plaguelands and is not granted. Quest 12619 "The Emblazoned Runeblade" casts 53431, whose
// effect is Learn Spell 53428 (Runeforging). Quest 12842 assumes that spell is already known.
uint32 constexpr SPELL_DEATH_GATE = 50977;
uint32 constexpr SPELL_RUNEFORGING = 53428;

struct SpellSnap
{
    uint32 Id = 0;
    uint8 SpecMask = 0;
};

struct TalentSnap
{
    uint32 Id = 0;
    uint8 SpecMask = 0;
};

struct GlyphSnap
{
    uint8 Spec = 0;
    uint16 Glyphs[MAX_GLYPH_SLOT_INDEX] = {};
};

struct ActionSnap
{
    uint8 Spec = 0;
    uint8 Button = 0;
    uint32 Action = 0;
    uint8 Type = 0;
};

struct SkillSnap
{
    uint16 Id = 0;
    uint16 Value = 0;
    uint16 Max = 0;
};

struct QuestSnap
{
    uint32 Id = 0;
    QuestStatusData Data;
};

struct EquipSnap
{
    uint8 Slot = 0;
    uint32 ItemLow = 0;
};

struct ProgressSnap
{
    bool Present = false;
    uint8 Level = 1;
    uint32 Xp = 0;
    float Rest = 0.f;
    uint8 Spec = 0;
    uint32 LevelTime = 0;
    uint32 PetId = 0;
    uint32 DailyBonusAt = 0;
};

struct ClassArchive
{
    std::vector<SpellSnap> Spells;
    std::vector<TalentSnap> Talents;
    std::vector<GlyphSnap> Glyphs;
    std::vector<ActionSnap> Actions;
    std::vector<SkillSnap> Skills;
    std::vector<QuestSnap> Quests;
    uint32 PetId = 0;
};

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

bool IsClassSkill(uint32 skillId)
{
    if (!skillId || IsProfessionOrRidingSkill(skillId))
        return false;

    SkillLineEntry const* skill = sSkillLineStore.LookupEntry(skillId);
    if (!skill)
        return false;

    return skill->categoryId == SKILL_CATEGORY_WEAPON
        || skill->categoryId == SKILL_CATEGORY_CLASS
        || skill->categoryId == SKILL_CATEGORY_ARMOR;
}

// Class family spells, plus weapon/armor/class skill-line spells that are not shared by every class.
// SPELLFAMILY_GENERIC (item mounts, companions) and professions stay on the character.
bool IsClassBoundSpell(uint32 spellId, uint8 playerClass)
{
    SpellInfo const* info = sSpellMgr->GetSpellInfo(spellId);
    if (!info)
        return false;

    uint32 family = SpellFamilyForClass(playerClass);
    if (family != SPELLFAMILY_GENERIC && info->SpellFamilyName == family)
        return true;

    SkillLineAbilityMapBounds bounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (auto itr = bounds.first; itr != bounds.second; ++itr)
    {
        SkillLineAbilityEntry const* ability = itr->second;
        if (!ability || IsProfessionOrRidingSkill(ability->SkillLine))
            continue;

        SkillLineEntry const* skill = sSkillLineStore.LookupEntry(ability->SkillLine);
        if (!skill || !IsClassSkill(skill->id))
            continue;

        if (!ability->ClassMask || ability->ClassMask == CLASSMASK_ALL_PLAYABLE)
            continue;

        if ((ability->ClassMask & (1u << (playerClass - 1))) == 0)
            continue;

        return true;
    }

    return false;
}

void RememberSpell(ClassArchive& archive, uint32 spellId, uint8 specMask)
{
    for (SpellSnap const& row : archive.Spells)
        if (row.Id == spellId)
            return;

    SpellSnap row;
    row.Id = spellId;
    row.SpecMask = specMask ? specMask : uint8(SPEC_MASK_ALL);
    archive.Spells.push_back(row);
}

void AppendClassDelete(CharacterDatabaseTransaction trans, CharacterDatabaseStatements id, uint32 guid,
    uint8 playerClass)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(id);
    stmt->SetData(0, guid);
    stmt->SetData(1, playerClass);
    trans->Append(stmt);
}

void AppendProgress(CharacterDatabaseTransaction trans, uint32 guid, uint8 playerClass, uint8 level, uint32 xp,
    float rest, uint8 spec, uint32 levelTime, uint32 petId, uint32 dailyBonusAt)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CLASS_PROGRESS);
    stmt->SetData(0, guid);
    stmt->SetData(1, playerClass);
    stmt->SetData(2, level);
    stmt->SetData(3, xp);
    stmt->SetData(4, rest);
    stmt->SetData(5, spec);
    stmt->SetData(6, levelTime);
    stmt->SetData(7, petId);
    stmt->SetData(8, dailyBonusAt);
    trans->Append(stmt);
}

void AppendPetSlot(CharacterDatabaseTransaction trans, uint32 owner, uint32 petId, uint8 slot, uint8 petClass)
{
    if (!petId)
        return;

    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_CLASS_SLOT);
    stmt->SetData(0, slot);
    stmt->SetData(1, petClass);
    stmt->SetData(2, owner);
    stmt->SetData(3, petId);
    trans->Append(stmt);
}

template <typename Snap>
void LoadRows(CharacterDatabaseStatements id, uint32 guid, uint8 playerClass, std::vector<Snap>& out,
    std::function<void(Field*, Snap&)> const& fill)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(id);
    stmt->SetData(0, guid);
    stmt->SetData(1, playerClass);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            Snap row;
            fill(result->Fetch(), row);
            out.push_back(row);
        } while (result->NextRow());
    }
}

ProgressSnap LoadProgress(uint32 guid, uint8 playerClass)
{
    ProgressSnap snap;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_PROGRESS);
    stmt->SetData(0, guid);
    stmt->SetData(1, playerClass);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        Field* fields = result->Fetch();
        snap.Present = true;
        snap.Level = fields[0].Get<uint8>();
        snap.Xp = fields[1].Get<uint32>();
        snap.Rest = fields[2].Get<float>();
        snap.Spec = fields[3].Get<uint8>();
        snap.LevelTime = fields[4].Get<uint32>();
        snap.PetId = fields[5].Get<uint32>();
        snap.DailyBonusAt = fields[6].Get<uint32>();
        if (!snap.Level)
            snap.Level = 1;
    }

    return snap;
}

ClassArchive LoadArchive(uint32 guid, uint8 playerClass, ProgressSnap const& progress)
{
    ClassArchive archive;
    archive.PetId = progress.PetId;
    if (!progress.Present)
        return archive;

    LoadRows<SpellSnap>(CHAR_SEL_CLASS_SPELL, guid, playerClass, archive.Spells, [](Field* fields, SpellSnap& row)
    {
        row.Id = fields[0].Get<uint32>();
        row.SpecMask = fields[1].Get<uint8>();
    });
    LoadRows<TalentSnap>(CHAR_SEL_CLASS_TALENT, guid, playerClass, archive.Talents, [](Field* fields, TalentSnap& row)
    {
        row.Id = fields[0].Get<uint32>();
        row.SpecMask = fields[1].Get<uint8>();
    });
    LoadRows<GlyphSnap>(CHAR_SEL_CLASS_GLYPH, guid, playerClass, archive.Glyphs, [](Field* fields, GlyphSnap& row)
    {
        row.Spec = fields[0].Get<uint8>();
        for (uint8 i = 0; i < MAX_GLYPH_SLOT_INDEX; ++i)
            row.Glyphs[i] = fields[1 + i].Get<uint16>();
    });
    LoadRows<ActionSnap>(CHAR_SEL_CLASS_ACTION, guid, playerClass, archive.Actions, [](Field* fields, ActionSnap& row)
    {
        row.Spec = fields[0].Get<uint8>();
        row.Button = fields[1].Get<uint8>();
        row.Action = fields[2].Get<uint32>();
        row.Type = fields[3].Get<uint8>();
    });
    LoadRows<SkillSnap>(CHAR_SEL_CLASS_SKILL, guid, playerClass, archive.Skills, [](Field* fields, SkillSnap& row)
    {
        row.Id = fields[0].Get<uint16>();
        row.Value = fields[1].Get<uint16>();
        row.Max = fields[2].Get<uint16>();
    });
    LoadRows<QuestSnap>(CHAR_SEL_CLASS_QUEST, guid, playerClass, archive.Quests, [](Field* fields, QuestSnap& row)
    {
        row.Id = fields[0].Get<uint32>();
        uint8 status = fields[1].Get<uint8>();
        row.Data.Status = status < MAX_QUEST_STATUS ? QuestStatus(status) : QUEST_STATUS_INCOMPLETE;
        row.Data.Explored = fields[2].Get<uint8>() != 0;
        row.Data.Timer = fields[3].Get<uint32>();
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            row.Data.CreatureOrGOCount[i] = fields[4 + i].Get<uint16>();
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            row.Data.ItemCount[i] = fields[8 + i].Get<uint16>();
        row.Data.PlayerCount = fields[14].Get<uint16>();
    });

    return archive;
}

void StoreArchive(uint32 guid, uint8 playerClass, ClassArchive const& archive, uint8 level, uint32 xp, float rest,
    uint8 spec, uint32 levelTime, uint32 dailyBonusAt, std::vector<EquipSnap> const& equipment)
{
    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    AppendClassDelete(trans, CHAR_DEL_CLASS_SPELL_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_TALENT_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_GLYPH_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_ACTION_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_SKILL_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_QUEST_BY_CLASS, guid, playerClass);

    for (SpellSnap const& row : archive.Spells)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_SPELL);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Id);
        stmt->SetData(3, row.SpecMask);
        trans->Append(stmt);
    }

    for (TalentSnap const& row : archive.Talents)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_TALENT);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Id);
        stmt->SetData(3, row.SpecMask);
        trans->Append(stmt);
    }

    for (GlyphSnap const& row : archive.Glyphs)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_GLYPH);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Spec);
        for (uint8 i = 0; i < MAX_GLYPH_SLOT_INDEX; ++i)
            stmt->SetData(3 + i, row.Glyphs[i]);
        trans->Append(stmt);
    }

    for (ActionSnap const& row : archive.Actions)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_ACTION);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Spec);
        stmt->SetData(3, row.Button);
        stmt->SetData(4, row.Action);
        stmt->SetData(5, row.Type);
        trans->Append(stmt);
    }

    for (SkillSnap const& row : archive.Skills)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_SKILL);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Id);
        stmt->SetData(3, row.Value);
        stmt->SetData(4, row.Max);
        trans->Append(stmt);
    }

    for (QuestSnap const& row : archive.Quests)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_QUEST);
        uint8 index = 0;
        stmt->SetData(index++, guid);
        stmt->SetData(index++, playerClass);
        stmt->SetData(index++, row.Id);
        stmt->SetData(index++, uint8(row.Data.Status));
        stmt->SetData(index++, uint8(row.Data.Explored));
        stmt->SetData(index++, row.Data.Timer);
        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            stmt->SetData(index++, row.Data.CreatureOrGOCount[i]);
        for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            stmt->SetData(index++, row.Data.ItemCount[i]);
        stmt->SetData(index, row.Data.PlayerCount);
        trans->Append(stmt);
    }

    AppendClassDelete(trans, CHAR_DEL_CLASS_EQUIPMENT_BY_CLASS, guid, playerClass);
    for (EquipSnap const& row : equipment)
    {
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CLASS_EQUIPMENT);
        stmt->SetData(0, guid);
        stmt->SetData(1, playerClass);
        stmt->SetData(2, row.Slot);
        stmt->SetData(3, row.ItemLow);
        trans->Append(stmt);
    }

    AppendProgress(trans, guid, playerClass, level, xp, rest, spec, levelTime, archive.PetId, dailyBonusAt);
    CharacterDatabase.DirectCommitTransaction(trans);
}

void ConsumeArchive(CharacterDatabaseTransaction trans, uint32 guid, uint8 playerClass)
{
    AppendClassDelete(trans, CHAR_DEL_CLASS_SPELL_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_TALENT_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_GLYPH_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_ACTION_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_SKILL_BY_CLASS, guid, playerClass);
    AppendClassDelete(trans, CHAR_DEL_CLASS_QUEST_BY_CLASS, guid, playerClass);
}

bool ItemFailsClassOrLevel(Player const* player, ItemTemplate const* proto)
{
    if (!proto)
        return false;

    // AllowableClass 0 is "no class restriction" on stored items. CanUseItem treats 0 as a
    // failure, so the unequip predicate stays explicit and does not call CanUseItem.
    bool classFail = proto->AllowableClass && (proto->AllowableClass & player->getClassMask()) == 0;
    bool levelFail = proto->RequiredLevel && player->GetLevel() < proto->RequiredLevel;
    return classFail || levelFail;
}

void ClampSkillToLevel(Player* player, uint16 skillId, uint16 value)
{
    uint16 cap = player->GetMaxSkillValueForLevel();
    if (!value)
        value = 1;
    if (value > cap)
        value = cap;

    player->SetSkill(skillId, 0, value, cap);
}

// floor(highest * ratio), capped. Returns 1 when the boost does not apply.
uint8 LegacyStartLevel(Player const* player)
{
    uint8 highest = player->GetHighestClassLevel();
    uint8 minLevel = sConfigMgr->GetOption<uint8>("Multiclass.LegacyMinLevel", 20);
    if (highest < minLevel)
        return 1;

    float ratio = sConfigMgr->GetOption<float>("Multiclass.LegacyLevelRatio", 0.5f);
    if (ratio <= 0.f)
        return 1;

    uint32 cap = sConfigMgr->GetOption<uint32>("Multiclass.LegacyLevelCap", 60);
    uint32 maxPlayer = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 scaled = uint32(std::floor(double(highest) * double(ratio)));
    if (cap && scaled > cap)
        scaled = cap;
    if (scaled > maxPlayer)
        scaled = maxPlayer;
    if (scaled <= 1)
        return 1;

    return uint8(scaled);
}

void UnequipInvalidGear(Player* player)
{
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!ItemFailsClassOrLevel(player, proto))
            continue;
        if (item->IsNotEmptyBag())
        {
            LOG_ERROR("entities.player", "Player {} ({}) kept non-empty bag {} in slot {} after class change",
                player->GetName(), player->GetGUID().ToString(), item->GetEntry(), uint32(slot));
            continue;
        }

        ItemPosCountVec dest;
        if (player->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) == EQUIP_ERR_OK)
        {
            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(dest, item, true);
            continue;
        }

        player->MoveItemFromInventory(INVENTORY_SLOT_BAG_0, slot, true);
        CharacterDatabaseTransaction mailTrans = CharacterDatabase.BeginTransaction();
        item->DeleteFromInventoryDB(mailTrans);
        item->SaveToDB(mailTrans);
        std::string subject = player->GetSession()->GetAcoreString(LANG_NOT_EQUIPPED_ITEM);
        MailDraft(subject, "This item cannot be equipped by your current class and level.")
            .AddItem(item)
            .SendMailTo(mailTrans, player, MailSender(player, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_COPIED);
        CharacterDatabase.CommitTransaction(mailTrans);
    }
}

void RestoreRememberedEquipment(Player* player, uint8 playerClass)
{
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_EQUIPMENT);
    stmt->SetData(0, player->GetGUID().GetCounter());
    stmt->SetData(1, playerClass);
    PreparedQueryResult result = CharacterDatabase.Query(stmt);
    if (!result)
        return;

    bool loading = player->GetSession() && player->GetSession()->PlayerLoading();
    do
    {
        Field* fields = result->Fetch();
        uint8 slot = fields[0].Get<uint8>();
        uint32 itemLow = fields[1].Get<uint32>();
        if (slot >= EQUIPMENT_SLOT_END)
            continue;

        Item* item = player->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(itemLow));
        if (!item)
            continue;
        if (item->GetBagSlot() == INVENTORY_SLOT_BAG_0 && item->GetSlot() == slot)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || ItemFailsClassOrLevel(player, proto))
            continue;

        Item* occupied = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (occupied)
        {
            if (occupied->IsNotEmptyBag())
                continue;

            ItemPosCountVec bagDest;
            if (player->CanStoreItem(NULL_BAG, NULL_SLOT, bagDest, occupied, false) != EQUIP_ERR_OK)
                continue;

            player->RemoveItem(INVENTORY_SLOT_BAG_0, slot, true);
            player->StoreItem(bagDest, occupied, true);
        }

        uint16 dest = 0;
        if (player->CanEquipItem(slot, dest, item, false, !loading) != EQUIP_ERR_OK)
            continue;

        player->RemoveItem(item->GetBagSlot(), item->GetSlot(), true);
        player->EquipItem(dest, item, true);
    } while (result->NextRow());

    player->AutoUnequipOffhandIfNeed();
}
}

// Fresh classes learn playercreateinfo skills, then level-scaled weapon/armor/class skills are raised to
// level * 5 so learnSkillRewardedSpells grants ranks for the current level. LearnCustomSpells runs when
// CONFIG_START_CUSTOM_SPELLS is set. learnQuestRewardedSpells is intentionally not called: it would
// teach the previous class's quest rewards (charger, dreadsteed, death gate). Action bars are not copied
// from playercreateinfo_action.
// A class with no progress row is created with one GiveLevel(legacyLevel) call, the same absolute grant
// .character level uses. That keeps talent points and skills on the normal level-up path. Mail and
// recruit-a-friend inside GiveLevel run only when the character-wide maximum increases, and reach-level
// criteria keep the highest counter, so a lower legacy level does not resend rewards or rewind achievements.
// A returning class is restored with SetLevel + InitStatsForLevel and never GiveLevel.
// XP is clamped below PLAYER_NEXT_LEVEL_XP. Rest and level play time move with the class.
// Equipped item guids are stored per class and put back when the item is still owned and passes
// AllowableClass and RequiredLevel. Anything else goes to bags, then mail.
// The switch row is kept with target_class 0 so last_switch_at can enforce the cooldown.
// Death knights use the same fresh and legacy level path as every other class. They are not
// created at the heroic start level and they are not sent to Ebon Hold. Entering grants Death
// Gate and Runeforging, allocates runes, and selects runic power. Leaving frees the rune object.
// A character already on map 609 is moved to the faction capital first, without a far teleport
// and without touching the hearthstone, so the save that wraps this call still persists.
bool Player::ChangeClass(uint8 newClass, CharacterDatabaseTransaction trans)
{
    uint8 oldClass = getClass();
    if (!newClass || newClass == oldClass)
        return false;

    if ((oldClass == CLASS_DEATH_KNIGHT || newClass == CLASS_DEATH_KNIGHT)
        && !sConfigMgr->GetOption<bool>("Multiclass.AllowDeathKnight", true))
    {
        LOG_ERROR("entities.player", "Player {} ({}) blocked a Death Knight class change ({} -> {})",
            GetName(), GetGUID().ToString(), uint32(oldClass), uint32(newClass));
        return false;
    }

    if (!sObjectMgr->GetPlayerInfo(getRace(true), newClass) || !sChrClassesStore.LookupEntry(newClass))
    {
        LOG_ERROR("entities.player", "Player {} ({}) has no playercreateinfo for race {} class {}",
            GetName(), GetGUID().ToString(), uint32(getRace(true)), uint32(newClass));
        return false;
    }

    uint32 cost = sConfigMgr->GetOption<uint32>("Multiclass.SwitchCost", 0);
    if (cost && !HasEnoughMoney(cost))
    {
        LOG_ERROR("entities.player", "Player {} ({}) cannot pay {} copper to change class",
            GetName(), GetGUID().ToString(), cost);
        return false;
    }

    uint32 guid = GetGUID().GetCounter();
    ProgressSnap incomingProgress = LoadProgress(guid, newClass);
    bool returning = incomingProgress.Present;
    ClassArchive incoming = LoadArchive(guid, newClass, incomingProgress);

    ClassArchive outgoing;
    for (auto const& itr : m_spells)
    {
        if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED || itr.second->State == PLAYERSPELL_TEMPORARY)
            continue;
        if (!IsClassBoundSpell(itr.first, oldClass))
            continue;

        SpellSnap row;
        row.Id = itr.first;
        row.SpecMask = itr.second->specMask ? itr.second->specMask : uint8(SPEC_MASK_ALL);
        outgoing.Spells.push_back(row);
    }

    // 53428 is a trade skill. IsClassBoundSpell keeps it when its skill line is class-bound,
    // and skips it otherwise. Remember it either way so the snapshot does not drop it.
    if (oldClass == CLASS_DEATH_KNIGHT)
    {
        uint32 const dkSpells[] = { SPELL_DEATH_GATE, SPELL_RUNEFORGING };
        for (uint32 spellId : dkSpells)
        {
            if (!HasSpell(spellId))
                continue;

            uint8 specMask = SPEC_MASK_ALL;
            auto itr = m_spells.find(spellId);
            if (itr != m_spells.end() && itr->second && itr->second->specMask)
                specMask = itr->second->specMask;
            RememberSpell(outgoing, spellId, specMask);
        }
    }

    for (auto const& itr : m_talents)
    {
        if (!itr.second || itr.second->State == PLAYERSPELL_REMOVED)
            continue;

        TalentSnap row;
        row.Id = itr.first;
        row.SpecMask = itr.second->specMask;
        outgoing.Talents.push_back(row);
    }

    for (uint8 spec = 0; spec < MAX_TALENT_SPECS; ++spec)
    {
        GlyphSnap row;
        row.Spec = spec;
        for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
            row.Glyphs[slot] = uint16(m_Glyphs[spec][slot]);
        outgoing.Glyphs.push_back(row);
    }

    for (auto const& itr : m_actionButtons)
    {
        if (itr.second.uState == ACTIONBUTTON_DELETED)
            continue;

        ActionSnap row;
        row.Spec = m_activeSpec;
        row.Button = itr.first;
        row.Action = itr.second.GetAction();
        row.Type = uint8(itr.second.GetType());
        outgoing.Actions.push_back(row);
    }

    for (uint8 spec = 0; spec < GetSpecsCount(); ++spec)
    {
        if (spec == m_activeSpec)
            continue;

        CharacterDatabasePreparedStatement* stmt =
            CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_ACTIONS_SPEC);
        stmt->SetData(0, GetGUID().GetRawValue());
        stmt->SetData(1, spec);
        if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        {
            do
            {
                Field* fields = result->Fetch();
                ActionSnap row;
                row.Spec = spec;
                row.Button = fields[0].Get<uint8>();
                row.Action = fields[1].Get<uint32>();
                row.Type = fields[2].Get<uint8>();
                outgoing.Actions.push_back(row);
            } while (result->NextRow());
        }
    }

    for (auto const& itr : mSkillStatus)
    {
        if (itr.second.uState == SKILL_DELETED || !IsClassSkill(itr.first))
            continue;

        SkillSnap row;
        row.Id = uint16(itr.first);
        row.Value = GetPureSkillValue(itr.first);
        row.Max = GetPureMaxSkillValue(itr.first);
        outgoing.Skills.push_back(row);
    }

    uint32 newClassMask = 1u << (newClass - 1);
    for (auto const& itr : m_QuestStatus)
    {
        if (itr.second.Status == QUEST_STATUS_NONE || itr.second.Status == QUEST_STATUS_REWARDED)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(itr.first);
        if (!quest)
            continue;

        uint32 required = quest->GetRequiredClasses();
        if (!required || (required & newClassMask) != 0)
            continue;

        QuestSnap row;
        row.Id = itr.first;
        row.Data = itr.second;
        outgoing.Quests.push_back(row);
    }

    if (GetPetStable() && GetPetStable()->CurrentPet)
        outgoing.PetId = GetPetStable()->CurrentPet->PetNumber;

    std::vector<EquipSnap> equipped;
    for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
    {
        Item* item = GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (!item)
            continue;

        EquipSnap row;
        row.Slot = slot;
        row.ItemLow = item->GetGUID().GetCounter();
        equipped.push_back(row);
    }

    StoreArchive(guid, oldClass, outgoing, GetLevel(), GetUInt32Value(PLAYER_XP), GetRestBonus(), GetActiveSpec(),
        GetLevelPlayedTime(), m_classDailyBonusAt, equipped);
    // The old class row is committed. Legacy level and catch-up both read this cache.
    RefreshHighestClassLevel();

    // Map 609 is the Scarlet Enclave, including the chapter phases on that map (phase bit 128).
    // There is no second starting-zone map. SaveToDB skips the write while a far teleport is in
    // progress, and this runs inside that save, so the capital is written onto the player directly.
    // Faction-change uses these same coordinates. The hearthstone is left alone.
    // Switching in while still on 609 would also make CalculateTalentsPoints use level - 55.
    if (GetMapId() == MAP_EBON_HOLD)
    {
        RemoveAurasByType(SPELL_AURA_PHASE);

        bool alliance = GetTeamId(true) == TEAM_ALLIANCE;
        uint32 mapId = alliance ? uint32(MAP_EASTERN_KINGDOMS) : uint32(MAP_KALIMDOR);
        float x = alliance ? -8867.68f : 1633.33f;
        float y = alliance ? 673.373f : -4439.11f;
        float z = alliance ? 97.9034f : 15.7588f;
        uint32 zoneId = alliance ? uint32(AREA_STORMWIND_CITY) : uint32(AREA_ORGRIMMAR);

        if (!IsInWorld())
        {
            Map* capital = sMapMgr->CreateMap(mapId, this);
            if (capital)
            {
                ResetMap();
                SetMap(capital);
            }
            else
            {
                LOG_ERROR("entities.player", "Player {} ({}) could not create map {} while leaving Ebon Hold",
                    GetName(), GetGUID().ToString(), mapId);
                SetLocationMapId(mapId);
            }
        }
        else
            SetLocationMapId(mapId);

        SetLocationInstanceId(0);
        Relocate(x, y, z, 0.0f);
        if (!IsInWorld() && FindMap() && FindMap()->GetId() == mapId)
            UpdatePositionData();
        else
        {
            _zoneId = zoneId;
            _areaId = zoneId;
            _updatePositionData = false;
        }

        if (!IsGameMaster() && GetPhaseMask() != PHASEMASK_NORMAL)
            SetPhaseMask(PHASEMASK_NORMAL, false);

        LOG_INFO("entities.player", "Player {} ({}) left Ebon Hold for map {} during a class change",
            GetName(), GetGUID().ToString(), GetMapId());
    }

    RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
    // InitStatsForLevel clears the mount display. Drop the aura too so a low level cannot stay mounted.
    RemoveAurasByType(SPELL_AURA_MOUNTED);
    uint32 oldFamily = SpellFamilyForClass(oldClass);
    if (oldFamily != SPELLFAMILY_GENERIC)
    {
        RemoveAppliedAuras([oldFamily](AuraApplication const* aurApp)
        {
            SpellInfo const* info = aurApp->GetBase()->GetSpellInfo();
            return info && info->SpellFamilyName == oldFamily;
        });
    }

    for (uint8 spec = 0; spec < MAX_TALENT_SPECS; ++spec)
    {
        for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
        {
            if (uint32 glyph = m_Glyphs[spec][slot])
                if (GlyphPropertiesEntry const* entry = sGlyphPropertiesStore.LookupEntry(glyph))
                    RemoveAurasDueToSpell(entry->SpellId);
            m_Glyphs[spec][slot] = 0;
        }
    }
    SetNeedToSaveGlyphs(true);

    std::vector<uint32> talentIds;
    for (auto const& itr : m_talents)
        if (itr.second && itr.second->State != PLAYERSPELL_REMOVED)
            talentIds.push_back(itr.first);

    for (uint32 spellId : talentIds)
    {
        _removeTalentAurasAndSpells(spellId);
        PlayerTalent const* talent = m_talents[spellId];
        TalentEntry const* talentInfo = talent ? sTalentStore.LookupEntry(talent->talentID) : nullptr;
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
        if (talentInfo && talentInfo->addToSpellBook && spellInfo && !spellInfo->HasAttribute(SPELL_ATTR0_PASSIVE)
            && !spellInfo->HasEffect(SPELL_EFFECT_LEARN_SPELL))
            removeSpell(spellId, SPEC_MASK_ALL, false);

        _removeTalent(spellId, SPEC_MASK_ALL);
    }
    m_usedTalentCount = 0;

    std::vector<uint32> spellIds;
    for (SpellSnap const& row : outgoing.Spells)
        spellIds.push_back(row.Id);
    for (uint32 spellId : spellIds)
        removeSpell(spellId, SPEC_MASK_ALL, false);

    for (SkillSnap const& row : outgoing.Skills)
        if (HasSkill(row.Id))
            SetSkill(row.Id, 0, 0, 0);

    for (QuestSnap const& row : outgoing.Quests)
    {
        uint16 slot = FindQuestSlot(row.Id);
        if (slot < MAX_QUEST_LOG_SIZE)
            SetQuestSlot(slot, 0);
        RemoveTimedQuest(row.Id);
        RemoveActiveQuest(row.Id, false);
    }

    uint32 parkedPetId = 0;
    if (PetStable* stable = GetPetStable())
    {
        if (stable->CurrentPet)
        {
            parkedPetId = stable->CurrentPet->PetNumber;
            stable->CurrentPet->Class = oldClass;
            stable->UnslottedPets.push_back(std::move(*stable->CurrentPet));
            stable->CurrentPet.reset();
        }
    }

    // GiveLevel and InitStatsForLevel sync a live pet to the owner level. Dismiss first so a
    // hunter pet is saved at the old class level instead of the level being restored.
    if (GetPet())
        RemovePet(nullptr, PET_SAVE_NOT_IN_SLOT);

    SetByteValue(UNIT_FIELD_BYTES_0, 1, newClass);

    // InitRunes allocates with no free of an existing object, and it returns without freeing
    // when the class is not a death knight. The destructor deletes m_runes, so this owns it.
    // Cooldowns are not stored. A returning death knight gets six ready runes.
    if (newClass == CLASS_DEATH_KNIGHT)
    {
        delete m_runes;
        m_runes = nullptr;
        InitRunes();
    }
    else if (m_runes)
    {
        delete m_runes;
        m_runes = nullptr;
        for (uint8 i = 0; i < NUM_RUNE_TYPES; ++i)
            SetFloatValue(PLAYER_RUNE_REGEN_1 + i, 0.0f);
    }

    uint8 restoredSpec = returning ? incomingProgress.Spec : GetActiveSpec();
    if (restoredSpec >= GetSpecsCount())
        restoredSpec = 0;
    SetActiveSpec(restoredSpec);
    SetByteValue(PLAYER_FIELD_BYTES, 2, restoredSpec);

    uint8 maxPlayerLevel = uint8(sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL));
    uint8 targetLevel = returning ? incomingProgress.Level : LegacyStartLevel(this);
    uint32 targetXp = returning ? incomingProgress.Xp : 0;
    float targetRest = returning ? incomingProgress.Rest : 0.f;
    uint32 targetLevelTime = returning ? incomingProgress.LevelTime : 0;
    // One grant per character per day. A class row may hold an older stamp.
    if (incomingProgress.DailyBonusAt > m_classDailyBonusAt)
        m_classDailyBonusAt = incomingProgress.DailyBonusAt;
    if (!targetLevel)
        targetLevel = 1;
    if (targetLevel > maxPlayerLevel)
        targetLevel = maxPlayerLevel;

    // InitStatsForLevel clears ghost and GM flags. Put them back or a dead character is saved alive.
    bool ghost = HasPlayerFlag(PLAYER_FLAGS_GHOST);
    bool gamemaster = HasPlayerFlag(PLAYER_FLAGS_GM);
    DeathState deathState = getDeathState();
    uint32 playerBytes2 = GetUInt32Value(PLAYER_FIELD_BYTES2);

    // One absolute GiveLevel, matching .character level. Not used for a restore.
    if (!returning && targetLevel > 1 && GetLevel() != targetLevel)
        GiveLevel(targetLevel);
    if (GetLevel() != targetLevel)
        SetLevel(targetLevel, false);

    InitStatsForLevel(true);
    if (newClass == CLASS_DEATH_KNIGHT)
    {
        if (getPowerType() != POWER_RUNIC_POWER)
            setPowerType(POWER_RUNIC_POWER);
        SetMaxPower(POWER_RUNE, 8);
        SetPower(POWER_RUNE, 8);
        SetMaxPower(POWER_RUNIC_POWER, 1000);
    }
    SetUInt32Value(PLAYER_FIELD_BYTES2, playerBytes2);
    if (gamemaster)
        SetPlayerFlag(PLAYER_FLAGS_GM);
    if (ghost)
    {
        SetPlayerFlag(PLAYER_FLAGS_GHOST);
        m_deathState = deathState;
        AddUnitState(UNIT_STATE_ISOLATED);
    }

    uint32 nextXp = GetUInt32Value(PLAYER_NEXT_LEVEL_XP);
    if (!nextXp || targetXp >= nextXp)
        targetXp = nextXp ? nextXp - 1 : 0;
    SetUInt32Value(PLAYER_XP, targetXp);
    SetRestBonus(targetRest);
    m_Played_time[PLAYED_TIME_LEVEL] = targetLevelTime;
    InitGlyphsForLevel();

    std::unordered_set<uint32> spellsBeforeRestore;
    for (auto const& itr : m_spells)
        spellsBeforeRestore.insert(itr.first);
    std::unordered_set<uint32> talentsBeforeRestore;
    for (auto const& itr : m_talents)
        talentsBeforeRestore.insert(itr.first);

    if (returning)
    {
        for (SkillSnap const& row : incoming.Skills)
            ClampSkillToLevel(this, row.Id, row.Value);
        LearnDefaultSkills();
        for (SpellSnap const& row : incoming.Spells)
            addSpell(row.Id, row.SpecMask ? row.SpecMask : uint8(SPEC_MASK_ALL), true);
    }
    else
    {
        // See the comment on ChangeClass. Raising SKILL_RANGE_LEVEL skills teaches spell ranks for this level.
        LearnDefaultSkills();
        uint16 maxSkill = GetMaxSkillValueForLevel();
        std::vector<uint32> skillIds;
        for (auto const& itr : mSkillStatus)
        {
            if (itr.second.uState == SKILL_DELETED || !IsClassSkill(itr.first))
                continue;
            SkillRaceClassInfoEntry const* rc = GetSkillRaceClassInfo(itr.first, getRace(), getClass());
            if (!rc || GetSkillRangeType(rc) != SKILL_RANGE_LEVEL)
                continue;
            skillIds.push_back(itr.first);
        }
        for (uint32 skillId : skillIds)
            SetSkill(uint16(skillId), GetSkillStep(uint16(skillId)), maxSkill, maxSkill);
        LearnCustomSpells();
    }

    if (newClass == CLASS_DEATH_KNIGHT)
    {
        // Granted on a fresh death knight and again when a snapshot from before this phase
        // does not contain them. The four Ebon Hold leave checks (TeleportTo, group and solo
        // battleground join, and the sidecar join check) all require 50977.
        if (!HasSpell(SPELL_DEATH_GATE))
            addSpell(SPELL_DEATH_GATE, SPEC_MASK_ALL, true);
        if (!HasSpell(SPELL_RUNEFORGING))
            addSpell(SPELL_RUNEFORGING, SPEC_MASK_ALL, true);

        InitTaxiNodesForLevel();
        for (uint8 field = 0; field < TaxiMaskSize; ++field)
        {
            uint32 bits = sDeathKnightTaxiNodesMask[field];
            for (uint8 bit = 0; bit < 32; ++bit)
                if (bits & (1u << bit))
                    m_taxi.SetTaximaskNode(uint32(field) * 32 + bit + 1);
        }
    }

    UpdateSkillsForLevel();

    for (TalentSnap const& row : incoming.Talents)
        if (row.SpecMask)
            addTalent(row.Id, row.SpecMask, 0);

    InitTalentForLevel();

    for (GlyphSnap const& row : incoming.Glyphs)
    {
        if (row.Spec >= MAX_TALENT_SPECS)
            continue;
        for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
        {
            if (row.Spec == GetActiveSpec())
                SetGlyph(slot, row.Glyphs[slot], false);
            else
                m_Glyphs[row.Spec][slot] = row.Glyphs[slot];
        }
    }
    SetNeedToSaveGlyphs(true);
    for (uint8 slot = 0; slot < MAX_GLYPH_SLOT_INDEX; ++slot)
    {
        if (uint32 glyph = m_Glyphs[GetActiveSpec()][slot])
            if (GlyphPropertiesEntry const* entry = sGlyphPropertiesStore.LookupEntry(glyph))
            {
                TriggerCastFlags flags = TriggerCastFlags(TRIGGERED_FULL_MASK &
                    ~(TRIGGERED_IGNORE_SHAPESHIFT | TRIGGERED_IGNORE_CASTER_AURASTATE));
                CastSpell(this, entry->SpellId, flags);
            }
    }

    std::vector<ActionSnap> liveBars = returning ? incoming.Actions : outgoing.Actions;
    m_actionButtons.clear();
    CharacterDatabasePreparedStatement* actionDelete = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_ACTION);
    actionDelete->SetData(0, GetGUID().GetRawValue());
    trans->Append(actionDelete);
    for (ActionSnap const& row : liveBars)
    {
        if (!returning && row.Type == ACTION_BUTTON_SPELL)
            continue;
        if (row.Spec == GetActiveSpec())
            addActionButton(row.Button, row.Action, row.Type);
        else
        {
            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_CHAR_ACTION);
            stmt->SetData(0, GetGUID().GetRawValue());
            stmt->SetData(1, row.Spec);
            stmt->SetData(2, row.Button);
            stmt->SetData(3, row.Action);
            stmt->SetData(4, row.Type);
            trans->Append(stmt);
        }
    }

    for (QuestSnap const& row : incoming.Quests)
    {
        m_QuestStatus[row.Id] = row.Data;
        m_QuestStatusSave[row.Id] = true;
        if (row.Data.Timer)
            AddTimedQuest(row.Id);

        uint16 slot = FindQuestSlot(0);
        if (slot >= MAX_QUEST_LOG_SIZE)
            continue;

        uint32 slotTimer = 0;
        if (row.Data.Timer)
            slotTimer = uint32(GameTime::GetGameTime().count() + row.Data.Timer / IN_MILLISECONDS);
        SetQuestSlot(slot, row.Id, slotTimer);
        if (row.Data.Status == QUEST_STATUS_COMPLETE)
            SetQuestSlotState(slot, QUEST_STATE_COMPLETE);
        else if (row.Data.Status == QUEST_STATUS_FAILED)
            SetQuestSlotState(slot, QUEST_STATE_FAIL);

        for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (row.Data.CreatureOrGOCount[i])
                SetQuestSlotCounter(slot, i, row.Data.CreatureOrGOCount[i]);
        if (row.Data.PlayerCount)
            SetQuestSlotCounter(slot, QUEST_PVP_KILL_SLOT, row.Data.PlayerCount);
    }

    if (returning && incoming.PetId)
    {
        if (PetStable* stable = GetPetStable())
        {
            if (!stable->CurrentPet || stable->CurrentPet->PetNumber != incoming.PetId)
            {
                for (auto itr = stable->UnslottedPets.begin(); itr != stable->UnslottedPets.end(); ++itr)
                {
                    if (itr->PetNumber != incoming.PetId)
                        continue;
                    stable->CurrentPet = std::move(*itr);
                    stable->UnslottedPets.erase(itr);
                    break;
                }
            }
        }
    }

    if (newClass == CLASS_DEATH_KNIGHT)
    {
        if (returning && incoming.PetId)
            SetShowDKPet(true);
    }
    else if (oldClass == CLASS_DEATH_KNIGHT)
        SetShowDKPet(false);

    if (newClass != CLASS_WARRIOR && CanTitanGrip())
        SetCanTitanGrip(false);
    if (!HasSpell(674) && CanDualWield())
        SetCanDualWield(false);
    AutoUnequipOffhandIfNeed();
    UnequipInvalidGear(this);
    RestoreRememberedEquipment(this, newClass);

    SetPower(getPowerType(), GetMaxPower(getPowerType()));

    if (isBeingLoaded())
    {
        for (auto& itr : m_spells)
            if (itr.second && itr.second->State == PLAYERSPELL_UNCHANGED && !spellsBeforeRestore.contains(itr.first))
                itr.second->State = PLAYERSPELL_NEW;

        for (auto& itr : m_talents)
            if (itr.second && itr.second->State == PLAYERSPELL_UNCHANGED && !talentsBeforeRestore.contains(itr.first))
                itr.second->State = PLAYERSPELL_NEW;
    }

    if (cost)
        ModifyMoney(-int32(cost));

    RemoveAtLoginFlag(AT_LOGIN_CHANGE_CLASS, false);

    sCharacterCache->UpdateCharacterClass(GetGUID(), newClass);
    if (uint32 guildId = GetGuildId())
        if (Guild* guild = sGuildMgr->GetGuildById(guildId))
            if (Guild::Member* member = guild->GetMember(GetGUID()))
                member->SetStats(this);

    ConsumeArchive(trans, guid, newClass);
    AppendProgress(trans, guid, newClass, GetLevel(), GetUInt32Value(PLAYER_XP), GetRestBonus(), GetActiveSpec(),
        GetLevelPlayedTime(), returning ? incoming.PetId : 0, m_classDailyBonusAt);
    AppendPetSlot(trans, guid, parkedPetId, uint8(PET_SAVE_NOT_IN_SLOT), oldClass);
    AppendPetSlot(trans, guid, returning ? incoming.PetId : 0, uint8(PET_SAVE_AS_CURRENT), newClass);

    CharacterDatabasePreparedStatement* done = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CLASS_SWITCH_COMPLETE);
    done->SetData(0, uint32(GameTime::GetGameTime().count()));
    done->SetData(1, guid);
    trans->Append(done);

    LOG_INFO("entities.player", "Player {} ({}) changed class from {} to {}",
        GetName(), GetGUID().ToString(), uint32(oldClass), uint32(newClass));
    ApplyRetainedSpells();
    return true;
}

void Player::ApplyPendingClassChange(CharacterDatabaseTransaction trans)
{
    if (!HasAtLoginFlag(AT_LOGIN_CHANGE_CLASS) || !trans)
        return;

    auto clearPending = [this, trans]()
    {
        RemoveAtLoginFlag(AT_LOGIN_CHANGE_CLASS, false);
        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CLASS_SWITCH_CLEAR);
        stmt->SetData(0, GetGUID().GetCounter());
        trans->Append(stmt);
    };

    if (!sConfigMgr->GetOption<bool>("Multiclass.Enable", true))
    {
        clearPending();
        LOG_INFO("entities.player", "Player {} ({}) skipped a class change because multiclass is disabled",
            GetName(), GetGUID().ToString());
        return;
    }

    uint8 targetClass = 0;
    CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_SWITCH);
    stmt->SetData(0, GetGUID().GetCounter());
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
        targetClass = result->Fetch()[0].Get<uint8>();

    if (!targetClass || !ChangeClass(targetClass, trans))
    {
        clearPending();
        if (GetSession())
            ChatHandler(GetSession()).SendSysMessage("Class change was cancelled.");
        LOG_INFO("entities.player", "Player {} ({}) cancelled pending class change to {}",
            GetName(), GetGUID().ToString(), uint32(targetClass));
    }
}
