// DigResult.h
// WorldForge — Phase 1: Digging Foundation (v5.0 SDF Brush Path)
// Pure data structs describing dig operations, results, and undo records.
// No dependencies beyond UE core types + GameplayTags.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "DigResult.generated.h"

// ===================================================================
// Brush shape enum (dig-side mirror of ESDFBrushShape)
// ===================================================================

/**
 * Shape of the SDF brush used for terrain excavation.
 * Maps 1:1 to ESDFBrushShape in TerrainModificationLayer.h at apply time.
 *
 * - Sphere:   General-purpose carving. Smooth edges on all axes.
 * - Cylinder: Vertical shafts. Circular XY cross-section, flat top/bottom.
 * - Box:      Room excavation. Axis-aligned rectangular volume.
 */
UENUM(BlueprintType)
enum class EDigBrushShape : uint8
{
	/** Spherical SDF subtraction. Best for single-block or aesthetic carving. */
	Sphere,

	/** Cylindrical SDF subtraction. Ideal for vertical shafts and wells. */
	Cylinder,

	/** Box SDF subtraction. Clean room/tunnel excavation. */
	Box
};

// ===================================================================
// Brush record (stored for undo)
// ===================================================================

/**
 * Records the exact SDF brush parameters applied during a dig.
 * Stored in FDigResult so the undo path can reconstruct and remove the brush.
 *
 * The BrushID is assigned by VoxelWorldManager::ApplyRuntimeBrush() and is
 * the primary key used by RemoveBrushByID() during undo.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FDigBrushRecord
{
	GENERATED_BODY()

	/** Runtime ID assigned by the modification layer. INDEX_NONE if not yet applied. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 BrushID = INDEX_NONE;

	/** Shape of the brush that was applied. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	EDigBrushShape Shape = EDigBrushShape::Sphere;

	/** World-space center of the brush volume. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FVector WorldCenter = FVector::ZeroVector;

	/**
	 * Primary radius (world units).
	 * - Sphere: radius of the sphere.
	 * - Cylinder: XY radius of the cylinder.
	 * - Box: ignored (use HalfExtents).
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float Radius = 0.0f;

	/**
	 * Half-extents for box/cylinder brushes (X, Y, Z in world units).
	 * - Box: full axis-aligned half-extents.
	 * - Cylinder: Z component is the half-height; X/Y ignored (use Radius).
	 * - Sphere: ignored entirely (use Radius).
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FVector HalfExtents = FVector::ZeroVector;

	/** SDF falloff distance (world units). Controls edge smoothness. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	float Falloff = 50.0f;

	/** True if the brush was successfully applied to the modification layer. */
	bool IsValid() const { return BrushID != INDEX_NONE; }
};

// ===================================================================
// Block-level records (resource tracking)
// ===================================================================

/**
 * Describes a single block that was present in the dig volume.
 * Used for resource-yield calculation and block-type statistics.
 *
 * NOTE (v5.0): Individual blocks are no longer removed via RemoveBlockAt().
 * They are sampled BEFORE the SDF brush is applied so we know what material
 * types the brush carved through. The brush handles the actual modification.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FRemovedBlockEntry
{
	GENERATED_BODY()

	/** Grid-space position of the removed block. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntVector GridPosition = FIntVector::ZeroValue;

	/**
	 * Gameplay tag identifying what type of terrain block was here.
	 * e.g. WorldForge.Terrain.Dirt, WorldForge.Terrain.Stone
	 * Used for resource yield calculation.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FGameplayTag BlockType;
};

/**
 * Describes a resource node that was revealed by a dig operation.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FRevealedNodeEntry
{
	GENERATED_BODY()

	/** Unique runtime ID assigned by ResourceNodeSubsystem. */
	UPROPERTY(BlueprintReadOnly)
	int32 NodeID = INDEX_NONE;

	/** Grid position where the node lives. */
	UPROPERTY(BlueprintReadOnly)
	FIntVector GridPosition = FIntVector::ZeroValue;

	/** The resource tag for what was revealed (e.g. WorldForge.Resource.Iron). */
	UPROPERTY(BlueprintReadOnly)
	FGameplayTag ResourceTag;

	/** World-space position of the revealed node. */
	UPROPERTY(BlueprintReadOnly)
	FVector WorldPosition = FVector::ZeroVector;

	/** Resource category (Metal, Crystal, etc.). */
	UPROPERTY(BlueprintReadOnly)
	EResourceCategory Category = EResourceCategory::Metal;
};

// ===================================================================
// Dig result (event payload + undo record)
// ===================================================================

/**
 * Complete result of a single DigAt() call.
 * Dispatched via EventBus as OnTerrainDug payload.
 * Stored in the undo stack for terrain restoration.
 *
 * v5.0: Added BrushRecord for SDF brush undo (replaces per-block RestoreBlockAt).
 * RemovedBlocks retained for resource yield tracking (sampled pre-brush).
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FDigResult
{
	GENERATED_BODY()

	// ---------------------------------------------------------------
	// Dig metadata
	// ---------------------------------------------------------------

	/** Center grid position where the dig was initiated. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntVector DigOrigin = FIntVector::ZeroValue;

	/** Radius used for this dig (1 = single block). */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DigRadius = 1;

	/** Brush shape used for this dig. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	EDigBrushShape BrushShape = EDigBrushShape::Sphere;

	// ---------------------------------------------------------------
	// SDF brush record (primary undo mechanism)
	// ---------------------------------------------------------------

	/**
	 * The SDF brush that was applied to the terrain modification layer.
	 * Undo removes this brush by ID — no per-block restoration needed.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FDigBrushRecord BrushRecord;

	// ---------------------------------------------------------------
	// Block-level data (resource tracking)
	// ---------------------------------------------------------------

	/** The deepest terrain depth reached by this dig (grid units from surface). */
	UPROPERTY(BlueprintReadOnly)
	int32 DepthReached = 0;

	/**
	 * Every block type that was present in the brush volume before application.
	 * Used for resource yield calculation (what materials did we mine?).
	 * Empty if the brush hit only air/void.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FRemovedBlockEntry> RemovedBlocks;

	/** Resource nodes that became revealed as a result of this dig. May be empty. */
	UPROPERTY(BlueprintReadOnly)
	TArray<FRevealedNodeEntry> RevealedNodes;

	// ---------------------------------------------------------------
	// Queries
	// ---------------------------------------------------------------

	/** True if the brush was applied and at least one solid block was in the volume. */
	bool WasEffective() const { return BrushRecord.IsValid() && RemovedBlocks.Num() > 0; }

	/** True if this dig exposed at least one resource node. */
	bool RevealedAnyNodes() const { return RevealedNodes.Num() > 0; }
};

// ===================================================================
// Undo stack entry
// ===================================================================

/**
 * Undo entry for the build-mode undo stack.
 * v5.0: Undo removes the SDF brush by ID via VoxelWorldManager::RemoveBrushByID().
 */
USTRUCT()
struct WORLDFORGE_API FDigUndoEntry
{
	GENERATED_BODY()

	/** The full dig result — contains brush record and block data. */
	UPROPERTY(SaveGame)
	FDigResult DigResult;

	/**
	 * Timestamp (SimulationTimeManager tick) when this dig occurred.
	 * Used for ordering in the undo stack.
	 */
	UPROPERTY(SaveGame)
	int64 SimTick = 0;
};