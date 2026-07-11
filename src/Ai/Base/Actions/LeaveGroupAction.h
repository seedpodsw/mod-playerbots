/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_LEAVEGROUPACTION_H
#define PLAYERBOTS_LEAVEGROUPACTION_H

#include "Action.h"

class Player;
class PlayerbotAI;

class LeaveGroupAction : public Action
{
public:
    LeaveGroupAction(PlayerbotAI* botAI, std::string const name = "leave") : Action(botAI, name) {}

    bool Execute(Event event) override;

    virtual bool Leave(bool leftForProgression = false);
};

class PartyCommandAction : public LeaveGroupAction
{
public:
    PartyCommandAction(PlayerbotAI* botAI) : LeaveGroupAction(botAI, "party command") {}

    bool Execute(Event event) override;
};

class UninviteAction : public LeaveGroupAction
{
public:
    UninviteAction(PlayerbotAI* botAI) : LeaveGroupAction(botAI, "uninvite") {}

    bool Execute(Event event) override;
};

class LeaveFarAwayAction : public LeaveGroupAction
{
public:
    LeaveFarAwayAction(PlayerbotAI* botAI) : LeaveGroupAction(botAI, "leave far away") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class LeaveForProgressionAction : public LeaveGroupAction
{
public:
    LeaveForProgressionAction(PlayerbotAI* botAI) : LeaveGroupAction(botAI, "leave for progression") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class PruneProgressionQuestsAction : public Action
{
public:
    PruneProgressionQuestsAction(PlayerbotAI* botAI) : Action(botAI, "prune progression quests") {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

// Real players release a random bot from its ambient nearby group so they can invite it.
class ReadyForInviteAction : public Action
{
public:
    ReadyForInviteAction(PlayerbotAI* botAI, std::string const name = "ready for invite")
        : Action(botAI, name) {}

    bool Execute(Event event) override;
    bool isUseful() override;
};

class DropGroupAction : public ReadyForInviteAction
{
public:
    DropGroupAction(PlayerbotAI* botAI) : ReadyForInviteAction(botAI, "drop group") {}

    bool Execute(Event event) override;
};

class GroupDisbandedAction : public Action
{
public:
    GroupDisbandedAction(PlayerbotAI* botAI) : Action(botAI, "group disbanded") {}

    bool Execute(Event event) override;
};

#endif
