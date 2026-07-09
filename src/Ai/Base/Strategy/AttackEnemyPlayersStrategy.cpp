/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "AttackEnemyPlayersStrategy.h"

#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"

void AttackEnemyPlayersStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    float priority = 55.0f;
    if (botAI->GetBot()->InBattleground())
        priority = float(sPlayerbotAIConfig.bgAttackPriority);
    else if (sRandomPlayerbotMgr.ShouldUseRandomBotOpenWorldPvp(botAI->GetBot()))
        priority = float(sPlayerbotAIConfig.randomBotOpenWorldPvpAttackPriority);

    triggers.push_back(new TriggerNode("random bot pvp seek", { NextAction("attack enemy player", priority) }));
    triggers.push_back(new TriggerNode("enemy player near", { NextAction("attack enemy player", priority) }));
}
