/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_QUESTOBJECTIVETARGETVALUE_H
#define PLAYERBOTS_QUESTOBJECTIVETARGETVALUE_H

#include "TargetValue.h"

class PlayerbotAI;
class Unit;

class QuestObjectiveTargetValue : public TargetValue
{
public:
    QuestObjectiveTargetValue(PlayerbotAI* botAI, std::string const name = "quest objective target")
        : TargetValue(botAI, name)
    {
    }

    Unit* Calculate() override;

private:
    bool MatchesQuestObjective(Unit* unit, uint32 questId, int32 focusObjective, int32& remainingCount) const;
    bool PassesBasicTargetFilters(Unit* unit) const;
};

#endif
