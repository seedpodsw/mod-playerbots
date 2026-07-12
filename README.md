<p align="center">
    <a href="https://github.com/mod-playerbots/mod-playerbots/blob/master/README.md">English</a>
    |
    <a href="https://github.com/mod-playerbots/mod-playerbots/blob/master/README_CN.md">中文</a>
    |
    <a href="https://github.com/mod-playerbots/mod-playerbots/blob/master/README_ES.md">Español</a>
</p>


<div align="center">
  <img src="banner.png" alt="Playerbots Banner" width="700px">
</div>

<div align="center">
    <img src="https://github.com/mod-playerbots/mod-playerbots/actions/workflows/macos_build.yml/badge.svg">
    <img src="https://github.com/mod-playerbots/mod-playerbots/actions/workflows/core_build.yml/badge.svg">
    <img src="https://github.com/mod-playerbots/mod-playerbots/actions/workflows/windows_build.yml/badge.svg">
</div>

## Fork changes (`feature/bot-grouping`)

This fork extends upstream `master` with ambient random-bot grouping, open-world progression/PvP, and stability fixes built on the abandoned upstream work in [PR #2544](https://github.com/mod-playerbots/mod-playerbots/pull/2544).

### RandomBotGroupNearby — ambient open-world parties

- **Stable social roles** for random bots (solo / member / leader 2–5) so groups persist instead of re-rolling every rotation window.
- **World-thread group ops** via new [`PlayerbotOperations.h`](src/Script/WorldThr/PlayerbotOperations.h) — invites, accept/decline, ambient release, promote-and-leave — avoiding AI-thread opcode crashes.
- **Lifecycle integration** in [`RandomPlayerbotMgr.cpp`](src/Bot/RandomPlayerbotMgr.cpp): persistent nearby groups skip randomize/teleport churn; BG / LFG / arena / logout rotation take priority and the bot leaves first.
- **No BG queue while grouped** — nearby-group bots never receive the `bg` strategy ([`AiFactory.cpp`](src/Bot/Factory/AiFactory.cpp), [`BattleGroundJoinAction.cpp`](src/Ai/Base/Actions/BattleGroundJoinAction.cpp)).
- **Real-player friendly**: bots can leave ambient groups for player invites; promote-on-leave; `CMSG_GROUP_DISBAND` for clean party exit.
- **Progression-aware**: bots leave stale nearby groups, respect rejoin cooldown, and can relocate the whole party to a level hub when walk fails.
- **Tunable in config** — see `AiPlayerbot.RandomBotGroupNearby*` keys in [`conf/playerbots.conf.dist`](conf/playerbots.conf.dist) (default **off**; set `RandomBotGroupNearby = 1` to enable).

### Open-world quest progression (solo + grouped)

- Random bots (solo and nearby-group leaders/members) follow **New RPG quest progression**: grind targets, quest accept/share/drop, hub relocation, stale-quest pruning.
- Group leaders use **RPG leader behavior** similar to solo bots so parties move and quest together instead of idling apart.
- **Hard-prefer DoQuest** when a worthwhile quest is available (open-world progression); weights only matter for the fallback when no quest is ready.
- **Tight grind level band** (min slack / max progression+2, mid-band scoring) so XP is not wasted on wrong-level mobs.
- **Wrong-zone hard relocate** — if the current zone bracket is below progression level (e.g. ~27 in Barrens), solo/leaders leave via flight → level-appropriate hub, else teleport; non-quest grind in that zone is skipped.
- **Quest/grind course scoring** — quest pick prefers near-level XP value; grind camps stay in level-appropriate zones (cross-zone same-map when escaping).
- **Ambient parties favor 2–3** (Leader4/5 weights default 0; invite target clamped) so group XP dilution stays limited — enable `RandomBotGroupNearby = 1` to see this.
- Starter **zone brackets** and RPG weight defaults added to [`conf/playerbots.conf.dist`](conf/playerbots.conf.dist).
- **Eyeball check:** after a long uptime (~18h), max bot level should clearly beat a low ceiling (e.g. stuck ~27–28); also watch zone spread and small party sizes.

### Open-world PvP for random bots

- New `RPG_OUTDOOR_PVP` RPG status; random bots flag and engage opposing bots/players in hostile/contested zones.
- **Leaders only in nearby groups** — ambient party leaders can PvP; members assist via DPS assist, not independent seek/attack.
- **Stable seek rolls** — proactive PvP uses a per-bot/enemy 2-minute decision window instead of re-rolling every tick (`RandomBotOpenWorldPvpProactiveChance`).
- **Close-range pursuit** — bots engage enemy players within ~25 yards in open-world PvP areas even outside active combat.
- **Post-BG restore** — leaving a battleground resets strategies and re-applies the open-world PvP flag.
- Configurable via `AiPlayerbot.RandomBotOpenWorldPvp*` (default on for PvP-style realms).

### PvP vs battlegrounds (with grouping)

- **Honor BGs stay solo** — bots in an ambient `RandomBotGroupNearby` party do not get the `bg` strategy and will not queue; only solo/ungrouped random bots auto-join.
- **Open-world fights defer BG queue** — bots actively engaged in open-world PvP skip BG join until the fight ends.
- **Real-player BG fill unchanged** — when a real player queues and slots need filling, `shouldJoinBg` fill logic can still pull eligible bots in (even above the bot-started per-bracket Count).
- See the PvP-realm comment block in [`conf/playerbots.conf.dist`](conf/playerbots.conf.dist) for how `RandomBotOpenWorldPvp`, `RandomBotJoinBG`, and `RandomBotAutoJoinMinOnlineRatio` interact.

### Performance / DB load

- **Deferred event persistence** (`RandomBotEventPersistInterval = 60`) and dirty-only repository saves (`RandomBotRepositoryDirtyOnly = 1`) to cut Character DB write volume at scale.
- **Logout save throttling** (`RandomBotLogoutSavesPerInterval = 8`) spreads mass-logout Character DB spikes.
- **Smaller login bursts** (`RandomBotsPerInterval = 25`, was 60) — fewer bots processed per manager tick.
- Optional **DB perf stats** logging (`DbPerfStatsEnabled`, `DbPerfStatsLogInterval`) for tuning large bot counts.
- Tip in dist: raise `Save.Interval` in `worldserver.conf` (e.g. 900–1800) for fewer core character saves.

### Config changes in `playerbots.conf.dist`

This fork ships tuned defaults in [`conf/playerbots.conf.dist`](conf/playerbots.conf.dist). On upgrade, diff your existing `playerbots.conf` against the new dist — do not blindly overwrite a customized config.

#### New options

| Key | Default | Purpose |
|-----|---------|---------|
| `RandomBotGroupNearbyGrouperWeight.*` | Solo 42 / Member 38 / Leader2–3 12–8 / Leader4–5 **0** | Stable social-role weights; ambient parties favor 2–3 |
| `RandomBotGroupNearbyMemberJoinChance` | 75 | % chance member-role bots accept ambient invites |
| `RandomBotGroupNearbyInviteChance` | 50 | % chance leaders attempt a nearby invite per tick |
| `RandomBotGroupNearbyLeaveForProgression` | 1 | Leave group when quests/level are stale |
| `RandomBotGroupNearbyLeaveDistanceMultiplier` | 3 | Grace distance before leaving (× `RpgDistance`) |
| `RandomBotGroupNearbyRejoinCooldownMinutes` | 5 | Cooldown before re-joining same leader |
| `RandomBotGroupNearbyRelocateParty` | 1 | Teleport whole party to level hub when walk fails |
| `RandomBotOpenWorldPvp` | 1 | Open-world faction PvP for random bots |
| `RandomBotOpenWorldPvpAttackPriority` | 85 | Action priority for PvP engage |
| `RandomBotOpenWorldPvpAggroRange` | 80 | Scan range for enemy players |
| `RandomBotOpenWorldPvpProactiveChance` | 30 | % chance to seek PvP when eligible |
| `RandomBotAutoJoinMinOnlineRatio` | 0.5 | Min fraction of bots online before BG auto-join |
| `RandomBotEventPersistInterval` | 60 | Batch-defer `playerbots_random_bots` event writes (0 = legacy immediate) |
| `RandomBotLogoutSavesPerInterval` | 8 | Max logout saves per manager tick |
| `RandomBotRepositoryDirtyOnly` | 1 | Skip repository save on logout if AI unchanged |
| `DbPerfStatsEnabled` | 0 | Log DB perf counters periodically |
| `DbPerfStatsLogInterval` | 300 | Seconds between DB perf log lines |

#### Changed defaults (vs upstream `master`)

**Population & lifecycle**

| Key | Upstream | This fork |
|-----|----------|-----------|
| `MinRandomBots` / `MaxRandomBots` | 500 | **300** |
| `DisabledWithoutRealPlayer` | 0 | **1** (bots only when a real player is online) |
| `EnablePeriodicOnlineOffline` | 0 | **1** (rotate bots on/off) |
| `MinRandomBotInWorldTime` | 600 s | **14400 s** (4 h) |
| `MaxRandomBotInWorldTime` | 28800 s | **86400 s** (24 h) |
| `MinRandomBotRandomizeTime` | 7200 s | **28800 s** (8 h) |
| `RandomBotUpdateInterval` | 20 s | **30 s** |

**AI timing & responsiveness**

| Key | Upstream | This fork |
|-----|----------|-----------|
| `MaxWaitForMove` | 5000 | **2000** |
| `ReactDelay` | 100 | **40** |
| `DynamicReactDelay` | 1 | **0** |
| `PassiveDelay` | 10000 | **3000** |
| `RepeatDelay` | 2000 | **1000** |
| `RpgDelay` | 10000 | **2000** |
| `LootDelay` | 1000 | **400** |
| `AlmostFullHealth` | 85 | **70** |
| `MediumMana` | 40 | **25** |

**Active-bot scaling (near real players)**

| Key | Upstream | This fork |
|-----|----------|-----------|
| `BotActiveAlone` | 10 | **60** (% of bots active when alone) |
| `BotActiveAloneDurationSeconds` | 30 | **45** |
| `BotActiveAloneForceWhenInRadius` | 150 | **200** |

**New RPG strategy (open-world behavior)**

| Key | Upstream | This fork |
|-----|----------|-----------|
| `RpgStatusProbWeight.DoQuest` | 60 | **70** |
| `RpgStatusProbWeight.GoGrind` | 15 | **25** |
| `RpgStatusProbWeight.WanderRandom` | 15 | **20** |
| `RpgStatusProbWeight.WanderNpc` | 20 | **10** |
| `RpgStatusProbWeight.GoCamp` | 10 | **5** |
| `RpgStatusProbWeight.Rest` | 5 | **0** |
| `ZoneBracket.*` (starter zones) | 5–12 | **1–12** (level-1 bots stay in starting zones) |

**Battlegrounds**

| Key | Upstream | This fork |
|-----|----------|-----------|
| `RandomBotAutoJoinBG` | 0 | **1** |
| `RandomBotAutoJoinWSBrackets` | 7 | **0,1,2,3,4,5,6,7** (all brackets) |
| `RandomBotAutoJoinABBrackets` | 6 | **0,1,2,3,4,5,6** (all brackets) |

- **Per-bracket instance cap** — `RandomBotAutoJoinBG*Count` is a hard max of concurrent bot-started games per BG type + level bracket (e.g. WSG 20–29). Bots already inside a BG count toward fill so they no longer keep opening extra stacks of the same bracket. Real-player queues still get filled even above that cap.

**Grouping section docs**

- `RandomBotGroupNearby` comment updated from “not functioning properly” to describe ambient persistent groups and priority rules (BG / LFG / real-player invite / logout supersede grouping).
- New PvP-realm comment block under **BATTLEGROUNDS & ARENAS & PVP** explains how open-world PvP, BG auto-join, and `RandomBotAutoJoinMinOnlineRatio` interact.

### Stability and roamer fixes

- BG auto-join waits for enough bots online (`RandomBotAutoJoinMinOnlineRatio`).
- Pull/follow/chat-shortcut fixes so grouped roamers assist leaders and respond to player commands.
- Spirit release, leave-promote, and bot-protection spam fixes for nearby groups.
- Player crash guard around bot broadcast/talk paths ([`PlayerbotAI.cpp`](src/Bot/PlayerbotAI.cpp), [`Playerbots.cpp`](src/Script/Playerbots.cpp)).

### Quick enable

```ini
# playerbots.conf — minimum to try nearby grouping + open-world behavior
AiPlayerbot.RandomBotGroupNearby = 1
AiPlayerbot.EnableNewRpgStrategy = 1
AiPlayerbot.RandomBotOpenWorldPvp = 1
```

> **Branch:** `feature/bot-grouping` · **13 commits** ahead of upstream `master` · ~3.4k lines changed across 54 files.

---

# Playerbots Module
`mod-playerbots` is an [AzerothCore](https://www.azerothcore.org/) module that adds player-like bots to a server. The project is based off [IKE3's Playerbots](https://github.com/ike3/mangosbot).

Features include:

- The ability to log in alt characters as bots, allowing players to interact with their other characters, form parties, level up, and more
- Random bots that wander through the world, complete quests, and otherwise behave like players, simulating the MMO experience
- Bots capable of running most raids and battlegrounds
- Highly configurable settings to define how bots behave
- Excellent performance, even when running thousands of bots

We also have a **[Discord server](https://discord.gg/NQm5QShwf9)** where you can discuss the project, ask questions, and get involved in the community!

## Installation

Supported platforms are Ubuntu, Windows, and macOS. Other Linux distributions may work, but may not receive support.

> **Important:** All `mod-playerbots` installations require a custom fork of AzerothCore: [mod-playerbots/azerothcore-wotlk (Playerbot branch)](https://github.com/mod-playerbots/azerothcore-wotlk/tree/Playerbot). The standard AzerothCore repository will **not** work.

### Quick Start

```bash
git clone https://github.com/mod-playerbots/azerothcore-wotlk.git --branch=Playerbot
cd azerothcore-wotlk/modules
git clone https://github.com/mod-playerbots/mod-playerbots.git --branch=master
```

Then build the server following the platform-specific instructions in our **[Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide)**.

> **Testing branch:** A `test-staging` branch is available with the latest features and fixes before they are merged into `master`. To use it, clone with `--branch=test-staging` instead. Note that this branch may contain unstable or breaking changes — use it at your own risk and only if you are comfortable troubleshooting issues.

### Detailed Guides

| Guide | Description |
|---|---|
| **[Installation Guide](https://github.com/mod-playerbots/mod-playerbots/wiki/Installation-Guide)** | Full step-by-step instructions for clean installs, migrating from existing AzerothCore, Docker setup, adding modules, and updating |
| **[Troubleshooting](https://github.com/mod-playerbots/mod-playerbots/wiki/Troubleshooting)** | Solutions to the most common build errors, database issues, configuration mistakes, crashes, and platform-specific problems |

For additional references, see the [AzerothCore Installation Guide](https://www.azerothcore.org/wiki/installation) and [Installing a Module](https://www.azerothcore.org/wiki/installing-a-module) pages.

## Documentation

The [Playerbots Wiki](https://github.com/mod-playerbots/mod-playerbots/wiki) contains an extensive overview of AddOns, commands, raids with programmed bot strategies, and recommended performance configurations. Please note that documentation may be incomplete or out-of-date in some sections, and contributions are welcome.

Bots are controlled via chat commands. For larger bot groups, this can be cumbersome. Because of this, community members have developed client AddOns to allow controlling bots through the in-game UI. We recommend you check out their projects listed in the [AddOns and Submodules](https://github.com/mod-playerbots/mod-playerbots/wiki/Playerbot-Addons-and-Sub%E2%80%90Modules) page.

## Contributing

This project is still under development. We encourage anyone to make contributions, anything from pull requests to reporting issues. If you encounter any errors or experience crashes, we encourage you [report them as GitHub issues](https://github.com/mod-playerbots/mod-playerbots/issues/new?template=bug_report.md). Your valuable feedback will help us improve this project collaboratively.

If you make coding contributions, `mod-playerbots` complies with the [C++ Code Standards](https://www.azerothcore.org/wiki/cpp-code-standards) established by AzerothCore. Each Pull Request must include all test scenarios the author performed, along with their results, to demonstrate that the changes were properly verified.

We recommend joining the [Discord server](https://discord.gg/NQm5QShwf9) to make your contributions to the project easier, as a lot of active support is carried out through this server.

Please click on the "⭐" button to stay up to date and help us gain more visibility on GitHub!

## Acknowledgements

`mod-playerbots` is based on [ZhengPeiRu21/mod-playerbots](https://github.com/ZhengPeiRu21/mod-playerbots) and [celguar/mangosbot-bots](https://github.com/celguar/mangosbot-bots). We extend our gratitude to [@ZhengPeiRu21](https://github.com/ZhengPeiRu21) and [@celguar](https://github.com/celguar) for their continued efforts in maintaining the module.

Also, a thank you to the many contributors who've helped build this project:

<a href="https://github.com/mod-playerbots/mod-playerbots/graphs/contributors">
  <img src="https://contrib.rocks/image?repo=mod-playerbots/mod-playerbots" />
</a>
