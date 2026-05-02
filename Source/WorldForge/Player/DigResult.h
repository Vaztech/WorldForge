// DigResult.h
// WorldForge — Phase 1: Digging Foundation
// Pure data struct describing the outcome of a single dig operation.
// No dependencies beyond UE core types.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "DigResult.generated.h"

/**
 * Describes a single block that was removed during a dig operation.
 * Stored in the undo stack so terrain can be restored.
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
	 * Used by undo to restore the correct block type.
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
};

/**
 * Complete result of a single DigAt() call.
 * Dispatched via EventBus as OnTerrainDug payload.
 * Also stored in the undo stack for terrain restoration.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FDigResult
{
	GENERATED_BODY()

	/** Center grid position where the dig was initiated. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	FIntVector DigOrigin = FIntVector::ZeroValue;

	/** Radius used for this dig (1 = single block). */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	int32 DigRadius = 1;

	/** The deepest terrain depth reached by this dig operation (in grid units from surface). */
	UPROPERTY(BlueprintReadOnly)
	int32 DepthReached = 0;

	/** Every block that was actually removed. Empty if dig hit air/void. */
	UPROPERTY(SaveGame, BlueprintReadOnly)
	TArray<FRemovedBlockEntry> RemovedBlocks;

	/** Resource nodes that became revealed as a result of this dig. May be empty. */
	UPROPERTY(BlueprintReadOnly)
	TArray<FRevealedNodeEntry> RevealedNodes;

	/** True if at least one block was removed. */
	bool WasEffective() const { return RemovedBlocks.Num() > 0; }

	/** True if this dig exposed at least one resource node. */
	bool RevealedAnyNodes() const { return RevealedNodes.Num() > 0; }
};

/**
 * Undo entry for the build-mode undo stack.
 * Stores everything needed to restore terrain after a dig.
 */
USTRUCT()
struct WORLDFORGE_API FDigUndoEntry
{
	GENERATED_BODY()

	/** The full dig result — contains all removed block positions and types. */
	UPROPERTY(SaveGame)
	FDigResult DigResult;

	/**
	 * Timestamp (SimulationTimeManager tick) when this dig occurred.
	 * Used for ordering in the undo stack.
	 */
	UPROPERTY(SaveGame)
	int64 SimTick = 0;
};
