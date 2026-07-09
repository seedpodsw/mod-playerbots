/*
 * Copyright (C) 2016+ AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license, you may redistribute it
 * and/or modify it under version 3 of the License, or (at your option), any later version.
 */

#ifndef PLAYERBOTS_RANDOMPLAYERBOTMGR_H
#define PLAYERBOTS_RANDOMPLAYERBOTMGR_H

#include "NewRpgInfo.h"
#include "ObjectGuid.h"
#include "PlayerbotMgr.h"
#include "BgOrderRegistry.h"
#include "GameTime.h"
#include "PlayerbotCommandServer.h"
#include <unordered_set>

class Group;

struct BattlegroundInfo
{
    std::vector<uint32> bgInstances;
    std::vector<uint32> ratedArenaInstances;
    std::vector<uint32> skirmishArenaInstances;
    uint32 bgInstanceCount = 0;
    uint32 ratedArenaInstanceCount = 0;
    uint32 skirmishArenaInstanceCount = 0;
    uint32 minLevel = 0;
    uint32 maxLevel = 0;
    uint32 activeRatedArenaQueue = 0;     // 0 = Inactive, 1 = Active
    uint32 activeSkirmishArenaQueue = 0;  // 0 = Inactive, 1 = Active
    uint32 activeBgQueue = 0;             // 0 = Inactive, 1 = Active

    // Bots (Arena)
    uint32 ratedArenaBotCount = 0;
    uint32 skirmishArenaBotCount = 0;

    // Bots (Battleground)
    uint32 bgHordeBotCount = 0;
    uint32 bgAllianceBotCount = 0;

    // Players (Arena)
    uint32 ratedArenaPlayerCount = 0;
    uint32 skirmishArenaPlayerCount = 0;

    // Players (Battleground)
    uint32 bgHordePlayerCount = 0;
    uint32 bgAlliancePlayerCount = 0;
};

class ChatHandler;
class PerfMonitorOperation;
class WorldLocation;
class Quest;

namespace PlayerbotGroupProgression
{
// Gray-quest level gap (matches client / CleanQuestLogAction).
int32 GetQuestTrivialLevelDiff(uint8 playerLevel);
bool IsQuestTrivialForLevel(uint8 playerLevel, Quest const* quest);
// Average level of alive group members; falls back to the bot's level.
uint8 GetGroupProgressionLevel(Group const* group, Player* fallback);
// Level used for gray-quest and grind decisions (group avg for nearby leaders, else bot level).
uint8 GetProgressionLevel(Player* bot);
// Level for quest gray/high-level checks (progression level when eligible, else bot level).
uint8 GetQuestLevelRef(Player* bot);
// Minimum mob level worth pulling while leveling as a party.
int32 GetPreferredMinMobLevel(uint8 progressionLevel);
// Minimum quest level worth keeping/accepting during open-world progression.
int32 GetPreferredMinQuestLevel(uint8 progressionLevel);
bool IsQuestBelowProgressionLevel(uint8 progressionLevel, Quest const* quest);
// Party bubble radius for nearby-group grind, pull, share, and POI filtering.
float GetNearbyPartyRadius();
// True if the bot has at least one non-stale quest worth keeping for solo progression.
bool HasValidProgressionQuest(Player* bot);
// True if a level-appropriate hostile is nearby (grind target or scan).
bool HasAppropriateMobNearby(Player* bot, PlayerbotAI* botAI);
}

struct CachedEvent
{
    uint32 value = 0;
    uint32 lastChangeTime = 0;
    uint32 validIn = 0;
    std::string data;

    bool IsEmpty() const { return !lastChangeTime; }
};

struct BotEventCache
{
    bool loaded = false;
    std::unordered_map<std::string, CachedEvent> events;
};

// https://gist.github.com/bradley219/5373998

class botPIDImpl;
class botPID
{
public:
    // Kp -  proportional gain
    // Ki -  Integral gain
    // Kd -  derivative gain
    // dt -  loop interval time
    // max - maximum value of manipulated variable
    // min - minimum value of manipulated variable
    botPID(double dt, double max, double min, double Kp, double Ki, double Kd);
    void adjust(double Kp, double Ki, double Kd);
    void reset();

    double calculate(double setpoint, double pv);
    ~botPID();

private:
    botPIDImpl* pimpl;
};

class RandomPlayerbotMgr : public PlayerbotHolder
{
public:
    static RandomPlayerbotMgr& instance()
    {
        static RandomPlayerbotMgr instance;

        return instance;
    }

    void LogPlayerLocation();
    void UpdateAIInternal(uint32 elapsed, bool minimal = false) override;

    uint32 activeBots = 0;
    static bool HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args);
    bool IsRandomBot(Player* bot) const;
    bool IsRandomBot(ObjectGuid::LowType bot) const;
    // True for a persistent open-world group formed by random bots via RandomBotGroupNearby:
    // non-LFG, non-BG, led by a random bot with no real-player master. Always false when the
    // config is disabled, so all call sites gate on it without changing default behavior.
    bool IsBotLedNearbyGroup(Group* group);
    // Solo random bots and nearby-group leaders: drop gray quests and prefer level-appropriate grind.
    bool ShouldUseOpenWorldProgression(Player* bot);
    // Per-bot progression checks (includes nearby-group members; for prune/leave only).
    bool ShouldUseOpenWorldProgressionChecks(Player* bot);
    // Leave ambient nearby group to resume solo RPG progression.
    bool ShouldLeaveNearbyGroupForProgression(Player* bot);
    // Member still benefits from party relocation (inverse of ShouldLeaveNearbyGroupForProgression).
    bool IsNearbyGroupMemberAlignedForRelocation(Player* member);
    // Teleport leader and aligned nearby-group members to the same level hub after walk relocation fails.
    bool RelocateNearbyGroupForProgression(Player* leader);
    // Drop trivial / below-progression quests for grouped or solo random bots.
    bool PruneProgressionQuests(Player* bot);
    // Open-world PvP for random bots (not alts): flag, target, and engage rules.
    bool ShouldUseRandomBotOpenWorldPvp(Player* bot);
    bool IsRandomBotOpenWorldPvpArea(Player* bot);
    bool ShouldEngageOpenWorldPvpTarget(Player* bot, Player* enemy);
    void UpdateRandomBotOpenWorldPvpFlag(Player* bot);
    bool TryHandleBgTeamOrderChat(Player* player, std::string const& msg);
    void SetBgTeamOrder(BgTeamOrder const& order);
    BgTeamOrder const* GetBgTeamOrder(Battleground* bg, TeamId team);
    void ClearBgTeamOrders(uint32 instanceId);
    bool IsAddclassBot(Player* bot);
    bool IsAddclassBot(ObjectGuid::LowType bot);
    void Randomize(Player* bot);
    void Clear(Player* bot);
    void RandomizeFirst(Player* bot);
    void RandomizeMin(Player* bot);
    void IncreaseLevel(Player* bot);
    void ScheduleTeleport(uint32 bot, uint32 time = 0);
    void ScheduleChangeStrategy(uint32 bot, uint32 time = 0);
    void HandleCommand(uint32 type, std::string const text, Player* fromPlayer, std::string channelName = "");
    std::string const HandleRemoteCommand(std::string const request);
    void OnPlayerLogout(Player* player);
    void OnPlayerLogin(Player* player);
    void OnPlayerLoginError(uint32 bot);
    Player* GetRandomPlayer();
    std::vector<Player*> GetPlayers() { return players; };
    PlayerBotMap GetAllBots() { return playerBots; };
    void PrintStats();
    double GetBuyMultiplier(Player* bot);
    double GetSellMultiplier(Player* bot);
    void AddTradeDiscount(Player* bot, Player* master, int32 value);
    void SetTradeDiscount(Player* bot, Player* master, uint32 value);
    uint32 GetTradeDiscount(Player* bot, Player* master);
    void Refresh(Player* bot);
    void RandomTeleportForLevel(Player* bot);
    void RandomTeleportGrindForLevel(Player* bot);
    void RandomTeleportForRpg(Player* bot);
    uint32 GetMaxAllowedBotCount();
    bool ProcessBot(Player* player);
    void Revive(Player* player);
    void ChangeStrategy(Player* player);
    void ChangeStrategyOnce(Player* player);
    uint32 GetValue(Player* bot, std::string const& type);
    uint32 GetValue(uint32 bot, std::string const& type);
    std::string GetData(uint32 bot, std::string const& type);
    void SetValue(uint32 bot, std::string const& type, uint32 value, std::string const& data = "");
    void SetValue(Player* bot, std::string const& type, uint32 value, std::string const& data = "");
    bool IsSpecPvp(uint32 bot, uint8 cls);
    void Remove(Player* bot);
    ObjectGuid GetBattleMasterGUID(Player* bot, BattlegroundTypeId bgTypeId);
    CreatureData const* GetCreatureDataByEntry(uint32 entry);
    void LoadBattleMastersCache();
    std::map<uint32, std::map<uint32, BattlegroundInfo>> BattlegroundData;
    std::map<uint32, std::map<uint32, std::map<TeamId, uint32>>> VisualBots;
    std::map<uint32, std::map<uint32, std::map<uint32, uint32>>> Supporters;
    std::map<TeamId, std::vector<uint32>> LfgDungeons;
    void CheckBgQueue();
    void CheckLfgQueue();
    void CheckPlayers();
    void LogBattlegroundInfo();

    std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> getBattleMastersCache()
    {
        return BattleMastersCache;
    }

    float getActivityMod() { return activityMod; }
    float getActivityPercentage() { return activityMod * 100.0f; }
    void setActivityPercentage(float percentage) { activityMod = percentage / 100.0f; }
    static uint8 GetTeamClassIdx(bool isAlliance, uint8 claz) { return isAlliance * 20 + claz; }

    void PrepareAddclassCache();
    void Init();
    void FlushDirtyEventCache();
    void FlushEventCacheForBot(uint32 bot);
    void ClearEventCaches();
    std::map<uint8, std::unordered_set<ObjectGuid>> addclassCache;

    // Account type management
    void AssignAccountTypes();
    bool IsAccountType(uint32 accountId, uint8 accountType);

protected:
    void OnBotLoginInternal(Player* const bot) override;

private:
    RandomPlayerbotMgr() : PlayerbotHolder(), processTicks(0)
    {
        this->playersLevel = sPlayerbotAIConfig.randombotStartingLevel;

        if (sPlayerbotAIConfig.enabled || sPlayerbotAIConfig.randomBotAutologin)
        {
            PlayerbotCommandServer::instance().Start();
        }

        BattlegroundData.clear();  // Clear here and here only.

        // Cleanup on server start: orphaned pet data that's often left behind by bot pets that no longer exist in the DB
        CharacterDatabase.Execute("DELETE FROM pet_aura WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.Execute("DELETE FROM pet_spell WHERE guid NOT IN (SELECT id FROM character_pet)");
        CharacterDatabase.Execute("DELETE FROM pet_spell_cooldown WHERE guid NOT IN (SELECT id FROM character_pet)");

        for (int bracket = BG_BRACKET_ID_FIRST; bracket < MAX_BATTLEGROUND_BRACKETS; ++bracket)
        {
            for (int queueType = BATTLEGROUND_QUEUE_AV; queueType < MAX_BATTLEGROUND_QUEUE_TYPES; ++queueType)
            {
                this->BattlegroundData[queueType][bracket] = BattlegroundInfo();
            }
        }

        this->BgCheckTimer = 0;
        this->LfgCheckTimer = 0;
        this->PlayersCheckTimer = 0;
    }

    ~RandomPlayerbotMgr() = default;

    RandomPlayerbotMgr(const RandomPlayerbotMgr&) = delete;
    RandomPlayerbotMgr& operator=(const RandomPlayerbotMgr&) = delete;

    RandomPlayerbotMgr(RandomPlayerbotMgr&&) = delete;
    RandomPlayerbotMgr& operator=(RandomPlayerbotMgr&&) = delete;

    // pid values are set in constructor
    botPID pid = botPID(1, 50, -50, 0, 0, 0);
    float activityMod = 0.25;
    bool _isBotInitializing = true;
    bool _isBotLogging = true;
    NewRpgStatistic rpgStasticTotal;
    CachedEvent* FindEvent(uint32 bot, std::string const& event);
    uint32 GetEventValue(uint32 bot, std::string const& event);
    std::string GetEventData(uint32 bot, std::string const& event);
    uint32 SetEventValue(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                         std::string const& data = "");
    void UpdateEventCache(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                          std::string const& data);
    void PersistEventValueToDatabase(uint32 bot, std::string const& event, uint32 value, uint32 validIn,
                                     std::string const& data);
    void MarkEventDirty(uint32 bot, std::string const& event);
    void MaybeFlushDirtyEventCache();
    static std::string MakeDirtyEventKey(uint32 bot, std::string const& event);
    void GetBots();
    std::vector<uint32> GetBgBots(uint32 bracket);
    time_t BgCheckTimer;
    time_t LfgCheckTimer;
    time_t PlayersCheckTimer;
    time_t RealPlayerLastTimeSeen = 0;
    time_t DelayLoginBotsTimer;
    time_t printStatsTimer;
    uint32 AddRandomBots();
    bool ProcessBot(uint32 bot);
    void ScheduleRandomize(uint32 bot, uint32 time);
    bool TeleportBotToProgressionHub(Player* bot, uint32 mapId, float x, float y, float z);
    void RandomTeleport(Player* bot);
    void RandomTeleport(Player* bot, std::vector<WorldLocation>& locs, bool hearth = false);
    uint32 GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ);
    typedef void (RandomPlayerbotMgr::*ConsoleCommandHandler)(Player*);
    std::vector<Player*> players;
    uint32 processTicks;

    // std::map<uint32, std::vector<WorldLocation>> rpgLocsCache;
    std::map<uint32, std::map<uint32, std::vector<WorldLocation>>> rpgLocsCacheLevel;
    std::map<TeamId, std::map<BattlegroundTypeId, std::vector<uint32>>> BattleMastersCache;
    std::unordered_map<uint32, BotEventCache> eventCache;
    std::unordered_set<std::string> dirtyEvents;
    time_t eventPersistLastFlush = 0;
    std::list<uint32> currentBots;
    uint32 bgBotsCount;
    uint32 playersLevel;

    // Account lists
    std::vector<uint32> rndBotTypeAccounts;             // Accounts marked as RNDbot (type 1)
    std::vector<uint32> addClassTypeAccounts;           // Accounts marked as AddClass (type 2)

    std::unordered_map<uint64_t, BgTeamOrder> bgTeamOrders;

    //void ScaleBotActivity();      // Deprecated function
    static inline uint32 NowSeconds() { return static_cast<uint32>(GameTime::GetGameTime().count()); }
};

#define sRandomPlayerbotMgr RandomPlayerbotMgr::instance()

#endif
