// DigSubsystem.h
// WorldForge — Phase 1: Digging Foundation
// UWorldSubsystem responsible for terrain excavation.
// Delegates voxel removal to VoxelWorldManager, queries ResourceNodeSubsystem
// for node reveals (Phase 2+), and dispatches results via EventBus.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Player/DigResult.h"
#include "DigSubsystem.generated.h"

class UVoxelWorldManager;
class UEventBus;

/**
 * Settings that control a single dig operation.
 * Passed into DigAt() — typically sourced from PlayerBuildContext or FDigSettings.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FDigParams
{
	GENERATED_BODY()

	/** Grid position to dig at (center of the dig volume). */
	UPROPERTY(BlueprintReadWrite)
	FIntVector TargetGridPos = FIntVector::ZeroValue;

	/**
	 * Radius of the dig in grid cells.
	 * 1 = single block, 2 = 3x3x1 slab, etc.
	 * Actual shape is a vertical column of blocks within this XY radius.
	 */
	UPROPERTY(BlueprintReadWrite)
	int32 Radius = 1;

	/**
	 * How many layers deep to dig in a single action.
	 * 1 = remove one layer, 2 = two layers, etc.
	 */
	UPROPERTY(BlueprintReadWrite)
	int32 DepthPerAction = 1;
};

/**
 * World subsystem that owns all terrain excavation logic.
 *
 * Responsibilities:
 * - Validate dig targets (can't dig air, can't dig below world floor)
 * - Remove voxel blocks via VoxelWorldManager
 * - Calculate depth reached
 * - Query ResourceNodeSubsystem for node reveals (Phase 2 — stubbed initially)
 * - Dispatch FDigResult via EventBus
 * - Provide undo support (restore removed blocks)
 *
 * Does NOT own input handling or ghost preview — that's BuildModeManager.
 */
UCLASS()
class WORLDFORGE_API UDigSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem interface
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// -------------------------------------------------------------------
	// Primary API
	// -------------------------------------------------------------------

	/**
	 * Execute a dig operation at the specified grid position.
	 *
	 * Removes blocks within the radius/depth, calculates results,
	 * dispatches OnTerrainDug via EventBus, and returns the result
	 * for the caller (BuildModeManager) to push onto the undo stack.
	 *
	 * @param Params  Dig parameters (position, radius, depth).
	 * @return        FDigResult describing what was removed and revealed.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	FDigResult DigAt(const FDigParams& Params);

	/**
	 * Undo a previous dig by restoring all removed blocks.
	 * Called by BuildModeManager when the player undoes a dig action.
	 *
	 * @param Result  The FDigResult from the original dig (stored in undo stack).
	 * @return        True if all blocks were successfully restored.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	bool UndoDig(const FDigResult& Result);

	// -------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------

	/**
	 * Check if a grid position can be dug (contains a solid block, within world bounds).
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	bool CanDigAt(const FIntVector& GridPos) const;

	/**
	 * Get the current terrain depth at an XY column (surface to first air gap).
	 * Returns the number of solid blocks from the surface downward.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetSurfaceDepthAt(const FIntPoint& GridXY) const;

	/**
	 * Get the depth of a specific grid position relative to the terrain surface
	 * at that XY column. Returns 0 if at surface, negative if above, positive if below.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetRelativeDepth(const FIntVector& GridPos) const;

private:
	// -------------------------------------------------------------------
	// Internal helpers
	// -------------------------------------------------------------------

	/**
	 * Collect all grid positions within the dig volume defined by Params.
	 * Filters to only solid blocks that are within world bounds.
	 */
	TArray<FIntVector> GatherDigTargets(const FDigParams& Params) const;

	/**
	 * Remove a single block and return its type.
	 * Returns an empty tag if the block was already air.
	 */
	FRemovedBlockEntry RemoveSingleBlock(const FIntVector& GridPos);

	/**
	 * Query ResourceNodeSubsystem for any nodes at the given positions
	 * that should now be revealed. (Phase 2 stub — returns empty array initially.)
	 */
	TArray<FRevealedNodeEntry> CheckForNodeReveals(const TArray<FIntVector>& DugPositions, int32 MaxDepth);

	/**
	 * Calculate the deepest depth reached by any block in the removed set,
	 * relative to the terrain surface at each block's XY column.
	 */
	int32 CalculateMaxDepthReached(const TArray<FRemovedBlockEntry>& Removed) const;

	/** Dispatch FDigResult through EventBus. */
	void BroadcastDigResult(const FDigResult& Result);

	// -------------------------------------------------------------------
	// Cached references
	// -------------------------------------------------------------------

	UPROPERTY()
	TObjectPtr<UVoxelWorldManager> CachedVoxelManager;

	UPROPERTY()
	TObjectPtr<UEventBus> CachedEventBus;

	/** Resolve and cache VoxelWorldManager. Returns nullptr if not available. */
	UVoxelWorldManager* GetVoxelManager() const;

	/** Resolve and cache EventBus. Returns nullptr if not available. */
	UEventBus* GetEventBus() const;
};
