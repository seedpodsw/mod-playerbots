/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_PROGRESSIONZONESCORER_H
#define PLAYERBOTS_PROGRESSIONZONESCORER_H

#include "Define.h"
#include <vector>

class Player;

namespace ProgressionZoneScorer
{
float ScoreZone(Player* bot, uint32 zoneId);
uint32 PickBestZone(Player* bot, std::vector<uint32> const& candidates);
float GetLevelFitScore(Player* bot, uint32 zoneId);
float GetQuestOpportunityScore(Player* bot, uint32 zoneId);
}

#endif
