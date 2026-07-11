/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_GROUPINVITEHELPER_H
#define PLAYERBOTS_GROUPINVITEHELPER_H

#include <string>

class Group;
class Player;
class WorldPacket;

namespace GroupInviteHelper
{
    bool IsRealPlayerInviter(Player* inviter);
    bool IsSameAccountAlt(Player* bot, Player* player);
    bool IsBotOwnedByPlayer(Player* bot, Player* player);

    Player* ResolveGroupInviter(Player* bot, Group* inviteGroup, WorldPacket const* invitePacket);

    // Leaves a stale ambient/conflicting group before a new invite is sent or accepted.
    // Never clears a pending group invite.
    bool LeaveStaleGroupForInvite(Player* bot, Player* inviter);

    // Leaves any group the bot is in that does not include the inviter (world thread).
    bool ForceLeaveConflictingGroup(Player* bot, Player* inviter);

    // Clears stale bot group membership and optionally a dangling pending invite.
    bool PrepareBotForGroupInvite(Player* bot, Player* inviter, bool clearPendingInvite = true);

    void ResetBotAfterGroupDisband(Player* bot);

    // Whisper a human inviter why the bot declined (no-op for bot inviters).
    void WhisperInviteDecline(Player* bot, Player* inviter, std::string const& message);

    // Accept or decline a pending group invite immediately (world thread).
    bool ProcessPendingGroupInvite(Player* bot);

    // Resolve group from GroupMgr without calling Player::SetGroup (unsafe during login).
    Group* LookupBotGroup(Player* bot);

    // Clear dangling Group* only — never assign a group here.
    void ClearStaleBotGroupPointer(Player* bot);
}

#endif
