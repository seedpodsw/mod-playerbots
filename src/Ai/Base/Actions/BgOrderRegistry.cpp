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
#include "BattlegroundWS.h"
#include "GameObject.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
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

    // Warsong Gulch pseudo-nodes (position objectives, not capture points).
    {BATTLEGROUND_WS, 1001, {"flag", "enemyflag", "enemy flag"}},
    {BATTLEGROUND_WS, 1002, {"ownflag", "own flag", "return"}},
    {BATTLEGROUND_WS, 1003, {"mid", "middle"}},
    {BATTLEGROUND_WS, 1004, {"base", "home"}},
    {BATTLEGROUND_WS, 1005, {"fc", "flagcarrier", "flag carrier"}},
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

bool ResolveWsNode(Battleground* bg, TeamId team, uint32 nodeId, BgNodeRef& out)
{
    if (!bg || team == TEAM_NEUTRAL)
        return false;

    BattlegroundWS* ws = static_cast<BattlegroundWS*>(bg);
    TeamId enemyTeam = bg->GetOtherTeamId(team);

    Position const WS_FLAG_HORDE = {915.958f, 1433.925f, 346.193f, 0.0f};
    Position const WS_FLAG_ALLIANCE = {1539.219f, 1481.747f, 352.458f, 0.0f};
    Position const WS_MID = {1227.446f, 1476.235f, 307.484f, 1.50f};

    out.nodeId = nodeId;
    out.gameObject = nullptr;

    switch (nodeId)
    {
        case 1001:  // enemy flag room
            out.position = (team == TEAM_ALLIANCE) ? WS_FLAG_HORDE : WS_FLAG_ALLIANCE;
            if (GameObject* go = bg->GetBGObject(team == TEAM_ALLIANCE ? BG_WS_OBJECT_H_FLAG : BG_WS_OBJECT_A_FLAG))
            {
                out.gameObject = go;
                out.position = go->GetPosition();
            }
            return true;
        case 1002:  // own flag / return
            if (ws->GetFlagState(team) == BG_WS_FLAG_STATE_ON_GROUND)
            {
                if (GameObject* ground = bg->GetBgMap()->GetGameObject(ws->GetDroppedFlagGUID(team)))
                {
                    out.gameObject = ground;
                    out.position = ground->GetPosition();
                    return true;
                }
            }
            out.position = (team == TEAM_ALLIANCE) ? WS_FLAG_ALLIANCE : WS_FLAG_HORDE;
            if (GameObject* go = bg->GetBGObject(team == TEAM_ALLIANCE ? BG_WS_OBJECT_A_FLAG : BG_WS_OBJECT_H_FLAG))
            {
                out.gameObject = go;
                out.position = go->GetPosition();
            }
            return true;
        case 1003:  // mid
            out.position = WS_MID;
            return true;
        case 1004:  // base
            out.position = (team == TEAM_ALLIANCE) ? WS_FLAG_ALLIANCE : WS_FLAG_HORDE;
            return true;
        case 1005:  // enemy FC
        {
            ObjectGuid fcGuid = ws->GetFlagPickerGUID(enemyTeam);
            if (fcGuid.IsEmpty())
                return false;

            if (Player* fc = ObjectAccessor::FindPlayer(fcGuid))
            {
                out.position = fc->GetPosition();
                return true;
            }
            return false;
        }
        default:
            return false;
    }
}

bool BgOrderRegistry::ResolveNodeById(Battleground* bg, BattlegroundTypeId bgType, uint32 nodeId, BgNodeRef& out)
{
    return ResolveNodeById(bg, bgType, nodeId, out, TEAM_NEUTRAL);
}

bool BgOrderRegistry::ResolveNodeById(Battleground* bg, BattlegroundTypeId bgType, uint32 nodeId, BgNodeRef& out,
                                      TeamId team)
{
    if (!bg)
        return false;

    if (bgType == BATTLEGROUND_WS)
    {
        if (team == TEAM_NEUTRAL)
            return false;
        return ResolveWsNode(bg, team, nodeId, out);
    }

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

bool BgOrderRegistry::ResolveNode(Battleground* bg, TeamId team, std::string const& nodeToken, BgNodeRef& out)
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
                return ResolveNodeById(bg, bgType, entry.nodeId, out, team);
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

bool BgOrderRegistry::ShouldFulfillTeamOrder(Player* bot, Battleground* bg, TeamId team, BgTeamOrder const& order)
{
    if (!bot || !bg || order.action == BgOrderAction::None)
        return false;

    if (order.expireTime <= GameTime::GetGameTime().count())
        return false;

    // Only a configured share of bots answer callouts; the rest keep other objectives.
    uint32 const volunteerPct = sPlayerbotAIConfig.bgOrderVolunteerPct;
    if (volunteerPct == 0)
        return false;

    if (volunteerPct < 100)
    {
        uint64_t hash = bot->GetGUID().GetCounter();
        hash ^= (uint64_t(order.instanceId) << 32);
        hash ^= uint64_t(order.nodeId) * 0x9e3779b97f4a7c15ULL;
        hash ^= uint64_t(static_cast<uint8>(order.action)) * 0xbf58476d1ce4e5b9ULL;
        hash ^= hash >> 33;
        hash *= 0xff51afd7ed558ccdULL;
        hash ^= hash >> 33;
        if ((hash % 100) >= volunteerPct)
            return false;
    }

    BattlegroundTypeId bgType = GetEffectiveBgType(bg);
    if (order.action == BgOrderAction::Defend && IsNodeSecure(bg, bgType, team, order.nodeId))
        return false;

    BgNodeRef node;
    if (!ResolveNodeById(bg, bgType, order.nodeId, node, team))
        return false;

    if (IsNodeSaturated(bg, team, node.position))
        return false;

    return true;
}

bool BgOrderRegistry::TrySelectTeamOrderObjective(Player* bot, Battleground* bg, BgTeamOrder const& order,
                                                  BgNodeRef& out)
{
    if (!bot || !bg || !ShouldFulfillTeamOrder(bot, bg, bot->GetTeamId(), order))
        return false;

    return ResolveNodeById(bg, GetEffectiveBgType(bg), order.nodeId, out, bot->GetTeamId());
}

namespace
{
uint64_t MakeAckKey(BgTeamOrder const& order)
{
    return BgOrderRegistry::MakeOrderKey(order.instanceId, order.teamId) ^ (uint64_t(order.nodeId) << 8) ^
           uint64_t(static_cast<uint8>(order.action));
}

void EmitBgOrderChat(PlayerbotAI* botAI, std::string const& text)
{
    if (!botAI || text.empty())
        return;

    if (botAI->SayToRaid(text))
        return;

    if (botAI->SayToParty(text))
        return;

    botAI->Say(text);
}

bool ShouldSpeakDecline(Player* bot, BgTeamOrder const& order)
{
    uint32 const declinePct = sPlayerbotAIConfig.bgOrderAckDeclinePct;
    if (declinePct == 0)
        return false;

    if (declinePct >= 100)
        return true;

    // Different salt from volunteer hash so decline chat is not the same 25% cohort.
    uint64_t hash = bot->GetGUID().GetCounter() ^ 0xA5A5A5A5ULL;
    hash ^= (uint64_t(order.instanceId) << 17);
    hash ^= uint64_t(order.nodeId) * 0x27d4eb2d459cffd1ULL;
    hash ^= uint64_t(static_cast<uint8>(order.action)) * 0x94d049bb133111ebULL;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ULL;
    hash ^= hash >> 33;
    return (hash % 100) < declinePct;
}
}  // namespace

void BgOrderRegistry::TrySpeakBgOrderAck(Player* bot, PlayerbotAI* botAI, BgTeamOrder const& order, bool accepted)
{
    if (!bot || !botAI || order.action == BgOrderAction::None)
        return;

    uint64_t const ackKey = MakeAckKey(order);
    if (botAI->bgLastAckOrderKey == ackKey)
        return;

    if (!accepted && !ShouldSpeakDecline(bot, order))
    {
        // Mark seen so we do not re-roll every tick.
        botAI->bgLastAckOrderKey = ackKey;
        return;
    }

    std::string const actionWord = order.action == BgOrderAction::Defend ? "defend" : "attack";
    std::string const nodeWord = order.nodeName.empty() ? "objective" : order.nodeName;

    std::map<std::string, std::string> placeholders;
    placeholders["%action"] = actionWord;
    placeholders["%node"] = nodeWord;

    std::string text;
    if (accepted)
    {
        text = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bg_order_accept", "On it — %action %node", placeholders);
    }
    else
    {
        text = PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "bg_order_busy", "Can't — holding elsewhere", placeholders);
    }

    EmitBgOrderChat(botAI, text);
    botAI->bgLastAckOrderKey = ackKey;
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

    if (bgType == BATTLEGROUND_AV)
    {
        BattlegroundAV* av = static_cast<BattlegroundAV*>(bg);
        float bestDefendScore = FLT_MAX;
        float bestAttackScore = FLT_MAX;
        BgNodeRef bestDefend;
        BgNodeRef bestAttack;

        static uint32 const avNodes[] = {
            BG_AV_NODES_SNOWFALL_GRAVE,   BG_AV_NODES_STONEHEART_GRAVE, BG_AV_NODES_ICEBLOOD_GRAVE,
            BG_AV_NODES_STORMPIKE_GRAVE,  BG_AV_NODES_FROSTWOLF_GRAVE, BG_AV_NODES_FIRSTAID_STATION,
            BG_AV_NODES_STONEHEART_BUNKER, BG_AV_NODES_ICEWING_BUNKER,  BG_AV_NODES_DUNBALDAR_SOUTH,
            BG_AV_NODES_DUNBALDAR_NORTH,  BG_AV_NODES_ICEBLOOD_TOWER,  BG_AV_NODES_TOWER_POINT,
            BG_AV_NODES_FROSTWOLF_ETOWER, BG_AV_NODES_FROSTWOLF_WTOWER};

        for (uint32 nodeId : avNodes)
        {
            BG_AV_NodeInfo const& info = av->GetAVNodeInfo(nodeId);
            if (info.State == POINT_DESTROYED)
                continue;

            GameObject* go = GetAvBanner(bg, nodeId);
            if (!go)
                continue;

            Position pos = go->GetPosition();
            if (IsNodeSaturated(bg, team, pos) || botAI->bgLastNodeId == nodeId)
                continue;

            float score = bot->GetDistance(go);
            score += float(CountFriendliesNearNode(bg, team, pos, sPlayerbotAIConfig.bgNodeRadius)) *
                     sPlayerbotAIConfig.bgSaturationPenalty;

            // Prefer mid-map graves over deep backline when offensive.
            if (GetEffectiveIndependenceLevel() >= 2)
            {
                float x = pos.GetPositionX();
                // Snowfall / stoneheart / iceblood sit in the contested middle band.
                if (nodeId != BG_AV_NODES_SNOWFALL_GRAVE && nodeId != BG_AV_NODES_STONEHEART_GRAVE &&
                    nodeId != BG_AV_NODES_ICEBLOOD_GRAVE)
                {
                    if ((team == TEAM_ALLIANCE && x > -200.0f) || (team == TEAM_HORDE && x < -500.0f))
                        score *= 1.35f;  // deep enemy backline penalty for non-front nodes
                }
            }

            bool owned = info.OwnerId == team;
            bool contested = info.State == POINT_ASSAULTED;

            if (owned && contested)
            {
                score *= 0.5f;
                if (score < bestDefendScore)
                {
                    bestDefendScore = score;
                    bestDefend = {nodeId, pos, go};
                }
            }
            else if (!owned && info.State != POINT_DESTROYED)
            {
                if (IsNodeSecure(bg, bgType, team, nodeId))
                    continue;

                if (contested)
                    score *= 0.65f;

                if (score < bestAttackScore)
                {
                    bestAttackScore = score;
                    bestAttack = {nodeId, pos, go};
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
