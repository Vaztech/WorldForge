# WorldForge Source Audit — Every TODO and Incomplete Item

**189 source files analyzed | 22,362 total lines of code**

---

## PART 1: EVERY TODO / STUB / INCOMPLETE ITEM

### CRITICAL — Backfill Items Still Stubbed (Were Marked Complete)

These were supposed to be done during backfill but the source code shows they are still stubs or placeholders.

| # | Item | File | Status | What's Wrong |
|---|---|---|---|---|
| BF-07 | SetTradeRouteFlowMod | DiplomacySubsystem.cpp:127 | **STILL STUB** | Logs only. Never mutates FTradeFlowState.DiplomaticMod. |
| BF-09 | GetRouteDirection | NavalSystem.cpp:156 | **STILL STUB** | Returns hardcoded `FVector2D(1.0f, 0.0f)`. Never queries real tile positions. |
| BF-10 | FaithFactor | CosmicSimulationSubsystem.cpp:194 | **STILL STUB** | Hardcoded `0.5f`. Never queries actual PurposeNeed average from colonists. |
| BF-11 | MoraleProcessor | MoraleProcessor.cpp | **STILL STUB** | 17 lines — empty `ProcessStep()` with `(void)` on all params. Zero morale logic. |
| BF-12 | IncidentProcessor | IncidentProcessor.cpp | **STILL STUB** | 17 lines — empty `ProcessStep()` with `(void)` on all params. Zero incident logic. |

### CRITICAL — WorldGen Pipeline TODO Steps

| Step | Function | File:Line | Status |
|---|---|---|---|
| 03 | Step03_RunTectonics | WorldGenPipeline.cpp:144 | **TODO** — logs "Tectonics -- TODO" and returns. No mountain/valley carving. |
| 05 | Step05_InitVoxelChunks | WorldGenPipeline.cpp:189 | **PARTIAL** — calls `GenerateTerrainFromTiles()` which exists in VoxelWorldManager but defers if VoxelWorld runtime isn't ready. The terrain generation code exists but may not be firing correctly (flat world observed). |
| 06 | Step06_PCGDecorationPass | WorldGenPipeline.cpp:220 | **TODO** — logs "Step06 -- TODO" and returns. No foliage/rock scatter. |
| 07 | Step07_ApplyWorldDriver | WorldGenPipeline.cpp:225 | **TODO** — logs "Step07 -- TODO" and returns. No IWorldDriver applied. |
| 08 | Step08_SeedResources | WorldGenPipeline.cpp:230 | **TODO** — logs "Step08 -- TODO" and returns. No ore/magic nodes placed. |
| 12 | Step12_GenerateCosmicRoster | WorldGenPipeline.cpp:236 | **TODO** — logs "Step12 -- TODO" and returns. No cosmic entity generation during worldgen. |
| 14 | Step14_AssignWorldTraits | WorldGenPipeline.cpp:241 | **TODO** — logs "Step14 -- TODO" and returns. No world identity traits. |
| 15 | Step15_RecordFinalState | WorldGenPipeline.cpp:246 | **TODO** — logs "Step15 -- TODO" and returns. History not recorded to final state. |
| 16 | Step16_FinalizeWorldMap | WorldGenPipeline.cpp:251 | **TODO** — logs "Step16 -- TODO" and returns. No world map render, no fog application. |

### CRITICAL — Phase 3.5 World History Generator Incomplete

Only 3 of 10 passes implemented. 7 passes missing entirely.

| Pass | Status | Lines |
|---|---|---|
| NameGenerationPass | **EXISTS** — basic syllable generation | 36 lines |
| RaceOriginPass | **EXISTS** — generates origin narratives | 47 lines |
| FigureGenerationPass | **EXISTS** — generates named figures with epithets | 54 lines |
| LineagePass | **MISSING** — family trees not generated |
| BattleDetailPass | **MISSING** — wars not resolved into named battles |
| ArtifactForgePass | **MISSING** — legendary artifacts not created |
| CreatureLorePass | **MISSING** — notable creatures not generated |
| FallenCityPass | **MISSING** — ruins with lore not generated |
| LegendSynthesisPass | **MISSING** — stories/legends not synthesized |
| ColonistBackstoryPass | **MISSING** — colonist backstories not generated |

**Also missing:**
- `FWorldLoreState` struct is NOT in `WorldForgeDataTypes.h`
- `WorldMapSubsystem` has no `WorldLoreState` field
- `SaveSubsystem` does not save/load `FWorldLoreState`
- `SaveGame` has `Artifacts` and `ArtifactHistory` arrays but no `FWorldLoreState` master container

### HIGH — Economy System Stubs

| Item | File:Line | Status |
|---|---|---|
| NotifyWarStarted | WorldEconomySimulation.cpp:1196 | **STUB** — logs only, no economy impact from wars |
| NotifyWarEnded | WorldEconomySimulation.cpp:1202 | **STUB** — logs only |
| NotifyPlagueEvent | WorldEconomySimulation.cpp:1208 | **STUB** — logs only |
| NotifyDisasterEvent | WorldEconomySimulation.cpp:1214 | **STUB** — logs only |
| NotifyGovernmentChange | WorldEconomySimulation.cpp:1220 | **STUB** — logs only |
| NotifyTradeRouteOpened | WorldEconomySimulation.cpp:1226 | **STUB** — logs only |
| NotifyTradeRouteClosed | WorldEconomySimulation.cpp:1231 | **STUB** — logs only |
| NotifyCivCollapse | WorldEconomySimulation.cpp:1237 | **STUB** — logs only |
| EpochSimulation | WorldEconomySimulation.cpp:171 | **STUB** — logs "Epoch simulation stub" |

### HIGH — Mod System Stubs

| Item | File:Line | Status |
|---|---|---|
| MountModPak | ModLoaderSubsystem.cpp:106 | **STUB** — "TODO Phase 4: Use IPlatformFile to mount .pak file" |
| RegisterModContent | ModLoaderSubsystem.cpp:114 | **STUB** — "TODO Phase 4: Walk mod content folder and register DataAssets" |

### HIGH — Player Build System Incomplete

| Item | Status |
|---|---|
| BuildModeManagerRef in BP_WorldForgePlayerController | **NOT ASSIGNED** — BeginPlay doesn't set it, so HandlePrimaryClick never fires |
| Event Tick → UpdateFromMouse | **NOT WIRED** — ghost doesn't follow cursor |
| R key → HandleRotate | **NOT WIRED** |
| Enter key → HandleConfirm | **NOT WIRED** |
| Escape key → HandleCancel | **NOT WIRED** |
| B key → toggle widget | **NOT WIRED** |
| Old prototype path (AddCell → SpawnDebugBlock) | **NOT DELETED** — still fires on every click |

### MEDIUM — Architecture Violations

| Item | File:Line | Issue |
|---|---|---|
| ContentFilterSubsystem | ContentFilterSubsystem.cpp:23-31 | Uses `.Contains()` string matching instead of `MatchesTag()`. Violates "Zero .ToString().Contains()" invariant. |

### MEDIUM — Other Stubs and Deferred Items

| Item | File:Line | Status |
|---|---|---|
| ContentFilter save/load | ContentFilterSubsystem.cpp:38 | **TODO** — "Phase 2: Load filter settings from saved config file" |
| ContentFilter persist | ContentFilterSubsystem.cpp:44 | **TODO** — "Phase 2: Save filter settings to config file" |
| DeriveEconomyScore fallback | CivilizationResilienceSystem.cpp:261 | Returns `0.5f` when EconSim is null — safe fallback but not real data |
| NavalSystem distance | NavalSystem.cpp:85 | Uses flat 50-step estimate instead of actual grid distance |
| VoxelSpawnSubsystem save | VoxelSpawnSubsystem.h:42-46 | Save/restore labeled "Stub" — minimal implementation exists |
| SettlementVisualManager layout | SettlementVisualManager.cpp:152 | "Placeholder deterministic layout" — not real colony grid positions |
| Custom build vertical layering | VoxelConstructionSystem.cpp:260 | "Deferred to a later targeted step" — custom builds only place on layer 0 |
| Cosmic entity domain tags | CosmicSimulationSubsystem.cpp:265 | String fallback for domain tags "until full tag tree is finalized" |

### LOW — Minor / Acceptable

| Item | File | Note |
|---|---|---|
| RaceDataAsset.cpp | 1 line (just include) | Data asset — logic in .h, .cpp is effectively empty by design |
| IMagicSystem.h | 33 lines, interface only | Phase 4 content — not yet needed |
| ITechSystem.h | 36 lines, interface only | Phase 4 content — not yet needed |

---

## PART 2: WHAT EVERY FILE DOES

### Build System (3 files)
| File | Lines | Purpose |
|---|---|---|
| WorldForge.Build.cs | — | UE5 build config: module dependencies, Voxel Plugin conditional compilation |
| WorldForge.Target.cs | — | Game build target |
| WorldForgeEditor.Target.cs | — | Editor build target |

### Root (2 files)
| File | Lines | Purpose |
|---|---|---|
| WorldForge.h / .cpp | — | Module definition, startup/shutdown |

### Civilization/ — Core Civ Systems (6 files)
| File | Lines | Purpose |
|---|---|---|
| CivilizationGenerator.h/.cpp | ~200 | **Orchestrator.** Runs civ generation passes in order. Builds CivGenContext, delegates to passes in Generation/ folder. |
| HistoricalSimulation.h/.cpp | ~200 | **Orchestrator.** Runs 4-epoch prehistory simulation. Delegates to passes in HistorySim/Passes/. Outputs FHistoryEventData array. |
| CultureDriftSystem.h/.cpp | ~150 | **Orchestrator.** Per-step cultural DNA drift. Delegates to passes in Culture/Passes/. |
| CivilizationResilienceSystem.h/.cpp | ~350 | **Orchestrator + scoring.** Evaluates civ health via territory/economy/population/military scores. Triggers collapse. Has AddHistoryEvent. Delegates to passes in Resilience/. |

### Civilization/Generation/ — Civ Gen Passes (16 files)
| File | Purpose |
|---|---|
| CivGenContext.h | Shared context struct for all civ generation passes |
| CivGenTypes.h | Enums and types for civ generation |
| CivPlacementPass.h/.cpp | Places civs on starting tiles via BRANCH_CIV_START |
| CivRaceSeedPass.h/.cpp | Assigns race to each civ |
| CivCultureSeedPass.h/.cpp | Seeds 10-axis Cultural DNA per civ |
| CivGovernmentSeedPass.h/.cpp | Derives government type from Cultural DNA |
| CivEconomySeedPass.h/.cpp | Creates currency definitions, initializes economy profiles |
| CivNamingPass.h/.cpp | Generates civ names |
| CivDiplomacySeedPass.h/.cpp | Seeds initial diplomatic relationships |

### Civilization/HistorySim/ — History Simulation Passes (12 files)
| File | Purpose |
|---|---|
| HistoricalSimContext.h | Shared context for history simulation |
| HistoricalSimTypes.h | Types/enums for epochs |
| HistPhaseEpochPass.h/.cpp | Manages epoch transitions (Emergence→Expansion→Conflict→Stabilization) |
| HistEventGenerationPass.h/.cpp | Generates events per epoch |
| HistExpansionPass.h/.cpp | Simulates territorial expansion events |
| HistConflictPass.h/.cpp | Simulates war and conflict events |
| HistStabilityPass.h/.cpp | Simulates stabilization period events |

### Civilization/Culture/ — Culture Drift Passes (8 files)
| File | Purpose |
|---|---|
| CultureDriftContext.h | Shared context for drift passes |
| CultureDNAUtils.h/.cpp | Helper for reading/writing Cultural DNA axis values |
| CultureEventDriftPass.h/.cpp | Drifts DNA based on events |
| CultureDNACapPass.h/.cpp | Clamps DNA values to valid ranges |
| CultureDriftLoggingPass.h/.cpp | Logs significant drift for debug |

### Civilization/Resilience/ — Resilience Passes (8 files)
| File | Purpose |
|---|---|
| CivilizationResilienceContext.h | Shared context for resilience evaluation |
| CivilizationResilienceScoringPass.h/.cpp | Computes territory/economy/population/military scores |
| CivilizationResiliencePopulationPass.h/.cpp | Updates TotalPopulation from settlements |
| CivilizationResilienceEvaluationPass.h/.cpp | Evaluates overall civ health |
| CivilizationResilienceOutcomePass.h/.cpp | Determines collapse type and triggers it |

### Civilization/WorldHistory/ — World History Generator (14 files)
| File | Purpose |
|---|---|
| WorldHistoryGenerator.h/.cpp | **Orchestrator.** Runs lore passes after HistoricalSimulation. Currently runs 3 passes. |
| WorldHistoryContext.h | Context struct holding history events, civs, output lore |
| WorldHistoryPass.h | Abstract base class for lore passes |
| WorldHistoryTypes.h | EHistoricalRole, ELegendType, ERelationType, EArrivalReason, ELoreDetailLevel |
| NameGenerationPass.h/.cpp | Generates per-race name tables using syllable modules |
| WorldHistoryNameModule.h | Interface for name generation modules |
| WorldHistoryNameModuleRegistry.h/.cpp | Registry of available name modules |
| WorldHistorySimpleSyllableNameModule.h/.cpp | Default syllable-based name generator |
| Passes/RaceOriginPass.h/.cpp | Generates FRaceOriginMyth per race |
| Passes/FigureGenerationPass.h/.cpp | Generates FHistoricalFigure per major event |

### Colony/ — Core Colony Systems (14 files)
| File | Lines | Purpose |
|---|---|---|
| SettlementManager.h/.cpp | ~600 | Central settlement data: stockpiles, buildings, agents. CreateSettlement, AddToStockpile, ConsumeFromStockpile, AddBuilding. |
| SimulationTimeManager.h/.cpp | ~200 | Fixed-step clock. Drives ProcessorManager at configurable interval. Pause/resume/speed control. |
| ProcessorManager.h/.cpp | ~300 | Owns 9 ordered processors. RunSimulationStep() each tick. Records timing per processor. |
| EventBus.h/.cpp | ~150 | Queue-based event dispatch. Dispatch() queues, FlushPendingEvents() fires after all processors. |
| NavigationContext.h/.cpp | ~200 | 2D grid pathfinding. A* on FNavCell grid. Single elevation layer. |
| ReservationSystem.h/.cpp | ~250 | 5 reservation types. TryReserve/Release with timeout and cleanup triggers. |
| WorldForgeColonist.h/.cpp | ~150 | Thin actor wrapper. Syncs visual position from FAgentData. |
| ColonyManager.h/.cpp | ~100 | Colony bootstrap: creates settlement, spawns initial colonists. |
| BuildingTagHelper.h/.cpp | ~50 | GetEraBuildingTag() — resolves building purpose + era to GameplayTag. |

### Colony/Processors/ — The 9 Processors (22 files)
| File | Lines | Purpose |
|---|---|---|
| BaseProcessor.h | ~30 | Abstract base with virtual ProcessStep() |
| NeedsProcessor.h/.cpp | ~150 | Decays Food/Sleep/Social/Safety/Purpose needs. Fires critical events below 20. |
| JobProcessor.h/.cpp | ~400 | Assigns jobs from queue. Posts harvest/eat/sleep jobs. Checks reservations. |
| JobQueueManager.h/.cpp | ~200 | Pending job queue with priority sorting. |
| ReservationProcessor.h/.cpp | ~100 | Validates and cleans up stale reservations each step. |
| ConstructionProcessor.h/.cpp | ~150 | Calls PlaceNextBlock() on active build tasks each step. |
| ProductionProcessor.h/.cpp | ~300 | Advances crafting, outputs goods, manages carry state. |
| MoraleProcessor.h/.cpp | **17** | **STUB.** Empty ProcessStep(). |
| HealthProcessor.h/.cpp | ~100 | Processes injury/illness/recovery state transitions. |
| IncidentProcessor.h/.cpp | **17** | **STUB.** Empty ProcessStep(). |
| EconomicProcessor.h/.cpp | ~150 | Updates local prices, currency trust, checks black market thresholds. |

### Core/ — Foundation Systems (8 files)
| File | Lines | Purpose |
|---|---|---|
| WorldForgeRNG.h/.cpp | ~150 | 15-branch LCG PRNG. InitializeWithSeed(), NextFloat(branch), NextInt(branch). |
| ContentFilterSubsystem.h/.cpp | ~100 | 9 filter categories. IsEventAllowed() gates dark content. Uses .Contains() (violation). |
| ModLoaderSubsystem.h/.cpp | ~200 | Scans /mods folder, parses mod.json. Pak mount and content registration are stubs. |
| CommandersDeskManager.h/.cpp | ~110 | Commander's Desk: narrative reports, economic intelligence display. |

### Cosmic/ (2 files)
| File | Lines | Purpose |
|---|---|---|
| CosmicSimulationSubsystem.h/.cpp | ~550 | Entity states, faith decay, anchor power, emergence triggers. FaithFactor hardcoded 0.5. |

### Data/ (5 files)
| File | Lines | Purpose |
|---|---|---|
| WorldForgeDataTypes.h | ~800+ | **Master struct file.** Every FStruct in the project. FWorldTileData, FCivilizationData, FAgentData, FSettlementState, FCurrencyDefinition, FMarketPrice, FActiveBuildTask, all save state structs. Missing: FWorldLoreState. |
| WorldForgeTags.h/.cpp | ~200 | Typed GameplayTag constants. TAG_Item_Wood, TAG_Item_Stone, etc. |
| WorldForgeDataRegistry.h/.cpp | ~100 | UDataRegistry: registers items, buildings, jobs. |

### Debug/ (2 files)
| File | Lines | Purpose |
|---|---|---|
| DebugSimulationSystem.h/.cpp | ~750 | 14 debug overlays: needs, tasks, reservations, pathfinding, stockpiles, buildings, colonist inspector, job queue, morale, incidents, processor timing, economy, overworld, and build mode. |

### Economy/ (6 files)
| File | Lines | Purpose |
|---|---|---|
| WorldEconomySimulation.h/.cpp | ~1250 | 5-force economy engine. Production, consumption, flow, price discovery, trust. Government profiles, currency generation. 8 notification stubs for Phase 3B events. |
| ProductionSimulator.h/.cpp | ~50 | Calculates production output from worker/skill/building/biome modifiers. |
| ConsumptionSimulator.h/.cpp | ~50 | Calculates demand from population/needs/season/culture. |

### Magic/ (1 file)
| File | Lines | Purpose |
|---|---|---|
| IMagicSystem.h | 33 | Interface. Phase 4 content — no implementations yet. |

### Overworld/ (6 files)
| File | Lines | Purpose |
|---|---|---|
| OverworldSubsystem.h/.cpp | ~350 | Caravan routing, outpost management, supply lines. BF-06 IsOutpostSupplied implemented. |
| DiplomacySubsystem.h/.cpp | ~550 | -100 to +100 relationships, treaties, currency pegging, embargoes. SetTradeRouteFlowMod still stub. |
| NavalSystem.h/.cpp | ~200 | Ship management, sailing, blockades. GetRouteDirection still stub. Distance estimation is flat. |

### Player/ (11 files)
| File | Lines | Purpose |
|---|---|---|
| PlayerBuildContext.h/.cpp | ~200 | Build state container. AddCell, SetMaterial, CommitPrefabBuild, CommitCustomBuild. Bridges to VoxelConstructionSystem. |
| BuildModeManager.h/.cpp | ~500 | WorldSubsystem. Build mode state, raycast, ghost management, input handlers. Not yet fully wired in BP. |
| BuildGhostActor.h/.cpp | ~300 | Preview ghost. Prefab mesh + cell highlight pool for custom draw. |
| BuildModeTypes.h | 20 | EBuildMode, EBuildGhostState enums. |
| WorldForgePlayerController.h/.cpp | ~200 | C++ player controller base. Enhanced Input setup, build mode toggle. |
| WorldForgeCamera.h/.cpp | ~200 | Camera pawn. RMB orbit, WASD move, Q/E rotate, Ctrl+mouse vertical pan. |

### Races/ (2 files)
| File | Lines | Purpose |
|---|---|---|
| RaceDataAsset.h/.cpp | 36 | URaceDataAsset schema. Stat modifiers, affinities. .cpp is empty (data-only asset). |

### Save/ (3 files)
| File | Lines | Purpose |
|---|---|---|
| SaveSubsystem.h/.cpp | ~500 | Async save/load. Collects from all subsystems, distributes on load. RNG restoration. Does NOT save FWorldLoreState. |
| WorldForgeSaveGame.h | ~90 | USaveGame class. Has SaveGame on all fields. Has Artifacts/ArtifactHistory but no FWorldLoreState master container. |

### Tech/ (1 file)
| File | Lines | Purpose |
|---|---|---|
| ITechSystem.h | 36 | Interface. Phase 4 content — no implementations yet. |

### UI/ (2 files)
| File | Lines | Purpose |
|---|---|---|
| BuildModeWidget.h/.cpp | ~200 | C++ base for UMG build panel. BlueprintCallable actions, BlueprintImplementableEvent callbacks. |

### Visual/ (12 files)
| File | Lines | Purpose |
|---|---|---|
| VoxelSpawnSubsystem.h/.cpp | ~150 | VSS master subsystem. Dirty flag polling, driver management. Save support labeled "stub". |
| SettlementVisualManager.h/.cpp | ~200 | HISM spawning for buildings. Placeholder layout logic. |
| ColonistVisualManager.h/.cpp | ~300 | Spawns/tracks colonist actors. Binds to SimulationTimeManager for per-step visual sync. |
| DA_VoxelVisualRuleSet.h/.cpp | ~100 | Data asset for visual rules. Mesh lookups by building tag and density tier. |
| FogOfWarManager.h/.cpp | ~80 | Bridges FWorldKnowledgeState to SkyScape fog volumes. RevealTile, GetTileWorldPosition. |
| SpriteCharacterComponent.h/.cpp | ~150 | Billboard sprite. Faces camera, shadow casting, PaperZD animation hookup. |

### Voxel/ (6 files)
| File | Lines | Purpose |
|---|---|---|
| VoxelWorldManager.h/.cpp | ~850 | Core voxel interface. GridToWorld, WorldToGrid, PlaceVoxelBlock (real Voxel Plugin writes), GenerateTerrainFromTiles, ApplyBiomeChunkNative. |
| VoxelConstructionSystem.h/.cpp | ~400 | Build tasks. StartBuildTask, StartCustomBuildTask, PlaceNextBlock, stockpile gating, FinalizeBuilding. |
| SplineRoadManager.h/.cpp | ~150 | Spline mesh roads/rivers between grid waypoints. |

### World/ (8 files)
| File | Lines | Purpose |
|---|---|---|
| WorldGenPipeline.h/.cpp | ~300 | 16-step world generation. Steps 01,02,04,05,09,10,11,13 work. Steps 03,06,07,08,12,14,15,16 are TODO. |
| WorldMapSubsystem.h/.cpp | ~200 | Holds TArray<FWorldTileData> and TArray<FCivilizationData>. RevealTile, AddHistoryEvent. No FWorldLoreState field. |
| WorldGenBootstrapActor.h/.cpp | ~50 | Level actor that triggers WorldGenPipeline on BeginPlay. |
| IBiome.h | ~30 | Interface for biome implementations. |
| IWorldDriver.h | ~30 | Interface for world drivers. |

---

## SUMMARY COUNT

| Category | Count |
|---|---|
| **Backfill items still stubbed** | 5 (BF-07, 09, 10, 11, 12) |
| **WorldGen pipeline TODO steps** | 8 (03, 06, 07, 08, 12, 14, 15, 16) |
| **Phase 3.5 missing passes** | 7 of 10 |
| **Phase 3.5 missing infrastructure** | 3 (FWorldLoreState struct, WorldMapSubsystem field, save/load) |
| **Economy notification stubs** | 8 |
| **Mod system stubs** | 2 |
| **Player build wiring gaps** | 6 |
| **Architecture violations** | 1 (ContentFilter .Contains()) |
| **Other stubs/deferred** | 8 |
| **Total incomplete items** | **48** |
