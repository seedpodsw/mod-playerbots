#include "NewRpgBaseAction.h"

#include <algorithm>
#include <cmath>

#include "BroadcastHelper.h"
#include "ChatHelper.h"
#include "Creature.h"
#include "G3D/Vector2.h"
#include "GameObject.h"
#include "GossipDef.h"
#include "GridTerrainData.h"
#include "IVMapMgr.h"
#include "NewRpgInfo.h"
#include "NewRpgStrategy.h"
#include "Object.h"
#include "ObjectAccessor.h"
#include "OutdoorPvPMgr.h"
#include "ObjectDefines.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "Position.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "Random.h"
#include "RandomPlayerbotMgr.h"
#include "SharedDefines.h"
#include "StatsWeightCalculator.h"
#include "Timer.h"
#include "TravelMgr.h"

bool NewRpgBaseAction::MoveFarTo(WorldPosition dest)
{
    RefreshBot();
    if (!GetValidBot())
        return false;

    if (dest == WorldPosition())
        return false;

    if (dest != botAI->rpgInfo.moveFarPos)
    {
        // clear stuck information if it's a new dest
        botAI->rpgInfo.SetMoveFarTo(dest);
    }

    // performance optimization
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
    {
        return false;
    }

    // Let previously committed movement finish before recomputing.
    //
    // MoveTo internally caps its stored delay at maxWaitForMove
    // (default 5s), but a long path (200+ yd routed around a
    // mountain) takes 30+ seconds to walk. After 5s
    // IsWaitingForLastMove returns false and MoveFarTo re-enters.
    // Without this gate, DoMovePoint would call mm->Clear() and
    // reissue MovePoint from the new bot position — and from a new
    // position mmap's partial-path endpoint often differs, so the
    // bot gets clobbered mid-walk and ends up oscillating (e.g.
    // cave entrance -> inside cave -> cave entrance -> mountain
    // base -> cave entrance...) around an unreachable destination.
    //
    // If the bot is still actively walking toward its last
    // committed point on the same map, just let the current spline
    // finish. The stuck counter below continues to track real
    // progress toward dest and triggers teleport recovery if the
    // committed paths genuinely aren't closing the gap.
    {
        LastMovement& lastMove = AI_VALUE(LastMovement&, "last movement");
        if (bot->isMoving() && lastMove.lastMoveToMapId == bot->GetMapId())
        {
            float remaining = bot->GetExactDist(lastMove.lastMoveToX, lastMove.lastMoveToY, lastMove.lastMoveToZ);
            if (remaining > 10.0f)
                return true;
        }
    }

    // stuck check
    float disToDest = bot->GetDistance(dest);
    // Require a meaningful improvement (5yd) to reset the stuck counter.
    // The old 1yd threshold was small enough that bots oscillating back
    // and forth around an obstacle would keep "making progress" forever
    // and never trigger the teleport recovery below.
    if (disToDest + 5.0f < botAI->rpgInfo.nearestMoveFarDis)
    {
        botAI->rpgInfo.nearestMoveFarDis = disToDest;
        botAI->rpgInfo.stuckTs = getMSTime();
        botAI->rpgInfo.stuckAttempts = 0;
    }
    else if (++botAI->rpgInfo.stuckAttempts >= 5 && GetMSTimeDiffToNow(botAI->rpgInfo.stuckTs) >= stuckTime)
    {
        botAI->rpgInfo.stuckTs = getMSTime();
        botAI->rpgInfo.stuckAttempts = 0;

        if (IsBotLedNearbyGroupBot())
        {
            // Ambient parties must stay together — never teleport away from members.
            botAI->rpgInfo.ChangeToIdle();
            return false;
        }

        // No meaningful progress toward dest for `stuckTime`: fall
        // back to teleporting directly so the bot can get on with
        // its RPG objective instead of oscillating indefinitely.
        const AreaTableEntry* entry = sAreaTableStore.LookupEntry(bot->GetZoneId());
        std::string zone_name = PlayerbotAI::GetLocalizedAreaName(entry);
        LOG_DEBUG(
            "playerbots",
            "[New RPG] Teleport {} from ({},{},{},{}) to ({},{},{},{}) as it stuck when moving far - Zone: {} ({})",
            bot->GetName(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetMapId(),
            dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), dest.GetMapId(), bot->GetZoneId(),
            zone_name);
        bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
        return bot->TeleportTo(dest);
    }

    float dis = bot->GetExactDist(dest);
    if (dis < pathFinderDis)
    {
        return MoveTo(dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), false, false,
                      false, true);
    }

    const uint32 typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;

    // Primary strategy: ask mmap for a route to the TRUE destination.
    // If mmap can reach it directly (PATHFIND_NORMAL) or partially
    // (PATHFIND_INCOMPLETE — destinations beyond the smooth-path cap
    // of ~296 yards, or where local geometry blocks the final step),
    // walk to the furthest reachable waypoint mmap computed. This
    // lets bots follow the real route around obstacles (mountains,
    // cave walls, cliffs) instead of trying to cut straight through.
    // The spline system walks the whole returned path smoothly, so
    // subsequent ticks early-out via IsWaitingForLastMove and no
    // further PathGenerator calls fire until the bot arrives.
    {
        PathGenerator path(bot);
        path.CalculatePath(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ());
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));
        if (canReach)
        {
            const G3D::Vector3& endPos = path.GetActualEndPosition();
            // Only commit if the mmap endpoint actually makes progress
            // toward the destination. For pathological INCOMPLETE
            // results (e.g. disconnected polys that still report
            // INCOMPLETE) the endpoint can land right under the bot;
            // fall through to cone sampling in that case.
            float endDistToDest = dest.GetExactDist(endPos.x, endPos.y, endPos.z);
            if (endDistToDest + 5.0f < disToDest)
            {
                return MoveTo(bot->GetMapId(), endPos.x, endPos.y, endPos.z, false, false, false, true);
            }
        }
    }

    // Fallback: mmap couldn't route to the destination. Sample the
    // forward cone for a reachable stepping stone so the bot keeps
    // moving and can try again from a new vantage point. Cap at 2
    // samples — we already spent one PathGenerator call above and at
    // 3000 bots every extra CalculatePath matters.
    float minDelta = M_PI;
    const float x = bot->GetPositionX();
    const float y = bot->GetPositionY();
    const float z = bot->GetPositionZ();
    const float baseAngle = bot->GetAngle(&dest);
    float rx, ry, rz;
    bool found = false;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        float delta = (rand_norm() - 0.5f) * static_cast<float>(M_PI);  // ±π/2, forward cone
        float sampleDis = (0.5f + rand_norm() * 0.5f) * pathFinderDis;
        float angle = baseAngle + delta;
        float dx = x + cos(angle) * sampleDis;
        float dy = y + sin(angle) * sampleDis;
        float dz = z + 0.5f;
        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        bool canReach = !(type & (~typeOk));

        if (canReach && fabs(delta) <= minDelta)
        {
            found = true;
            const G3D::Vector3& endPos = path.GetActualEndPosition();
            rx = endPos.x;
            ry = endPos.y;
            rz = endPos.z;
            minDelta = fabs(delta);
        }
    }
    if (found)
    {
        return MoveTo(bot->GetMapId(), rx, ry, rz, false, false, false, true);
    }
    return false;
}

bool NewRpgBaseAction::MoveWorldObjectTo(ObjectGuid guid, float distance)
{
    if (IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
    {
        return false;
    }

    WorldObject* object = botAI->GetWorldObject(guid);
    if (!object)
        return false;
    float x = object->GetPositionX();
    float y = object->GetPositionY();
    float z = object->GetPositionZ();
    float mapId = object->GetMapId();
    float angle = 0.f;

    if (!object->ToUnit() || !object->ToUnit()->isMoving())
        angle = object->GetAngle(bot) + (M_PI * irand(-25, 25) / 100.0);  // Closest 45 degrees towards the target
    else
        angle = object->GetOrientation() +
                (M_PI * irand(-25, 25) / 100.0);  // 45 degrees infront of target (leading it's movement)

    float rnd = rand_norm();
    x += cos(angle) * distance * rnd;
    y += sin(angle) * distance * rnd;
    if (!object->GetMap()->CheckCollisionAndGetValidCoords(object, object->GetPositionX(), object->GetPositionY(),
                                                           object->GetPositionZ(), x, y, z))
    {
        x = object->GetPositionX();
        y = object->GetPositionY();
        z = object->GetPositionZ();
    }
    return MoveTo(mapId, x, y, z, false, false, false, true);
}

bool NewRpgBaseAction::MoveRandomNear(float moveStep, MovementPriority priority, WorldObject*)
{
    if (IsWaitingForLastMove(priority))
        return false;

    Map* map = bot->GetMap();
    const float x = bot->GetPositionX();
    const float y = bot->GetPositionY();
    const float z = bot->GetPositionZ();
    // Cap retries — each attempt is a PathGenerator::CalculatePath. 3 is enough
    // to escape a bad roll without spamming mmap at high bot counts.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        float distance = (0.4f + rand_norm() * 0.6f) * moveStep;
        float angle = (float)rand_norm() * 2 * static_cast<float>(M_PI);
        float dx = x + distance * cos(angle);
        float dy = y + distance * sin(angle);
        float dz = z;

        PathGenerator path(bot);
        path.CalculatePath(dx, dy, dz);
        PathType type = path.GetPathType();
        uint32 typeOk = PATHFIND_NORMAL | PATHFIND_INCOMPLETE | PATHFIND_FARFROMPOLY;
        bool canReach = !(type & (~typeOk));

        if (!canReach)
            continue;

        if (!map->CanReachPositionAndGetValidCoords(bot, dx, dy, dz))
            continue;

        if (map->IsInWater(bot->GetPhaseMask(), dx, dy, dz, bot->GetCollisionHeight()))
            continue;

        bool moved = MoveTo(bot->GetMapId(), dx, dy, dz, false, false, false, true, priority);
        if (moved)
            return true;
    }

    return false;
}

bool NewRpgBaseAction::ForceToWait(uint32 duration, MovementPriority priority)
{
    AI_VALUE(LastMovement&, "last movement")
        .Set(bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), bot->GetOrientation(),
             duration, priority);
    return true;
}

/// @TODO: Fix redundant code
/// Quest related method refer to TalkToQuestGiverAction.h
bool NewRpgBaseAction::InteractWithNpcOrGameObjectForQuest(ObjectGuid guid)
{
    WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);
    if (!object || !bot->CanInteractWithQuestGiver(object))
        return false;

    // Creature* creature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    // if (creature)
    // {
    //     WorldPacket packet(CMSG_GOSSIP_HELLO);
    //     packet << guid;
    //     bot->GetSession()->HandleGossipHelloOpcode(packet);
    // }

    bot->PrepareQuestMenu(guid);
    const QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();
    if (menu.Empty())
        return true;

    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;

        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_NONE && bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) &&
            IsQuestWorthDoing(quest) && IsQuestCapableDoing(quest))
        {
            AcceptQuest(quest, guid);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "new_rpg_quest_accepted",
                    "Quest accepted %quest",
                    {{"%quest", ChatHelper::FormatQuest(quest)}}));
            BroadcastHelper::BroadcastQuestAccepted(botAI, bot, quest);
            botAI->rpgStatistic.questAccepted++;
            LOG_DEBUG("playerbots", "[New RPG] {} accept quest {}", bot->GetName(), quest->GetQuestId());
        }
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            TurnInQuest(quest, guid);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "new_rpg_quest_rewarded",
                    "Quest rewarded %quest",
                    {{"%quest", ChatHelper::FormatQuest(quest)}}));
            BroadcastHelper::BroadcastQuestTurnedIn(botAI, bot, quest);
            botAI->rpgStatistic.questRewarded++;
            LOG_DEBUG("playerbots", "[New RPG] {} turned in quest {}", bot->GetName(), quest->GetQuestId());
        }
    }
    return true;
}

bool NewRpgBaseAction::CanInteractWithQuestGiver(Object* questGiver)
{
    // This is a variant of Player::CanInteractWithQuestGiver
    // that removes the distance check and keeps all other checks
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT: // Player::GetNPCIfCanInteractWith
        {
            ObjectGuid guid = questGiver->GetGUID();

            // unit checks
            if (!guid)
                return false;

            if (!bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
                return false;

            if (bot->IsInFlight())
                return false;

            // exist (we need look pets also for some interaction (quest/etc)
            Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
            if (!creature)
                return false;

            // Deathstate checks
            if (!bot->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_VISIBLE_TO_GHOSTS))
                return false;

            // alive or spirit healer
            if (!creature->IsAlive() &&
                !(creature->GetCreatureTemplate()->type_flags & CREATURE_TYPE_FLAG_INTERACT_WHILE_DEAD))
                return false;

            // appropriate npc type
            if (!creature->HasNpcFlag(UNIT_NPC_FLAG_QUESTGIVER))
                return false;

            // not allow interaction under control, but allow with own pets
            if (creature->GetCharmerGUID())
                return false;

            // xinef: perform better check
            if (creature->GetReactionTo(bot) <= REP_UNFRIENDLY)
                return false;

            return true;
        }
        case TYPEID_GAMEOBJECT: // Player::GetGameObjectIfCanInteractWith
        {
            ObjectGuid guid = questGiver->GetGUID();

            if (GameObject* go = bot->GetMap()->GetGameObject(guid))
            {
                if (go->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
                {
                    // Players cannot interact with gameobjects that use the "Point" icon
                    if (go->GetGOInfo()->IconName == "Point")
                        return false;

                    return true;
                }
            }

            return false;
        }
        // unused for now
        // case TYPEID_PLAYER:
        //     return bot->IsAlive() && questGiver->ToPlayer()->IsAlive();
        // case TYPEID_ITEM:
        //     return bot->IsAlive();
        default:
            break;
    }
    return false;
}

bool NewRpgBaseAction::IsWithinInteractionDist(Object* questGiver)
{
    // This is a variant of Player::CanInteractWithQuestGiver
    // that only keep the distance check
    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            // unit checks
            if (!guid)
                return false;

            // exist (we need look pets also for some interaction (quest/etc)
            Creature* creature = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
            if (!creature)
                return false;

            if (!creature->IsWithinDistInMap(bot, INTERACTION_DISTANCE))
                return false;

            return true;
        }
        case TYPEID_GAMEOBJECT:
        {
            ObjectGuid guid = questGiver->GetGUID();
            if (GameObject* go = bot->GetMap()->GetGameObject(guid))
            {
                if (go->IsWithinDistInMap(bot))
                {
                    return true;
                }
            }
            return false;
        }
        // case TYPEID_PLAYER:
        //     return bot->IsAlive() && questGiver->ToPlayer()->IsAlive();
        // case TYPEID_ITEM:
        //     return bot->IsAlive();
        default:
            break;
    }
    return false;
}

bool NewRpgBaseAction::AcceptQuest(Quest const* quest, ObjectGuid guid)
{
    WorldPacket p(CMSG_QUESTGIVER_ACCEPT_QUEST);
    uint32 unk1 = 0;
    p << guid << quest->GetQuestId() << unk1;
    p.rpos(0);
    bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(p);

    return true;
}

bool NewRpgBaseAction::TurnInQuest(Quest const* quest, ObjectGuid guid)
{
    uint32 questID = quest->GetQuestId();

    if (bot->GetQuestRewardStatus(questID))
    {
        return false;
    }

    if (!bot->CanRewardQuest(quest, false))
    {
        return false;
    }

    bot->PlayDistanceSound(621);

    WorldPacket p(CMSG_QUESTGIVER_CHOOSE_REWARD);
    p << guid << quest->GetQuestId();
    if (quest->GetRewChoiceItemsCount() <= 1)
    {
        p << 0;
        bot->GetSession()->HandleQuestgiverChooseRewardOpcode(p);
    }
    else
    {
        uint32 bestId = BestRewardIndex(quest);
        p << bestId;
        bot->GetSession()->HandleQuestgiverChooseRewardOpcode(p);
    }

    return true;
}

uint32 NewRpgBaseAction::BestRewardIndex(Quest const* quest)
{
    ItemIds returnIds;
    ItemUsage bestUsage = ITEM_USAGE_NONE;
    if (quest->GetRewChoiceItemsCount() <= 1)
        return 0;
    else
    {
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                bestUsage = ITEM_USAGE_EQUIP;
            else if (usage == ITEM_USAGE_BAD_EQUIP && bestUsage != ITEM_USAGE_EQUIP)
                bestUsage = usage;
            else if (usage != ITEM_USAGE_NONE && bestUsage == ITEM_USAGE_NONE)
                bestUsage = usage;
        }
        StatsWeightCalculator calc(bot);
        uint32 best = 0;
        float bestScore = 0;
        for (uint8 i = 0; i < quest->GetRewChoiceItemsCount(); ++i)
        {
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", quest->RewardChoiceItemId[i]);
            if (usage == bestUsage || usage == ITEM_USAGE_REPLACE)
            {
                float score = calc.CalculateItem(quest->RewardChoiceItemId[i]);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = i;
                }
            }
        }
        return best;
    }
}

bool NewRpgBaseAction::IsQuestWorthDoing(Quest const* quest)
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    uint8 const level = PlayerbotGroupProgression::GetQuestLevelRef(activeBot);

    if (PlayerbotGroupProgression::IsQuestTrivialForLevel(level, quest))
        return false;

    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot) &&
        PlayerbotGroupProgression::IsQuestBelowProgressionLevel(level, quest))
        return false;

    if (quest->IsRepeatable())
        return false;

    if (quest->IsSeasonal())
        return false;

    return true;
}

bool NewRpgBaseAction::IsQuestCapableDoing(Quest const* quest)
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    uint8 const levelRef = PlayerbotGroupProgression::GetQuestLevelRef(activeBot);
    if (levelRef + 3 < activeBot->GetQuestLevel(quest))
        return false;

    // Elite quest and dungeon quest etc
    if (quest->GetType() != 0)
        return false;

    if (quest->GetSuggestedPlayers() >= 2)
    {
        Group* group = activeBot->GetGroup();
        if (group && sRandomPlayerbotMgr.IsBotLedNearbyGroup(group) && group->IsLeader(activeBot->GetGUID()))
        {
            uint32 aliveCount = 0;
            for (GroupReference const* itr = group->GetFirstMember(); itr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (member && member->IsAlive())
                    ++aliveCount;
            }

            if (aliveCount >= quest->GetSuggestedPlayers())
                return true;
        }

        return false;
    }

    return true;
}

bool NewRpgBaseAction::OrganizeQuestLog()
{
    int32 freeSlotNum = 0;

    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            freeSlotNum++;
    }

    // it's ok if we have two more free slots
    if (freeSlotNum >= 2)
        return false;

    int32 dropped = 0;
    // remove quests that not worth doing or not capable of doing
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        bool shouldDrop = bot->GetQuestStatus(questId) == QUEST_STATUS_FAILED || !IsQuestCapableDoing(quest) ||
                          quest->IsRepeatable() || quest->IsSeasonal();

        if (!shouldDrop && sPlayerbotAIConfig.dropObsoleteQuests)
        {
            uint8 const levelRef = PlayerbotGroupProgression::GetQuestLevelRef(bot);
            shouldDrop = PlayerbotGroupProgression::IsQuestTrivialForLevel(levelRef, quest);
            if (!shouldDrop && sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(bot))
                shouldDrop = PlayerbotGroupProgression::IsQuestBelowProgressionLevel(levelRef, quest);
        }

        if (shouldDrop)
        {
            LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
            WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
            packet << (uint8)i;
            WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
            removeQuest.Read();
            bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "new_rpg_quest_dropped",
                    "Quest dropped %quest",
                    {{"%quest", ChatHelper::FormatQuest(quest)}}));
            botAI->rpgStatistic.questDropped++;
            dropped++;
        }
    }

    // drop more than 8 quests at once to avoid repeated accept and drop
    if (dropped >= 8)
        return true;

    // remove festival/class quests and quests in different zone
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        const int64_t botZoneId = this->bot->GetZoneId();

        if (quest->GetZoneOrSort() < 0 || (quest->GetZoneOrSort() > 0 && quest->GetZoneOrSort() != botZoneId))
        {
            LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
            WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
            packet << (uint8)i;
            WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
            removeQuest.Read();
            bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
            if (botAI->GetMaster())
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "new_rpg_quest_dropped",
                    "Quest dropped %quest",
                    {{"%quest", ChatHelper::FormatQuest(quest)}}));
            botAI->rpgStatistic.questDropped++;
            dropped++;
        }
    }

    if (dropped >= 8)
        return true;

    // clear quests log
    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
        LOG_DEBUG("playerbots", "[New RPG] {} drop quest {}", bot->GetName(), questId);
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << (uint8)i;
        WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
        removeQuest.Read();
        bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
        if (botAI->GetMaster())
            botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "new_rpg_quest_dropped",
                "Quest dropped %quest",
                {{"%quest", ChatHelper::FormatQuest(quest)}}));
        botAI->rpgStatistic.questDropped++;
    }

    return true;
}

bool NewRpgBaseAction::PruneObsoleteQuests()
{
    if (!sPlayerbotAIConfig.dropObsoleteQuests)
        return false;

    if (!sRandomPlayerbotMgr.ShouldUseOpenWorldProgressionChecks(bot))
        return false;

    uint8 const progressionLevel = PlayerbotGroupProgression::GetQuestLevelRef(bot);
    bool dropped = false;

    for (uint16 i = 0; i < MAX_QUEST_LOG_SIZE; ++i)
    {
        uint32 questId = bot->GetQuestSlotQuestId(i);
        if (!questId)
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest)
            continue;

        if (!PlayerbotGroupProgression::IsQuestTrivialForLevel(progressionLevel, quest) &&
            IsQuestWorthDoing(quest) && IsQuestCapableDoing(quest))
            continue;

        LOG_DEBUG("playerbots", "[New RPG] {} prune obsolete quest {}", bot->GetName(), questId);
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << (uint8)i;
        WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
        removeQuest.Read();
        bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
        botAI->lowPriorityQuest.insert(questId);
        botAI->rpgStatistic.questDropped++;
        dropped = true;

        if (botAI->rpgInfo.GetStatus() == RPG_DO_QUEST)
        {
            auto* dataPtr = std::get_if<NewRpgInfo::DoQuest>(&botAI->rpgInfo.data);
            if (dataPtr && dataPtr->questId == questId)
                botAI->rpgInfo.ChangeToIdle();
        }
    }

    return dropped;
}

bool NewRpgBaseAction::SearchQuestGiverAndAcceptOrReward()
{
    PruneObsoleteQuests();
    OrganizeQuestLog();
    if (ObjectGuid npcOrGo = ChooseNpcOrGameObjectToInteract(true, 80.0f))
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, npcOrGo);
        if (bot->CanInteractWithQuestGiver(object))
        {
            InteractWithNpcOrGameObjectForQuest(npcOrGo);
            ForceToWait(5000);
            return true;
        }
        return MoveWorldObjectTo(npcOrGo);
    }
    return false;
}

ObjectGuid NewRpgBaseAction::ChooseNpcOrGameObjectToInteract(bool questgiverOnly, float distanceLimit)
{
    GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
    GuidVector possibleGameObjects = AI_VALUE(GuidVector, "possible new rpg game objects");

    if (possibleTargets.empty() && possibleGameObjects.empty())
        return ObjectGuid();

    WorldObject* nearestObject = nullptr;
    for (ObjectGuid& guid : possibleTargets)
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);

        if (!object || !object->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(object) > distanceLimit)
            continue;

        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            if (!nearestObject || bot->GetExactDist(nearestObject) > bot->GetExactDist(object))
                nearestObject = object;
            break;
        }
    }

    for (ObjectGuid& guid : possibleGameObjects)
    {
        WorldObject* object = ObjectAccessor::GetWorldObject(*bot, guid);

        if (!object || !object->IsInWorld())
            continue;

        if (distanceLimit && bot->GetDistance(object) > distanceLimit)
            continue;

        if (CanInteractWithQuestGiver(object) && HasQuestToAcceptOrReward(object))
        {
            if (!nearestObject || bot->GetExactDist(nearestObject) > bot->GetExactDist(object))
                nearestObject = object;
            break;
        }
    }

    if (nearestObject)
        return nearestObject->GetGUID();

    // No questgiver to accept or reward
    if (questgiverOnly)
        return ObjectGuid();

    if (possibleTargets.empty())
        return ObjectGuid();

    int idx = urand(0, possibleTargets.size() - 1);
    ObjectGuid guid = possibleTargets[idx];
    WorldObject* object = ObjectAccessor::GetCreatureOrPetOrVehicle(*bot, guid);
    if (!object)
        object = ObjectAccessor::GetGameObject(*bot, guid);

    if (object && object->IsInWorld())
    {
        return object->GetGUID();
    }
    return ObjectGuid();
}

bool NewRpgBaseAction::HasQuestToAcceptOrReward(WorldObject* object)
{
    ObjectGuid guid = object->GetGUID();
    bot->PrepareQuestMenu(guid);
    const QuestMenu& menu = bot->PlayerTalkClass->GetQuestMenu();
    if (menu.Empty())
        return false;

    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;
        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_COMPLETE && bot->CanRewardQuest(quest, 0, false))
        {
            return true;
        }
    }
    for (uint8 idx = 0; idx < menu.GetMenuItemCount(); idx++)
    {
        const QuestMenuItem& item = menu.GetItem(idx);
        const Quest* quest = sObjectMgr->GetQuestTemplate(item.QuestId);
        if (!quest)
            continue;

        const QuestStatus& status = bot->GetQuestStatus(item.QuestId);
        if (status == QUEST_STATUS_NONE && bot->CanTakeQuest(quest, false) && bot->CanAddQuest(quest, false) &&
            IsQuestWorthDoing(quest) && IsQuestCapableDoing(quest))
        {
            return true;
        }
    }
    return false;
}

static std::vector<float> GenerateRandomWeights(int n)
{
    std::vector<float> weights(n);
    float sum = 0.0;

    for (int i = 0; i < n; ++i)
    {
        weights[i] = rand_norm();
        sum += weights[i];
    }
    for (int i = 0; i < n; ++i)
    {
        weights[i] /= sum;
    }
    return weights;
}

float NewRpgBaseAction::GetQuestPoiMaxDistance() const
{
    float maxDistance = 1500.0f;
    Player* activeBot = GetValidBot();
    if (activeBot && sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot) && activeBot->GetLevel() < 10)
        maxDistance = 3500.0f;

    return maxDistance;
}

bool NewRpgBaseAction::GetQuestPOIPosAndObjectiveIdx(uint32 questId, std::vector<POIInfo>& poiInfo, bool toComplete)
{
    RefreshBot();
    if (!GetValidBot())
        return false;

    Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
    if (!quest)
        return false;

    const QuestPOIVector* poiVector = sObjectMgr->GetQuestPOIVector(questId);
    if (!poiVector)
    {
        return false;
    }

    const QuestStatusData& q_status = bot->getQuestStatusMap().at(questId);

    if (toComplete && q_status.Status == QUEST_STATUS_COMPLETE)
    {
        for (const QuestPOI& qPoi : *poiVector)
        {
            if (qPoi.MapId != bot->GetMapId())
                continue;

            // not the poi pos to reward quest
            if (qPoi.ObjectiveIndex != -1)
                continue;

            if (qPoi.points.size() == 0)
                continue;

            float dx = 0, dy = 0;
            std::vector<float> weights = GenerateRandomWeights(qPoi.points.size());
            for (size_t i = 0; i < qPoi.points.size(); i++)
            {
                const QuestPOIPoint& point = qPoi.points[i];
                dx += point.x * weights[i];
                dy += point.y * weights[i];
            }

            if (bot->GetDistance2d(dx, dy) >= GetQuestPoiMaxDistance())
                continue;

            float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

            if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
                continue;

            if (bot->GetZoneId() != bot->GetMap()->GetZoneId(bot->GetPhaseMask(), dx, dy, dz))
                continue;

            poiInfo.push_back({{dx, dy}, qPoi.ObjectiveIndex});
        }

        if (poiInfo.empty())
            return false;

        return true;
    }

    if (q_status.Status != QUEST_STATUS_INCOMPLETE)
        return false;

    // Get incomplete quest objective index
    std::vector<int32> incompleteObjectiveIdx;
    for (int i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
    {
        int32 npcOrGo = quest->RequiredNpcOrGo[i];
        if (!npcOrGo)
            continue;

        if (q_status.CreatureOrGOCount[i] < quest->RequiredNpcOrGoCount[i])
            incompleteObjectiveIdx.push_back(i);
    }
    for (int i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
    {
        uint32 itemId = quest->RequiredItemId[i];
        if (!itemId)
            continue;

        if (q_status.ItemCount[i] < quest->RequiredItemCount[i])
            incompleteObjectiveIdx.push_back(QUEST_OBJECTIVES_COUNT + i);
    }

    float const maxPoiDistance = GetQuestPoiMaxDistance();

    // Get POIs to go
    for (const QuestPOI& qPoi : *poiVector)
    {
        if (qPoi.MapId != bot->GetMapId())
            continue;

        bool inComplete = false;
        for (uint32 objective : incompleteObjectiveIdx)
        {
            if (qPoi.ObjectiveIndex == static_cast<int32>(objective))
            {
                inComplete = true;
                break;
            }
        }
        if (!inComplete)
            continue;
        if (qPoi.points.size() == 0)
            continue;
        float dx = 0, dy = 0;
        std::vector<float> weights = GenerateRandomWeights(qPoi.points.size());
        for (size_t i = 0; i < qPoi.points.size(); i++)
        {
            const QuestPOIPoint& point = qPoi.points[i];
            dx += point.x * weights[i];
            dy += point.y * weights[i];
        }

        if (bot->GetDistance2d(dx, dy) >= maxPoiDistance)
            continue;

        float dz = std::max(bot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), bot->GetMap()->GetWaterLevel(dx, dy));

        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            continue;

        if (bot->GetZoneId() != bot->GetMap()->GetZoneId(bot->GetPhaseMask(), dx, dy, dz))
            continue;

        poiInfo.push_back({{dx, dy}, qPoi.ObjectiveIndex});
    }

    if (poiInfo.size() == 0)
    {
        // LOG_DEBUG("playerbots", "[New rpg] {}: No available poi can be found for quest {}", bot->GetName(), questId);
        return false;
    }

    return true;
}

WorldPosition NewRpgBaseAction::SelectRandomGrindPos(Player* bot, bool forceRelocate)
{
    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        return WorldPosition{};

    uint8 grindLevel = bot->GetLevel();
    bool const useProgression = sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(bot);
    if (useProgression)
        grindLevel = PlayerbotGroupProgression::GetProgressionLevel(bot);

    const std::vector<WorldLocation>& locs = sTravelMgr.GetLocsPerLevelCache(grindLevel);
    float hiRange = 500.0f;
    float loRange = 2500.0f;
    std::vector<WorldLocation> lo_prepared_locs, hi_prepared_locs;

    bool inCity = false;
    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    bool const currentZoneUnderleveled =
        useProgression && PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(bot);

    for (auto& loc : locs)
    {
        if (bot->GetMapId() != loc.GetMapId())
            continue;

        if (bot->GetExactDist(loc) > loRange)
            continue;

        uint32 const locZoneId = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(), loc.GetPositionY(),
                                                          loc.GetPositionZ());

        // Stay in-zone while leveling normally; when the current zone is underleveled,
        // allow same-map cross-zone camps that match the progression bracket.
        if (!inCity && !currentZoneUnderleveled && locZoneId != bot->GetZoneId())
            continue;

        if (useProgression && !sTravelMgr.IsZoneAppropriateForLevel(locZoneId, grindLevel))
            continue;

        if (bot->GetExactDist(loc) < hiRange)
            hi_prepared_locs.push_back(loc);

        lo_prepared_locs.push_back(loc);
    }

    // If stuck in an underleveled zone with no valid same-map camps, widen to any
    // level-appropriate camps on this map (ignore distance) so callers can walk/tele.
    if (lo_prepared_locs.empty() && currentZoneUnderleveled)
    {
        for (auto& loc : locs)
        {
            if (bot->GetMapId() != loc.GetMapId())
                continue;

            uint32 const locZoneId = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(), loc.GetPositionY(),
                                                              loc.GetPositionZ());
            if (!sTravelMgr.IsZoneAppropriateForLevel(locZoneId, grindLevel))
                continue;

            lo_prepared_locs.push_back(loc);
            if (bot->GetExactDist(loc) < hiRange)
                hi_prepared_locs.push_back(loc);
        }
    }

    WorldPosition dest{};
    if (forceRelocate && !lo_prepared_locs.empty())
    {
        uint32 bestIdx = 0;
        float bestDist = bot->GetExactDist(lo_prepared_locs[0]);
        for (uint32 i = 1; i < lo_prepared_locs.size(); ++i)
        {
            float dist = bot->GetExactDist(lo_prepared_locs[i]);
            if (dist > bestDist)
            {
                bestDist = dist;
                bestIdx = i;
            }
        }
        dest = lo_prepared_locs[bestIdx];
    }
    else if (urand(1, 100) <= 50 && !hi_prepared_locs.empty())
    {
        uint32 idx = urand(0, hi_prepared_locs.size() - 1);
        dest = hi_prepared_locs[idx];
    }
    else if (!lo_prepared_locs.empty())
    {
        uint32 idx = urand(0, lo_prepared_locs.size() - 1);
        dest = lo_prepared_locs[idx];
    }
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random grind pos (lvl {}) Map:{} X:{} Y:{} Z:{} ({}+{} available in {})",
              bot->GetName(), grindLevel, dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
              hi_prepared_locs.size(), lo_prepared_locs.size() - hi_prepared_locs.size(), locs.size());
    return dest;
}

WorldPosition NewRpgBaseAction::SelectRandomCampPos(Player* bot)
{
    if (!bot || !bot->IsInWorld() || bot->IsDuringRemoveFromWorld())
        return WorldPosition{};

    const std::vector<WorldLocation> locs = sTravelMgr.GetTravelHubs(bot);

    bool inCity = false;

    if (AreaTableEntry const* zone = sAreaTableStore.LookupEntry(bot->GetZoneId()))
    {
        if (zone->flags & AREA_FLAG_CAPITAL)
            inCity = true;
    }

    std::vector<WorldLocation> prepared_locs;
    for (auto& loc : locs)
    {
        if (bot->GetMapId() != loc.GetMapId())
            continue;

        float range = 2500.0f;
        if (bot->GetExactDist(loc) > range)
            continue;

        if (bot->GetExactDist(loc) < 50.0f)
            continue;

        if (!inCity && bot->GetMap()->GetZoneId(bot->GetPhaseMask(), loc.GetPositionX(), loc.GetPositionY(),
                                                loc.GetPositionZ()) != bot->GetZoneId())
            continue;

        prepared_locs.push_back(loc);
    }
    WorldPosition dest{};
    if (!prepared_locs.empty())
    {
        uint32 idx = urand(0, prepared_locs.size() - 1);
        dest = prepared_locs[idx];
    }
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random inn keeper pos Map:{} X:{} Y:{} Z:{} ({} available in {})",
              bot->GetName(), dest.GetMapId(), dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(),
              prepared_locs.size(), locs.size());
    return dest;
}

bool NewRpgBaseAction::SelectRandomFlightTaxiNode(uint32& flightMasterEntry, WorldPosition& flightMasterPos, std::vector<uint32>& path)
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    TravelMgr::FlightMasterInfo const* info = sTravelMgr.GetNearestFlightMasterInfo(activeBot);
    if (!info)
        return false;

    std::vector<std::vector<uint32>> availablePaths = sTravelMgr.GetOptimalFlightDestinations(activeBot);
    if (availablePaths.empty())
        return false;

    flightMasterEntry = info->templateEntry;
    flightMasterPos = info->pos;
    path = availablePaths[urand(0, availablePaths.size() - 1)];
    LOG_DEBUG("playerbots", "[New RPG] Bot {} select random flight taxi node from:{} (node {}) to:{} ({} available)",
              activeBot->GetName(), flightMasterEntry, path[0], path[path.size() - 1], availablePaths.size());
    return true;
}

bool NewRpgBaseAction::RandomChangeStatus(std::vector<NewRpgStatus> candidateStatus)
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    // Wrong-zone escape: leave underleveled zones before picking grind/wander work.
    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot) &&
        PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(activeBot))
    {
        if (TryHardRelocateForWrongZone())
            return true;
    }

    NewRpgStatus chosenStatus = RPG_STATUS_END;

    // Hard-prefer DoQuest for open-world progression bots when a worthwhile quest is
    // actually available. Bypass RpgStatusProbWeight==0 for this availability check;
    // weights still apply to the fallback roll when DoQuest is unavailable.
    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot))
    {
        bool doQuestCandidate = false;
        for (NewRpgStatus status : candidateStatus)
        {
            if (status == RPG_DO_QUEST)
            {
                doQuestCandidate = true;
                break;
            }
        }

        if (doQuestCandidate && CheckRpgStatusAvailable(RPG_DO_QUEST))
        {
            chosenStatus = RPG_DO_QUEST;
            LOG_DEBUG("playerbots", "[New RPG] Bot {} hard-prefers DoQuest (open-world progression)",
                      activeBot->GetName());
        }
    }

    if (chosenStatus == RPG_STATUS_END)
    {
        std::vector<NewRpgStatus> availableStatus;
        uint32 probSum = 0;
        for (NewRpgStatus status : candidateStatus)
        {
            if (sPlayerbotAIConfig.RpgStatusProbWeight[status] == 0)
                continue;

            // Avoid starting a grind camp while the current zone is clearly underleveled.
            if (status == RPG_GO_GRIND && sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot) &&
                PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(activeBot))
                continue;

            if (CheckRpgStatusAvailable(status))
            {
                availableStatus.push_back(status);
                probSum += sPlayerbotAIConfig.RpgStatusProbWeight[status];
            }
        }
        // Safety check. Default to "rest" if all RPG weights = 0
        if (availableStatus.empty() || probSum == 0)
        {
            botAI->rpgInfo.ChangeToRest();
            activeBot->SetStandState(UNIT_STAND_STATE_SIT);
            return true;
        }
        uint32 rand = urand(1, probSum);
        uint32 accumulate = 0;
        for (NewRpgStatus status : availableStatus)
        {
            accumulate += sPlayerbotAIConfig.RpgStatusProbWeight[status];
            if (accumulate >= rand)
            {
                chosenStatus = status;
                break;
            }
        }
    }

    switch (chosenStatus)
    {
        case RPG_WANDER_RANDOM:
        {
            botAI->rpgInfo.ChangeToWanderRandom();
            return true;
        }
        case RPG_WANDER_NPC:
        {
            botAI->rpgInfo.ChangeToWanderNpc();
            return true;
        }
        case RPG_GO_GRIND:
        {
            WorldPosition pos = SelectRandomGrindPos(activeBot);
            if (pos != WorldPosition())
            {
                botAI->rpgInfo.ChangeToGoGrind(pos);
                return true;
            }
            return false;
        }
        case RPG_GO_CAMP:
        {
            WorldPosition pos = SelectRandomCampPos(activeBot);
            if (pos != WorldPosition())
            {
                botAI->rpgInfo.ChangeToGoCamp(pos);
                return true;
            }
            return false;
        }
        case RPG_DO_QUEST:
        {
            std::vector<uint32> availableQuests;
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = activeBot->GetQuestSlotQuestId(slot);
                if (botAI->lowPriorityQuest.find(questId) != botAI->lowPriorityQuest.end())
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest || !IsQuestWorthDoing(quest) || !IsQuestCapableDoing(quest))
                    continue;

                std::vector<POIInfo> poiInfo;
                if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true) && FilterQuestPoiForNearbyGroup(poiInfo))
                {
                    availableQuests.push_back(questId);
                }
            }
            if (availableQuests.size())
            {
                uint32 questId = availableQuests[0];
                uint32 bestScore = 0;
                for (uint32 id : availableQuests)
                {
                    Quest const* candidate = sObjectMgr->GetQuestTemplate(id);
                    if (!candidate)
                        continue;

                    uint32 score = ScoreQuestForProgression(activeBot, candidate);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        questId = id;
                    }
                }

                const Quest* quest = sObjectMgr->GetQuestTemplate(questId);
                if (quest)
                {
                    botAI->rpgInfo.ChangeToDoQuest(questId, quest);
                    return true;
                }
            }
            return false;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            uint32 flightMasterEntry = 0;
            WorldPosition flightMasterPos;
            std::vector<uint32> path;
            if (SelectRandomFlightTaxiNode(flightMasterEntry, flightMasterPos, path))
            {
                botAI->rpgInfo.ChangeToTravelFlight(flightMasterEntry, flightMasterPos, path);
                return true;
            }
            return false;
        }
        case RPG_IDLE:
        {
            botAI->rpgInfo.ChangeToIdle();
            return true;
        }
        case RPG_REST:
        {
            botAI->rpgInfo.ChangeToRest();
            activeBot->SetStandState(UNIT_STAND_STATE_SIT);
            return true;
        }
        case RPG_OUTDOOR_PVP:
        {
            botAI->rpgInfo.ChangeToOutdoorPvp();
            return true;
        }
        default:
            break;
    }

    return false;
}

bool NewRpgBaseAction::CheckRpgStatusAvailable(NewRpgStatus status)
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    switch (status)
    {
        case RPG_IDLE:
        case RPG_REST:
            return true;
        case RPG_WANDER_RANDOM:
        {
            Unit* target = AI_VALUE(Unit*, "grind target");
            return target != nullptr;
        }
        case RPG_GO_GRIND:
        {
            WorldPosition pos = SelectRandomGrindPos(activeBot);
            return pos != WorldPosition();
        }
        case RPG_GO_CAMP:
        {
            WorldPosition pos = SelectRandomCampPos(activeBot);
            return pos != WorldPosition();
        }
        case RPG_WANDER_NPC:
        {
            GuidVector possibleTargets = AI_VALUE(GuidVector, "possible new rpg targets");
            return possibleTargets.size() >= 3;
        }
        case RPG_DO_QUEST:
        {
            std::vector<uint32> availableQuests;
            for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 questId = activeBot->GetQuestSlotQuestId(slot);
                if (botAI->lowPriorityQuest.find(questId) != botAI->lowPriorityQuest.end())
                    continue;

                Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
                if (!quest || !IsQuestWorthDoing(quest) || !IsQuestCapableDoing(quest))
                    continue;

                std::vector<POIInfo> poiInfo;
                if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true) && FilterQuestPoiForNearbyGroup(poiInfo))
                    return true;
            }
            return false;
        }
        case RPG_TRAVEL_FLIGHT:
        {
            if (IsBotLedNearbyGroupBot())
                return false;

            uint32 flightMasterEntry = 0;
            WorldPosition flightMasterPos;
            std::vector<uint32> path;
            return SelectRandomFlightTaxiNode(flightMasterEntry, flightMasterPos, path);
        }
        case RPG_OUTDOOR_PVP:
        {
            if (!activeBot->IsPvP())
                return false;
            uint32 zoneId = activeBot->GetZoneId();
            if (zoneId == AREA_NAGRAND)
                return false;

            OutdoorPvP* outdoorPvP = sOutdoorPvPMgr->GetOutdoorPvPToZoneId(zoneId);
            return outdoorPvP != nullptr;
        }
        default:
            return false;
    }
    return false;
}

bool NewRpgBaseAction::IsBotLedNearbyGroupBot() const
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    Group* group = activeBot->GetGroup();
    return group && sRandomPlayerbotMgr.IsBotLedNearbyGroup(group);
}

bool NewRpgBaseAction::FilterQuestPoiForNearbyGroup(std::vector<POIInfo>& poiInfo) const
{
    if (!IsBotLedNearbyGroupBot() || poiInfo.empty())
        return !poiInfo.empty();

    Player* activeBot = GetValidBot();
    if (!activeBot)
        return !poiInfo.empty();

    Group* group = activeBot->GetGroup();
    float const partyRadius = PlayerbotGroupProgression::GetNearbyPartyRadius();
    float leadRadius = partyRadius * 2.0f;
    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgressionChecks(activeBot) && activeBot->GetLevel() < 10)
        leadRadius = std::max(leadRadius, GetQuestPoiMaxDistance());

    std::vector<POIInfo> filtered;
    std::vector<POIInfo> relaxed;

    for (POIInfo const& poi : poiInfo)
    {
        float dx = poi.pos.x;
        float dy = poi.pos.y;
        float dz = std::max(activeBot->GetMap()->GetHeight(dx, dy, MAX_HEIGHT), activeBot->GetMap()->GetWaterLevel(dx, dy));
        if (dz == INVALID_HEIGHT || dz == VMAP_INVALID_HEIGHT_VALUE)
            continue;

        WorldPosition pos(activeBot->GetMapId(), dx, dy, dz);
        if (activeBot->GetDistance(pos) > leadRadius)
            continue;

        relaxed.push_back(poi);

        bool partyCanFollow = true;
        for (GroupReference* gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->GetSource();
            if (!member || !member->IsAlive() || member == activeBot)
                continue;

            if (member->GetMapId() != activeBot->GetMapId() || member->GetDistance(activeBot) > partyRadius)
            {
                partyCanFollow = false;
                break;
            }
        }

        if (partyCanFollow)
            filtered.push_back(poi);
    }

    if (!filtered.empty())
    {
        poiInfo = std::move(filtered);
        return true;
    }

    if (!relaxed.empty())
    {
        poiInfo = std::move(relaxed);
        return true;
    }

    return false;
}

bool NewRpgBaseAction::HasLevelAppropriateContentNearby()
{
    // Underleveled zones are never "appropriate" even if a few edge-band mobs remain.
    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(bot) &&
        PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(bot))
        return false;

    if (Unit* target = AI_VALUE(Unit*, "grind target"))
        return true; // GrindTargetValue already applied OW band / quest-needed rules.

    for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 questId = bot->GetQuestSlotQuestId(slot);
        if (!questId || botAI->lowPriorityQuest.find(questId) != botAI->lowPriorityQuest.end())
            continue;

        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        if (!quest || !IsQuestWorthDoing(quest) || !IsQuestCapableDoing(quest))
            continue;

        std::vector<POIInfo> poiInfo;
        if (GetQuestPOIPosAndObjectiveIdx(questId, poiInfo, true) && FilterQuestPoiForNearbyGroup(poiInfo))
            return true;
    }

    return false;
}

uint32 NewRpgBaseAction::ScoreQuestForProgression(Player* bot, Quest const* quest)
{
    if (!bot || !quest)
        return 0;

    uint8 const levelRef = PlayerbotGroupProgression::GetQuestLevelRef(bot);
    int32 questLevel = quest->GetQuestLevel();
    if (questLevel < 0)
        questLevel = levelRef;

    // Prefer quests near progression level; slight bias toward on-level or +1/+2.
    int32 delta = questLevel - int32(levelRef);
    if (delta < 0)
        delta = -delta;
    uint32 score = 1000;
    if (delta > 6)
        score = 100;
    else
        score = 1000 - uint32(delta) * 80;

    if (questLevel >= int32(levelRef) && questLevel <= int32(levelRef) + 2)
        score += 120;

    // Prefer higher XP rewards among near-level quests.
    score += std::min<uint32>(quest->XPValue(levelRef) / 50, 200);

    return score;
}

bool NewRpgBaseAction::TryHardRelocateForWrongZone()
{
    Player* activeBot = GetValidBot();
    if (!activeBot)
        return false;

    if (!sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(activeBot))
        return false;

    if (!PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(activeBot))
        return false;

    Group* group = activeBot->GetGroup();
    bool const nearbyGroup = group && sRandomPlayerbotMgr.IsBotLedNearbyGroup(group);
    if (nearbyGroup && !group->IsLeader(activeBot->GetGUID()))
        return false;

    // Prefer flight to a level-appropriate next hub.
    uint32 flightMasterEntry = 0;
    WorldPosition flightMasterPos;
    std::vector<uint32> path;
    if (SelectRandomFlightTaxiNode(flightMasterEntry, flightMasterPos, path))
    {
        botAI->rpgInfo.ChangeToTravelFlight(flightMasterEntry, flightMasterPos, path);
        LOG_DEBUG("playerbots", "[New RPG] {} hard-relocating via flight from underleveled zone {}",
                  activeBot->GetName(), activeBot->GetZoneId());
        return true;
    }

    // Same-map appropriate grind camp as a walk/teleport stepping stone.
    WorldPosition pos = SelectRandomGrindPos(activeBot, true);
    if (pos != WorldPosition())
    {
        uint32 const destZone = activeBot->GetMap()->GetZoneId(activeBot->GetPhaseMask(), pos.GetPositionX(),
                                                                pos.GetPositionY(), pos.GetPositionZ());
        if (destZone != activeBot->GetZoneId() &&
            sTravelMgr.IsZoneAppropriateForLevel(destZone, PlayerbotGroupProgression::GetProgressionLevel(activeBot)))
        {
            botAI->rpgInfo.ChangeToGoGrind(pos);
            LOG_DEBUG("playerbots", "[New RPG] {} hard-relocating to grind camp in zone {} from underleveled {}",
                      activeBot->GetName(), destZone, activeBot->GetZoneId());
            return true;
        }
    }

    if (nearbyGroup)
    {
        if (sRandomPlayerbotMgr.RelocateNearbyGroupForProgression(activeBot))
        {
            botAI->rpgInfo.ChangeToIdle();
            LOG_DEBUG("playerbots", "[New RPG] {} party hub teleport escaping underleveled zone {}",
                      activeBot->GetName(), activeBot->GetZoneId());
            return true;
        }
        return false;
    }

    sRandomPlayerbotMgr.RandomTeleportForLevel(activeBot);
    botAI->Reset(true);
    botAI->rpgInfo.ChangeToIdle();
    LOG_DEBUG("playerbots", "[New RPG] {} teleporting out of underleveled zone {} for progression",
              activeBot->GetName(), activeBot->GetZoneId());
    return true;
}

bool NewRpgBaseAction::TryRelocateForProgressionStagnation()
{
    if (!sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(bot) &&
        !sRandomPlayerbotMgr.ShouldUseOpenWorldProgressionChecks(bot))
        return false;

    // Wrong-zone escape takes priority over local stagnation recovery.
    if (sRandomPlayerbotMgr.ShouldUseOpenWorldProgression(bot) &&
        PlayerbotGroupProgression::IsCurrentZoneUnderleveledForProgression(bot) &&
        TryHardRelocateForWrongZone())
        return true;

    if (HasLevelAppropriateContentNearby())
        return false;

    Group* group = bot->GetGroup();
    bool const nearbyGroup = group && sRandomPlayerbotMgr.IsBotLedNearbyGroup(group);

    if (nearbyGroup)
    {
        if (sRandomPlayerbotMgr.ShouldLeaveNearbyGroupForProgression(bot))
        {
            botAI->LeaveOrDisbandGroup();
            botAI->rpgInfo.ChangeToIdle();
            botAI->Reset(true);
            LOG_DEBUG("playerbots", "[New RPG] {} leaving nearby group for progression stagnation", bot->GetName());
            return true;
        }

        if (!group->IsLeader(bot->GetGUID()))
            return false;

        WorldPosition pos = SelectRandomGrindPos(bot, true);
        if (pos != WorldPosition())
        {
            botAI->rpgInfo.ChangeToGoGrind(pos);
            LOG_DEBUG("playerbots", "[New RPG] {} leading nearby group to grind pos for progression Map:{} X:{} Y:{} Z:{}",
                      bot->GetName(), pos.GetMapId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
            return true;
        }

        if (sRandomPlayerbotMgr.RelocateNearbyGroupForProgression(bot))
        {
            botAI->rpgInfo.ChangeToIdle();
            LOG_DEBUG("playerbots", "[New RPG] {} party hub teleport for progression stagnation", bot->GetName());
            return true;
        }

        return false;
    }

    WorldPosition pos = SelectRandomGrindPos(bot, true);
    if (pos != WorldPosition())
    {
        botAI->rpgInfo.ChangeToGoGrind(pos);
        LOG_DEBUG("playerbots", "[New RPG] {} relocating to grind pos for progression Map:{} X:{} Y:{} Z:{}",
                  bot->GetName(), pos.GetMapId(), pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ());
        return true;
    }

    sRandomPlayerbotMgr.RandomTeleportForLevel(bot);
    botAI->Reset(true);
    LOG_DEBUG("playerbots", "[New RPG] {} teleporting for progression stagnation", bot->GetName());
    return true;
}
