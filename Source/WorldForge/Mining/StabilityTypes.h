// StabilityTypes.h
// WorldForge – Structural Stability System
// Place in: Source/WorldForge/Mining/StabilityTypes.h

#pragma once

#include "CoreMinimal.h"
#include "StabilityTypes.generated.h"

// ─────────────────────────────────────────────────────────────────────────────
// Tuning constants
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Maximum horizontal span (in voxels) a ceiling tile may bridge without a
 * supporting column. Matches Going Medieval's default of 4.
 * A column = any solid voxel that itself has solid directly below it (Z-1).
 */
static constexpr int32 WF_STABILITY_MAX_SPAN = 4;

/**
 * Maximum chain depth for cascading collapses.  Prevents runaway recursion
 * on edge-case geometry while still allowing realistic cave-in propagation.
 */
static constexpr int32 WF_COLLAPSE_MAX_CASCADE = 8;

/**
 * VoxelSize in Unreal Units (must match VoxelPlugin actor setting, 100 UU).
 * Used for world-space proximity checks (agent injury radius, etc.).
 */
static constexpr float WF_VOXEL_SIZE_UU = 100.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Enums
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Coarse stability tier shown in overlay and used in log messages.
 * Derived from the raw span-distance value returned by ComputeSpanStability().
 */
UENUM(BlueprintType)
enum class EStabilityTier : uint8
{
    /** span == 0  – will collapse next tick */
    Unstable   = 0,
    /** span == 1  – one tile from collapse; player must act */
    Critical   = 1,
    /** span == 2  */
    Poor       = 2,
    /** span == 3  */
    Adequate   = 3,
    /** span >= 4 or voxel is itself a column  */
    Stable     = 4,
};

// ─────────────────────────────────────────────────────────────────────────────
// Structs
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Result produced by UStabilitySubsystem::OnVoxelRemoved().
 * MiningSubsystem inspects this to log / forward damage events.
 */
USTRUCT(BlueprintType)
struct FCollapseResult
{
    GENERATED_BODY()

    /** Voxel grid coordinates of every voxel that caved in (including cascades). */
    UPROPERTY(SaveGame)
    TArray<FIntVector> CollapsedVoxels;

    /**
     * Agent IDs found inside the collapse zone (1-voxel radius per collapsed
     * voxel).  Forwarded as a damage event via EventBus; HealthProcessor owns
     * the actual HP deduction.
     */
    UPROPERTY(SaveGame)
    TArray<int32> InjuredAgentIDs;

    /** Settlement that triggered the original mine operation. */
    UPROPERTY(SaveGame)
    int32 SettlementID = INDEX_NONE;

    /** Total cascade generations that fired (0 = no collapse). */
    UPROPERTY(SaveGame)
    int32 MaxCascadeDepthReached = 0;

    bool WasTriggered() const { return CollapsedVoxels.Num() > 0; }
};

/**
 * Per-voxel stability snapshot cached inside the subsystem.
 * Only tracked for voxels near mined areas; natural terrain is implicitly Stable.
 */
USTRUCT()
struct FVoxelStabilityEntry
{
    GENERATED_BODY()

    /** Span-distance to nearest column (0 = no support, WF_STABILITY_MAX_SPAN = column). */
    UPROPERTY(SaveGame)
    int32 SpanDistance = WF_STABILITY_MAX_SPAN;

    /** Resolved tier (derived from SpanDistance). */
    UPROPERTY(SaveGame)
    EStabilityTier Tier = EStabilityTier::Stable;

    /** True when the entry was written this frame and may need re-check. */
    bool bDirty = false;
};
