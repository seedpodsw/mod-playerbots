/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AcceptInvitationAction.h"

#include "Event.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "WorldPacket.h"

namespace
{
bool IsRealPlayerInviter(Player* inviter)
{
    if (!inviter)
        return false;

    PlayerbotAI* inviterAI = GET_PLAYERBOT_AI(inviter);
    return !inviterAI || inviterAI->IsRealPlayer();
}
}  // namespace

bool AcceptInvitationAction::Execute(Event /*event*/)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;

    Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());

    // Decline instead of leaving the invite dangling: an unanswered invite blocks the bot
    // from ever being invited again, and blocks the inviter's pending slot.
    bool accept = true;
    if (!inviter)
        accept = false;
    else if (bot->GetGroup() && !IsRealPlayerInviter(inviter))
        accept = false;
    else if (!botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
        accept = false;

    // Group modifications must run on the world thread; the operation re-validates state
    // and performs the post-accept AI setup (master, follow strategies, summon).
    auto answerOp = std::make_unique<GroupAnswerInviteOperation>(bot->GetGUID(), accept);
    PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(answerOp));

    return accept;
}
