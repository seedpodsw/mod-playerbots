/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "GmWatchHelper.h"
#include "BattleGroundTactics.h"
#include "Chat.h"
#include "GuildTaskMgr.h"
#include "ObjectAccessor.h"
#include "PerfMonitor.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

namespace
{
    bool EnsureSelfControl(ChatHandler* handler, Player* player)
    {
        if (player->isPossessing())
        {
            handler->SendSysMessage("You are possessing something. Use .release or .unpossess first.");
            return false;
        }

        if (player->m_mover != player)
        {
            handler->SendSysMessage("You must control yourself (dismount / leave vehicle first).");
            return false;
        }

        return true;
    }

    Player* ResolveOnlinePlayer(ChatHandler* handler, char const* args, char const* commandName)
    {
        char const* trimmed = args;
        if (trimmed)
        {
            while (*trimmed == ' ')
                ++trimmed;

            if (!*trimmed)
                trimmed = nullptr;
        }

        if (!trimmed)
        {
            if (Player* selected = handler->getSelectedPlayer())
                return selected;

            handler->PSendSysMessage("No player selected. Usage: .{} <player name>", commandName);
            return nullptr;
        }

        char nameBuf[512];
        if (strlen(trimmed) >= sizeof(nameBuf))
        {
            handler->SendSysMessage("Player name too long.");
            return nullptr;
        }

        strcpy(nameBuf, trimmed);

        Player* target = nullptr;
        std::string playerName;
        if (!handler->extractPlayerTarget(nameBuf, &target, nullptr, &playerName))
            return nullptr;

        if (target)
            return target;

        handler->PSendSysMessage("Player '{}' not found or not online.",
            playerName.empty() ? trimmed : playerName.c_str());
        return nullptr;
    }
}

class playerbots_commandscript : public CommandScript
{
public:
    playerbots_commandscript() : CommandScript("playerbots_commandscript") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable playerbotsDebugCommandTable = {
            {"bg", HandleDebugBGCommand, SEC_GAMEMASTER, Console::Yes},
        };

        static ChatCommandTable playerbotsAccountCommandTable = {
            {"setKey", HandleSetSecurityKeyCommand, SEC_PLAYER, Console::No},
            {"link", HandleLinkAccountCommand, SEC_PLAYER, Console::No},
            {"linkedAccounts", HandleViewLinkedAccountsCommand, SEC_PLAYER, Console::No},
            {"unlink", HandleUnlinkAccountCommand, SEC_PLAYER, Console::No},
        };

        static ChatCommandTable playerbotsCommandTable = {
            {"bot", HandlePlayerbotCommand, SEC_PLAYER, Console::No},
            {"gtask", HandleGuildTaskCommand, SEC_GAMEMASTER, Console::Yes},
            {"pmon", HandlePerfMonCommand, SEC_GAMEMASTER, Console::Yes},
            {"rndbot", HandleRandomPlayerbotCommand, SEC_GAMEMASTER, Console::Yes},
            {"debug", playerbotsDebugCommandTable},
            {"account", playerbotsAccountCommandTable},
        };

        static ChatCommandTable commandTable = {
            {"watch", HandleWatchCommand, SEC_GAMEMASTER, Console::No},
            {"unwatch", HandleUnwatchCommand, SEC_GAMEMASTER, Console::No},
            {"takeover", HandleTakeoverCommand, SEC_GAMEMASTER, Console::No},
            {"release", HandleReleaseCommand, SEC_GAMEMASTER, Console::No},
            {"playerbots", playerbotsCommandTable},
        };

        return commandTable;
    }

    static bool HandlePlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, args);
    }

    static bool HandleRandomPlayerbotCommand(ChatHandler* handler, char const* args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, args);
    }

    static bool HandleGuildTaskCommand(ChatHandler* handler, char const* args)
    {
        return GuildTaskMgr::HandleConsoleCommand(handler, args);
    }

    static bool HandlePerfMonCommand(ChatHandler* /*handler*/, char const* args)
    {
        if (!strcmp(args, "reset"))
        {
            sPerfMonitor.Reset();
            return true;
        }

        if (!strcmp(args, "tick"))
        {
            sPerfMonitor.PrintStats(true, false);
            return true;
        }

        if (!strcmp(args, "stack"))
        {
            sPerfMonitor.PrintStats(false, true);
            return true;
        }

        if (!strcmp(args, "toggle"))
        {
            sPlayerbotAIConfig.perfMonEnabled = !sPlayerbotAIConfig.perfMonEnabled;
            if (sPlayerbotAIConfig.perfMonEnabled)
                LOG_INFO("playerbots", "Performance monitor enabled");
            else
                LOG_INFO("playerbots", "Performance monitor disabled");
            return true;
        }

        sPerfMonitor.PrintStats();
        return true;
    }

    static bool HandleDebugBGCommand(ChatHandler* handler, char const* args)
    {
        return BGTactics::HandleConsoleCommand(handler, args);
    }

    static bool HandleSetSecurityKeyCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
        {
            handler->PSendSysMessage("Usage: .playerbots account setKey <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();
        std::string key = args;

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleSetSecurityKeyCommand(player, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleLinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        char* key = strtok(nullptr, " ");

        if (!accountName || !key)
        {
            handler->PSendSysMessage("Usage: .playerbots account link <accountName> <securityKey>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleLinkAccountCommand(player, accountName, key);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleViewLinkedAccountsCommand(ChatHandler* handler, char const* /*args*/)
    {
        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleViewLinkedAccountsCommand(player);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleUnlinkAccountCommand(ChatHandler* handler, char const* args)
    {
        if (!args || !*args)
            return false;

        char* accountName = strtok((char*)args, " ");
        if (!accountName)
        {
            handler->PSendSysMessage("Usage: .playerbots account unlink <accountName>");
            return false;
        }

        Player* player = handler->GetSession()->GetPlayer();

        PlayerbotMgr* mgr = PlayerbotsMgr::instance().GetPlayerbotMgr(player);
        if (mgr)
        {
            mgr->HandleUnlinkAccountCommand(player, accountName);
            return true;
        }
        else
        {
            handler->PSendSysMessage("PlayerbotMgr instance not found.");
            return false;
        }
    }

    static bool HandleWatchCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!EnsureSelfControl(handler, player))
            return false;

        Player* target = ResolveOnlinePlayer(handler, args, "watch");
        if (!target)
            return false;

        if (target->GetGUID() == player->GetGUID())
        {
            handler->SendSysMessage("Can't watch yourself.");
            return false;
        }

        return GmWatchHelper::BeginWatch(handler, player, target);
    }

    static bool HandleUnwatchCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        GmWatchHelper::EndWatch(player, true);
        handler->SendSysMessage("Stopped watching and returned to your previous location.");
        return true;
    }

    static bool HandleTakeoverCommand(ChatHandler* handler, char const* args)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (!EnsureSelfControl(handler, player))
            return false;

        Player* target = ResolveOnlinePlayer(handler, args, "takeover");
        if (!target)
            return false;

        if (target->GetGUID() == player->GetGUID())
        {
            handler->SendSysMessage("Can't take over yourself.");
            return false;
        }

        return GmWatchHelper::BeginTakeover(handler, player, target);
    }

    static bool HandleReleaseCommand(ChatHandler* handler)
    {
        Player* player = handler->GetSession()->GetPlayer();
        GmWatchHelper::EndWatch(player, true);
        handler->SendSysMessage("Released watch/control and returned to your previous location.");
        return true;
    }
};

void AddPlayerbotsCommandscripts() { new playerbots_commandscript(); }
