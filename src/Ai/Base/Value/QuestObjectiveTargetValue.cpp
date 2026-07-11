/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "QuestObjectiveTargetValue.h"

#include "GrindTargetValue.h"
#include "LootMgr.h"
#include "NewRpgInfo.h"
#include "PlayerbotAIConfig.h"
#include "Playerbots.h"
#include "QuestValues.h"
#include "RandomPlayerbotMgr.h"
#include "ReputationMgr.h"
#include "ServerFacade.h"

#include <cfloat>

bool QuestObjectiveTargetValue::PassesBasicTargetFilters(Unit* unit) const
{
    if (!unit || !unit->IsAlive())
        return false;

    if (!unit->IsInWorld() || unit->IsDuringRemoveFromWorld())
        return false;

    if (unit->ToCreature() && !unit->ToCreature()->GetCreatureTemplate()->lootid &&
        bot->GetReactionTo(unit) >= REP_NEUTRAL)
        return false;

    if (!bot->IsHostileTo(unit) && unit->GetNpcFlags() != UNIT_NPC_FLAG_NONE)
        return false;

    if (!bot->isHonorOrXPTarget(unit))
        return false;

    if (std::abs(bot->GetPositionZ() - unit->GetPositionZ()) > INTERACTION_DISTANCE)
        return false;

    if (!bot->IsWithinLOSInMap(unit))
        return false;

    return true;
}

bool QuestObjectiveTargetValue::MatchesQuestObjective(Unit* unit, uint32 questId, int32 focusObjective,
                                                      int32& remainingCount) const
{
    Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
    if (!questTemplate)
        return false;

    QuestStatus status = bot->GetQuestStatus(questId);
    if (status != QUEST_STATUS_INCOMPLETE)
        return false;

    QuestStatusData const* questStatus = &bot->getQuestStatusMap()[questId];

    auto checkObjective = [&](int32 objectiveIdx) -> bool
    {
        int32 entry = questTemplate->RequiredNpcOrGo[objectiveIdx];
        if (!entry)
            return false;

        int32 required = questTemplate->RequiredNpcOrGoCount[objectiveIdx];
        int32 available = questStatus->CreatureOrGOCount[objectiveIdx];
        if (!required || available >= required)
            return false;

        remainingCount = required - available;

        if (entry > 0 && unit->GetEntry() == uint32(entry))
            return true;

        return false;
    };

    if (focusObjective >= 0 && focusObjective < QUEST_OBJECTIVES_COUNT)
        return checkObjective(focusObjective);

    for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
    {
        if (checkObjective(i))
            return true;
    }

    if (CreatureTemplate const* data = sObjectMgr->GetCreatureTemplate(unit->GetEntry()))
    {
        if (uint32 lootId = data->lootid)
        {
            if (LootTemplates_Creature.HaveQuestLootForPlayer(lootId, bot))
            {
                for (int32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                {
                    if (!questTemplate->RequiredItemId[i])
                        continue;

                    int32 required = questTemplate->RequiredItemCount[i];
                    int32 available = questStatus->ItemCount[i];
                    if (required && available < required)
                    {
                        remainingCount = required - available;
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

Unit* QuestObjectiveTargetValue::Calculate()
{
    if (botAI->rpgInfo.GetStatus() != RPG_DO_QUEST)
        return nullptr;

    NewRpgInfo::DoQuest const* doQuest = std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data);
    if (!doQuest || !doQuest->questId)
        return nullptr;

    GuidVector targets = *context->GetValue<GuidVector>("possible targets");
    if (targets.empty())
        return nullptr;

    std::vector<GuidPosition> const& objectiveSpawns =
        context->GetValue<std::vector<GuidPosition>>("active quest objectives")->Get();

    Unit* bestTarget = nullptr;
    float bestScore = -FLT_MAX;
    uint32 const questId = doQuest->questId;
    int32 const focusObjective = doQuest->objectiveIdx;

    for (ObjectGuid const guid : targets)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!PassesBasicTargetFilters(unit))
            continue;

        int32 remainingCount = INT32_MAX;
        if (!MatchesQuestObjective(unit, questId, focusObjective, remainingCount))
            continue;

        float score = -bot->GetDistance(unit);
        score -= float(remainingCount) * 5.0f;

        if (focusObjective >= 0 && focusObjective < QUEST_OBJECTIVES_COUNT)
        {
            Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
            if (questTemplate)
            {
                int32 entry = questTemplate->RequiredNpcOrGo[focusObjective];
                if (entry > 0 && unit->GetEntry() == uint32(entry))
                    score += 200.0f;
            }
        }

        for (GuidPosition const& spawn : objectiveSpawns)
        {
            if (spawn.GetMapId() != bot->GetMapId())
                continue;

            float spawnDist = unit->GetDistance(spawn.GetPositionX(), spawn.GetPositionY(), spawn.GetPositionZ());
            if (spawnDist < 80.0f)
            {
                score += 50.0f - spawnDist * 0.5f;
                break;
            }
        }

        if (score > bestScore)
        {
            bestScore = score;
            bestTarget = unit;
        }
    }

    return bestTarget;
}
