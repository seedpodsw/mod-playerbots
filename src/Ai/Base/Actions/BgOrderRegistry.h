/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_BGORDERREGISTRY_H
#define PLAYERBOTS_BGORDERREGISTRY_H

#include "SharedDefines.h"
#include "Position.h"

class Battleground;
class GameObject;
class Player;
class PlayerbotAI;
class WorldObject;

enum class BgOrderAction : uint8
{
    None = 0,
    Defend,
    Attack
};

struct BgNodeRef
{
    uint32 nodeId = 0;
    Position position;
    GameObject* gameObject = nullptr;
};

struct BgTeamOrder
{
    BgOrderAction action = BgOrderAction::None;
    uint32 nodeId = 0;
    uint32 instanceId = 0;
    TeamId teamId = TEAM_NEUTRAL;
    time_t expireTime = 0;
    std::string nodeName;
    Position position;
};

namespace BgOrderRegistry
{
uint64_t MakeOrderKey(uint32 instanceId, TeamId teamId);

bool TryParseOrder(std::string msg, BgOrderAction& action, std::string& nodeToken);

bool ResolveNode(Battleground* bg, TeamId team, std::string const& nodeToken, BgNodeRef& out);
bool ResolveNodeById(Battleground* bg, BattlegroundTypeId bgType, uint32 nodeId, BgNodeRef& out);

BattlegroundTypeId GetEffectiveBgType(Battleground* bg);

uint32 CountFriendliesNearNode(Battleground* bg, TeamId team, Position const& pos, float radius);
bool IsNodeContested(Battleground* bg, BattlegroundTypeId bgType, TeamId team, uint32 nodeId);
bool IsNodeSecure(Battleground* bg, BattlegroundTypeId bgType, TeamId team, uint32 nodeId);
bool IsNodeSaturated(Battleground* bg, TeamId team, Position const& pos);

bool ShouldFulfillTeamOrder(Battleground* bg, TeamId team, BgTeamOrder const& order);
bool TrySelectTeamOrderObjective(Player* bot, Battleground* bg, BgTeamOrder const& order, BgNodeRef& out);
bool TrySelectAutonomousObjective(Player* bot, PlayerbotAI* botAI, Battleground* bg, BgNodeRef& out,
                                  BgOrderAction& suggestedAction);

void AssignObjectivePosition(Player* bot, PlayerbotAI* botAI, PositionMap& posMap, BgNodeRef const& node);

uint32 GetEffectiveIndependenceLevel();
uint32 GetDefenderRollForSecureNode();
uint32 GetDefenderRollForContestedNode();
uint32 GetEnemyDetourChance(BattlegroundTypeId bgType);
uint32 GetAbEnemyDetourChance();

bool ShouldPushObjectiveWhileInCombat();
}  // namespace BgOrderRegistry

#endif
