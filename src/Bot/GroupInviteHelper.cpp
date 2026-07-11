/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "GroupInviteHelper.h"
#include "Event.h"

#include "AcceptInvitationAction.h"
#include "CharacterCache.h"
#include "Group.h"
#include "GroupMgr.h"
#include "ObjectAccessor.h"
#include "Opcodes.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "Util.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "PlayerbotTextMgr.h"

namespace GroupInviteHelper
{
bool IsRealPlayerInviter(Player* inviter)
{
    if (!inviter || !inviter->GetSession())
        return false;

    if (!inviter->GetSession()->IsBot())
        return true;

    if (PlayerbotAI* inviterAI = GET_PLAYERBOT_AI(inviter))
        return inviterAI->IsRealPlayer();

    return false;
}

bool IsSameAccountAlt(Player* bot, Player* player)
{
    if (!bot || !player || !bot->GetSession() || !player->GetSession())
        return false;

    if (sRandomPlayerbotMgr.IsRandomBot(bot))
        return false;

    return bot->GetSession()->GetAccountId() == player->GetSession()->GetAccountId();
}

bool IsBotOwnedByPlayer(Player* bot, Player* player)
{
    if (!bot || !player)
        return false;

    if (PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot))
        if (botAI->GetMaster() == player)
            return true;

    return IsSameAccountAlt(bot, player);
}

Player* ResolveGroupInviter(Player* bot, Group* inviteGroup, WorldPacket const* invitePacket)
{
    if (invitePacket && invitePacket->size() > 2)
    {
        WorldPacket packet(*invitePacket);
        packet.rpos(0);

        uint8 invitedFlag = 0;
        std::string inviterName;
        packet >> invitedFlag >> inviterName;

        if (invitedFlag && !inviterName.empty())
        {
            if (normalizePlayerName(inviterName))
            {
                if (Player* invitingPlayer = ObjectAccessor::FindPlayerByName(inviterName, false))
                    return invitingPlayer;
            }
        }
    }

    if (inviteGroup)
    {
        if (Player* leader = ObjectAccessor::FindConnectedPlayer(inviteGroup->GetLeaderGUID()))
            return leader;
    }

    if (bot)
        if (Group* invite = bot->GetGroupInvite())
            if (Player* leader = ObjectAccessor::FindConnectedPlayer(invite->GetLeaderGUID()))
                return leader;

    return nullptr;
}

bool LeaveStaleGroupForInvite(Player* bot, Player* inviter)
{
    if (!bot || !inviter)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    bool const realPlayerInviter = IsRealPlayerInviter(inviter);

    if (!realPlayerInviter && !IsBotOwnedByPlayer(bot, inviter))
        return false;

    return ForceLeaveConflictingGroup(bot, inviter);
}

bool ForceLeaveConflictingGroup(Player* bot, Player* inviter)
{
    if (!bot || !inviter)
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    Group* const botGroup = bot->GetGroup();
    Group* const inviterGroup = inviter->GetGroup();

    if (botGroup && inviterGroup && botGroup == inviterGroup && botGroup->IsMember(inviter->GetGUID()))
        return false;

    bool changed = false;

    if (botGroup)
    {
        Player::RemoveFromGroup(botGroup, bot->GetGUID(), GROUP_REMOVEMETHOD_LEAVE);
        changed = true;
    }

    if (Group* originalGroup = bot->GetOriginalGroup())
    {
        if (!inviterGroup || originalGroup != inviterGroup)
        {
            bot->SetOriginalGroup(nullptr);
            changed = true;
        }
    }

    if (changed)
    {
        botAI->rpgInfo.ChangeToIdle();

        if (sRandomPlayerbotMgr.IsRandomBot(bot) && IsRealPlayerInviter(inviter))
        {
            if (botAI->GetMaster() != inviter)
                botAI->SetMaster(nullptr);
            botAI->ResetStrategies();
        }
        else if (botAI->HasActivePlayerMaster() || botAI->GetMaster() == inviter || IsSameAccountAlt(bot, inviter))
        {
            botAI->ChangeStrategy("-follow,-passive,-stay,-lfg,-bg", BOT_STATE_NON_COMBAT);
            botAI->ResetStrategies(!sRandomPlayerbotMgr.IsRandomBot(bot));
        }
    }

    return changed;
}

bool PrepareBotForGroupInvite(Player* bot, Player* inviter, bool clearPendingInvite)
{
    if (!bot || !inviter)
        return false;

    bool changed = LeaveStaleGroupForInvite(bot, inviter);

    if (clearPendingInvite && bot->GetGroupInvite())
    {
        bot->UninviteFromGroup();
        changed = true;
    }

    return changed;
}

void ResetBotAfterGroupDisband(Player* bot)
{
    if (!bot)
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    if (bot->GetGroupInvite())
        bot->UninviteFromGroup();

    if (bot->GetGroup())
        botAI->LeaveOrDisbandGroup();

    botAI->rpgInfo.ChangeToIdle();

    if (botAI->HasActivePlayerMaster())
    {
        botAI->ChangeStrategy("-follow,-passive,-stay,-lfg,-bg", BOT_STATE_NON_COMBAT);
        botAI->ResetStrategies(!sRandomPlayerbotMgr.IsRandomBot(bot));
    }
}

void WhisperInviteDecline(Player* bot, Player* inviter, std::string const& message)
{
    if (!bot || !inviter || message.empty())
        return;

    if (!IsRealPlayerInviter(inviter))
        return;

    if (!bot->IsInWorld() || !inviter->IsInWorld())
        return;

    bot->Whisper(message, LANG_UNIVERSAL, inviter);
}

bool ProcessPendingGroupInvite(Player* bot)
{
    if (!bot || !bot->GetGroupInvite())
        return false;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return false;

    AcceptInvitationAction accept(botAI);
    return accept.Execute(Event());
}

Group* LookupBotGroup(Player* bot)
{
    if (!bot)
        return nullptr;

    ObjectGuid const groupGuid = sCharacterCache->GetCharacterGroupGuidByGuid(bot->GetGUID());
    if (groupGuid.IsEmpty())
        return nullptr;

    Group* group = sGroupMgr->GetGroupByGUID(groupGuid.GetCounter());
    if (!group || !group->IsMember(bot->GetGUID()))
        return nullptr;

    return group;
}

void ClearStaleBotGroupPointer(Player* bot)
{
    if (!bot || !bot->GetSession() || !bot->GetSession()->IsBot())
        return;

    if (!bot->GetGroup())
        return;

    ObjectGuid const groupGuid = sCharacterCache->GetCharacterGroupGuidByGuid(bot->GetGUID());
    Group* registered = groupGuid.IsEmpty() ? nullptr : sGroupMgr->GetGroupByGUID(groupGuid.GetCounter());

    if (registered && registered->IsMember(bot->GetGUID()))
        return;

    bot->SetGroup(nullptr);
    bot->SetOriginalGroup(nullptr);
}
}  // namespace GroupInviteHelper
