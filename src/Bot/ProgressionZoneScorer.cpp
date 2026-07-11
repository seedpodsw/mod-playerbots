/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "ProgressionZoneScorer.h"

#include <cmath>

#include "ObjectMgr.h"
#include "PlayerbotAIConfig.h"
#include "QuestDef.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "TravelMgr.h"

namespace ProgressionZoneScorer
{
float GetLevelFitScore(Player* bot, uint32 zoneId)
{
    if (!bot)
        return 0.0f;

    uint32 low = 0;
    uint32 high = 0;
    if (!sTravelMgr.TryGetZoneLevelRange(zoneId, low, high))
        return 0.0f;

    uint8 const progressionLevel = PlayerbotGroupProgression::GetProgressionLevel(bot);
    float const mid = (float(low) + float(high)) * 0.5f;
    float const delta = std::abs(float(progressionLevel) - mid);
    return std::max(0.0f, 100.0f - delta * 8.0f);
}

float GetQuestOpportunityScore(Player* bot, uint32 zoneId)
{
    if (!bot || !bot->GetMap())
        return 0.0f;

    float score = 0.0f;
    uint8 const levelRef = PlayerbotGroupProgression::GetQuestLevelRef(bot);

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        QuestStatus status = bot->GetQuestStatus(questId);
        if (status != QUEST_STATUS_INCOMPLETE && status != QUEST_STATUS_COMPLETE)
            continue;

        if (PlayerbotGroupProgression::IsQuestTrivialForLevel(levelRef, quest))
            continue;

        QuestPOIVector const* poiVector = sObjectMgr->GetQuestPOIVector(questId);
        if (!poiVector)
            continue;

        for (QuestPOI const& qPoi : *poiVector)
        {
            if (qPoi.MapId != bot->GetMapId() || qPoi.points.empty())
                continue;

            float dx = 0.0f;
            float dy = 0.0f;
            for (QuestPOIPoint const& point : qPoi.points)
            {
                dx += point.x;
                dy += point.y;
            }
            dx /= float(qPoi.points.size());
            dy /= float(qPoi.points.size());

            float dz = bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT);
            if (bot->GetMap()->GetZoneId(bot->GetPhaseMask(), dx, dy, dz) == zoneId)
            {
                score += 15.0f;
                break;
            }
        }
    }

    return score;
}

float ScoreZone(Player* bot, uint32 zoneId)
{
    if (!bot)
        return 0.0f;

    float score = GetLevelFitScore(bot, zoneId) * sPlayerbotAIConfig.zoneScoreLevelFitWeight;
    score += GetQuestOpportunityScore(bot, zoneId) * sPlayerbotAIConfig.zoneScoreQuestWeight;
    score -= float(sRandomPlayerbotMgr.GetRandomBotCountInZone(zoneId)) * sPlayerbotAIConfig.zoneScoreBotDensityPenalty;
    score += frand(-3.0f, 3.0f);

    LOG_DEBUG("playerbots.progression", "Zone score for {} zone {}: {:.1f} (bots in zone: {})", bot->GetName(), zoneId,
              score, sRandomPlayerbotMgr.GetRandomBotCountInZone(zoneId));

    return score;
}

uint32 PickBestZone(Player* bot, std::vector<uint32> const& candidates)
{
    if (candidates.empty())
        return 0;

    uint32 bestZone = candidates[0];
    float bestScore = ScoreZone(bot, bestZone);

    for (size_t i = 1; i < candidates.size(); ++i)
    {
        float score = ScoreZone(bot, candidates[i]);
        if (score > bestScore)
        {
            bestScore = score;
            bestZone = candidates[i];
        }
    }

    return bestZone;
}
}  // namespace ProgressionZoneScorer
