# WorldForge Source Audit — Updated Status

**Original: 48 incomplete items | Resolved across sessions: 26 | Remaining: 22**

---

## COMPLETED THIS SESSION (Overlay Blending System)

### Multi-Overlay Map System (6 items)

| # | Item | Fix Applied |
|---|---|---|
| ✅ | Single-mode overlay (radio buttons) | Replaced `EWorldMapOverlayMode OverlayMode` with `int32 ActiveOverlayLayers` bitfield. `WorldForgeOverlay` namespace defines per-bit constants. Legacy `SetOverlayMode()`/`GetOverlayMode()` preserved — routes through bitfield internally. |
| ✅ | Per-layer alpha blending | Six `UPROPERTY` alpha values: `PoliticalAlpha=0.45`, `EconomyAlpha=0.35`, `CosmicAlpha=0.30`, `TradeAlpha=0.35`, `UndergroundAlpha=0.50`, `FogAlpha=0.65`. Central `ComputeBlendedTileColor()` composites all active layers in fixed order. |
| ✅ | Fog overlay layer | New `MakeFogLayerColor()` — Unexplored tiles → near-black, Rumored → dim, Surveyed/default → pass-through. Fog composited last in the blending pipeline. |
| ✅ | Toggle UI (checkboxes) | `WorldForgeFullMapWidget.h` declares six `UCheckBox*` (`CHK_Political` through `CHK_Fog`) with `BindWidgetOptional`. `SyncCheckboxesToOverlayState()` keeps checkboxes in sync. Legacy `UButton*` overlays kept for backwards compat. |
| ✅ | Keyboard overlay toggles | Keys 1–6 toggle Political/Economy/Cosmic/Trade/Underground/Fog via `ToggleOverlayLayer()` in `NativeOnKeyDown`. Confirmed working in PIE (log shows `0x01` → `0x7F` round-trip). |
| ✅ | DF-style staged worldgen updates | `PushPartialMapUpdate()` added to `WorldGenPipeline`. Calls `UpdateFromPartialPipeline()` + `RebuildMapTexture()` after Steps 02, 03, 04, 09, 13, 16. PIE log confirms 6 partial updates firing with correct tile/civ counts. |

### PIE Log Verification

Overlay toggle trace from logs confirms correct bitfield behavior:

```
Layers=0x41 (Terrain+Fog) → default on boot
→ 0x01 (Fog off via key 6)
→ 0x03 (Political on via key 1)
→ 0x07 (Economy on via key 2)
→ 0x0F → 0x1F → 0x3F → 0x7F (all layers on)
→ 0x3F → 0x1F → ... → 0x01 (toggled back off one by one)
```

Staged updates confirmed:

```
Step02 → Partial update: Tiles=65536 Civs=0 CivTiles=0
Step03 → Partial update: Tiles=65536 Civs=0 CivTiles=0
Step04 → Partial update: Tiles=65536 Civs=0 CivTiles=0
Step09 → Partial update: Tiles=65536 Civs=4 CivTiles=412
Step13 → Partial update: Tiles=65536 Civs=4 CivTiles=412
Step16 → Partial update: Tiles=65536 Civs=4 CivTiles=412
```

### Files Modified This Session

| File | Changes |
|---|---|
| WorldMapSubsystem.h | Replaced `EWorldMapOverlayMode OverlayMode` with `int32 ActiveOverlayLayers` bitfield. Added `WorldForgeOverlay` namespace, per-layer alpha UPROPERTYs, toggle API (`SetOverlayLayerActive`, `ToggleOverlayLayer`, `IsOverlayLayerActive`, `SetAllOverlayLayers`). Refactored private overlay color methods to `Make*LayerColor` (raw overlay, no terrain base). Added `ComputeBlendedTileColor`, `BlendLayer`, `MakeFogLayerColor`. Removed old `GetOverlayAwareTileColor`/`GetOverlayAwareTileColorAtCell`. |
| WorldMapSubsystem.cpp | Central blending pipeline in `ComputeBlendedTileColor()`. Six `Make*LayerColor` functions return raw overlay color. `BlendLayer` static helper. `SetOverlayMode`/`GetOverlayMode` route through bitfield. Detailed map texture uses same overlay blending. `GetCivColor` consolidated to use `GetStableCivColor`. |
| WorldForgeFullMapWidget.h | Added `UCheckBox*` for 6 overlays (`CHK_Political` through `CHK_Fog`), all `BindWidgetOptional`. Added toggle handlers, `SetOverlayLayerAndRefresh`, `SyncCheckboxesToOverlayState`. |
| WorldForgeFullMapWidget.cpp | Checkbox binding in `NativeConstruct`. Toggle handlers call `SetOverlayLayerActive`. Keyboard shortcuts 1–6. `UpdateSelectionText` shows `Layers=0xNN`. Cleaned up duplicate civ-territory check in `HandleConfirmClicked` (resolved tile now uses actual resolved index). |
| WorldGenPipeline.h | Added `void PushPartialMapUpdate()` private method. |
| WorldGenPipeline.cpp | `PushPartialMapUpdate()` implementation. Six calls inserted into `GenerateWorld()` after Steps 02, 03, 04, 09, 13, 16. |

---

## PREVIOUSLY COMPLETED (Prior Sessions)

### Original PIE Log Issues (All Resolved)

| # | Item | Fix Applied |
|---|---|---|
| ✅ | Decoration pass producing 0 actors | Biome tag mismatch fixed — `Forest` → `Woodland`/`Taiga`/`Shrubland`/`Hills`/`Mountain`/`Alpine`/`Canyon`/`Steppe`. 8 actors now spawning. |
| ✅ | BUILD TIMESTAMP logged as Error | Changed to `Log` severity in VoxelWorldManager.cpp |
| ✅ | DIAGNOSTIC/terrain samples/classification logged as Warning | All changed to `Log` severity in VoxelWorldManager.cpp |
| ✅ | Mods folder "not found" warning | ModLoaderSubsystem.cpp now auto-creates `/mods/` on first run |
| ✅ | WorldHistory names logged as Warning | Changed to `Log` in NameGenerationPass.cpp |
| ✅ | FigurePass figures logged as Warning | Changed to `Log` in FigureGenerationPass.cpp |
| ✅ | VoxelWorld pool double-creation | User set `CreateGlobalPool = false` (set back to `true` to clear inverse warning) |

### Backfill Items (5 of 5 Resolved)

| # | Item | Fix Applied |
|---|---|---|
| ✅ BF-07 | SetTradeRouteFlowMod | DiplomacySubsystem.cpp — real TradeReputation trust deltas via QueueTrustDelta. |
| ✅ BF-09 | GetRouteDirection | NavalSystem.cpp — queries SettlementManager for WorldOrigin, computes normalized 2D direction. |
| ✅ BF-10 | FaithFactor | CosmicSimulationSubsystem.cpp — caches real average PurposeNeed/100 per settlement. |
| ✅ BF-11 | MoraleProcessor | Full implementation — weighted needs, injury penalties, smooth blending, EventBus dispatch. |
| ✅ BF-12 | IncidentProcessor | Full implementation — RNG-driven daily check, 4 incident types, EventBus dispatch. |

### WorldGen Pipeline Steps (5 of 8 TODO Steps Resolved)

| Step | Status |
|---|---|
| ✅ 07 | Phase 4 IWorldDriver extension point. |
| ✅ 12 | Seeds 6 cosmic entities into CosmicSimulationSubsystem. |
| ✅ 14 | Assigns trait tags: Maritime, Continental, Mountainous, Volcanic, Diverse, Warlike. |
| ✅ 15 | Logs complete generation summary. Placeholder for FWorldLoreState persistence. |
| ✅ 16 | Initializes fog-of-war: civ-controlled tiles → Surveyed, others → Unexplored. |

### Pipeline Steps Already Working (Confirmed by PIE)

| Step | Status |
|---|---|
| ✅ 03 | Step03_RunTectonics — provinces, ranges, rivers, canyons, volcanoes |
| ✅ 05 | Step05_InitVoxelChunks — terrain generates via density generator |
| ✅ 06 | Step06_PCGDecorationPass — fires via HandleTerrainReady |
| ✅ 08 | Step08_SeedResources — resource tags applied by biome |

---

## REMAINING ITEMS (22)

### NEW — Cosmetic Issues Found This Session

| # | Item | File | Severity |
|---|---|---|---|
| 🔧 | "Confrim" typo on WBP confirm button | WBP_FullMapWidget (Blueprint) | LOW — visual only |
| 🔧 | FullMapWidget "Constructed" still logged as Warning | WorldForgeFullMapWidget.cpp line ~73 | LOW — log severity |
| 🔧 | `[CivGen] Total controlled tiles` still logged as Warning | WorldGenPipeline.cpp Step09 | LOW — log severity |

### ACTION REQUIRED — GameplayTag Registration

Steps 12, 14, and 16 are code-complete but produce zero output because the tags aren't registered. Add these to your GameplayTags config (DefaultGameplayTags.ini or a DataTable):

**Cosmic entities (Step12):**
`WorldForge.Cosmic.Entity.War`, `.Nature`, `.Commerce`, `.Knowledge`, `.Death`, `.Creation`
`WorldForge.Domain.War`, `.Nature`, `.Commerce`, `.Knowledge`, `.Death`, `.Creation`

**World traits (Step14):**
`WorldForge.WorldTrait.Maritime`, `.Continental`, `.Mountainous`, `.Volcanic`, `.Diverse`, `.Warlike`

**Incident events (IncidentProcessor):**
`WorldForge.Sim.Event.Disease`, `.Injury`, `.FoodSpoilage`, `.GoodOmen`, `.MoraleCritical`

### ACTION REQUIRED — CivilizationGenerator Gap

Step09 places civilizations but never sets `FWorldTileData::ControllingCivID` on tiles within their territory. This causes Step16's fog-of-war to show 0 civ-controlled tiles. Fix location: `CivilizationGenerator.cpp` in `GenerateCivilizations()`.

### HIGH — Phase 3.5 World History Generator (7 missing passes)

| Pass | Status |
|---|---|
| LineagePass | **MISSING** — family trees not generated |
| BattleDetailPass | **MISSING** — wars not resolved into named battles |
| ArtifactForgePass | **MISSING** — legendary artifacts not created |
| CreatureLorePass | **MISSING** — notable creatures not generated |
| FallenCityPass | **MISSING** — ruins with lore not generated |
| LegendSynthesisPass | **MISSING** — stories/legends not synthesized |
| ColonistBackstoryPass | **MISSING** — colonist backstories not generated |

**Also missing infrastructure:**
- `FWorldLoreState` struct not in `WorldForgeDataTypes.h`
- `WorldMapSubsystem` has no `WorldLoreState` field
- `SaveSubsystem` does not save/load `FWorldLoreState`

### HIGH — Economy System Stubs (9 items)

| Item | File | Status |
|---|---|---|
| NotifyWarStarted | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyWarEnded | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyPlagueEvent | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyDisasterEvent | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyGovernmentChange | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyTradeRouteOpened | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyTradeRouteClosed | WorldEconomySimulation.cpp | **STUB** — logs only |
| NotifyCivCollapse | WorldEconomySimulation.cpp | **STUB** — logs only |
| EpochSimulation | WorldEconomySimulation.cpp | **STUB** — logs "Epoch simulation stub" |

### HIGH — Mod System Stubs (2 items)

| Item | File | Status |
|---|---|---|
| MountModPak | ModLoaderSubsystem.cpp | **STUB** — Phase 4 |
| RegisterModContent | ModLoaderSubsystem.cpp | **STUB** — Phase 4 |

### MEDIUM — Architecture Violation (1 item)

| Item | File | Issue |
|---|---|---|
| ContentFilterSubsystem | ContentFilterSubsystem.cpp | Uses `.Contains()` string matching instead of `MatchesTag()`. Violates "Zero .ToString().Contains()" invariant. |

### MEDIUM — Other Stubs and Deferred Items (8 items)

| Item | File | Status |
|---|---|---|
| ContentFilter save/load | ContentFilterSubsystem.cpp | **TODO** — Phase 2 |
| ContentFilter persist | ContentFilterSubsystem.cpp | **TODO** — Phase 2 |
| DeriveEconomyScore fallback | CivilizationResilienceSystem.cpp | Returns 0.5f when EconSim null |
| NavalSystem distance | NavalSystem.cpp | Uses flat 50-step estimate instead of actual grid distance |
| VoxelSpawnSubsystem save | VoxelSpawnSubsystem.h | Save/restore labeled "Stub" |
| SettlementVisualManager layout | SettlementVisualManager.cpp | Placeholder deterministic layout |
| Custom build vertical layering | VoxelConstructionSystem.cpp | Deferred — custom builds only place on layer 0 |
| Cosmic entity domain tags | CosmicSimulationSubsystem.cpp | String fallback until full tag tree finalized |

### LOW — Acceptable / Phase 4

| Item | File | Note |
|---|---|---|
| RaceDataAsset.cpp | 1 line | Data asset — .cpp empty by design |
| IMagicSystem.h | 33 lines | Phase 4 interface |
| ITechSystem.h | 36 lines | Phase 4 interface |

---

## NOTE — Staged Updates Are Visual-Only (For Now)

The `PushPartialMapUpdate()` calls fire correctly during `GenerateWorld()` (6 rebuilds confirmed in PIE logs), but the player won't see progressive rendering because `GenerateWorld()` runs synchronously on the game thread. The viewport only repaints after the full function returns. To get true DF-style "watch the map build itself" playback, the pipeline needs to be refactored into a tick-driven state machine or latent action. The data plumbing is in place — this is a presentation-layer change only.
