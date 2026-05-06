#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "ResourceSpawnSubsystem.generated.h"

class UDataTable;

/**
 * Deterministic spawning of Shatter Stone ore node actors.
 *
 * Listens for chunk activation from VoxelWorldManager and spawns
 * BP_Mining_Node_Master instances at positions derived from the
 * tile's FResourceVeinConfig (seeded during Step08).
 *
 * All RNG is derived from SpawnSeeds — zero runtime randomness.
 *
 * ── EXPANSION GUIDE ──
 * To add a new resource category (e.g. "Gemstone"):
 *   1. Add entry to EResourceCategory before COUNT
 *   2. Add an FResourceNoiseProfile in FResourceNoiseRegistry
 *   3. Add a noise field in WorldForgeTerrainGenerator
 *   4. Add a UDataTable* slot below and register it via SetOreDataTable()
 *   5. Add mapping cases in PickOreRowName() and PickResourceTag()
 *   6. Add a TerrainLayer_OreXxx entry in the terrain generator header
 *
 * To add a new ore type within an existing category:
 *   1. Add a row to the matching Shatter Stone Data Table
 *   2. Add depth/threshold logic in PickOreRowName()
 *   3. Add a gameplay tag in DefaultGameplayTags.ini
 */
UCLASS()
class WORLDFORGE_API UResourceSpawnSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Core lifecycle (called by VoxelWorldManager) ───────────

    /** Spawn ore nodes for a newly activated chunk. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void OnChunkActivated(
        FIntPoint ChunkCell,
        const FWorldTileData& TileData,
        float ChunkWorldSize,
        float SurfaceHeightWorld);

    /** Despawn actors for a deactivated chunk.  Records are kept. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void OnChunkDeactivated(FIntPoint ChunkCell);

    // ─── Queries (called by DigGhostActor, UI, etc.) ────────────

    /** Find all non-depleted nodes within radius. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    TArray<FSpawnedResourceNode> GetNodesInRadius(
        FVector WorldPos, float Radius) const;

    /** Check if any node is within radius (cheaper than full query). */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Resources")
    bool HasNodeInRadius(FVector WorldPos, float Radius) const;

    /** Get a node by ID. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Resources")
    bool GetNodeByID(int32 NodeID, FSpawnedResourceNode& OutNode) const;

    // ─── Depletion / Exposure (called from dig/Shatter Stone) ─────

    /** Mark a node as depleted. Actor destruction is Shatter Stone's job. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void MarkNodeDepleted(int32 NodeID);

    /** Mark a node as exposed (revealed by a dig). Sets bExposed = true. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void MarkNodeExposed(int32 NodeID);

    // ─── Configuration (call once at game start from BP) ────────

    /** Register a Shatter Stone Data Table for a resource category. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void SetOreDataTable(EResourceCategory Category, UDataTable* Table);

    /** Set the Blueprint class to spawn (BP_Mining_Node_Master). */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resources")
    void SetNodeBlueprintClass(TSubclassOf<AActor> InClass);

    /** How far (voxels) a shallow node is pushed up to be half-exposed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Resources")
    float ExposedNodeZOffset = 50.0f;

    /** Depth threshold (voxels) below which a node is considered "exposed". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Resources")
    float ExposedDepthThreshold = 3.0f;

    // ─── Save / Load helpers ────────────────────────────────────

    /** Get all node records for serialization. */
    const TMap<int32, FSpawnedResourceNode>& GetAllNodeRecords() const { return AllNodes; }

    /** Restore node records from a save.  Actors are re-spawned on chunk activation. */
    void RestoreNodeRecords(const TMap<int32, FSpawnedResourceNode>& InRecords);

private:
    UPROPERTY()
    TMap<int32, FSpawnedResourceNode> AllNodes;

    TMap<FIntPoint, TArray<int32>> ChunkNodeMap;

    int32 NextNodeID = 1;

    UPROPERTY()
    TObjectPtr<UDataTable> CategoryDataTables[static_cast<int32>(EResourceCategory::COUNT)];

    UPROPERTY()
    TSubclassOf<AActor> NodeBlueprintClass;

    void ComputeSpawnPositions(
        const FResourceVeinConfig& VeinConfig,
        int32 VeinIndex,
        FIntPoint ChunkCell,
        float ChunkWorldSize,
        float SurfaceHeightWorld,
        TArray<FVector>& OutPositions,
        TArray<bool>& OutExposed);

    FName PickOreRowName(EResourceCategory Category, float Depth);
    FGameplayTag PickResourceTag(EResourceCategory Category, float Depth);
};