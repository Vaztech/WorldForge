// DigSubsystem.h
// WorldForge — Digging Foundation
//
// UDigSubsystem owns terrain excavation gameplay logic.
// It samples dig volumes, calls the public VoxelWorldManager edit API,
// records removed block data for undo/resource yield, detects resource node
// reveals, and dispatches dig events through EventBus.
//
// VoxelWorldManager owns the actual runtime/SDF terrain modification layer.
// DigSubsystem does not call private brush helpers directly.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Player/DigResult.h"
#include "DigSubsystem.generated.h"

class UVoxelWorldManager;
class UEventBus;
class UResourceSpawnSubsystem;

/**
 * Settings that control a single dig operation.
 * Passed into DigAt() — typically sourced from PlayerBuildContext or FDigSettings.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FDigParams
{
	GENERATED_BODY()

	/** Grid position to dig at, centered on the intended dig volume. */
	UPROPERTY(BlueprintReadWrite)
	FIntVector TargetGridPos = FIntVector::ZeroValue;

	/**
	 * Radius of the dig in grid cells.
	 *
	 * 1 = single block footprint.
	 * 2 = 3x3 footprint.
	 * Higher values expand the X/Y area depending on BrushShape.
	 */
	UPROPERTY(BlueprintReadWrite)
	int32 Radius = 1;

	/**
	 * How many vertical layers to dig in a single action.
	 *
	 * Surface digs carve downward from the resolved surface/anchor.
	 * Underground digs carve from the exact target Z so horizontal tunneling
	 * does not snap back to the surface.
	 */
	UPROPERTY(BlueprintReadWrite)
	int32 DepthPerAction = 1;

	/**
	 * Dig brush shape used for sampling and debug visualization.
	 *
	 * Current public-API path samples/removes discrete grid blocks, while
	 * VoxelWorldManager owns the actual SDF/runtime modification behavior.
	 *
	 * - Sphere:   Smooth/small excavation style.
	 * - Cylinder: Vertical shaft style.
	 * - Box:      Clean axis-aligned room/tunnel style.
	 */
	UPROPERTY(BlueprintReadWrite)
	EDigBrushShape BrushShape = EDigBrushShape::Box;

	/**
	 * SDF/debug falloff in world units.
	 *
	 * Higher values = softer visual/debug transition.
	 * 0 or negative = use default behavior.
	 */
	UPROPERTY(BlueprintReadWrite)
	float FalloffOverride = -1.0f;
};

/**
 * World subsystem that owns terrain excavation logic.
 *
 * Current public-API path:
 * - Samples solid blocks in the requested dig volume.
 * - Removes blocks through VoxelWorldManager::RemoveBlockAt().
 * - Restores blocks through VoxelWorldManager::RestoreBlockAt().
 * - Tracks removed block positions/types for undo and resource yield.
 * - Detects nearby resource node reveals after digging.
 * - Dispatches FDigResult through EventBus.
 *
 * Important:
 * VoxelWorldManager owns the actual runtime/SDF terrain modification layer.
 * DigSubsystem does not call private brush helpers directly.
 *
 * Does NOT own input handling or ghost preview — that is BuildModeManager.
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
	 * Current flow:
	 * 1. Compute brush/debug parameters from FDigParams.
	 * 2. Sample solid blocks in the intended dig volume.
	 * 3. Remove sampled blocks through VoxelWorldManager::RemoveBlockAt().
	 * 4. Record actually removed blocks in FDigResult.
	 * 5. Check for newly exposed resource nodes.
	 * 6. Broadcast OnTerrainDug through EventBus.
	 *
	 * Surface behavior:
	 * - If the target is air/invalid, the dig resolves to a nearby surface block.
	 *
	 * Underground behavior:
	 * - If the target is already solid, the dig keeps the exact target Z.
	 * - This allows horizontal cave/tunnel digging along X/Y without creating
	 *   unwanted vertical columns from the surface.
	 *
	 * @param Params  Dig parameters: position, radius, depth, and brush shape.
	 * @return        FDigResult describing what was removed and revealed.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	FDigResult DigAt(const FDigParams& Params);

	/**
	 * Undo a previous dig by restoring the removed block entries recorded in
	 * the original FDigResult.
	 *
	 * @param Result  The FDigResult from the original dig, stored in the undo stack.
	 * @return        True if at least one block was restored.
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	bool UndoDig(const FDigResult& Result);

	// -------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------

	/**
	 * Check whether a grid position can be dug.
	 *
	 * Direct underground solid targets are allowed.
	 * If the target is not solid, this falls back to finding a nearby surface block.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	bool CanDigAt(const FIntVector& GridPos) const;

	/**
	 * Get the current terrain depth at an XY column.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetSurfaceDepthAt(const FIntPoint& GridXY) const;

	/**
	 * Get the depth of a specific grid position relative to the terrain surface
	 * at that XY column.
	 *
	 * 0 = surface.
	 * Positive values = underground.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetRelativeDepth(const FIntVector& GridPos) const;

private:
	// -------------------------------------------------------------------
	// Brush computation
	// -------------------------------------------------------------------

	/**
	 * Compute world-space brush/debug parameters from dig params.
	 *
	 * This mirrors the same surface-vs-underground behavior used by
	 * SampleBlocksInVolume():
	 *
	 * - Solid target: keep exact target Z for cave/tunnel digging.
	 * - Air/invalid target: snap to nearby surface for normal surface digging.
	 *
	 * @param Params      Input dig parameters.
	 * @param OutRecord   Populated with shape, center, radius, half-extents, and falloff.
	 *                    A pseudo BrushID is assigned after successful removal.
	 * @return            True if parameters are valid.
	 */
	bool ComputeBrushParameters(const FDigParams& Params, FDigBrushRecord& OutRecord) const;

	// -------------------------------------------------------------------
	// Block sampling
	// -------------------------------------------------------------------

	/**
	 * Sample all solid blocks inside the intended dig volume.
	 *
	 * Called before removal so block types can be tracked for undo,
	 * resource yield, and future mining rules.
	 *
	 * Key behavior:
	 * - Surface digs follow local surface height per X/Y column.
	 * - Underground digs keep the target Z plane so tunnels dig horizontally
	 *   instead of carving vertical shafts.
	 *
	 * @param Params  Dig parameters defining the volume.
	 * @return        Array of block entries with grid positions and terrain types.
	 */
	TArray<FRemovedBlockEntry> SampleBlocksInVolume(const FDigParams& Params) const;

	/**
	 * Check whether any hidden resource nodes are close enough to the newly
	 * dug blocks to become exposed.
	 */
	TArray<FRevealedNodeEntry> CheckForNodeReveals(
		const TArray<FIntVector>& DugPositions,
		int32 MaxDepth);

	/**
	 * Calculate the deepest depth reached by any removed block, relative to
	 * the terrain surface at each block's XY column.
	 */
	int32 CalculateMaxDepthReached(const TArray<FRemovedBlockEntry>& Sampled) const;

	/** Dispatch FDigResult through EventBus. */
	void BroadcastDigResult(const FDigResult& Result);

	// -------------------------------------------------------------------
	// Cached references
	// -------------------------------------------------------------------

	UPROPERTY()
	TObjectPtr<UVoxelWorldManager> CachedVoxelManager;

	UPROPERTY()
	TObjectPtr<UEventBus> CachedEventBus;

	UPROPERTY()
	TObjectPtr<UResourceSpawnSubsystem> CachedResourceSpawn;

	/**
	 * Public-API dig path does not receive a real private SDF BrushID from
	 * VoxelWorldManager. This counter gives each successful dig result a stable
	 * pseudo ID so WasEffective()/undo-stack logic can treat it as valid.
	 */
	int64 NextDigResultID = 1;

	/** Resolve and cache VoxelWorldManager. Returns nullptr if not available. */
	UVoxelWorldManager* GetVoxelManager() const;

	/** Resolve and cache EventBus. Returns nullptr if not available. */
	UEventBus* GetEventBus() const;

	/** Resolve and cache ResourceSpawnSubsystem. Returns nullptr if not available. */
	UResourceSpawnSubsystem* GetResourceSpawnSubsystem() const;
};