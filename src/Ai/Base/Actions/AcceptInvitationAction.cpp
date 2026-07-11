/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AcceptInvitationAction.h"

#include "Event.h"
#include "GroupInviteHelper.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotOperations.h"
#include "PlayerbotSecurity.h"
#include "PlayerbotTextMgr.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "Playerbots.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"

bool AcceptInvitationAction::Execute(Event /*event*/)
{
    Group* grp = bot->GetGroupInvite();
    if (!grp)
        return false;

    Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());

    // Decline instead of leaving the invite dangling: an unanswered invite blocks the bot
    // from ever being invited again, and blocks the inviter's pending slot.
    bool accept = inviter != nullptr;
    bool const realPlayerInviter = inviter && GroupInviteHelper::IsRealPlayerInviter(inviter);
    std::string declineMessage;

    if (accept && realPlayerInviter)
    {
        // Real human players always get a join/decline — skip gearscore/level gates.
        if (botAI->IsOpposing(inviter))
        {
            accept = false;
            declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "group_invite_decline_opposing", "You are the enemy.", {});
        }
    }
    else if (accept && inviter && GroupInviteHelper::IsBotOwnedByPlayer(bot, inviter))
    {
        accept = true;
    }
    else if (accept && bot->GetGroup() && !realPlayerInviter)
    {
        accept = false;
        declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "group_invite_decline_in_group", "I'm already in a group.", {});
    }
    else if (accept && !botAI->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
    {
        // CheckLevelFor already whispers the reason to real players.
        accept = false;
    }
    else if (accept && !realPlayerInviter && sRandomPlayerbotMgr.IsRandomBot(bot) &&
             sPlayerbotAIConfig.randomBotGroupNearby && !botAI->HasRealPlayerMaster())
    {
        GrouperType const grouperType = botAI->GetGrouperType();
        if (grouperType == GrouperType::SOLO)
        {
            accept = false;
            declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "group_invite_decline_solo", "Sorry, I prefer to play solo.", {});
        }
        else if (grouperType == GrouperType::MEMBER && sPlayerbotAIConfig.randomBotNearbyMemberJoinChance < 100 &&
                 urand(1, 100) > sPlayerbotAIConfig.randomBotNearbyMemberJoinChance)
        {
            accept = false;
            declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "group_invite_decline_busy", "Sorry, not looking for a group right now.", {});
        }
    }

    if (accept && inviter && !realPlayerInviter && botAI->ShouldDeclineAmbientGroupInvite(inviter))
    {
        accept = false;
        declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "group_invite_decline_cooldown",
            "I just left your group. Give me a few minutes before inviting again.", {});
    }

    if (!accept && inviter && declineMessage.empty() && realPlayerInviter)
    {
        declineMessage = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "group_invite_decline_generic", "Sorry, I can't join your group right now.", {});
    }

    // Never accept/decline inline: invite packets are handled during Map::Update and
    // AddInvite hooks — mutating the group there corrupts GroupReference lists.
    auto answerOp = std::make_unique<GroupAnswerInviteOperation>(bot->GetGUID(), accept, declineMessage);
    PlayerbotWorldThreadProcessor::instance().QueueOperation(std::move(answerOp));

    return accept;
}
