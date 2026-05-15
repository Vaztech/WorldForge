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

	UPROPERTY(BlueprintReadWrite)
	int32 Radius = 1;

	UPROPERTY(BlueprintReadWrite)
	int32 DepthPerAction = 1;

	UPROPERTY(BlueprintReadWrite)
	EDigBrushShape BrushShape = EDigBrushShape::Box;

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
 *
 * v5.1 additions (Stability integration):
 * - DigVoxelAtGrid()         : direct grid-coord removal, used by UStabilitySubsystem
 *                              cascade collapses. No designation, no job, no EventBus.
 * - GetOreSpawnWorldPosition(): converts voxel grid → correct world-space Z for
 *                              ShatterStone BP_MetalOreNode actor placement.
 *                              ResourceSpawnSubsystem must use this instead of
 *                              raw tile-heightmap Z values to prevent ore actors
 *                              spawning at wrong depths on SDF terrain.
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

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	FDigResult DigAt(const FDigParams& Params);

	UFUNCTION(BlueprintCallable, Category = "WorldForge|Dig")
	bool UndoDig(const FDigResult& Result);

	// -------------------------------------------------------------------
	// Stability integration — called by UStabilitySubsystem only
	// -------------------------------------------------------------------

	/**
	 * Remove a single voxel at the given voxel-grid coordinate.
	 *
	 * This is the entry point for UStabilitySubsystem cascade collapses.
	 * It calls VoxelWorldManager::RemoveBlockAt() directly — no designation,
	 * no job queue, no EventBus dispatch.  The StabilitySubsystem dispatches
	 * its own TAG_Sim_Event_CaveCollapse event after the full cascade resolves.
	 *
	 * @param VoxelGrid  Voxel-index coordinates (same space as MiningSubsystem
	 *                   designations, e.g. X=-1495 Y=-187 Z=29).
	 */
	void DigVoxelAtGrid(FIntVector VoxelGrid);

	// -------------------------------------------------------------------
	// Ore spawn Z helper — called by ResourceSpawnSubsystem
	// -------------------------------------------------------------------

	/**
	 * Returns the correct world-space position (cm) for placing a ShatterStone
	 * BP_MetalOreNode actor at the given voxel grid coordinate.
	 *
	 * WHY THIS EXISTS:
	 * Step 08 seeds resources during world gen using tile-heightmap Z values.
	 * The actual SDF voxel terrain rendered by VoxelPlugin can differ from the
	 * 2D heightmap by 1-2 voxels (100-200 UU) at hills/slopes. If ore actors
	 * are placed using the raw tile Z, they appear floating or half-buried at
	 * wrong depths.
	 *
	 * USAGE in ResourceSpawnSubsystem — replace raw tile-height placement with:
	 *   UDigSubsystem* DS = GetWorld()->GetSubsystem<UDigSubsystem>();
	 *   FVector SpawnPos  = DS->GetOreSpawnWorldPosition(FIntVector(VoxelX, VoxelY, VoxelZ));
	 *   World->SpawnActor<AActor>(OreClass, SpawnPos, FRotator::ZeroRotator);
	 *
	 * This delegates to VoxelWorldManager::GridToWorld() which is the single
	 * authoritative voxel→world conversion used by the entire DigSubsystem.
	 */
	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	FVector GetOreSpawnWorldPosition(FIntVector VoxelGrid) const;

	// -------------------------------------------------------------------
	// Queries
	// -------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	bool CanDigAt(const FIntVector& GridPos) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetSurfaceDepthAt(const FIntPoint& GridXY) const;

	UFUNCTION(BlueprintPure, Category = "WorldForge|Dig")
	int32 GetRelativeDepth(const FIntVector& GridPos) const;

private:
	// -------------------------------------------------------------------
	// Brush computation
	// -------------------------------------------------------------------

	bool ComputeBrushParameters(const FDigParams& Params, FDigBrushRecord& OutRecord) const;

	// -------------------------------------------------------------------
	// Block sampling
	// -------------------------------------------------------------------

	TArray<FRemovedBlockEntry> SampleBlocksInVolume(const FDigParams& Params) const;

	TArray<FRevealedNodeEntry> CheckForNodeReveals(
		const TArray<FIntVector>& DugPositions,
		int32 MaxDepth);

	int32 CalculateMaxDepthReached(const TArray<FRemovedBlockEntry>& Sampled) const;

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

	int64 NextDigResultID = 1;

	UVoxelWorldManager*      GetVoxelManager() const;
	UEventBus*               GetEventBus() const;
	UResourceSpawnSubsystem* GetResourceSpawnSubsystem() const;
};
