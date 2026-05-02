# WorldForge Carry-Over Prompt — v3

You are continuing a UE 5.7 C++ WorldForge recovery/debugging session. World
foundation recovery is now COMPLETE. Active focus is Phase 4 (Player Build
Interaction Layer). Preserve all of the following context, rules, architecture
invariants, current status, known findings, and next-step priorities exactly.

---

## PROJECT

- Project name: WorldForge
- Engine: UE 5.7
- Project path: C:\Users\Vaz\Documents\Unreal Projects\WorldForge
- Active focus: Phase 4 — Player Build Interaction Layer
- World foundation recovery: COMPLETE (see "Recently Resolved" below)

---

## PRIMARY MISSION (current)

Resolve the Phase 4 player build interaction bug:

- Custom-draw build cells get added to the BuildMode pending list correctly
  (logs show `[BuildMode] Added cell (X, Y) — N total`)
- Exiting build mode does NOT result in any voxel blocks being placed
- ConstructionProcessor never posts build jobs after CustomDraw cells are
  committed
- No `[ConstructionProcessor]`, `[VoxelConstruction]`, `PlaceVoxelBlock`, or
  build-job assignment logs ever appear after build mode exit

The bridge between "user committed cells in CustomDraw mode" and
"ConstructionProcessor posts build jobs to JobProcessor /
VoxelConstructionSystem" is broken somewhere.

Files to inspect first:
1. ConstructionProcessor.cpp / .h
2. VoxelConstructionSystem.cpp / .h
3. BuildModeManager.cpp / .h (if the trail leads there)

---

## CRITICAL ARCHITECTURE INVARIANTS — NEVER BREAK

1. ALL randomness uses UWorldForgeRNG. No FMath::Rand().
2. ALL dark events must check UContentFilterSubsystem::IsEventAllowed().
3. ALL EventBus dispatch is queued via Dispatch(). FlushPendingEvents() fires
   AFTER all 9 processors and Phase 3B systems.
4. ALL persistable data structs live in WorldForgeDataTypes.h and use
   SaveGame where appropriate.
5. ALL gameplay tag comparisons use WorldForgeTags.h typed constants and
   MatchesTag(). No .ToString().Contains() logic.
6. VSS invariant: UVoxelSpawnSubsystem and all VSS drivers are READ-ONLY
   observers. They never write to simulation structs.
7. bSmoothSDF = true on all voxel profiles.
8. FWorldLoreState is immutable after world generation; gameplay-time lore
   goes elsewhere.
9. Core systems are orchestrator + pass pipelines. Hook into passes rather
   than inventing monolithic replacements.
10. Gameplay logic should live in C++; Blueprints are allowed only for editor
    assets, UI layout shells, data assets, and narrow visual/editor bridges
    intentionally retained.

---

## ARCHITECTURE CHANGE RULE — CRITICAL

Default rule:
**FIX THE EXISTING PIPELINE. DO NOT REINVENT IT.**

Required approach:
- prefer minimal, additive fixes over rewrites
- do not replace whole systems when a missing implementation or broken hook
  is the real issue
- do not redesign the orchestrator/pass-pipeline structure
- do not introduce a parallel worldgen architecture
- do not rewrite VoxelWorldManager, WorldGenPipeline, or related subsystems
  from scratch unless there is no viable incremental path

This rule applies equally to ConstructionProcessor / VoxelConstructionSystem.
The Phase 4 bug is almost certainly a missing hook, not a missing system.

---

## CURRENT HIGH-LEVEL ARCHITECTURE

Working runtime chain:

```
WorldGenBootstrapActor
  -> WorldGenPipeline
  -> WorldMapSubsystem.InitializeFromPipeline(...)
  -> UVoxelWorldManager.GenerateTerrainFromTiles(...)
  -> native deferred/retry terrain generation into AVoxelWorld
  -> AColonyManager waits for terrain readiness via OnTerrainReady event
  -> SettlementManager / ProcessorManager / NavigationContext bootstrap colony
  -> ProcessorManager.RegisterAgent(...)
  -> UColonistVisualManager spawns BP_WorldForgeColonist actors
  -> AWorldForgeColonist resolves surface world position via
     UVoxelWorldManager::TryGetSurfaceWorldLocation through the new
     VoxelCellToChunkCell conversion bridge
  -> SimulationTimeManager fires OnSimulationStep at FixedStepInterval
  -> ProcessorManager runs all 9 processors per step
  -> JobProcessor posts/assigns jobs and advances agent positions via
     TickMovement
  -> ColonistVisualManager.HandleSimulationStep calls UpdateVisuals on each
     tracked actor every step
  -> AWorldForgeColonist::Tick lerps actor toward TargetWorldPos
```

Build chain (PARTIAL — broken between Build Mode and ConstructionProcessor):

```
WorldForgePC (Enhanced Input)
  -> BuildModeManager.HandlePrimaryClick (CustomDraw mode)
  -> BuildModeManager adds cell to pending list ✓
  -> [User exits build mode]
  -> ?????  <-- MISSING/BROKEN LINK
  -> ConstructionProcessor (never receives committed cells)
  -> VoxelConstructionSystem (never asked to place voxels)
  -> JobProcessor (never receives build jobs)
  -> agents (never assigned construction work)
  -> PlaceVoxelBlock (never called)
```

---

## RECENTLY RESOLVED — World Foundation Recovery

The following bugs were fixed in the prior session and MUST NOT be reopened
without hard new evidence:

### 1. Coordinate space mismatch (voxel-cell vs chunk-cell)

**Bug:** `UVoxelWorldManager::TryGetSurfaceWorldLocation` interprets its
GridCell argument as a chunk-indexed cell (1 unit = ChunkWorldSize = 3200 UE),
because the cached terrain metadata is laid out chunk-indexed. But settlement
origins, FAgentData::CurrentGridPos, and the colonist visual placement code
all live in voxel-cell space (1 unit = VoxelSize = 100 UE). Passing voxel
cells directly to `TryGetSurfaceWorldLocation` placed colonists 32× too far
from origin.

**Fix:** Added `UVoxelWorldManager::VoxelCellToChunkCell(FIntPoint)` helper
that integer-divides voxel cells by `GetChunkSizeInVoxels()` (32). Used at
two call sites in `AWorldForgeColonist`:
- `ResolveWorldLocationFromAgentData` — converts before chunk-cache query,
  then takes Z from the surface query but X/Y from `GridToWorld` (voxel
  space) so colonists land at the correct in-voxel-space horizontal.
- `UpdateMovement` — same conversion before per-tick surface-Z lookup.

The `ClampTerrainCellToBounds` clamp from the previous session is still
present and still correct — it operates on chunk-space bounds.

**Files touched:**
- VoxelWorldManager.h (added one declaration)
- VoxelWorldManager.cpp (added one implementation)
- WorldForgeColonist.cpp (two call sites updated)

### 2. ColonistVisualManager::ResolveCellSize returned wrong value

**Bug:** `ResolveCellSize` returned `ChunkWorldSize` (3200) based on a
prior-session comment that misunderstood the coordinate space. Colonist code
uses voxel-space cells; cell size should be `VoxelSize` (100). This was a
latent bug — ResolveCellSize only feeds the deep-fallback path that never
runs in normal operation, so it was not the cause of any visible symptom.

**Fix:** Returns `VoxelWorldManager->VoxelSize` (100). Fallback returns
`100.0f`. Comment block rewritten to reflect voxel-space semantics.

**Files touched:**
- ColonistVisualManager.h (comment update)
- ColonistVisualManager.cpp (one-line return value change)

### 3. Visual sync diagnostic gap

**Bug:** ColonistVisualManager::HandleSimulationStep only logged at Verbose
level every 25 steps. When debugging "colonists not moving," there was no
way to confirm at a glance whether the visual sync hook was firing.

**Fix:** Logs at Log level every step, including step number and tracked
actor count. Format: `[ColonistVisualManager] HandleSimulationStep
settlement=N step=M trackedActors=K`

This is a permanent diagnostic improvement and should NOT be removed unless
log volume becomes a problem.

### 4. WorldToTerrainCell log spam

**Bug:** `bLogGridConversions` defaulted to `true` on UVoxelWorldManager,
producing tens of thousands of `[VoxelWorldManager] WorldToTerrainCell ...`
log lines per PIE session and drowning all other logs.

**Fix:** Default flipped to `false` in VoxelWorldManager.h. Re-enable
manually if coordinate math debugging is needed.

### 5. Visual presentation path was misdiagnosed as broken

**Wasted investigation:** An earlier session believed `BP_WorldForgeColonist`
had a sprite billboarding / camera-target / null-flipbook bug. None of those
were real. Verified working:
- `BP_SpriteCharacterComponent` resolves flipbooks correctly
- `BroadcastFlipbookChanged` fires correctly
- `BP_WorldForgeColonist` Event Graph binds `OnFlipbookChanged` to
  `HandleFlipbookChanged` and applies the flipbook to a `Flipbook Renderer`
  component
- `CameraTarget` is assigned in BP `BeginPlay` from the player camera actor
- Yaw billboarding works
- Walk anim plays correctly when `EAgentState::MovingToTarget`

DO NOT re-investigate the visual presentation path unless there is a NEW
specific symptom that cannot be explained by sim-side or coordinate-side
bugs.

### 6. Sim time tuning for visible movement during testing

**Not a bug, but documented for future sessions:**

`USimulationTimeManager::TargetMinutesPerDay` was 20.0 (production value),
giving FixedStepInterval = 24s/step. At that step interval, colonist
movement was imperceptibly slow at typical PIE camera distances even though
the simulation was running correctly.

**Tuning changes applied (TESTING VALUES — REVERT BEFORE SHIPPING):**
- `USimulationTimeManager::TargetMinutesPerDay`: 20.0 → 1.0 (FixedStepInterval
  becomes 1.2s)
- `USimulationTimeManager::FixedStepInterval` default: 24.0 → 1.2 (kept
  consistent with TargetMinutesPerDay; RecalculateTimeSettings() overwrites
  this on init anyway)
- `UJobProcessor::MovementCellsPerStep`: 1 → 4 (Phase 2 default was 1; bumped
  for visible motion at PIE camera distances)

Combined effect: ~3.3 m/s agent movement, clearly visible. Production
tuning will likely revert TargetMinutesPerDay to 20.0; MovementCellsPerStep
may stay at 4 or be re-tuned in Phase 3 with race/terrain modifiers.

Both files have inline comments marking these as testing values.

---

## CONFIRMED WORKING (end-to-end loop)

Latest PIE evidence (95 sim steps in one session):

- Worldgen: complete in ~17s, all 65536 tiles applied, all civs/economy/
  history layers populated
- Terrain ready event fires; colony bootstrap waits correctly
- Settlement created, 16 agents registered, all 16 visual actors spawned
- Sim time advances continuously (1.2s/step, 50 steps/day, day rollover at
  step 50 confirmed)
- All 9 processors run every step (`[ProcessorManager] Completed simulation
  step N for 2 settlement(s).`)
- JobProcessor posts harvest jobs, assigns them, agents enter MovingToTarget
- Agents arrive at targets, ProductionProcessor fires:
  - `[ProductionProcessor] +1 WorldForge.Item.Food in settlement 0`
  - `[ProductionProcessor] Agent N ENTERED CARRY STATE`
  - `[ProductionProcessor] Complete: WorldForge.Job.Harvest.X agent N`
- Stockpiles climb naturally: started Food=5/Wood=2/Stone=2, by step 89
  reached Food=26/Wood=16/Stone=14
- EconomySim posts MARKET REPORT every 10 steps with prices, supply, demand
- CivResilience reports civilization stats every 50 steps
- Walk animations play during movement, idle anims when at target
- 16 colonists visible in PIE, sprite-rendered with correct race art

Known cosmetic issue (resolved this session, see below):
- Colonists visually float ~1-2m above terrain surface

---

## OPEN ISSUES

### Issue A: Floating colonists (cosmetic — fix in BP defaults)

**Status:** Likely 1-line BP edit, no C++ change.

`AWorldForgeColonist::TerrainZOffset` defaults to 50.0 UE. Combined with
sprite plane geometry on `BP_WorldForgeColonist`, colonists render ~1-2m
above ground level.

**Try first:** In `BP_WorldForgeColonist` Details panel, Colonist|Visual
category, set `Terrain Z Offset` to `0.0`. Test in PIE. Adjust in 25-unit
increments if needed (negative values allowed).

**If that doesn't work:** The `Flipbook Renderer` component in
`BP_WorldForgeColonist` has a non-zero relative Z offset, or its sprite
pivot is set to "Center" instead of "Bottom Center". Fix on the component
defaults.

### Issue B: Phase 4 build bug — CustomDraw cells never become voxels

**Status:** Active investigation. Files needed:
- ConstructionProcessor.cpp / .h
- VoxelConstructionSystem.cpp / .h
- BuildModeManager.cpp / .h (if needed)

**Symptoms (from PIE log):**
```
[BuildMode] Entered CustomDraw mode: WorldForge.Voxel.Material.Stone
[BuildMode] Added cell (20, 9) — 1 total
[BuildMode] Added cell (20, 10) — 2 total
[BuildMode] Added cell (20, 11) — 3 total
[BuildMode] Exited build mode.
```

After exit, NO logs from:
- VoxelConstruction
- ConstructionProcessor
- JobProcessor (no build job posted)
- PlaceVoxelBlock

**Hypotheses (in order of likelihood):**

1. **Missing submit-on-exit hook.** BuildModeManager accumulates pending
   cells but `ExitBuildMode()` (or wherever build mode is left) does not
   call any "commit pending cells to ConstructionProcessor" function. The
   cells are abandoned. Most likely cause based on the carry-over doc's
   prior-session note that ConstructionProcessor "never posts build jobs
   despite tasks creating successfully."

2. **Submit hook exists but routes to wrong destination.** Cells get sent
   somewhere (ConstructionProcessor's pending list?) but the next-step
   ProcessStep on ConstructionProcessor doesn't drain the list into
   JobProcessor. Suggested by the prior-session description that
   "ConstructionProcessor never posts build jobs."

3. **Jobs get posted but with wrong tag/skill so no agent picks them up.**
   Less likely — the JobProcessor logs would still show
   `[JobProcessor] Posted job WorldForge.Job.Build...` and we don't see
   that.

4. **Jobs get posted and assigned but agents never arrive at target.** Even
   less likely — we'd see assignment logs.

The first hypothesis is by far the strongest. The fix is probably a
~10-line addition somewhere in BuildModeManager's exit path that walks the
pending cell list and calls a `ConstructionProcessor::SubmitBuildCells()`
or `VoxelConstructionSystem::QueueBuildCells()` (whichever exists).

### Issue C: WorldGen has TODO steps remaining (deferred — not blocking)

Per prior carry-over and confirmed in latest log:
- Step03: Tectonics — TODO (terrain stays flat-ish, pure Perlin fBm)
- Step06, Step07, Step08 — TODO
- Step12 — TODO
- Step14, Step15, Step16 — TODO

These are KNOWN unimplemented worldgen passes, not bugs. Address only after
Phase 4 build interaction is solid.

### Issue D: Two settlements being created (cosmetic — investigate later)

Log shows:
```
[SettlementManager] Created 'PlayerColony' ID=0
... 16 agents registered ...
[SettlementManager] Created 'PlayerColony' ID=1
[ProcessorManager] Settlement 1 registered.
```

Settlement 1 has zero agents and zero stockpile, but receives MARKET REPORTS
every 10 steps. Unclear why it's being created. Not blocking anything but
should be tracked down — probably a duplicate `CreateSettlement` call
somewhere in `BP_NewColonyManager` or in the test bootstrap path. Low
priority.

---

## FILES TOUCHED THIS SESSION

All changes are minimal and additive. No systems were removed or rewritten.

### VoxelWorldManager.h
- Added `VoxelCellToChunkCell(FIntPoint VoxelCell) const` declaration
- Flipped `bLogGridConversions` default from `true` to `false`

### VoxelWorldManager.cpp
- Added `VoxelCellToChunkCell` implementation using `GetChunkSizeInVoxels()`
  and `FMath::FloorToInt` for negative-safe floor division

### WorldForgeColonist.cpp
- `ResolveWorldLocationFromAgentData`: converts voxel cell → chunk cell
  before surface query and fallback spawnable search; uses `GridToWorld`
  for voxel-space X/Y and surface query result for Z; warning log now
  prints both VoxelCell and ChunkCell
- `UpdateMovement`: same voxel→chunk conversion before per-tick Z lookup

### WorldForgeColonist.h
- No changes

### ColonistVisualManager.h
- Updated comment block on `ResolveCellSize` to reflect voxel-space semantics

### ColonistVisualManager.cpp
- `ResolveCellSize` returns `VoxelWorldManager->VoxelSize` (100), fallback
  100.0f
- `HandleSimulationStep` logs every step at Log level (was every-25-step
  Verbose)

### SimulationTimeManager.h
- `TargetMinutesPerDay` default 20.0 → 1.0 (TESTING VALUE)
- `FixedStepInterval` default 24.0 → 1.2 (TESTING VALUE, kept consistent)
- Both have inline comments noting they should be reverted before shipping

### JobProcessor.h
- `MovementCellsPerStep` default 1 → 4 (TESTING VALUE)
- Inline comment noting Phase 2 default was 1 and Phase 3 may re-tune

---

## FOLLOW-UP CLEANUP (low priority, do later)

1. **Rename `UVoxelWorldManager::WorldToTerrainCell`** — it's misleadingly
   named. It returns voxel cells (divides world by VoxelSize), not terrain
   cells (which would be chunk cells). Suggested rename:
   `WorldToVoxelCell`. Same applies to anything else named "TerrainCell"
   that actually operates in voxel space.

2. **Rename `ClampTerrainCellToBounds`** — clamps to chunk-space bounds, so
   the name is correct *for the cache* but confusing relative to the rest
   of the codebase's "TerrainCell" naming. Either accept as-is or rename
   to `ClampChunkCellToBounds` once the rename pass above is done.

3. **Revert testing tuning values** before shipping:
   - `SimulationTimeManager::TargetMinutesPerDay` → 20.0
   - `SimulationTimeManager::FixedStepInterval` → 24.0 (or whatever the
     production interval should be)
   - `JobProcessor::MovementCellsPerStep` → 1 (or whatever Phase 3 settles
     on after race/terrain modifiers are added)

4. **Investigate Settlement ID=1 duplicate creation** (Issue D above).

5. **Implement WorldGen TODO steps** (Issue C above).

---

## IMPLEMENTATION RULES FOR NEXT SESSION

- Be extremely careful and factual.
- Distinguish clearly between confirmed-from-logs/code, strong inference,
  and unverified.
- Prefer minimal, additive code changes.
- Do not regress the now-working sim/visual/coordinate pipelines.
- Do not re-investigate the visual presentation path unless a new symptom
  demands it.
- Do not re-investigate the coordinate-space conversion unless a new
  symptom demands it.
- Do not touch SimulationTimeManager tuning unless explicitly asked.
- Keep BP_NewColonyManager as a wrapper child of native AColonyManager.
- Keep gameplay logic in C++; keep Blueprint only as wrapper/editor bridge.

---

## NEXT TASKS — DO THESE IN ORDER

1. Resolve Issue A (floating colonists) via BP defaults edit. No code from
   the assistant unless the BP edit alone doesn't work.

2. Resolve Issue B (Phase 4 build bug):
   - Read ConstructionProcessor.h/.cpp first
   - Read VoxelConstructionSystem.h/.cpp second
   - Read BuildModeManager.h/.cpp only if the trail leads there
   - Diagnose which of the four hypotheses above is correct
   - Propose the smallest possible additive fix
   - Apply, test in PIE, verify build cells become voxels

3. After Issue B is resolved, write v4 of this carry-over doc reflecting the
   updated state.

4. Then either:
   - Move to WorldGen TODO step implementation (Step03 Tectonics is highest
     visible payoff), OR
   - Move to Phase 4 polish (build mode UI feedback, prefab placement,
     undo/redo, etc.)

---

## REQUEST RULE FOR ASSISTANT

Only request the smallest next set of files needed. Do not ask for the whole
project. Do not expand scope unless the current investigation proves clean.

---

## CURRENT USER EXPECTATION

Practical debugging help, precise code-level diagnosis, minimal edits, and
continuity without re-explaining everything from scratch.
