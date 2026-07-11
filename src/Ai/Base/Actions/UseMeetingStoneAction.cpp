/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "UseMeetingStoneAction.h"

#include "CellImpl.h"
#include "Event.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "GroupInviteHelper.h"
#include "NearestGameObjects.h"
#include "ObjectAccessor.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotTextMgr.h"
#include "Playerbots.h"
#include "PositionValue.h"
#include "InstanceSaveMgr.h"
#include "Map.h"
#include "RandomPlayerbotMgr.h"

bool UseMeetingStoneAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;

    if (master->GetTarget() && master->GetTarget() != bot->GetGUID())
        return false;

    if (!master->GetTarget() && master->GetGroup() != bot->GetGroup())
        return false;

    if (master->IsBeingTeleported())
        return false;

    if (bot->IsInCombat())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "meeting_stone_in_combat", "I am in combat", {}));
        return false;
    }

    Map* map = master->GetMap();
    if (!map)
        return false;

    GameObject* gameObject = map->GetGameObject(guid);
    if (!gameObject)
        return false;

    GameObjectTemplate const* goInfo = gameObject->GetGOInfo();
    if (!goInfo || goInfo->entry != 179944)
        return false;

    return Teleport(master, bot, false);
}

namespace
{
Player* ResolveSummoner(Event& event, PlayerbotAI* botAI, Player* bot)
{
    if (Player* owner = event.getOwner())
    {
        if (owner->GetSession() && !owner->GetSession()->IsBot())
        {
            if (GroupInviteHelper::IsBotOwnedByPlayer(bot, owner))
                return owner;

            if (Group* group = bot->GetGroup())
                if (group->IsMember(owner->GetGUID()))
                    return owner;
        }
    }

    if (Player* master = botAI->GetValidMaster())
        return master;

    return nullptr;
}
}  // namespace

bool SummonAction::CanDirectTeleport(Player* summoner) const
{
    if (!summoner || summoner == bot)
        return false;

    if (summoner->IsGameMaster() || summoner->CanBeGameMaster())
        return true;

    if (summoner->GetSession()->GetSecurity() > SEC_PLAYER)
        return true;

    if (GroupInviteHelper::IsBotOwnedByPlayer(bot, summoner))
        return true;

    if (Group* const botGroup = bot->GetGroup())
    {
        if (botGroup->IsMember(summoner->GetGUID()))
            return true;
    }

    return false;
}

bool SummonAction::NeedsGroupPull(Player const* summoner, Player const* player)
{
    if (!summoner || !player)
        return false;

    if (summoner->GetMapId() != player->GetMapId())
        return true;

    if (summoner->GetInstanceId() != player->GetInstanceId())
        return true;

    return summoner->GetDistance(player) > sPlayerbotAIConfig.sightDistance;
}

void SummonAction::ScheduleGroupPull(Player* bot, Player* inviter)
{
    if (!bot || !inviter || !sPlayerbotAIConfig.summonWhenGroup)
        return;

    if (!NeedsGroupPull(inviter, bot))
        return;

    PlayerbotAI* botAI = GET_PLAYERBOT_AI(bot);
    if (!botAI)
        return;

    ObjectGuid const inviterGuid = inviter->GetGUID();
    ObjectGuid const botGuid = bot->GetGUID();

    botAI->AddTimedEvent(
        [botGuid, inviterGuid]()
        {
            Player* botPtr = ObjectAccessor::FindPlayer(botGuid);
            Player* inviterPtr = ObjectAccessor::FindPlayer(inviterGuid);
            if (!botPtr || !inviterPtr)
                return;

            PlayerbotAI* ai = GET_PLAYERBOT_AI(botPtr);
            if (!ai)
                return;

            Group* const botGroup = botPtr->GetGroup();
            if (!botGroup || (!botGroup->IsMember(inviterPtr->GetGUID()) && inviterPtr->GetGroup() != botGroup))
                return;

            if (!NeedsGroupPull(inviterPtr, botPtr))
                return;

            SummonAction summon(ai, "group summon");
            summon.Teleport(inviterPtr, botPtr, true, true);
        },
        100);
}

bool SummonAction::Execute(Event event)
{
    Player* summoner = ResolveSummoner(event, botAI, bot);
    if (!summoner)
        return false;

    if (bot->GetPet())
        botAI->PetFollow();

    bool const directSummon = CanDirectTeleport(summoner);
    if (directSummon)
    {
        AI_VALUE(std::list<FleeInfo>&, "recently flee info").clear();
        if (Teleport(summoner, bot, true, true))
        {
            botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                "hello", "Hello!", {}));
            return true;
        }
    }

    if (SummonUsingGos(summoner, bot, true) || SummonUsingNpcs(summoner, bot, true))
    {
        botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "hello", "Hello!", {}));
        return true;
    }

    if (SummonUsingGos(bot, summoner, true) || SummonUsingNpcs(bot, summoner, true))
    {
        botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "meeting_stone_welcome", "Welcome!", {}));
        return true;
    }

    return false;
}

bool SummonAction::SummonUsingGos(Player* summoner, Player* player, bool preserveAuras)
{
    std::list<GameObject*> targets;
    AnyGameObjectInObjectRangeCheck u_check(summoner, sPlayerbotAIConfig.sightDistance);
    Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> searcher(summoner, targets, u_check);
    Cell::VisitObjects(summoner, searcher, sPlayerbotAIConfig.sightDistance);

    for (GameObject* go : targets)
    {
        if (go->isSpawned() && go->GetGoType() == GAMEOBJECT_TYPE_MEETINGSTONE)
            return Teleport(summoner, player, preserveAuras);
    }

    botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        summoner == bot ? "meeting_stone_none_nearby" : "meeting_stone_none_near_you",
        summoner == bot ? "There is no meeting stone nearby" : "There is no meeting stone near you",
        {}));
    return false;
}

bool SummonAction::SummonUsingNpcs(Player* summoner, Player* player, bool preserveAuras)
{
    if (!sPlayerbotAIConfig.summonAtInnkeepersEnabled)
        return false;

    std::list<Unit*> targets;
    Acore::AnyUnitInObjectRangeCheck u_check(summoner, sPlayerbotAIConfig.sightDistance);
    Acore::UnitListSearcher<Acore::AnyUnitInObjectRangeCheck> searcher(summoner, targets, u_check);
    Cell::VisitObjects(summoner, searcher, sPlayerbotAIConfig.sightDistance);

    for (Unit* unit : targets)
    {
        if (!unit || !unit->HasNpcFlag(UNIT_NPC_FLAG_INNKEEPER))
            continue;

        if (Creature* innkeeper = unit->ToCreature())
            if (innkeeper->GetReactionTo(summoner) <= REP_UNFRIENDLY)
                continue;

        if (!player->HasItemCount(6948, 1, false))
        {
            botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                player == bot ? "meeting_stone_no_hearthstone_self" : "meeting_stone_no_hearthstone_you",
                player == bot ? "I have no hearthstone" : "You have no hearthstone",
                {}));
            return false;
        }

        if (player->HasSpellCooldown(8690))
        {
            botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                player == bot ? "meeting_stone_hearthstone_not_ready_self" : "meeting_stone_hearthstone_not_ready_you",
                player == bot ? "My hearthstone is not ready" : "Your hearthstone is not ready",
                {}));
            return false;
        }

        // Trigger cooldown
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(8690);
        if (!spellInfo)
            return false;

        Spell spell(player, spellInfo, TRIGGERED_NONE);
        spell.SendSpellCooldown();

        return Teleport(summoner, player, preserveAuras);
    }

    botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
        summoner == bot ? "meeting_stone_no_innkeepers_nearby" : "meeting_stone_no_innkeepers_near_you",
        summoner == bot ? "There are no innkeepers nearby" : "There are no innkeepers near you",
        {}));
    return false;
}

bool SummonAction::Teleport(Player* summoner, Player* player, bool preserveAuras, bool forceGroupPull)
{
    // Player* master = GetMaster();
    if (!summoner || summoner == player)
        return false;

    if (player->GetVehicle())
    {
        botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
            "meeting_stone_cannot_summon_vehicle", "You cannot summon me while I'm on a vehicle", {}));
        return false;
    }

    if (player->IsInFlight())
    {
        player->GetMotionMaster()->MovementExpired();
        player->CleanupAfterTaxiFlight();
    }

    if (summoner->IsInFlight())
    {
        summoner->GetMotionMaster()->MovementExpired();
        summoner->CleanupAfterTaxiFlight();
    }

    Map* summonerMap = summoner->GetMap();
    if (summonerMap && summonerMap->IsDungeon())
    {
        InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
            player->GetGUID(), summoner->GetMapId(), summoner->GetDifficulty(summonerMap->IsRaid()));

        if (!bind)
        {
            if (InstanceSave* save = sInstanceSaveMgr->GetInstanceSave(summoner->GetInstanceId()))
                sInstanceSaveMgr->PlayerBindToInstance(player->GetGUID(), save, !save->CanReset(), player);
        }

        if (summonerMap->IsRaid())
            player->SetRaidDifficulty(summoner->GetRaidDifficulty());
        else
            player->SetDungeonDifficulty(summoner->GetDungeonDifficulty());
    }

    uint32 teleportOptions = 0;
    if (summoner->IsGameMaster() || summoner->CanBeGameMaster() || GET_PLAYERBOT_AI(player))
        teleportOptions = TELE_TO_GM_MODE;

    bool const unrestrictedTeleport = CanDirectTeleport(summoner) ||
        (forceGroupPull && sPlayerbotAIConfig.summonWhenGroup &&
            (GroupInviteHelper::IsBotOwnedByPlayer(bot, summoner) ||
                (bot->GetGroup() &&
                    (bot->GetGroup() == summoner->GetGroup() || bot->GetGroup()->IsMember(summoner->GetGUID())))));

    if (!summoner->IsBeingTeleported() && !player->IsBeingTeleported())
    {
        uint32 const mapId = summoner->GetMapId();
        float const baseX = summoner->GetPositionX();
        float const baseY = summoner->GetPositionY();
        float const baseZ = summoner->GetPositionZ();
        float followAngle = GetFollowAngle();
        auto tryTeleportTo = [&](float x, float y, float z) -> bool
        {
            bool const crossMap = player->GetMapId() != mapId;
            if (!unrestrictedTeleport && !crossMap && !summoner->IsWithinLOS(x, y, z))
                return false;

            if (sPlayerbotAIConfig.botRepairWhenSummon && GET_PLAYERBOT_AI(player))
                player->DurabilityRepairAll(false, 1.0f, false);

            if (summoner->IsInCombat() && !sPlayerbotAIConfig.allowSummonInCombat)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "meeting_stone_cannot_summon_master_in_combat",
                    "You cannot summon me while you're in combat",
                    {}));
                return false;
            }

            if (!summoner->IsAlive() && !sPlayerbotAIConfig.allowSummonWhenMasterIsDead)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "meeting_stone_cannot_summon_master_dead",
                    "You cannot summon me while you're dead",
                    {}));
                return false;
            }

            if (bot->isDead() && !bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) &&
                !sPlayerbotAIConfig.allowSummonWhenBotIsDead)
            {
                botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "meeting_stone_cannot_summon_bot_dead",
                    "You cannot summon me while I'm dead, you need to release my spirit first",
                    {}));
                return false;
            }

            bool revive =
                sPlayerbotAIConfig.reviveBotWhenSummoned == 2 ||
                (sPlayerbotAIConfig.reviveBotWhenSummoned == 1 && !summoner->IsInCombat() && summoner->IsAlive());

            if (bot->isDead() && revive)
            {
                bot->ResurrectPlayer(1.0f, false);
                bot->SpawnCorpseBones();
                botAI->TellMasterNoFacing(PlayerbotTextMgr::instance().GetBotTextOrDefault(
                    "meeting_stone_revived", "I live, again!", {}));
                botAI->GetAiObjectContext()->GetValue<GuidVector>("prioritized targets")->Reset();
            }

            player->GetMotionMaster()->Clear();
            AI_VALUE(LastMovement&, "last movement").clear();

            if (!preserveAuras)
                player->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED |
                                                      AURA_INTERRUPT_FLAG_CHANGE_MAP);
            if (!player->TeleportTo(mapId, x, y, z, player->GetOrientation(), teleportOptions, summoner))
                return false;

            player->SetPhaseMask(summoner->GetPhaseMask() | 1, false);
            if (player->GetPet())
                player->GetPet()->NearTeleportTo(x, y, z, player->GetOrientation());
            if (player->GetGuardianPet())
                player->GetGuardianPet()->NearTeleportTo(x, y, z, player->GetOrientation());
            if (botAI->HasStrategy("stay", botAI->GetState()))
            {
                PositionMap& posMap = AI_VALUE(PositionMap&, "position");
                PositionInfo stayPosition = posMap["stay"];

                stayPosition.Set(x, y, z, mapId);
                posMap["stay"] = stayPosition;
            }

            return true;
        };

        if (unrestrictedTeleport)
        {
            if (tryTeleportTo(baseX, baseY, baseZ))
                return true;
        }

        for (float angle = followAngle - M_PI; angle <= followAngle + M_PI; angle += M_PI / 4)
        {
            float x = baseX + cos(angle) * sPlayerbotAIConfig.followDistance;
            float y = baseY + sin(angle) * sPlayerbotAIConfig.followDistance;
            float z = baseZ;

            if (tryTeleportTo(x, y, z))
                return true;
        }
    }

    if (summoner != player)
         botAI->TellError(PlayerbotTextMgr::instance().GetBotTextOrDefault(
             "meeting_stone_not_enough_space", "Not enough place to summon", {}));
    return false;
}
