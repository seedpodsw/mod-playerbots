/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "BgOrderRegistry.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "Battleground.h"
#include "BattlegroundAB.h"
#include "BattlegroundAV.h"
#include "BattlegroundEY.h"
#include "BattlegroundIC.h"
#include "GameObject.h"
#include "GameTime.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PositionValue.h"

namespace
{
struct BgNodeAliasEntry
{
    BattlegroundTypeId bgType;
    uint32 nodeId;
    std::vector<std::string> aliases;
};

static std::vector<BgNodeAliasEntry> const nodeAliases = {
    {BATTLEGROUND_AB, BG_AB_NODE_STABLES, {"stables", "stable"}},
    {BATTLEGROUND_AB, BG_AB_NODE_BLACKSMITH, {"blacksmith", "smith", "bs"}},
    {BATTLEGROUND_AB, BG_AB_NODE_FARM, {"farm"}},
    {BATTLEGROUND_AB, BG_AB_NODE_LUMBER_MILL, {"lumber", "lumbermill", "lumber mill", "mill"}},
    {BATTLEGROUND_AB, BG_AB_NODE_GOLD_MINE, {"goldmine", "gold mine", "mine"}},

    {BATTLEGROUND_EY, POINT_FEL_REAVER, {"felreaver", "fel reaver", "reaver"}},
    {BATTLEGROUND_EY, POINT_BLOOD_ELF, {"bloodelf", "blood elf", "blood"}},
    {BATTLEGROUND_EY, POINT_DRAENEI_RUINS, {"draenei", "draenei ruins", "ruins"}},
    {BATTLEGROUND_EY, POINT_MAGE_TOWER, {"magetower", "mage tower", "tower"}},

    {BATTLEGROUND_AV, BG_AV_NODES_SNOWFALL_GRAVE, {"snowfall", "snowfall gy"}},
    {BATTLEGROUND_AV, BG_AV_NODES_STONEHEART_GRAVE, {"stoneheart", "stoneheart gy", "stoneheart grave"}},
    {BATTLEGROUND_AV, BG_AV_NODES_STONEHEART_BUNKER, {"stoneheart bunker"}},
    {BATTLEGROUND_AV, BG_AV_NODES_ICEWING_BUNKER, {"icewing", "icewing bunker"}},
    {BATTLEGROUND_AV, BG_AV_NODES_STORMPIKE_GRAVE, {"stormpike", "stormpike gy"}},
    {BATTLEGROUND_AV, BG_AV_NODES_DUNBALDAR_SOUTH, {"dun south", "dunbaldar south"}},
    {BATTLEGROUND_AV, BG_AV_NODES_DUNBALDAR_NORTH, {"dun north", "dunbaldar north"}},
    {BATTLEGROUND_AV, BG_AV_NODES_FIRSTAID_STATION, {"firstaid", "first aid"}},
    {BATTLEGROUND_AV, BG_AV_NODES_ICEBLOOD_GRAVE, {"iceblood", "iceblood gy"}},
    {BATTLEGROUND_AV, BG_AV_NODES_ICEBLOOD_TOWER, {"iceblood tower"}},
    {BATTLEGROUND_AV, BG_AV_NODES_TOWER_POINT, {"tower point"}},
    {BATTLEGROUND_AV, BG_AV_NODES_FROSTWOLF_GRAVE, {"frostwolf", "frostwolf gy"}},
    {BATTLEGROUND_AV, BG_AV_NODES_FROSTWOLF_ETOWER, {"frostwolf east", "east tower"}},
    {BATTLEGROUND_AV, BG_AV_NODES_FROSTWOLF_WTOWER, {"frostwolf west", "west tower"}},

    {BATTLEGROUND_IC, NODE_TYPE_WORKSHOP, {"workshop"}},
    {BATTLEGROUND_IC, NODE_TYPE_DOCKS, {"docks", "dock"}},
    {BATTLEGROUND_IC, NODE_TYPE_HANGAR, {"hangar"}},
};

std::string NormalizeToken(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char c) { return c == '-' || c == '_'; }),
                value.end());

    return value;
}

std::string NormalizePhrase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());

    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();

    return value;
}

bool MatchAlias(std::string const& token, std::string const& alias)
{
    return NormalizeToken(token) == NormalizeToken(alias);
}

GameObject* GetAbBanner(Battleground* bg, uint32 nodeId)
{
    return bg ? bg->GetBGObject(nodeId * BG_AB_OBJECTS_PER_NODE) : nullptr;
}

GameObject* GetEyBanner(Battleground* bg, uint32 nodeId)
{
    if (!bg)
        return nullptr;

    switch (nodeId)
    {
        case POINT_FEL_REAVER:
            return bg->GetBGObject(BG_EY_OBJECT_FLAG_FEL_REAVER);
        case POINT_BLOOD_ELF:
            return bg->GetBGObject(BG_EY_OBJECT_FLAG_BLOOD_ELF);
        case POINT_DRAENEI_RUINS:
            return bg->GetBGObject(BG_EY_OBJECT_FLAG_DRAENEI_RUINS);
        case POINT_MAGE_TOWER:
            return bg->GetBGObject(BG_EY_OBJECT_FLAG_MAGE_TOWER);
        default:
            return nullptr;
    }
}

GameObject* GetAvBanner(Battleground* bg, uint32 nodeId)
{
    if (!bg)
        return nullptr;

    switch (nodeId)
    {
        case BG_AV_NODES_FIRSTAID_STATION:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_FIRSTAID_STATION);
        case BG_AV_NODES_STORMPIKE_GRAVE:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_STORMPIKE_GRAVE);
        case BG_AV_NODES_STONEHEART_GRAVE:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_STONEHEART_GRAVE);
        case BG_AV_NODES_SNOWFALL_GRAVE:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_N_SNOWFALL_GRAVE);
        case BG_AV_NODES_ICEBLOOD_GRAVE:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_ICEBLOOD_GRAVE);
        case BG_AV_NODES_FROSTWOLF_GRAVE:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_FROSTWOLF_GRAVE);
        case BG_AV_NODES_DUNBALDAR_SOUTH:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_DUNBALDAR_SOUTH);
        case BG_AV_NODES_DUNBALDAR_NORTH:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_DUNBALDAR_NORTH);
        case BG_AV_NODES_ICEWING_BUNKER:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_ICEWING_BUNKER);
        case BG_AV_NODES_STONEHEART_BUNKER:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_A_STONEHEART_BUNKER);
        case BG_AV_NODES_ICEBLOOD_TOWER:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_ICEBLOOD_TOWER);
        case BG_AV_NODES_TOWER_POINT:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_TOWER_POINT);
        case BG_AV_NODES_FROSTWOLF_ETOWER:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_FROSTWOLF_ETOWER);
        case BG_AV_NODES_FROSTWOLF_WTOWER:
            return bg->GetBGObject(BG_AV_OBJECT_FLAG_H_FROSTWOLF_WTOWER);
        default:
            return nullptr;
    }
}

GameObject* GetIcBanner(Battleground* bg, uint32 nodeId)
{
    if (!bg)
        return nullptr;

    switch (nodeId)
    {
        case NODE_TYPE_WORKSHOP:
            return bg->GetBGObject(BG_IC_GO_WORKSHOP_BANNER);
        case NODE_TYPE_DOCKS:
            return bg->GetBGObject(BG_IC_GO_DOCKS_BANNER);
        case NODE_TYPE_HANGAR:
            return bg->GetBGObject(BG_IC_GO_HANGAR_BANNER);
        default:
            return nullptr;
    }
}

bool IsAbNodeAttackable(BattlegroundAB* ab, TeamId team, uint32 nodeId)
{
    uint8 state = ab->GetCapturePointInfo(nodeId)._state;
    if (state == BG_AB_NODE_STATE_NEUTRAL)
        return true;

    if (team == TEAM_ALLIANCE)
        return state == BG_AB_NODE_STATE_HORDE_OCCUPIED || state == BG_AB_NODE_STATE_ALLY_CONTESTED ||
               state == BG_AB_NODE_STATE_HORDE_CONTESTED;

    return state == BG_AB_NODE_STATE_ALLY_OCCUPIED || state == BG_AB_NODE_STATE_HORDE_CONTESTED ||
           state == BG_AB_NODE_STATE_ALLY_CONTESTED;
}

bool IsAbNodeDefendable(BattlegroundAB* ab, TeamId team, uint32 nodeId)
{
    uint8 state = ab->GetCapturePointInfo(nodeId)._state;
    if (team == TEAM_ALLIANCE)
        return state == BG_AB_NODE_STATE_ALLY_OCCUPIED || state == BG_AB_NODE_STATE_HORDE_CONTESTED ||
               state == BG_AB_NODE_STATE_ALLY_CONTESTED;

    return state == BG_AB_NODE_STATE_HORDE_OCCUPIED || state == BG_AB_NODE_STATE_ALLY_CONTESTED ||
           state == BG_AB_NODE_STATE_HORDE_CONTESTED;
}

float ScoreAbNode(BattlegroundAB* ab, Player* bot, PlayerbotAI* botAI, TeamId team, uint32 nodeId,
                  BgOrderAction action)
{
    if (action == BgOrderAction::Defend && !IsAbNodeDefendable(ab, team, nodeId))
        return FLT_MAX;

    if (action == BgOrderAction::Attack && !IsAbNodeAttackable(ab, team, nodeId))
        return FLT_MAX;

    GameObject* go = GetAbBanner(ab, nodeId);
    if (!go)
        return FLT_MAX;

    Position pos = go->GetPosition();
    if (BgOrderRegistry::IsNodeSaturated(ab, team, pos))
        return FLT_MAX;

    if (botAI && botAI->bgLastNodeId == nodeId)
        return FLT_MAX;

    float score = bot->GetDistance(go);
    score += float(BgOrderRegistry::CountFriendliesNearNode(ab, team, pos, sPlayerbotAIConfig.bgNodeRadius)) *
             sPlayerbotAIConfig.bgSaturationPenalty;

    if (BgOrderRegistry::IsNodeContested(ab, BATTLEGROUND_AB, team, nodeId))
        score *= 0.5f;

    return score;
}
}  // namespace

uint64_t BgOrderRegistry::MakeOrderKey(uint32 instanceId, TeamId teamId)
{
    return (uint64_t(instanceId) << 32) | teamId;
}

BattlegroundTypeId BgOrderRegistry::GetEffectiveBgType(Battleground* bg)
{
    if (!bg)
        return BATTLEGROUND_TYPE_NONE;

    BattlegroundTypeId bgType = bg->GetBgTypeID();
    if (bgType == BATTLEGROUND_RB)
        bgType = bg->GetBgTypeID(true);

    return bgType;
}

bool BgOrderRegistry::TryParseOrder(std::string msg, BgOrderAction& action, std::string& nodeToken)
{
    msg = NormalizePhrase(msg);
    if (msg.empty())
        return false;

    if (msg.rfind("bg ", 0) == 0)
        msg = msg.substr(3);

    auto parseAction = [&](std::string const& prefix, BgOrderAction act) -> bool
    {
        if (msg.rfind(prefix + " ", 0) != 0)
            return false;

        nodeToken = msg.substr(prefix.size() + 1);
        nodeToken = NormalizePhrase(nodeToken);
        if (nodeToken.empty())
            return false;

        action = act;
        return true;
    };

    if (parseAction("defend", BgOrderAction::Defend) || parseAction("attack", BgOrderAction::Attack))
        return true;

    return false;
}

bool BgOrderRegistry::ResolveNodeById(Battleground* bg, BattlegroundTypeId bgType, uint32 nodeId, BgNodeRef& out)
{
    if (!bg)
        return false;

    GameObject* go = nullptr;
    switch (bgType)
    {
        case BATTLEGROUND_AB:
            go = GetAbBanner(bg, nodeId);
            break;
        case BATTLEGROUND_EY:
            go = GetEyBanner(bg, nodeId);
            break;
        case BATTLEGROUND_AV:
            go = GetAvBanner(bg, nodeId);
            break;
        case BATTLEGROUND_IC:
            go = GetIcBanner(bg, nodeId);
            break;
        default:
            break;
    }

    if (!go)
        return false;

    out.nodeId = nodeId;
    out.gameObject = go;
    out.position = go->GetPosition();
    return true;
}

bool BgOrderRegistry::ResolveNode(Battleground* bg, TeamId /*team*/, std::string const& nodeToken, BgNodeRef& out)
{
    if (!bg)
        return false;

    BattlegroundTypeId bgType = GetEffectiveBgType(bg);
    std::string const normalized = NormalizePhrase(nodeToken);

    for (BgNodeAliasEntry const& entry : nodeAliases)
    {
        if (entry.bgType != bgType)
            continue;

        for (std::string const& alias : entry.aliases)
        {
            if (MatchAlias(normalized, alias))
                return ResolveNodeById(bg, bgType, entry.nodeId, out);
        }
    }

    return false;
}

uint32 BgOrderRegistry::CountFriendliesNearNode(Battleground* bg, TeamId team, Position const& pos, float radius)
{
    if (!bg)
        return 0;

    uint32 count = 0;
    for (auto const& [guid, player] : bg->GetPlayers())
    {
        if (!player || player->GetTeamId() != team || !player->IsAlive())
            continue;

        if (player->GetDistance(pos) <= radius)
            ++count;
    }

    return count;
}

bool BgOrderRegistry::IsNodeSaturated(Battleground* bg, TeamId team, Position const& pos)
{
    return CountFriendliesNearNode(bg, team, pos, sPlayerbotAIConfig.bgNodeRadius) >= sPlayerbotAIConfig.bgMaxBotsPerNode;
}

bool BgOrderRegistry::IsNodeContested(Battleground* bg, BattlegroundTypeId bgType, TeamId team, uint32 nodeId)
{
    if (!bg)
        return false;

    switch (bgType)
    {
        case BATTLEGROUND_AB:
        {
            BattlegroundAB* ab = static_cast<BattlegroundAB*>(bg);
            uint8 state = ab->GetCapturePointInfo(nodeId)._state;
            if (team == TEAM_ALLIANCE)
                return state == BG_AB_NODE_STATE_HORDE_CONTESTED || state == BG_AB_NODE_STATE_ALLY_CONTESTED;

            return state == BG_AB_NODE_STATE_ALLY_CONTESTED || state == BG_AB_NODE_STATE_HORDE_CONTESTED;
        }
        case BATTLEGROUND_EY:
        {
            BattlegroundEY* ey = static_cast<BattlegroundEY*>(bg);
            CaptureEYPointInfo const& info = ey->GetCapturePointInfo(nodeId);
            if (info._ownerTeamId != team)
                return info._barStatus != BG_EY_PROGRESS_BAR_STATE_MIDDLE;

            if (team == TEAM_ALLIANCE)
                return info._barStatus < BG_EY_PROGRESS_BAR_ALI_CONTROLLED;

            return info._barStatus > BG_EY_PROGRESS_BAR_HORDE_CONTROLLED;
        }
        case BATTLEGROUND_AV:
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
            return node.State == POINT_ASSAULTED;
        }
        case BATTLEGROUND_IC:
        {
            BattlegroundIC* ic = static_cast<BattlegroundIC*>(bg);
            uint8 state = ic->GetNodeState(nodeId);
            return state == NODE_STATE_CONFLICT_A || state == NODE_STATE_CONFLICT_H;
        }
        default:
            return false;
    }
}

bool BgOrderRegistry::IsNodeSecure(Battleground* bg, BattlegroundTypeId bgType, TeamId team, uint32 nodeId)
{
    if (!bg)
        return false;

    if (IsNodeContested(bg, bgType, team, nodeId))
        return false;

    switch (bgType)
    {
        case BATTLEGROUND_AB:
        {
            BattlegroundAB* ab = static_cast<BattlegroundAB*>(bg);
            uint8 state = ab->GetCapturePointInfo(nodeId)._state;
            if (team == TEAM_ALLIANCE)
                return state == BG_AB_NODE_STATE_ALLY_OCCUPIED;

            return state == BG_AB_NODE_STATE_HORDE_OCCUPIED;
        }
        case BATTLEGROUND_EY:
        {
            BattlegroundEY* ey = static_cast<BattlegroundEY*>(bg);
            CaptureEYPointInfo const& info = ey->GetCapturePointInfo(nodeId);
            if (info._ownerTeamId != team)
                return false;

            if (team == TEAM_ALLIANCE)
                return info._barStatus >= BG_EY_PROGRESS_BAR_ALI_CONTROLLED;

            return info._barStatus <= BG_EY_PROGRESS_BAR_HORDE_CONTROLLED;
        }
        case BATTLEGROUND_AV:
        {
            BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
            BG_AV_NodeInfo const& node = av->GetAVNodeInfo(nodeId);
            if (node.State == POINT_DESTROYED)
                return false;

            return node.OwnerId == team && node.State == POINT_CONTROLLED;
        }
        case BATTLEGROUND_IC:
        {
            BattlegroundIC* ic = static_cast<BattlegroundIC*>(bg);
            uint8 state = ic->GetNodeState(nodeId);
            if (team == TEAM_ALLIANCE)
                return state == NODE_STATE_CONTROLLED_A;

            return state == NODE_STATE_CONTROLLED_H;
        }
        default:
            return false;
    }
}

bool BgOrderRegistry::ShouldFulfillTeamOrder(Battleground* bg, TeamId team, BgTeamOrder const& order)
{
    if (!bg || order.action == BgOrderAction::None)
        return false;

    if (order.expireTime <= GameTime::GetGameTime().count())
        return false;

    BattlegroundTypeId bgType = GetEffectiveBgType(bg);
    if (order.action == BgOrderAction::Defend && IsNodeSecure(bg, bgType, team, order.nodeId))
        return false;

    BgNodeRef node;
    if (!ResolveNodeById(bg, bgType, order.nodeId, node))
        return false;

    if (IsNodeSaturated(bg, team, node.position))
        return false;

    return true;
}

bool BgOrderRegistry::TrySelectTeamOrderObjective(Player* bot, Battleground* bg, BgTeamOrder const& order,
                                                  BgNodeRef& out)
{
    if (!bot || !bg || !ShouldFulfillTeamOrder(bg, bot->GetTeamId(), order))
        return false;

    return ResolveNodeById(bg, GetEffectiveBgType(bg), order.nodeId, out);
}

bool BgOrderRegistry::TrySelectAutonomousObjective(Player* bot, PlayerbotAI* botAI, Battleground* bg, BgNodeRef& out,
                                                   BgOrderAction& suggestedAction)
{
    if (!bot || !botAI || !bg)
        return false;

    if (GetEffectiveIndependenceLevel() == 0)
        return false;

    BattlegroundTypeId bgType = GetEffectiveBgType(bg);
    TeamId team = bot->GetTeamId();
    suggestedAction = BgOrderAction::Attack;

    if (bgType == BATTLEGROUND_AB)
    {
        BattlegroundAB* ab = static_cast<BattlegroundAB*>(bg);
        float bestDefendScore = FLT_MAX;
        float bestAttackScore = FLT_MAX;
        BgNodeRef bestDefend;
        BgNodeRef bestAttack;

        static uint32 const abNodes[] = {BG_AB_NODE_STABLES, BG_AB_NODE_BLACKSMITH, BG_AB_NODE_FARM,
                                       BG_AB_NODE_LUMBER_MILL, BG_AB_NODE_GOLD_MINE};

        for (uint32 nodeId : abNodes)
        {
            GameObject* go = GetAbBanner(ab, nodeId);
            if (!go)
                continue;

            if (IsNodeContested(bg, bgType, team, nodeId) && IsAbNodeDefendable(ab, team, nodeId))
            {
                if (bot->GetDistance(go) > sPlayerbotAIConfig.bgContestRadarRange)
                    continue;

                float score = ScoreAbNode(ab, bot, botAI, team, nodeId, BgOrderAction::Defend);
                if (score < bestDefendScore)
                {
                    bestDefendScore = score;
                    bestDefend = {nodeId, go->GetPosition(), go};
                }
            }

            if (IsAbNodeAttackable(ab, team, nodeId))
            {
                float score = ScoreAbNode(ab, bot, botAI, team, nodeId, BgOrderAction::Attack);
                if (score < bestAttackScore)
                {
                    bestAttackScore = score;
                    bestAttack = {nodeId, go->GetPosition(), go};
                }
            }
        }

        if (bestDefend.gameObject && bestDefendScore <= bestAttackScore)
        {
            out = bestDefend;
            suggestedAction = BgOrderAction::Defend;
            return true;
        }

        if (bestAttack.gameObject)
        {
            out = bestAttack;
            suggestedAction = BgOrderAction::Attack;
            return true;
        }

        return false;
    }

    if (bgType == BATTLEGROUND_EY)
    {
        float bestScore = FLT_MAX;
        static uint32 const eyNodes[] = {POINT_FEL_REAVER, POINT_BLOOD_ELF, POINT_DRAENEI_RUINS, POINT_MAGE_TOWER};

        for (uint32 nodeId : eyNodes)
        {
            GameObject* go = GetEyBanner(bg, nodeId);
            if (!go)
                continue;

            BattlegroundEY* ey = static_cast<BattlegroundEY*>(bg);
            bool owned = ey->GetCapturePointInfo(nodeId)._ownerTeamId == team;
            if (owned && IsNodeSecure(bg, bgType, team, nodeId))
                continue;

            Position pos = go->GetPosition();
            if (IsNodeSaturated(bg, team, pos) || botAI->bgLastNodeId == nodeId)
                continue;

            float score = bot->GetDistance(go);
            score += float(CountFriendliesNearNode(bg, team, pos, sPlayerbotAIConfig.bgNodeRadius)) *
                     sPlayerbotAIConfig.bgSaturationPenalty;

            if (IsNodeContested(bg, bgType, team, nodeId))
                score *= 0.5f;

            if (score < bestScore)
            {
                bestScore = score;
                out = {nodeId, pos, go};
                suggestedAction = owned ? BgOrderAction::Defend : BgOrderAction::Attack;
            }
        }

        return out.gameObject != nullptr;
    }

    if (bgType == BATTLEGROUND_IC)
    {
        float bestScore = FLT_MAX;
        static uint32 const icNodes[] = {NODE_TYPE_WORKSHOP, NODE_TYPE_DOCKS, NODE_TYPE_HANGAR};

        for (uint32 nodeId : icNodes)
        {
            GameObject* go = GetIcBanner(bg, nodeId);
            if (!go)
                continue;

            if (IsNodeSecure(bg, bgType, team, nodeId))
                continue;

            Position pos = go->GetPosition();
            if (IsNodeSaturated(bg, team, pos) || botAI->bgLastNodeId == nodeId)
                continue;

            float score = bot->GetDistance(go);
            score += float(CountFriendliesNearNode(bg, team, pos, sPlayerbotAIConfig.bgNodeRadius)) *
                     sPlayerbotAIConfig.bgSaturationPenalty;

            if (score < bestScore)
            {
                bestScore = score;
                out = {nodeId, pos, go};
                suggestedAction = IsNodeContested(bg, bgType, team, nodeId) ? BgOrderAction::Defend
                                                                            : BgOrderAction::Attack;
            }
        }

        return out.gameObject != nullptr;
    }

    return false;
}

void BgOrderRegistry::AssignObjectivePosition(Player* bot, PlayerbotAI* botAI, PositionMap& posMap, BgNodeRef const& node)
{
    float rx, ry, rz;
    Position objPos = node.position;
    bot->GetRandomPoint(objPos, frand(5.0f, 15.0f), rx, ry, rz);

    if (Map* map = bot->GetMap())
    {
        float groundZ = map->GetHeight(rx, ry, rz);
        if (groundZ != VMAP_INVALID_HEIGHT_VALUE)
            rz = groundZ;
    }

    PositionInfo pos;
    pos.Set(rx, ry, rz, bot->GetMapId());
    posMap["bg objective"] = pos;

    if (botAI)
    {
        botAI->bgLastNodeId = node.nodeId;
        botAI->bgObjectiveSetTime = GameTime::GetGameTime().count();
    }
}

uint32 BgOrderRegistry::GetEffectiveIndependenceLevel()
{
    if (sPlayerbotAIConfig.bgIndependenceLevel)
        return sPlayerbotAIConfig.bgIndependenceLevel;

    return sPlayerbotAIConfig.hardModeBG ? 2 : 0;
}

uint32 BgOrderRegistry::GetDefenderRollForSecureNode()
{
    if (GetEffectiveIndependenceLevel() >= 2)
        return 40;

    if (GetEffectiveIndependenceLevel() == 1)
        return 60;

    return 85;
}

uint32 BgOrderRegistry::GetDefenderRollForContestedNode()
{
    return 85;
}

uint32 BgOrderRegistry::GetEnemyDetourChance(BattlegroundTypeId bgType)
{
    if (bgType == BATTLEGROUND_AV)
        return sPlayerbotAIConfig.bgAvEnemyDetourChance;

    return sPlayerbotAIConfig.bgAbEnemyDetourChance;
}

uint32 BgOrderRegistry::GetAbEnemyDetourChance()
{
    return sPlayerbotAIConfig.bgAbEnemyDetourChance;
}

bool BgOrderRegistry::ShouldPushObjectiveWhileInCombat()
{
    return sPlayerbotAIConfig.hardModeBG && sPlayerbotAIConfig.bgCombatObjectivePush;
}
