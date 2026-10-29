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

#include "AllBattlegroundScript.h"
#include "Chat.h"
#include "CommandScript.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "PlayerScript.h"
#include "RBAC.h"
#include "StringConvert.h"
#include "World.h"
#include "WorldSession.h"
#ifdef MOD_PLAYERBOTS
#include "PlayerbotsDatabase.h"
#endif

#include <algorithm>
#include <cctype>
#include <string>

using namespace Acore::ChatCommands;

namespace
{
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

Optional<uint8> ParseClassArgument(std::string name)
{
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (name == "warrior")
        return CLASS_WARRIOR;
    if (name == "paladin")
        return CLASS_PALADIN;
    if (name == "hunter")
        return CLASS_HUNTER;
    if (name == "rogue")
        return CLASS_ROGUE;
    if (name == "priest")
        return CLASS_PRIEST;
    if (name == "deathknight" || name == "death_knight" || name == "dk")
        return CLASS_DEATH_KNIGHT;
    if (name == "shaman")
        return CLASS_SHAMAN;
    if (name == "mage")
        return CLASS_MAGE;
    if (name == "warlock")
        return CLASS_WARLOCK;
    if (name == "druid")
        return CLASS_DRUID;

    if (Optional<uint32> id = Acore::StringTo<uint32>(name))
        if (*id && *id <= 255 && sChrClassesStore.LookupEntry(*id))
            return uint8(*id);
    return std::nullopt;
}

Player* RetainCommandPlayer(ChatHandler* handler)
{
    Player* player = handler->getSelectedPlayerOrSelf();
    if (!player)
    {
        handler->SendSysMessage("No player selected.");
        return nullptr;
    }
    if (IsRandomBotGuid(player->GetGUID().GetCounter()) || (player->GetSession() && player->GetSession()->IsBot()))
    {
        handler->SendSysMessage("Bots cannot use retained spells.");
        return nullptr;
    }
    return player;
}
}

class multiclass_commandscript : public CommandScript
{
public:
    multiclass_commandscript() : CommandScript("multiclass_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable retainCommandTable =
        {
            { "list", HandleRetainListCommand, rbac::RBAC_PERM_COMMAND_MULTICLASS_RETAIN_LIST, Console::No },
            { "add", HandleRetainAddCommand, rbac::RBAC_PERM_COMMAND_MULTICLASS_RETAIN_ADD, Console::No },
            { "remove", HandleRetainRemoveCommand, rbac::RBAC_PERM_COMMAND_MULTICLASS_RETAIN_REMOVE, Console::No },
            { "clear", HandleRetainClearCommand, rbac::RBAC_PERM_COMMAND_MULTICLASS_RETAIN_CLEAR, Console::No }
        };

        static ChatCommandTable multiclassCommandTable =
        {
            { "mastery", HandleMulticlassMasteryCommand, rbac::RBAC_PERM_COMMAND_MULTICLASS_MASTERY, Console::Yes },
            { "retain", retainCommandTable }
        };

        static ChatCommandTable commandTable =
        {
            { "multiclass", multiclassCommandTable }
        };
        return commandTable;
    }

    static bool HandleRetainListCommand(ChatHandler* handler)
    {
        Player* player = RetainCommandPlayer(handler);
        if (!player)
            return false;
        player->ListRetainedSpells(handler);
        return true;
    }

    static bool HandleRetainAddCommand(ChatHandler* handler, uint32 spellId, Optional<std::string> first,
        Optional<std::string> second)
    {
        Player* player = RetainCommandPlayer(handler);
        if (!player)
            return false;

        bool synergy = false;
        int32 slot = -1;
        if (first)
        {
            std::string token = *first;
            std::transform(token.begin(), token.end(), token.begin(),
                [](unsigned char c) { return char(std::tolower(c)); });
            if (token == "synergy")
                synergy = true;
            else if (Optional<uint32> parsed = Acore::StringTo<uint32>(token))
            {
                if (*parsed > 255)
                {
                    handler->SendSysMessage("That slot does not exist.");
                    return false;
                }
                slot = int32(*parsed);
            }
            else
            {
                handler->SendSysMessage("Use a slot number or the word synergy.");
                return false;
            }
        }
        if (second)
        {
            if (!synergy)
            {
                handler->SendSysMessage("Use a slot number or the word synergy.");
                return false;
            }
            Optional<uint32> parsed = Acore::StringTo<uint32>(*second);
            if (!parsed || *parsed > 255)
            {
                handler->SendSysMessage("That slot does not exist.");
                return false;
            }
            slot = int32(*parsed);
        }

        return player->AddRetainedSpell(spellId, synergy, slot, handler);
    }

    static bool HandleRetainRemoveCommand(ChatHandler* handler, uint32 spellId)
    {
        Player* player = RetainCommandPlayer(handler);
        if (!player)
            return false;
        return player->RemoveRetainedSpell(spellId, handler);
    }

    static bool HandleRetainClearCommand(ChatHandler* handler, Optional<std::string> className)
    {
        Player* player = RetainCommandPlayer(handler);
        if (!player)
            return false;

        int32 sourceClass = -1;
        if (className)
        {
            Optional<uint8> parsed = ParseClassArgument(*className);
            if (!parsed)
            {
                handler->SendSysMessage("Unknown class. Use an id or warrior, paladin, hunter, rogue, priest, "
                    "deathknight, shaman, mage, warlock, druid.");
                return false;
            }
            sourceClass = *parsed;
        }
        return player->ClearRetainedSpells(sourceClass, handler);
    }

    static bool HandleMulticlassMasteryCommand(ChatHandler* handler, Optional<PlayerIdentifier> player)
    {
        if (!player)
            player = PlayerIdentifier::FromTargetOrSelf(handler);
        if (!player)
            return false;

        if (WorldSession* session = handler->GetSession())
            if (Player* self = session->GetPlayer())
                if (self->GetGUID() != player->GetGUID() && session->GetSecurity() == SEC_PLAYER)
                {
                    handler->SendSysMessage("You can only list your own mastered classes.");
                    return false;
                }

        if (Player* online = player->GetConnectedPlayer())
            if (handler->HasLowerSecurity(online, ObjectGuid::Empty))
                return false;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CLASS_MASTERY_LIST);
        stmt->SetData(0, player->GetGUID().GetCounter());
        PreparedQueryResult result = CharacterDatabase.Query(stmt);
        if (!result)
        {
            handler->SendSysMessage("No class has been mastered.");
            return true;
        }

        LocaleConstant locale = handler->GetSession()
            ? handler->GetSession()->GetSessionDbcLocale()
            : sWorld->GetDefaultDbcLocale();

        handler->SendSysMessage("Mastered classes:");
        do
        {
            Field* fields = result->Fetch();
            uint8 classId = fields[0].Get<uint8>();
            uint8 level = fields[1].Get<uint8>();
            char const* name = nullptr;
            if (ChrClassesEntry const* entry = sChrClassesStore.LookupEntry(classId))
                name = entry->name[locale];
            if (!name || !name[0])
                name = "Unknown";

            handler->PSendSysMessage("{} (level {})", name, uint32(level));
        } while (result->NextRow());

        return true;
    }
};

class RetainLevelPlayerScript : public PlayerScript
{
public:
    RetainLevelPlayerScript() : PlayerScript("RetainLevelPlayerScript", { PLAYERHOOK_ON_LEVEL_CHANGED }) { }

    void OnPlayerLevelChanged(Player* player, uint8 /*oldLevel*/) override
    {
        if (!player || player->HasAtLoginFlag(AT_LOGIN_CHANGE_CLASS))
            return;
        player->ApplyRetainedSpells();
    }
};

class RetainBattlegroundScript : public AllBattlegroundScript
{
public:
    RetainBattlegroundScript() : AllBattlegroundScript("RetainBattlegroundScript",
        {
            ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_ADD_PLAYER,
            ALLBATTLEGROUNDHOOK_ON_BATTLEGROUND_REMOVE_PLAYER_AT_LEAVE
        }) { }

    void OnBattlegroundAddPlayer(Battleground* /*bg*/, Player* player) override
    {
        if (player)
            player->SetRetainActivesSuppressed(true);
    }

    void OnBattlegroundRemovePlayerAtLeave(Battleground* /*bg*/, Player* player) override
    {
        if (player)
            player->SetRetainActivesSuppressed(false);
    }
};

void AddSC_multiclass_commandscript()
{
    new multiclass_commandscript();
    new RetainLevelPlayerScript();
    new RetainBattlegroundScript();
}
