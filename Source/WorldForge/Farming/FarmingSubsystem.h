// Source/WorldForge/Farming/FarmingSubsystem.h
//
// UWorldSubsystem managing farm zones, crop records, and sim tick processing.
// Owns FWorldForgeFarmZone records. Delegates all BP interaction to
// WildHarvestAdapter (never touches BP actors directly).
//
// Called from ProcessorManager::RunSimulationStep() via TickFarmZones().
// Posts farming jobs to JobQueueManager via EventBus dispatch.
//
// Important initialization note:
// Unreal does not guarantee sibling WorldSubsystem initialization order.
// FarmingSubsystem may initialize before WildHarvestAdapter, so this class
// uses safe late-binding helpers instead of assuming cached pointers are valid
// during Initialize().
// ════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "FarmingTypes.h"
#include "FarmingSubsystem.generated.h"

class UWildHarvestAdapter;
class UEventBus;

/**
 * FarmingSubsystem — manages all farm zones in the colony.
 *
 * Responsibilities:
 *   1. Create / modify / disband farm zones (from Build Mode)
 *   2. Tick all active zones each sim step:
 *      - Poll crop states via WildHarvestAdapter
 *      - Post Plant / Water / Harvest / Clear jobs
 *      - Update aggregate zone stats
 *   3. Handle job completion callbacks from ProductionProcessor
 *   4. Broadcast farming events via EventBus
 *
 * Key principle:
 *   FarmingSubsystem decides WHEN and WHY.
 *   WildHarvestAdapter decides HOW (BP interaction).
 */
UCLASS()
class WORLDFORGE_API UFarmingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ─── Zone Management ────────────────────────────────────────

    /**
     * Create a new farm zone. Called from BuildModeManager when
     * the player confirms a farm zone designation.
     *
     * @param SettlementID  Owning settlement
     * @param Origin        Top-left tile (settlement-local grid)
     * @param SizeX         Width in tiles
     * @param SizeY         Height in tiles
     * @param Category      Crop category for this zone
     * @param CropRow       Specific crop row (empty = default for category)
     * @param bAutoReplant  Whether to auto-replant after harvest
     * @return              The new zone's ID, or INDEX_NONE on failure
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    int32 CreateFarmZone(
        int32 SettlementID,
        FIntPoint Origin,
        int32 SizeX,
        int32 SizeY,
        ECropCategory Category,
        FName CropRow,
        bool bAutoReplant = true);

    /**
     * Create a new farm zone from exact settlement-local cells.
     *
     * This is the long-term player-selection path:
     * - 1 clicked cell creates 1 farm tile
     * - 3 separate clicked cells create 3 farm tiles
     * - 2x5 drag creates exactly 10 farm tiles
     *
     * FarmGridOrigin is the absolute voxel/grid origin used to convert local farm
     * cells back into world grid cells for crop visual placement.
     * Origin/SizeX/SizeY are cached as the bounding box for debug/UI only.
     */
    int32 CreateFarmZoneFromCells(
        int32 SettlementID,
        FIntPoint FarmGridOrigin,
        const TSet<FIntPoint>& LocalCells,
        ECropCategory Category,
        FName CropRow,
        bool bAutoReplant = true);

    /**
     * Disband (soft-delete) a farm zone.
     * Sets state to Disbanded and despawns crop actors through the adapter.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void DisbandFarmZone(int32 ZoneID);

    /** Pause/unpause a farm zone. Paused zones don't post new jobs. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void SetFarmZonePaused(int32 ZoneID, bool bPaused);

    /** Change the crop for an existing zone. Clears all planted tiles. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void ChangeFarmZoneCrop(int32 ZoneID, ECropCategory NewCategory, FName NewCropRow);

    /** Enable/disable automatic replanting for an existing farm zone. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void SetAutoReplant(int32 ZoneID, bool bEnable);

    /** Re-queue a single harvested tile that is waiting for manual replant. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    bool ManualReplantTile(int32 ZoneID, FIntPoint TilePos);

    /** Re-queue every harvested tile in a zone that is waiting for manual replant. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    int32 ManualReplantZone(int32 ZoneID);

    // ─── Sim Tick ───────────────────────────────────────────────

    /**
     * Main per-step update. Called from ProcessorManager::RunSimulationStep().
     * Iterates all active zones, polls crop states, and posts jobs.
     */
    void TickFarmZones(int32 StepNumber);

    // ─── Job Completion Callbacks ───────────────────────────────
    // Called by ProductionProcessor when a colonist finishes a farming job.

    /** A colonist completed a Prep Soil job at the given tile. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void OnPrepSoilCompleted(int32 ZoneID, FIntPoint TilePos);

    /** A colonist completed a Plant job at the given tile. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void OnPlantCompleted(int32 ZoneID, FIntPoint TilePos);

    /** A colonist completed a Water job at the given tile. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void OnWaterCompleted(int32 ZoneID, FIntPoint TilePos);

    /** A colonist completed a Harvest job at the given tile. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    void OnHarvestCompleted(int32 ZoneID, FIntPoint TilePos, int32 YieldAmount);

    // ─── Queries ────────────────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    const TArray<FWorldForgeFarmZone>& GetAllZones() const { return FarmZones; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    bool GetZoneByID(int32 ZoneID, FWorldForgeFarmZone& OutZone) const;

    /** Get zones belonging to a specific settlement. */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Farming")
    TArray<FWorldForgeFarmZone> GetZonesForSettlement(int32 SettlementID) const;

    /** Check if a tile position is inside any active farm zone. */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Farming")
    bool IsTileInFarmZone(FIntPoint TilePos, int32 SettlementID) const;

    // ─── Save / Load ────────────────────────────────────────────

    const TArray<FWorldForgeFarmZone>& GetAllZoneRecords() const { return FarmZones; }
    void RestoreZoneRecords(const TArray<FWorldForgeFarmZone>& InZones);

private:
    UPROPERTY()
    TArray<FWorldForgeFarmZone> FarmZones;

    int32 NextZoneID = 1;

    /**
     * Cached subsystem pointer.
     *
     * This may be null during Initialize() because WildHarvestAdapter can
     * initialize after FarmingSubsystem. Use GetWildHarvestAdapterSafe()
     * whenever farming actually needs adapter access.
     */
    UPROPERTY()
    TObjectPtr<UWildHarvestAdapter> Adapter;

    /**
     * Cached EventBus pointer.
     *
     * EventBus usually initializes before FarmingSubsystem in current logs,
     * but we still late-bind safely to avoid future subsystem-order bugs.
     */
    UPROPERTY()
    TObjectPtr<UEventBus> EventBus;

    /** Water threshold — tiles below this need Water jobs. */
    float WaterThreshold = 0.3f;

    /**
     * Safely resolves WildHarvestAdapter.
     *
     * This fixes the current PIE pattern:
     *   [FarmingSubsystem] Initialized. Adapter=null
     *   [WildHarvestAdapter] Initialized.
     *
     * FarmingSubsystem should not stay broken just because the adapter
     * initialized later.
     */
    UWildHarvestAdapter* GetWildHarvestAdapterSafe();

    /**
     * Safely resolves EventBus.
     *
     * Keeps job posting robust if subsystem initialization order changes.
     */
    UEventBus* GetEventBusSafe();

    /** Find a mutable zone by ID. Returns nullptr if not found. */
    FWorldForgeFarmZone* FindZoneMutable(int32 ZoneID);

    /** Resolve the seed item tag for a crop category. Returns invalid if seed tags are not configured. */
    FGameplayTag GetSeedTagForCategory(ECropCategory Category);

    /** True if this zone can currently post plant jobs based on available seed stock. */
    bool HasSeedsForZone(const FWorldForgeFarmZone& Zone) const;

    /** Find a const zone by ID. Returns nullptr if not found. */
    const FWorldForgeFarmZone* FindZone(int32 ZoneID) const;
};