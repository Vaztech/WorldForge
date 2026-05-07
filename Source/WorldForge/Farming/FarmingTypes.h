// Source/WorldForge/Farming/FarmingTypes.h
//
// Enums, structs, and constants for the Wild Harvest farming integration.
// No dependencies beyond WorldForgeDataTypes.h and GameplayTags.
// ════════════════════════════════════════════════════════════════════

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "FarmingTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════════

/**
 * Maps 1:1 to the six NV3D Wild Harvest sub-packs.
 * Used by WildHarvestAdapter to resolve crop → Data Table.
 */
UENUM(BlueprintType)
enum class ECropCategory : uint8
{
    Berries        UMETA(DisplayName = "Berries"),
    Grains         UMETA(DisplayName = "Grains"),
    Herbs          UMETA(DisplayName = "Herbs"),
    Mushrooms      UMETA(DisplayName = "Mushrooms"),
    RootVegetables UMETA(DisplayName = "Root Vegetables"),
    SaladGarden    UMETA(DisplayName = "Salad Garden"),
    COUNT          UMETA(Hidden)
};

/**
 * Lifecycle state of a farm zone.  Drives job posting logic
 * in FarmingSubsystem::TickFarmZones().
 */
UENUM(BlueprintType)
enum class EFarmZoneState : uint8
{
    Active     UMETA(DisplayName = "Active"),      // Normal operation
    Paused     UMETA(DisplayName = "Paused"),      // Player-suspended, no jobs posted
    Disbanded  UMETA(DisplayName = "Disbanded")    // Marked for cleanup
};

/**
 * Growth stage as reported by Wild Harvest BP actors.
 * WildHarvestAdapter reads the BP's internal state and maps it
 * to one of these C++ values for sim-layer consumption.
 *
 * NOTE: The actual enum values/names inside the Wild Harvest BP
 * may differ.  WildHarvestAdapter::GetCropState() handles the
 * mapping so that all C++ code only sees this enum.
 */
UENUM(BlueprintType)
enum class ECropGrowthStage : uint8
{
    None       UMETA(DisplayName = "None"),        // No crop actor present
    BareSoil   UMETA(DisplayName = "Bare Soil"),
    Growing    UMETA(DisplayName = "Growing"),
    Ready      UMETA(DisplayName = "Ready"),
    Harvested  UMETA(DisplayName = "Harvested"),
    Dead       UMETA(DisplayName = "Dead")
};

// ═══════════════════════════════════════════════════════════════════
// STRUCTS
// ═══════════════════════════════════════════════════════════════════

/**
 * Per-tile crop record within a farm zone.
 * Tracks the link between a grid tile and its Wild Harvest BP actor.
 */
USTRUCT(BlueprintType)
struct FFarmTileRecord
{
    GENERATED_BODY()

    /** Tile position in settlement-local grid coordinates. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FIntPoint TilePos = FIntPoint::ZeroValue;

    /** Current growth stage (polled from BP via adapter). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    ECropGrowthStage GrowthStage = ECropGrowthStage::None;

    /** Water level [0,1]. Below WaterThreshold → adapter waters. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float WaterLevel = 0.0f;

    /** True once a colonist has prepared/tilled this soil tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bSoilPrepared = false;

    /** True while a Prep Soil job is already queued/assigned/in progress. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bPrepSoilJobPending = false;

    /** Last sim step a Prep Soil job was posted for this tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LastPrepSoilJobStep = INDEX_NONE;

    /** Sim step when this tile became prepared soil. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SoilPreparedStep = INDEX_NONE;

    /** True once a colonist has planted a crop BP on this tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bPlanted = false;

    /** True while a Plant job is already queued/assigned/in progress. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bPlantJobPending = false;

    /** True while a Water job is already queued/assigned/in progress. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bWaterJobPending = false;

    /** True while a Harvest job is already queued/assigned/in progress. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bHarvestJobPending = false;

    /** True when this harvested tile is waiting for the player to manually replant it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bAwaitingManualReplant = false;

    /** Last sim step a Plant job was posted for this tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LastPlantJobStep = INDEX_NONE;

    /** Last sim step a Water job was posted for this tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LastWaterJobStep = INDEX_NONE;

    /** Last sim step a Harvest job was posted for this tile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LastHarvestJobStep = INDEX_NONE;


    /** Sim-driven growth stage index. Keeps WorldForge deterministic even if BP timers do not advance. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SimGrowthStageIndex = INDEX_NONE;

    /** Last sim step C++ advanced this crop's growth stage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LastGrowthAdvanceStep = INDEX_NONE;

    /** Sim step when this tile was planted. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 PlantedStep = INDEX_NONE;

    /** Sim step when this tile was harvested. Used for delayed auto-replant. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 HarvestedStep = INDEX_NONE;

    /**
     * Weak reference to the spawned Wild Harvest crop BP actor.
     * Not saved — adapter re-spawns on zone reload.
     */
    UPROPERTY(Transient, BlueprintReadOnly)
    TWeakObjectPtr<AActor> CropActor;
};

/**
 * A rectangular farming zone designated by the player.
 * Managed by FarmingSubsystem; each zone covers a contiguous
 * tile region and grows a single crop category.
 */
USTRUCT(BlueprintType)
struct FWorldForgeFarmZone
{
    GENERATED_BODY()

    /** Unique zone ID (monotonically increasing, assigned by FarmingSubsystem). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ZoneID = INDEX_NONE;

    /** Owning settlement. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SettlementID = -1;

    /** Which crop category this zone grows. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    ECropCategory CropCategory = ECropCategory::Grains;

    /**
     * Specific crop row name within the Data Table.
     * e.g. "Wheat", "Potato".  Empty = zone uses default for category.
     * Resolved by WildHarvestAdapter from the per-pack DT.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName CropRowName;

    /** Zone lifecycle state. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    EFarmZoneState ZoneState = EFarmZoneState::Active;

    /** Top-left corner of the zone rectangle (settlement-local grid). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FIntPoint Origin = FIntPoint::ZeroValue;

    /** Width in tiles (X axis). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SizeX = 1;

    /** Height in tiles (Y axis). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SizeY = 1;

    /** If true, harvested tiles automatically get replant jobs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bAutoReplant = true;

    /** Seed item required when planting this zone's selected crop category. Invalid tag preserves legacy no-seed behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag SeedItemTag;

    /** Per-tile records.  Indexed linearly: index = Y * SizeX + X. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FFarmTileRecord> Tiles;

    // ── Aggregate stats (updated by FarmingSubsystem on tick) ──

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 TotalTiles = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 PlantedCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ReadyCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 DeadCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 TotalYieldThisSeason = 0;

    // ── Helpers ──

    /** Get the tile record at local offset (X, Y) within the zone. */
    FFarmTileRecord* GetTile(int32 X, int32 Y)
    {
        const int32 Idx = Y * SizeX + X;
        return Tiles.IsValidIndex(Idx) ? &Tiles[Idx] : nullptr;
    }

    const FFarmTileRecord* GetTile(int32 X, int32 Y) const
    {
        const int32 Idx = Y * SizeX + X;
        return Tiles.IsValidIndex(Idx) ? &Tiles[Idx] : nullptr;
    }

    /** Initialize the Tiles array for the current SizeX * SizeY. */
    void InitTiles()
    {
        TotalTiles = SizeX * SizeY;
        Tiles.SetNum(TotalTiles);

        for (int32 Y = 0; Y < SizeY; ++Y)
        {
            for (int32 X = 0; X < SizeX; ++X)
            {
                FFarmTileRecord& Tile = Tiles[Y * SizeX + X];
                Tile.TilePos = Origin + FIntPoint(X, Y);
                Tile.GrowthStage = ECropGrowthStage::None;
                Tile.WaterLevel = 0.0f;
                Tile.bSoilPrepared = false;
                Tile.bPrepSoilJobPending = false;
                Tile.LastPrepSoilJobStep = INDEX_NONE;
                Tile.SoilPreparedStep = INDEX_NONE;
                Tile.bPlanted = false;
                Tile.bPlantJobPending = false;
                Tile.bWaterJobPending = false;
                Tile.bHarvestJobPending = false;
                Tile.bAwaitingManualReplant = false;
                Tile.LastPlantJobStep = INDEX_NONE;
                Tile.LastWaterJobStep = INDEX_NONE;
                Tile.LastHarvestJobStep = INDEX_NONE;
                Tile.SimGrowthStageIndex = INDEX_NONE;
                Tile.LastGrowthAdvanceStep = INDEX_NONE;
                Tile.PlantedStep = INDEX_NONE;
                Tile.HarvestedStep = INDEX_NONE;
                Tile.CropActor = nullptr;
            }
        }
    }
};

/**
 * Configuration snapshot for the Build Mode farm tool.
 * Stored in PlayerBuildContext alongside existing build configs.
 */
USTRUCT(BlueprintType)
struct FBuildModeFarmConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    ECropCategory SelectedCategory = ECropCategory::Grains;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FName SelectedCropRow;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bAutoReplant = true;
};
