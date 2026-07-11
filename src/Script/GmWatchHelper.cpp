/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#include "GmWatchHelper.h"

#include "Chat.h"
#include "InstanceSaveMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"

#include <unordered_map>

namespace
{
    enum class GmPendingAction
    {
        None,
        Watch,
        Takeover
    };

    std::unordered_map<ObjectGuid, std::pair<ObjectGuid, GmPendingAction>> s_pendingActions;
    std::unordered_map<ObjectGuid, WorldLocation> s_watchReturnPoints;
    std::unordered_map<ObjectGuid, bool> s_savedGmVisible;

    constexpr uint32 GM_POSSESS_SPELL = 530;

    bool IsInWatchSession(Player* observer)
    {
        return observer->GetViewpoint()
            || s_pendingActions.contains(observer->GetGUID())
            || s_watchReturnPoints.contains(observer->GetGUID());
    }

    void SaveWatchReturnPoint(Player* observer)
    {
        if (IsInWatchSession(observer))
            return;

        s_watchReturnPoints[observer->GetGUID()] = WorldLocation(
            observer->GetMapId(),
            observer->GetPositionX(),
            observer->GetPositionY(),
            observer->GetPositionZ(),
            observer->GetOrientation());
    }

    bool ReturnFromWatch(Player* observer)
    {
        auto itr = s_watchReturnPoints.find(observer->GetGUID());
        if (itr == s_watchReturnPoints.end())
            return false;

        WorldLocation returnLoc = itr->second;
        s_watchReturnPoints.erase(itr);

        return observer->TeleportTo(returnLoc, TELE_TO_GM_MODE);
    }

    void ClearObservation(Player* player)
    {
        if (WorldObject* viewpoint = player->GetViewpoint())
            player->SetViewpoint(viewpoint, false);

        player->StopCastingBindSight();

        if (player->isPossessing())
            player->RemoveCharmAuras();
    }

    void ApplyWatchViewpoint(Player* observer, Player* target)
    {
        observer->UpdateVisibilityOf(target);
        observer->SetViewpoint(target, true);
    }

    void SaveGmVisibility(Player* observer)
    {
        if (s_savedGmVisible.contains(observer->GetGUID()))
            return;

        s_savedGmVisible[observer->GetGUID()] = observer->isGMVisible();
    }

    void ApplyWatchStealth(Player* observer)
    {
        SaveGmVisibility(observer);

        if (observer->isGMVisible())
        {
            observer->SetGMVisible(false);
            observer->UpdateObjectVisibility();
        }
    }

    void RestoreGmVisibility(Player* observer)
    {
        auto itr = s_savedGmVisible.find(observer->GetGUID());
        if (itr == s_savedGmVisible.end())
            return;

        observer->SetGMVisible(itr->second);
        observer->UpdateObjectVisibility();
        s_savedGmVisible.erase(itr);
    }

    bool NeedsRelocate(Player* observer, Player* target)
    {
        if (!observer->IsInWorld() || !target->IsInWorld())
            return true;

        if (observer->GetMapId() != target->GetMapId())
            return true;

        if (observer->GetInstanceId() != target->GetInstanceId())
            return true;

        return false;
    }

    bool TeleportGmNearTarget(Player* observer, Player* target)
    {
        if (!target || !target->IsInWorld())
            return false;

        Map* map = target->GetMap();
        if (!map)
            return false;

        if (map->IsDungeon())
        {
            InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(
                observer->GetGUID(), target->GetMapId(), target->GetDifficulty(map->IsRaid()));

            if (!bind)
            {
                if (InstanceSave* save = sInstanceSaveMgr->GetInstanceSave(target->GetInstanceId()))
                    sInstanceSaveMgr->PlayerBindToInstance(observer->GetGUID(), save, !save->CanReset(), observer);
            }

            if (map->IsRaid())
                observer->SetRaidDifficulty(target->GetRaidDifficulty());
            else
                observer->SetDungeonDifficulty(target->GetDungeonDifficulty());
        }

        if (observer->IsInFlight())
        {
            observer->GetMotionMaster()->MovementExpired();
            observer->CleanupAfterTaxiFlight();
        }

        return observer->TeleportTo(
            target->GetMapId(),
            target->GetPositionX(),
            target->GetPositionY(),
            target->GetPositionZ() + 0.25f,
            observer->GetOrientation(),
            TELE_TO_GM_MODE,
            target);
    }

    void QueueAction(Player* observer, Player* target, GmPendingAction action)
    {
        s_pendingActions[observer->GetGUID()] = { target->GetGUID(), action };
    }

    void ClearPending(Player* observer)
    {
        s_pendingActions.erase(observer->GetGUID());
    }

    bool ApplyPending(Player* observer)
    {
        auto itr = s_pendingActions.find(observer->GetGUID());
        if (itr == s_pendingActions.end())
            return false;

        Player* target = ObjectAccessor::FindPlayer(itr->second.first);
        if (!target || !target->IsInWorld())
        {
            ClearPending(observer);
            return false;
        }

        if (observer->IsBeingTeleported()
            || observer->GetMapId() != target->GetMapId()
            || observer->GetInstanceId() != target->GetInstanceId())
            return false;

        GmPendingAction const action = itr->second.second;
        ClearPending(observer);

        if (action == GmPendingAction::Watch)
        {
            ApplyWatchViewpoint(observer, target);
            ChatHandler(observer->GetSession()).PSendSysMessage("Watching {}.", target->GetName());
        }
        else if (action == GmPendingAction::Takeover)
        {
            observer->CastSpell(target, GM_POSSESS_SPELL, true);
            ChatHandler(observer->GetSession()).PSendSysMessage("Now controlling {}.", target->GetName());
        }

        return true;
    }
}

namespace GmWatchHelper
{
    bool BeginWatch(ChatHandler* handler, Player* observer, Player* target)
    {
        if (!observer->IsGameMaster())
        {
            handler->SendSysMessage("Enable GM mode first: .gm on");
            return false;
        }

        SaveWatchReturnPoint(observer);
        ClearObservation(observer);
        ClearPending(observer);

        if (NeedsRelocate(observer, target))
        {
            ApplyWatchStealth(observer);
            QueueAction(observer, target, GmPendingAction::Watch);

            if (!TeleportGmNearTarget(observer, target))
            {
                ClearPending(observer);
                handler->SendSysMessage("Failed to teleport to target.");
                return false;
            }

            handler->PSendSysMessage("Traveling to {}...", target->GetName());
            return true;
        }

        ApplyWatchViewpoint(observer, target);
        handler->PSendSysMessage("Watching {}. Use .unwatch or .release to stop.", target->GetName());
        return true;
    }

    bool BeginTakeover(ChatHandler* handler, Player* observer, Player* target)
    {
        if (!observer->IsGameMaster())
        {
            handler->SendSysMessage("Enable GM mode first: .gm on");
            return false;
        }

        SaveWatchReturnPoint(observer);
        ClearObservation(observer);
        ClearPending(observer);

        if (NeedsRelocate(observer, target))
        {
            ApplyWatchStealth(observer);
            QueueAction(observer, target, GmPendingAction::Takeover);

            if (!TeleportGmNearTarget(observer, target))
            {
                ClearPending(observer);
                handler->SendSysMessage("Failed to teleport to target.");
                return false;
            }

            handler->PSendSysMessage("Traveling to {}...", target->GetName());
            return true;
        }

        observer->CastSpell(target, GM_POSSESS_SPELL, true);
        handler->PSendSysMessage("Now controlling {}. Use .release or .unpossess to stop.", target->GetName());
        return true;
    }

    void EndWatch(Player* observer, bool returnToEntry)
    {
        ClearPending(observer);
        ClearObservation(observer);
        RestoreGmVisibility(observer);

        if (!returnToEntry)
            return;

        if (ReturnFromWatch(observer))
            return;

        if (observer->GetEntryPoint().GetMapId() != MAPID_INVALID)
            observer->TeleportToEntryPoint();
    }

    void Update(Player* observer)
    {
        if (!observer)
            return;

        WorldSession* session = observer->GetSession();
        if (!session || session->IsBot())
            return;

        ApplyPending(observer);
    }
}
