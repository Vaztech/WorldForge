// StabilitySubsystem.h
// WorldForge – Structural Stability System
// Place in: Source/WorldForge/Mining/StabilitySubsystem.h

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Mining/StabilityTypes.h"
#include "StabilitySubsystem.generated.h"

/**
 * UStabilitySubsystem
 *
 * Implements Going Medieval-style span-based structural stability.
 *
 * MODEL SUMMARY
 * ─────────────
 * A voxel is a "column" if it is solid and its neighbour directly below (Z-1)
 * is also solid.  Columns anchor the ceiling.
 *
 * A solid voxel V with air directly below it (Z-1 is air) is a "ceiling" voxel.
 * Its stability = the horizontal BFS distance to the nearest column at the same
 * Z level, bounded by WF_STABILITY_MAX_SPAN.
 *   • stability == WF_STABILITY_MAX_SPAN → V is (or touches) a column → Stable
 *   • stability ∈ [1, MAX_SPAN-1]        → spans supported but narrowing margin
 *   • stability == 0                     → no column reachable → cave-in
 *
 * INTEGRATION
 * ───────────
 * 1. MiningSubsystem calls OnVoxelRemoved() after every successful dig.
 * 2. The return value (FCollapseResult) carries collapsed voxels + injured agents.
 * 3. MiningSubsystem forwards injured agent IDs to EventBus
 *    (WorldForge.Event.CaveCollapse) so HealthProcessor can apply damage.
 * 4. DigSubsystem is called directly for cascade removals so the Voxel Plugin
 *    mesh updates correctly.
 *
 * QUERYING (overlay / UI)
 * ───────────────────────
 * GetVoxelStability()   → raw span value
 * GetStabilityTier()    → coarse enum bucket
 * GetStabilityColor()   → overlay colour (red → green gradient)
 * IsAirVoxel()          → whether a voxel has been mined / collapsed
 */
UCLASS()
class WORLDFORGE_API UStabilitySubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

    // ── Subsystem lifecycle ──────────────────────────────────────────────────
public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ── Primary API (called by MiningSubsystem) ──────────────────────────────
public:
    /**
     * Must be called immediately after every voxel is physically removed from
     * the world.  Registers the voxel as air, recalculates the surrounding
     * ceiling slab, and fires cascading collapses if any ceiling tiles become
     * unsupported.
     *
     * @param VoxelGrid   Voxel-index coordinates of the removed voxel
     *                    (same space used by MiningSubsystem designations,
     *                     e.g. X=-1495 Y=-187 Z=29).
     * @param SettlementID  Owning settlement (forwarded into FCollapseResult).
     * @return  Summary of every voxel that caved in and agents in the zone.
     *          Result.WasTriggered() == false if no collapse occurred.
     */
    FCollapseResult OnVoxelRemoved(FIntVector VoxelGrid, int32 SettlementID);

    // ── Query API (overlay / UI / debug) ────────────────────────────────────
public:
    /**
     * Returns the span-distance to the nearest supporting column for a solid
     * ceiling voxel.  Natural (untracked) terrain always returns WF_STABILITY_MAX_SPAN.
     * Air voxels return 0.
     */
    int32 GetVoxelStability(FIntVector VoxelGrid) const;

    /** Bucket the raw span value into a human-readable tier. */
    EStabilityTier GetStabilityTier(FIntVector VoxelGrid) const;

    /**
     * Returns an RGBA overlay colour suitable for the DebugSimulationSystem
     * stability overlay pass.
     *   Unstable  → red     (1,0,0)
     *   Critical  → orange  (1,0.4,0)
     *   Poor      → yellow  (1,1,0)
     *   Adequate  → lime    (0.6,1,0)
     *   Stable    → green   (0,1,0)
     */
    FLinearColor GetStabilityColor(FIntVector VoxelGrid) const;

    /** Whether this voxel is currently air (mined out or collapsed). */
    bool IsAirVoxel(FIntVector VoxelGrid) const;

    /** Returns how many voxels are currently tracked as air. */
    int32 GetAirVoxelCount() const { return AirVoxels.Num(); }

    // ── Internal implementation ──────────────────────────────────────────────
private:

    // ── Voxel state helpers ──
    /**
     * A voxel is solid if it has never been mined or collapsed.
     * All terrain starts solid; only OnVoxelRemoved / ProcessCollapses create air.
     */
    bool IsSolidVoxel(FIntVector V) const;

    /**
     * A solid voxel "has vertical support" if either:
     *  (a) V.Z == 0  (bedrock floor), or
     *  (b) its immediate neighbour at V.Z-1 is also solid.
     * This is the definition of a "column base".
     */
    bool HasVerticalSupport(FIntVector V) const;

    // ── Stability computation ──
    /**
     * Horizontal BFS from ceiling voxel C at C.Z.
     * Traverses only solid voxels at the same Z level.
     * Returns WF_STABILITY_MAX_SPAN - distance_to_nearest_column, clamped [0, MAX_SPAN].
     * Returns 0 if no column found within MAX_SPAN steps.
     */
    int32 ComputeSpanStability(FIntVector C) const;

    /**
     * Scans the slab of voxels around RemovedVoxel across multiple Z levels
     * (Z-1 to Z+WF_STABILITY_MAX_SPAN+1) and within a horizontal radius of
     * WF_STABILITY_MAX_SPAN+1.  Populates OutUnstable with every solid ceiling
     * voxel whose computed span-stability is 0.
     */
    void FindUnstableCeilingVoxels(FIntVector RemovedVoxel, TArray<FIntVector>& OutUnstable);

    // ── Collapse processing ──
    /**
     * Iterates CollapseQueue, removes each voxel from the world via DigSubsystem,
     * checks for agent proximity, appends to Result, then calls itself recursively
     * for any new ceiling voxels exposed by the cascade (bounded by
     * WF_COLLAPSE_MAX_CASCADE).
     */
    void ProcessCollapses(
        TArray<FIntVector>& CollapseQueue,
        FCollapseResult& Result,
        int32 CascadeDepth);

    // ── Agent proximity ──
    /**
     * Returns Agent IDs whose world-space position is within 1.5 voxel-radii of
     * the collapsed voxel's world centre.  Queries UNavigationContext for the
     * settlement's agent grid positions.
     *
     * Stub note: adapt to actual NavigationContext / ColonyManager agent-position
     * API once those headers are available in context.
     */
    TArray<int32> GetAgentsNearVoxel(FIntVector VoxelGrid, int32 SettlementID) const;

    // ── Coordinate helpers ──
    /** Converts voxel-index coords to world-space (cm). VoxelSize = 100 UU. */
    static FVector VoxelGridToWorld(FIntVector VG);

    /** Invalidates StabilityCache entries in the 3³ neighbourhood of Center. */
    void InvalidateCacheAround(FIntVector Center, int32 Radius = 2);

    // ── State ────────────────────────────────────────────────────────────────
private:
    /**
     * All voxels that are currently air: initially empty, grown by every
     * OnVoxelRemoved() call and every cascade collapse.
     * This is the single source of truth for "solid vs. air" in the subsystem.
     */
    TSet<FIntVector> AirVoxels;

    /**
     * Cached span-stability values for ceiling voxels near mined areas.
     * Invalidated by InvalidateCacheAround() whenever a voxel transitions to air.
     */
    TMap<FIntVector, int32> StabilityCache;

    // ── Overlay colour constants (static, defined in .cpp) ──
    static const FLinearColor Color_Unstable;
    static const FLinearColor Color_Critical;
    static const FLinearColor Color_Poor;
    static const FLinearColor Color_Adequate;
    static const FLinearColor Color_Stable;
};
