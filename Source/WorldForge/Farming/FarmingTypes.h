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

    /** Height in tiles (Y axis).
     *  For exact-cell zones, this is a cached bounding-box height used for UI/debug only.
     *  The authoritative tile list is ExplicitLocalCells / Tiles. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SizeY = 1;

    /** Absolute voxel/grid origin for this farm zone.
     *
     *  Exact-cell farm zones store TilePos as settlement-local grid offsets.
     *  FarmGridOrigin converts those local offsets back to absolute voxel/grid
     *  coordinates when spawning crop visuals or raycasting terrain.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FIntPoint FarmGridOrigin = FIntPoint::ZeroValue;

    /** Exact farm cells in settlement-local coordinates.
     *
     *  This supports long-term cell-accurate farming/zoning:
     *  - 1 click = 1 farm cell
     *  - 3 separate clicks = 3 farm cells
     *  - 2x5 drag = exactly 10 farm cells
     *
     *  Origin/SizeX/SizeY remain as a cached bounding box for older UI/debug code,
     *  but they are not the source of truth for exact-cell zones. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TSet<FIntPoint> ExplicitLocalCells;

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

    /** True if the exact local tile is part of this farm zone. */
    bool ContainsLocalTile(FIntPoint LocalTile) const
    {
        if (ExplicitLocalCells.Num() > 0)
        {
            return ExplicitLocalCells.Contains(LocalTile);
        }

        const FIntPoint LocalOffset = LocalTile - Origin;
        return
            LocalOffset.X >= 0 && LocalOffset.X < SizeX &&
            LocalOffset.Y >= 0 && LocalOffset.Y < SizeY;
    }

    /** Get the tile record by settlement-local tile position. */
    FFarmTileRecord* GetTileByPosition(FIntPoint LocalTile)
    {
        for (FFarmTileRecord& Tile : Tiles)
        {
            if (Tile.TilePos == LocalTile)
            {
                return &Tile;
            }
        }

        return nullptr;
    }

    const FFarmTileRecord* GetTileByPosition(FIntPoint LocalTile) const
    {
        for (const FFarmTileRecord& Tile : Tiles)
        {
            if (Tile.TilePos == LocalTile)
            {
                return &Tile;
            }
        }

        return nullptr;
    }

    /** Legacy helper: Get tile at rectangle offset (X,Y). */
    FFarmTileRecord* GetTile(int32 X, int32 Y)
    {
        return GetTileByPosition(Origin + FIntPoint(X, Y));
    }

    const FFarmTileRecord* GetTile(int32 X, int32 Y) const
    {
        return GetTileByPosition(Origin + FIntPoint(X, Y));
    }

    static void ResetTileDefaults(FFarmTileRecord& Tile)
    {
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

    /** Initialize the Tiles array for the current SizeX * SizeY rectangle.
     *  This is kept for legacy rectangle callers. It also fills ExplicitLocalCells
     *  so exact-cell queries work for both old and new zone creation paths. */
    void InitTiles()
    {
        ExplicitLocalCells.Reset();

        for (int32 Y = 0; Y < SizeY; ++Y)
        {
            for (int32 X = 0; X < SizeX; ++X)
            {
                ExplicitLocalCells.Add(Origin + FIntPoint(X, Y));
            }
        }

        InitTilesFromCells(ExplicitLocalCells);
    }

    /** Initialize the Tiles array from exact settlement-local cells. */
    void InitTilesFromCells(const TSet<FIntPoint>& InLocalCells)
    {
        ExplicitLocalCells = InLocalCells;

        TArray<FIntPoint> SortedCells;
        SortedCells.Reserve(ExplicitLocalCells.Num());

        for (const FIntPoint& Cell : ExplicitLocalCells)
        {
            SortedCells.Add(Cell);
        }
        SortedCells.Sort([](const FIntPoint& A, const FIntPoint& B)
        {
            return A.Y == B.Y ? A.X < B.X : A.Y < B.Y;
        });

        if (SortedCells.Num() == 0)
        {
            Origin = FIntPoint::ZeroValue;
            SizeX = 0;
            SizeY = 0;
            TotalTiles = 0;
            Tiles.Reset();
            return;
        }

        int32 MinX = TNumericLimits<int32>::Max();
        int32 MinY = TNumericLimits<int32>::Max();
        int32 MaxX = TNumericLimits<int32>::Min();
        int32 MaxY = TNumericLimits<int32>::Min();

        for (const FIntPoint& Cell : SortedCells)
        {
            MinX = FMath::Min(MinX, Cell.X);
            MinY = FMath::Min(MinY, Cell.Y);
            MaxX = FMath::Max(MaxX, Cell.X);
            MaxY = FMath::Max(MaxY, Cell.Y);
        }

        Origin = FIntPoint(MinX, MinY);
        SizeX = MaxX - MinX + 1;
        SizeY = MaxY - MinY + 1;
        TotalTiles = SortedCells.Num();

        Tiles.SetNum(TotalTiles);

        for (int32 Index = 0; Index < SortedCells.Num(); ++Index)
        {
            FFarmTileRecord& Tile = Tiles[Index];
            Tile.TilePos = SortedCells[Index];
            ResetTileDefaults(Tile);
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
