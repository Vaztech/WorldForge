// Source/WorldForge/Farming/FarmingSubsystem.cpp
//
// Farming subsystem implementation.
//
// Important:
// This subsystem may initialize before WildHarvestAdapter. Do not rely on the
// Adapter pointer being valid from Initialize(). Always use
// GetWildHarvestAdapterSafe() when adapter access is required.
// ════════════════════════════════════════════════════════════════════

#include "Farming/FarmingSubsystem.h"

#include "Farming/WildHarvestAdapter.h"
#include "Colony/EventBus.h"
#include "Data/WorldForgeTags.h"
#include "Data/WorldForgeDataTypes.h"
#include "Colony/ProcessorManager.h"
#include "Colony/Processors/JobProcessor.h"
#include "Colony/SettlementManager.h"
#include "Voxel/VoxelWorldManager.h"
#include "Utility/WorldForgeTerrainSnapUtility.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"

namespace
{
    // WorldForge sim-step farming driver.
    // Wild Harvest's internal timer is real-time and can stall/reset under our
    // step simulation, so C++ advances crop stages.
    //
    // These are intentionally based on simulation steps, not real seconds.
    // That makes them safe to later connect to day/night, seasons, weather,
    // sleep/rest speedups, and time-skip systems.
    constexpr int32 WF_FarmGrowthAdvanceSteps = 50;
    constexpr int32 WF_FarmReadyGrowthStageIndex = 4;
    constexpr int32 WF_FarmHarvestedVisualHoldSteps = 25;
    constexpr int32 WF_FarmSummaryLogIntervalSteps = 25;

    constexpr int32 WF_FarmPrepSoilJobCooldownSteps = 10;
    constexpr int32 WF_FarmPlantJobCooldownSteps = 10;
    constexpr int32 WF_FarmWaterJobCooldownSteps = 10;
    constexpr int32 WF_FarmHarvestJobCooldownSteps = 10;

    constexpr float WF_FarmWaterDrainPerStep = 0.025f;
    constexpr float WF_FarmReadyWaterFloor = 0.90f;

    static bool WF_IsValidCropActor(AActor* CropActor)
    {
        return IsValid(CropActor) &&
            !CropActor->IsActorBeingDestroyed() &&
            !CropActor->IsPendingKillPending();
    }

    static void WF_ResetFarmTileRuntimeState(FFarmTileRecord& Tile)
    {
        Tile.CropActor = nullptr;
        Tile.bPlanted = false;
        Tile.bSoilPrepared = false;
        Tile.bPrepSoilJobPending = false;
        Tile.bPlantJobPending = false;
        Tile.bWaterJobPending = false;
        Tile.bHarvestJobPending = false;
        Tile.bAwaitingManualReplant = false;
        Tile.LastPrepSoilJobStep = INDEX_NONE;
        Tile.LastPlantJobStep = INDEX_NONE;
        Tile.LastWaterJobStep = INDEX_NONE;
        Tile.LastHarvestJobStep = INDEX_NONE;
        Tile.SoilPreparedStep = INDEX_NONE;
        Tile.SimGrowthStageIndex = INDEX_NONE;
        Tile.LastGrowthAdvanceStep = INDEX_NONE;
        Tile.PlantedStep = INDEX_NONE;
        Tile.HarvestedStep = INDEX_NONE;
        Tile.GrowthStage = ECropGrowthStage::None;
        Tile.WaterLevel = 0.0f;
    }

    static bool WF_IsHarvestLocked(const FFarmTileRecord& Tile)
    {
        return Tile.GrowthStage == ECropGrowthStage::Ready ||
            Tile.GrowthStage == ECropGrowthStage::Harvested ||
            Tile.bHarvestJobPending ||
            Tile.SimGrowthStageIndex >= WF_FarmReadyGrowthStageIndex;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void UFarmingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UWorld* World = GetWorld();
    if (World)
    {
        Adapter = World->GetSubsystem<UWildHarvestAdapter>();
        EventBus = World->GetSubsystem<UEventBus>();
    }

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Initialized. InitialAdapter=%s EventBus=%s. Adapter may late-bind after WildHarvestAdapter initializes."),
        IsValid(Adapter) ? TEXT("OK") : TEXT("null"),
        IsValid(EventBus) ? TEXT("OK") : TEXT("null"));
}

void UFarmingSubsystem::Deinitialize()
{
    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();

    if (IsValid(SafeAdapter))
    {
        for (FWorldForgeFarmZone& Zone : FarmZones)
        {
            for (FFarmTileRecord& Tile : Zone.Tiles)
            {
                AActor* CropActor = Tile.CropActor.Get();

                if (WF_IsValidCropActor(CropActor))
                {
                    SafeAdapter->DespawnCropPatch(CropActor);
                }

                Tile.CropActor = nullptr;
            }
        }
    }

    FarmZones.Reset();

    Adapter = nullptr;
    EventBus = nullptr;

    UE_LOG(LogTemp, Log, TEXT("[FarmingSubsystem] Deinitialized."));

    Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════════
// Zone Management
// ═══════════════════════════════════════════════════════════════════

int32 UFarmingSubsystem::CreateFarmZone(
    int32 SettlementID,
    FIntPoint Origin,
    int32 SizeX,
    int32 SizeY,
    ECropCategory Category,
    FName CropRow,
    bool bAutoReplant)
{
    if (SizeX <= 0 || SizeY <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] CreateFarmZone failed: Invalid size %dx%d"),
            SizeX, SizeY);

        return INDEX_NONE;
    }

    const int32 NewMinX = Origin.X;
    const int32 NewMinY = Origin.Y;
    const int32 NewMaxX = Origin.X + SizeX - 1;
    const int32 NewMaxY = Origin.Y + SizeY - 1;

    for (const FWorldForgeFarmZone& ExistingZone : FarmZones)
    {
        if (ExistingZone.SettlementID != SettlementID ||
            ExistingZone.ZoneState == EFarmZoneState::Disbanded)
        {
            continue;
        }

        const int32 ExistingMinX = ExistingZone.Origin.X;
        const int32 ExistingMinY = ExistingZone.Origin.Y;
        const int32 ExistingMaxX = ExistingZone.Origin.X + ExistingZone.SizeX - 1;
        const int32 ExistingMaxY = ExistingZone.Origin.Y + ExistingZone.SizeY - 1;

        const bool bOverlaps =
            NewMinX <= ExistingMaxX &&
            NewMaxX >= ExistingMinX &&
            NewMinY <= ExistingMaxY &&
            NewMaxY >= ExistingMinY;

        if (bOverlaps)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[FarmingSubsystem] CreateFarmZone blocked: new zone Origin=(%d,%d) Size=%dx%d overlaps existing Zone=%d Origin=(%d,%d) Size=%dx%d Settlement=%d"),
                Origin.X,
                Origin.Y,
                SizeX,
                SizeY,
                ExistingZone.ZoneID,
                ExistingZone.Origin.X,
                ExistingZone.Origin.Y,
                ExistingZone.SizeX,
                ExistingZone.SizeY,
                SettlementID);

            return INDEX_NONE;
        }
    }

    FWorldForgeFarmZone Zone;
    Zone.ZoneID = NextZoneID++;
    Zone.SettlementID = SettlementID;
    Zone.CropCategory = Category;
    Zone.CropRowName = CropRow;
    Zone.ZoneState = EFarmZoneState::Active;
    Zone.Origin = Origin;
    Zone.SizeX = SizeX;
    Zone.SizeY = SizeY;
    Zone.bAutoReplant = bAutoReplant;

    Zone.InitTiles();

    // v2: Resolve and cache the seed tag for this crop category.
    Zone.SeedItemTag = GetSeedTagForCategory(Category);

    const int32 NewID = Zone.ZoneID;
    FarmZones.Add(MoveTemp(Zone));

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Created farm zone %d: Settlement=%d Origin=(%d,%d) Size=%dx%d Category=%d CropRow=%s AutoReplant=%s"),
        NewID,
        SettlementID,
        Origin.X,
        Origin.Y,
        SizeX,
        SizeY,
        static_cast<int32>(Category),
        *CropRow.ToString(),
        bAutoReplant ? TEXT("true") : TEXT("false"));

    return NewID;
}

int32 UFarmingSubsystem::CreateFarmZoneFromCells(
    int32 SettlementID,
    FIntPoint FarmGridOrigin,
    const TSet<FIntPoint>& LocalCells,
    ECropCategory Category,
    FName CropRow,
    bool bAutoReplant)
{
    if (LocalCells.Num() <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] CreateFarmZoneFromCells failed: no local cells."));

        return INDEX_NONE;
    }

    // Reject exact-cell zones that overlap existing active zones. This checks
    // exact membership rather than bounding boxes, so separate islands are safe.
    for (const FWorldForgeFarmZone& ExistingZone : FarmZones)
    {
        if (ExistingZone.SettlementID != SettlementID ||
            ExistingZone.ZoneState == EFarmZoneState::Disbanded)
        {
            continue;
        }

        for (const FIntPoint& LocalCell : LocalCells)
        {
            if (ExistingZone.ContainsLocalTile(LocalCell))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[FarmingSubsystem] CreateFarmZoneFromCells blocked: LocalCell=(%d,%d) overlaps existing Zone=%d Settlement=%d"),
                    LocalCell.X,
                    LocalCell.Y,
                    ExistingZone.ZoneID,
                    SettlementID);

                return INDEX_NONE;
            }
        }
    }

    FWorldForgeFarmZone Zone;
    Zone.ZoneID = NextZoneID++;
    Zone.SettlementID = SettlementID;
    Zone.CropCategory = Category;
    Zone.CropRowName = CropRow;
    Zone.ZoneState = EFarmZoneState::Active;
    Zone.FarmGridOrigin = FarmGridOrigin;
    Zone.bAutoReplant = bAutoReplant;
    Zone.SeedItemTag = GetSeedTagForCategory(Category);
    Zone.InitTilesFromCells(LocalCells);

    const int32 NewID = Zone.ZoneID;
    const int32 ExactTileCount = Zone.TotalTiles;
    const FIntPoint BoundsOrigin = Zone.Origin;
    const int32 BoundsSizeX = Zone.SizeX;
    const int32 BoundsSizeY = Zone.SizeY;

    FarmZones.Add(MoveTemp(Zone));

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Created exact-cell farm zone %d: Settlement=%d Cells=%d FarmGridOrigin=(%d,%d) BoundsOrigin=(%d,%d) BoundsSize=%dx%d Category=%d CropRow=%s AutoReplant=%s"),
        NewID,
        SettlementID,
        ExactTileCount,
        FarmGridOrigin.X,
        FarmGridOrigin.Y,
        BoundsOrigin.X,
        BoundsOrigin.Y,
        BoundsSizeX,
        BoundsSizeY,
        static_cast<int32>(Category),
        *CropRow.ToString(),
        bAutoReplant ? TEXT("true") : TEXT("false"));

    return NewID;
}

void UFarmingSubsystem::DisbandFarmZone(int32 ZoneID)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] DisbandFarmZone ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    Zone->ZoneState = EFarmZoneState::Disbanded;

    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        AActor* CropActor = Tile.CropActor.Get();

        if (IsValid(SafeAdapter) && WF_IsValidCropActor(CropActor))
        {
            SafeAdapter->DespawnCropPatch(CropActor);
        }

        WF_ResetFarmTileRuntimeState(Tile);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Disbanded farm zone %d"),
        ZoneID);
}

void UFarmingSubsystem::SetFarmZonePaused(int32 ZoneID, bool bPaused)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] SetFarmZonePaused ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    if (Zone->ZoneState == EFarmZoneState::Disbanded)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] SetFarmZonePaused ignored: ZoneID=%d is disbanded."),
            ZoneID);

        return;
    }

    Zone->ZoneState = bPaused ? EFarmZoneState::Paused : EFarmZoneState::Active;

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Zone %d paused=%s"),
        ZoneID,
        bPaused ? TEXT("true") : TEXT("false"));
}

void UFarmingSubsystem::ChangeFarmZoneCrop(
    int32 ZoneID,
    ECropCategory NewCategory,
    FName NewCropRow)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] ChangeFarmZoneCrop ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        AActor* CropActor = Tile.CropActor.Get();

        if (IsValid(SafeAdapter) && WF_IsValidCropActor(CropActor))
        {
            SafeAdapter->DespawnCropPatch(CropActor);
        }

        WF_ResetFarmTileRuntimeState(Tile);
    }

    Zone->CropCategory = NewCategory;
    Zone->CropRowName = NewCropRow;
    Zone->SeedItemTag = GetSeedTagForCategory(NewCategory);
    Zone->PlantedCount = 0;
    Zone->ReadyCount = 0;
    Zone->DeadCount = 0;

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Zone %d crop changed to Category=%d Row=%s"),
        ZoneID,
        static_cast<int32>(NewCategory),
        *NewCropRow.ToString());
}

// ═══════════════════════════════════════════════════════════════════
// Sim Tick
// ═══════════════════════════════════════════════════════════════════

void UFarmingSubsystem::TickFarmZones(int32 StepNumber)
{
    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();

    if (!IsValid(SafeAdapter))
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[FarmingSubsystem] TickFarmZones skipped. Adapter invalid Step=%d"),
            StepNumber);

        return;
    }

    UWorld* World = GetWorld();
    UProcessorManager* ProcessorManager = World ? World->GetSubsystem<UProcessorManager>() : nullptr;
    UJobProcessor* JobProcessor = ProcessorManager ? ProcessorManager->GetTypedProcessor<UJobProcessor>() : nullptr;

    if (!JobProcessor)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[FarmingSubsystem] TickFarmZones skipped. JobProcessor=null Step=%d"),
            StepNumber);

        return;
    }

    auto HasExistingFarmJob = [JobProcessor](int32 SettlementID, FGameplayTag JobTag, int32 ZoneID, FIntPoint TilePos) -> bool
        {
            for (const FJobRequest& ExistingJob : JobProcessor->GetAllJobs())
            {
                if (ExistingJob.SettlementID == SettlementID &&
                    ExistingJob.JobTag == JobTag &&
                    ExistingJob.TargetID == ZoneID &&
                    ExistingJob.TargetGridPos == TilePos)
                {
                    return true;
                }
            }

            return false;
        };

    auto PostFarmJob = [JobProcessor, &HasExistingFarmJob, StepNumber](
        int32 SettlementID,
        FGameplayTag JobTag,
        int32 ZoneID,
        FIntPoint TilePos,
        EJobPriority Priority) -> bool
        {
            if (HasExistingFarmJob(SettlementID, JobTag, ZoneID, TilePos))
            {
                return false;
            }

            FJobRequest Req;
            Req.SettlementID = SettlementID;
            Req.JobTag = JobTag;
            Req.RequiredSkill = TAG_Skill_Farming;
            Req.Priority = Priority;
            Req.PostedOnStep = StepNumber;
            Req.TargetID = ZoneID;
            Req.TargetGridPos = TilePos;

            JobProcessor->PostJob(Req);

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[FarmingSubsystem] Posted farm job %s Settlement=%d Zone=%d Tile=(%d,%d) Step=%d"),
                *JobTag.ToString(),
                SettlementID,
                ZoneID,
                TilePos.X,
                TilePos.Y,
                StepNumber);

            return true;
        };

    int32 ActiveZoneCount = 0;
    int32 TotalTileCount = 0;
    int32 TotalGrowingCount = 0;
    int32 TotalReadyCount = 0;
    int32 TotalHarvestedCount = 0;
    int32 TotalPrepPendingCount = 0;
    int32 TotalPlantPendingCount = 0;
    int32 TotalWaterPendingCount = 0;
    int32 TotalHarvestPendingCount = 0;

    for (FWorldForgeFarmZone& Zone : FarmZones)
    {
        if (Zone.ZoneState != EFarmZoneState::Active)
        {
            continue;
        }

        ++ActiveZoneCount;

        Zone.PlantedCount = 0;
        Zone.ReadyCount = 0;
        Zone.DeadCount = 0;

        for (FFarmTileRecord& Tile : Zone.Tiles)
        {
            ++TotalTileCount;

            // Harvested visual hold must run before crop polling.
            if (Tile.GrowthStage == ECropGrowthStage::Harvested)
            {
                ++Zone.ReadyCount;
                ++TotalHarvestedCount;

                if (Tile.HarvestedStep == INDEX_NONE)
                {
                    Tile.HarvestedStep = StepNumber;
                }

                const bool bVisualHoldExpired =
                    (StepNumber - Tile.HarvestedStep) >= WF_FarmHarvestedVisualHoldSteps;

                if (bVisualHoldExpired)
                {
                    AActor* HarvestedActor = Tile.CropActor.Get();

                    if (WF_IsValidCropActor(HarvestedActor))
                    {
                        SafeAdapter->DespawnCropPatch(HarvestedActor);
                    }

                    // v2: After visual hold, decide replant policy.
                    // Preserve soil prep state so the field stays "prepared".
                    const bool bWasPrepared = Tile.bSoilPrepared;
                    const int32 PrevSoilStep = Tile.SoilPreparedStep;

                    WF_ResetFarmTileRuntimeState(Tile);

                    // Field stays prepared — only the crop is gone.
                    Tile.bSoilPrepared = bWasPrepared;
                    Tile.SoilPreparedStep = PrevSoilStep;
                    Tile.GrowthStage = bWasPrepared
                        ? ECropGrowthStage::BareSoil
                        : ECropGrowthStage::None;

                    if (Zone.bAutoReplant)
                    {
                        // Auto-replant: tile goes straight back into the
                        // plant queue (seed check happens in BareSoil handler).
                        Tile.bAwaitingManualReplant = false;
                    }
                    else
                    {
                        // Manual replant: tile waits until player issues
                        // ManualReplantTile() or ManualReplantZone().
                        Tile.bAwaitingManualReplant = true;
                    }
                }
                else
                {
                    Tile.bPlanted = false;
                    Tile.bPlantJobPending = false;
                    Tile.bWaterJobPending = false;
                    Tile.bHarvestJobPending = false;
                    Tile.WaterLevel = 0.0f;
                    continue;
                }
            }

            AActor* CropActor = Tile.CropActor.Get();
            const bool bHasValidCropActor = WF_IsValidCropActor(CropActor);

            if (bHasValidCropActor)
            {
                const int32 BPGrowthIndex = SafeAdapter->GetGrowthStageIndex(CropActor);
                const float PolledWaterLevel = SafeAdapter->GetCropWaterLevel(CropActor);

                if (Tile.SimGrowthStageIndex == INDEX_NONE)
                {
                    Tile.SimGrowthStageIndex = FMath::Clamp(BPGrowthIndex, 0, WF_FarmReadyGrowthStageIndex);
                }

                if (Tile.LastGrowthAdvanceStep == INDEX_NONE)
                {
                    Tile.LastGrowthAdvanceStep = StepNumber;
                }

                if (Tile.WaterLevel <= 0.0f)
                {
                    Tile.WaterLevel = PolledWaterLevel > 0.0f ? PolledWaterLevel : 1.0f;
                }

                const bool bAlreadyReady =
                    Tile.SimGrowthStageIndex >= WF_FarmReadyGrowthStageIndex ||
                    Tile.GrowthStage == ECropGrowthStage::Ready ||
                    Tile.bHarvestJobPending;

                if (bAlreadyReady)
                {
                    // Ready/harvest-pending crops should never request or keep water jobs.
                    Tile.WaterLevel = FMath::Max(Tile.WaterLevel, WF_FarmReadyWaterFloor);
                    Tile.bWaterJobPending = false;
                    Tile.LastWaterJobStep = StepNumber;
                }
                else
                {
                    Tile.WaterLevel = FMath::Clamp(Tile.WaterLevel - WF_FarmWaterDrainPerStep, 0.0f, 1.0f);
                }

                const bool bCanGrow =
                    !bAlreadyReady &&
                    Tile.WaterLevel > 0.0f &&
                    Tile.SimGrowthStageIndex < WF_FarmReadyGrowthStageIndex;

                if (bCanGrow &&
                    (StepNumber - Tile.LastGrowthAdvanceStep) >= WF_FarmGrowthAdvanceSteps)
                {
                    SafeAdapter->AdvanceCropGrowth(CropActor);

                    ++Tile.SimGrowthStageIndex;
                    Tile.SimGrowthStageIndex = FMath::Clamp(Tile.SimGrowthStageIndex, 0, WF_FarmReadyGrowthStageIndex);
                    Tile.LastGrowthAdvanceStep = StepNumber;

                    UE_LOG(LogTemp, Log,
                        TEXT("[FarmingSubsystem] Sim advanced crop growth. Zone=%d Tile=(%d,%d) SimGrowthStage=%d Step=%d"),
                        Zone.ZoneID,
                        Tile.TilePos.X,
                        Tile.TilePos.Y,
                        Tile.SimGrowthStageIndex,
                        StepNumber);
                }

                Tile.GrowthStage =
                    Tile.SimGrowthStageIndex >= WF_FarmReadyGrowthStageIndex
                    ? ECropGrowthStage::Ready
                    : ECropGrowthStage::Growing;

                if (Tile.GrowthStage == ECropGrowthStage::Ready)
                {
                    // Polishing rule:
                    // Once ready, harvesting owns the tile. Water is no longer relevant.
                    Tile.bWaterJobPending = false;
                    Tile.WaterLevel = FMath::Max(Tile.WaterLevel, WF_FarmReadyWaterFloor);
                    Tile.LastWaterJobStep = StepNumber;
                }

                UE_LOG(LogTemp, VeryVerbose,
                    TEXT("[FarmingSubsystem] Crop poll. Zone=%d Tile=(%d,%d) Actor=%s Stage=%d SimGrowth=%d BPGrowth=%d Water=%.2f PlantPending=%s WaterPending=%s HarvestPending=%s bPlanted=%s"),
                    Zone.ZoneID,
                    Tile.TilePos.X,
                    Tile.TilePos.Y,
                    *CropActor->GetName(),
                    static_cast<int32>(Tile.GrowthStage),
                    Tile.SimGrowthStageIndex,
                    BPGrowthIndex,
                    Tile.WaterLevel,
                    Tile.bPlantJobPending ? TEXT("true") : TEXT("false"),
                    Tile.bWaterJobPending ? TEXT("true") : TEXT("false"),
                    Tile.bHarvestJobPending ? TEXT("true") : TEXT("false"),
                    Tile.bPlanted ? TEXT("true") : TEXT("false"));
            }
            else
            {
                // No crop actor is expected for:
                // - empty unprepared soil
                // - prepared bare soil waiting for Plant
                // - harvested tiles during visual hold
                //
                // Only reset if we previously believed a live crop existed.
                if (Tile.bPlanted || Tile.CropActor.IsValid())
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[FarmingSubsystem] Tile crop actor invalid; resetting planted crop state. Zone=%d Tile=(%d,%d)"),
                        Zone.ZoneID,
                        Tile.TilePos.X,
                        Tile.TilePos.Y);

                    const bool bWasPrepared = Tile.bSoilPrepared;
                    const int32 PreviousSoilPreparedStep = Tile.SoilPreparedStep;
                    const bool bWasPrepPending = Tile.bPrepSoilJobPending;
                    const int32 PreviousPrepStep = Tile.LastPrepSoilJobStep;

                    WF_ResetFarmTileRuntimeState(Tile);

                    // Preserve soil prep if this was already prepared soil and only the crop actor failed.
                    Tile.bSoilPrepared = bWasPrepared;
                    Tile.SoilPreparedStep = PreviousSoilPreparedStep;
                    Tile.bPrepSoilJobPending = bWasPrepPending;
                    Tile.LastPrepSoilJobStep = PreviousPrepStep;

                    if (Tile.bSoilPrepared)
                    {
                        Tile.GrowthStage = ECropGrowthStage::BareSoil;
                    }
                }
                else if (Tile.bSoilPrepared)
                {
                    // This is the normal state after PrepSoil completes:
                    // prepared soil, no crop actor yet, waiting for Plant.
                    Tile.GrowthStage = ECropGrowthStage::BareSoil;
                }
                else if (Tile.GrowthStage != ECropGrowthStage::Dead &&
                    Tile.GrowthStage != ECropGrowthStage::Harvested)
                {
                    // Empty, unprepared tile. Leave it as None so PrepSoil can post.
                    Tile.GrowthStage = ECropGrowthStage::None;
                    Tile.WaterLevel = 0.0f;
                }
            }

            switch (Tile.GrowthStage)
            {
            case ECropGrowthStage::None:
            case ECropGrowthStage::BareSoil:
            {
                if (Tile.CropActor.IsValid() || Tile.bPlanted)
                {
                    break;
                }

                if (!Tile.bSoilPrepared)
                {
                    const bool bPrepCooldownExpired =
                        Tile.LastPrepSoilJobStep == INDEX_NONE ||
                        (StepNumber - Tile.LastPrepSoilJobStep) >= WF_FarmPrepSoilJobCooldownSteps;

                    if (!Tile.bPrepSoilJobPending && bPrepCooldownExpired)
                    {
                        const bool bPosted = PostFarmJob(
                            Zone.SettlementID,
                            TAG_Job_Farm_PrepSoil,
                            Zone.ZoneID,
                            Tile.TilePos,
                            EJobPriority::Normal);

                        if (bPosted)
                        {
                            Tile.bPrepSoilJobPending = true;
                            Tile.LastPrepSoilJobStep = StepNumber;
                        }
                    }

                    break;
                }

                // v2: Skip plant posting if tile is waiting for manual replant.
                if (Tile.bAwaitingManualReplant)
                {
                    break;
                }

                // v2: Seed-gated planting — only post Plant job if stockpile
                // has at least 1 seed for this crop category.
                if (!HasSeedsForZone(Zone))
                {
                    break;
                }

                const bool bPlantCooldownExpired =
                    Tile.LastPlantJobStep == INDEX_NONE ||
                    (StepNumber - Tile.LastPlantJobStep) >= WF_FarmPlantJobCooldownSteps;

                if (!Tile.bPlantJobPending && bPlantCooldownExpired)
                {
                    const bool bPosted = PostFarmJob(
                        Zone.SettlementID,
                        TAG_Job_Farm_Plant,
                        Zone.ZoneID,
                        Tile.TilePos,
                        EJobPriority::Normal);

                    if (bPosted)
                    {
                        Tile.bPlantJobPending = true;
                        Tile.LastPlantJobStep = StepNumber;
                    }
                }

                break;
            }

            case ECropGrowthStage::Growing:
            {
                ++Zone.PlantedCount;
                ++TotalGrowingCount;

                const bool bWaterCooldownExpired =
                    Tile.LastWaterJobStep == INDEX_NONE ||
                    (StepNumber - Tile.LastWaterJobStep) >= WF_FarmWaterJobCooldownSteps;

                const bool bMayRequestWater =
                    !WF_IsHarvestLocked(Tile) &&
                    Tile.CropActor.IsValid() &&
                    Tile.WaterLevel < WaterThreshold &&
                    !Tile.bWaterJobPending &&
                    !Tile.bHarvestJobPending &&
                    bWaterCooldownExpired;

                if (bMayRequestWater)
                {
                    const bool bPosted = PostFarmJob(
                        Zone.SettlementID,
                        TAG_Job_Farm_Water,
                        Zone.ZoneID,
                        Tile.TilePos,
                        EJobPriority::Low);

                    if (bPosted)
                    {
                        Tile.bWaterJobPending = true;
                        Tile.LastWaterJobStep = StepNumber;
                    }
                }

                break;
            }

            case ECropGrowthStage::Ready:
            {
                ++Zone.ReadyCount;
                ++TotalReadyCount;

                // Polishing rule:
                // Ready crops are waiting for harvest. Do not keep/queue water work.
                Tile.bWaterJobPending = false;
                Tile.WaterLevel = FMath::Max(Tile.WaterLevel, WF_FarmReadyWaterFloor);
                Tile.LastWaterJobStep = StepNumber;

                const bool bHarvestCooldownExpired =
                    Tile.LastHarvestJobStep == INDEX_NONE ||
                    (StepNumber - Tile.LastHarvestJobStep) >= WF_FarmHarvestJobCooldownSteps;

                if (Tile.CropActor.IsValid() &&
                    !Tile.bHarvestJobPending &&
                    bHarvestCooldownExpired)
                {
                    const bool bPosted = PostFarmJob(
                        Zone.SettlementID,
                        TAG_Job_Farm_Harvest,
                        Zone.ZoneID,
                        Tile.TilePos,
                        EJobPriority::High);

                    if (bPosted)
                    {
                        Tile.bHarvestJobPending = true;
                        Tile.bWaterJobPending = false;
                        Tile.LastHarvestJobStep = StepNumber;
                    }
                }

                break;
            }

            case ECropGrowthStage::Dead:
            {
                ++Zone.DeadCount;

                AActor* DeadActor = Tile.CropActor.Get();

                if (WF_IsValidCropActor(DeadActor))
                {
                    SafeAdapter->DespawnCropPatch(DeadActor);
                }

                WF_ResetFarmTileRuntimeState(Tile);

                break;
            }

            case ECropGrowthStage::Harvested:
            default:
                break;
            }

            if (Tile.bPrepSoilJobPending)
            {
                ++TotalPrepPendingCount;
            }

            if (Tile.bPlantJobPending)
            {
                ++TotalPlantPendingCount;
            }

            if (Tile.bWaterJobPending)
            {
                ++TotalWaterPendingCount;
            }

            if (Tile.bHarvestJobPending)
            {
                ++TotalHarvestPendingCount;
            }
        }
    }

    if (StepNumber > 0 && (StepNumber % WF_FarmSummaryLogIntervalSteps) == 0 && ActiveZoneCount > 0)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[FarmingSubsystem] Farm summary Step=%d ActiveZones=%d Tiles=%d Growing=%d Ready=%d Harvested=%d PendingPrep=%d PendingPlant=%d PendingWater=%d PendingHarvest=%d"),
            StepNumber,
            ActiveZoneCount,
            TotalTileCount,
            TotalGrowingCount,
            TotalReadyCount,
            TotalHarvestedCount,
            TotalPrepPendingCount,
            TotalPlantPendingCount,
            TotalWaterPendingCount,
            TotalHarvestPendingCount);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Job Completion Callbacks
// ═══════════════════════════════════════════════════════════════════

void UFarmingSubsystem::OnPrepSoilCompleted(int32 ZoneID, FIntPoint TilePos)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnPrepSoilCompleted ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.TilePos != TilePos)
        {
            continue;
        }

        Tile.bSoilPrepared = true;
        Tile.bPrepSoilJobPending = false;
        Tile.SoilPreparedStep = INDEX_NONE;
        Tile.GrowthStage = ECropGrowthStage::BareSoil;
        Tile.WaterLevel = 0.0f;

        UE_LOG(LogTemp, Log,
            TEXT("[FarmingSubsystem] Soil prep completed. Zone=%d Tile=(%d,%d)"),
            ZoneID,
            TilePos.X,
            TilePos.Y);

        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[FarmingSubsystem] OnPrepSoilCompleted ignored: Tile=(%d,%d) not found in Zone=%d."),
        TilePos.X,
        TilePos.Y,
        ZoneID);
}

void UFarmingSubsystem::OnPlantCompleted(int32 ZoneID, FIntPoint TilePos)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnPlantCompleted ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();
    if (!IsValid(SafeAdapter))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnPlantCompleted failed: WildHarvestAdapter is invalid. ZoneID=%d Tile=(%d,%d)"),
            ZoneID,
            TilePos.X,
            TilePos.Y);

        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnPlantCompleted failed: World is null. ZoneID=%d Tile=(%d,%d)"),
            ZoneID,
            TilePos.X,
            TilePos.Y);

        return;
    }

    // Option 2 farming:
    // TilePos is a settlement-local voxel/grid offset. FarmGridOrigin is the
    // absolute voxel/grid origin captured when the farm zone was committed.
    // Do not derive this from Settlement->WorldOrigin here; that double-converts
    // exact-grid farms and spawns crop visuals thousands of cells away.
    const FIntPoint WorldGridCell = Zone->FarmGridOrigin + TilePos;

    FVector WorldPos = FVector::ZeroVector;

    if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
    {
        int32 SurfaceVoxelZ = 0;
        const bool bHasSurfaceZ = VoxelWorldManager->TryGetTerrainSurfaceVoxelZ(
            WorldGridCell,
            SurfaceVoxelZ);

        const FVector CellCenterXY = bHasSurfaceZ
            ? VoxelWorldManager->GridToWorld(WorldGridCell, SurfaceVoxelZ)
            : FVector(
                (static_cast<float>(WorldGridCell.X) + 0.5f) * 100.0f,
                (static_cast<float>(WorldGridCell.Y) + 0.5f) * 100.0f,
                0.0f);

        const FVector TraceStart = CellCenterXY + FVector(0.0f, 0.0f, 100000.0f);
        const FVector TraceEnd = CellCenterXY - FVector(0.0f, 0.0f, 100000.0f);

        FHitResult Hit;
        FCollisionQueryParams Params(SCENE_QUERY_STAT(FarmCropSurfaceTrace), false);
        Params.bTraceComplex = true;

        const bool bHitTerrain = World->LineTraceSingleByChannel(
            Hit,
            TraceStart,
            TraceEnd,
            ECC_Visibility,
            Params);

        if (bHitTerrain)
        {
            WorldPos = Hit.ImpactPoint;

            UE_LOG(LogTemp, Log,
                TEXT("[FarmingSubsystem] Crop surface raycast hit. Zone=%d LocalTile=(%d,%d) WorldGridCell=(%d,%d) Hit=%s Actor=%s"),
                ZoneID,
                TilePos.X,
                TilePos.Y,
                WorldGridCell.X,
                WorldGridCell.Y,
                *WorldPos.ToString(),
                Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("None"));
        }
        else
        {
            WorldPos = CellCenterXY;

            UE_LOG(LogTemp, Warning,
                TEXT("[FarmingSubsystem] Crop surface raycast missed. Fallback WorldPos=%s Zone=%d LocalTile=(%d,%d) WorldGridCell=(%d,%d)"),
                *WorldPos.ToString(),
                ZoneID,
                TilePos.X,
                TilePos.Y,
                WorldGridCell.X,
                WorldGridCell.Y);
        }
    }
    else
    {
        WorldPos = FVector(
            (static_cast<float>(WorldGridCell.X) + 0.5f) * 100.0f,
            (static_cast<float>(WorldGridCell.Y) + 0.5f) * 100.0f,
            0.0f);

        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] Crop placement fallback: VoxelWorldManager missing. WorldPos=%s WorldGridCell=(%d,%d)"),
            *WorldPos.ToString(),
            WorldGridCell.X,
            WorldGridCell.Y);
    }

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.TilePos == TilePos)
        {
            Tile.bPlantJobPending = false;
            Tile.bPrepSoilJobPending = false;
            Tile.bSoilPrepared = true;

            AActor* ExistingActor = Tile.CropActor.Get();

            if (WF_IsValidCropActor(ExistingActor))
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[FarmingSubsystem] Plant completed skipped: crop actor already exists. Zone=%d Tile=(%d,%d) Actor=%s"),
                    ZoneID,
                    TilePos.X,
                    TilePos.Y,
                    *ExistingActor->GetName());

                Tile.bPlanted = true;
                return;
            }

            AActor* CropActor = SafeAdapter->SpawnCropPatch(
                Zone->CropRowName,
                WorldPos);

            if (CropActor)
            {
                const bool bInteracted = SafeAdapter->InteractWithCrop(CropActor);

                UE_LOG(LogTemp, Log,
                    TEXT("[FarmingSubsystem] Plant interaction sent. Zone=%d Tile=(%d,%d) Actor=%s Success=%s"),
                    ZoneID,
                    TilePos.X,
                    TilePos.Y,
                    *CropActor->GetName(),
                    bInteracted ? TEXT("true") : TEXT("false"));

                Tile.CropActor = CropActor;
                Tile.bPlanted = true;
                Tile.GrowthStage = ECropGrowthStage::Growing;
                Tile.WaterLevel = 1.0f;
                Tile.bWaterJobPending = false;
                Tile.bHarvestJobPending = false;
                Tile.LastHarvestJobStep = INDEX_NONE;
                Tile.SimGrowthStageIndex = 0;

                // OnPlantCompleted() does not receive StepNumber.
                // TickFarmZones() will initialize LastGrowthAdvanceStep on the next sim tick.
                Tile.LastGrowthAdvanceStep = INDEX_NONE;
                Tile.PlantedStep = INDEX_NONE;
                Tile.HarvestedStep = INDEX_NONE;

                UE_LOG(LogTemp, Log,
                    TEXT("[FarmingSubsystem] Plant completed. Zone=%d LocalTile=(%d,%d) WorldGridCell=(%d,%d) Crop=%s Actor=%s"),
                    ZoneID,
                    TilePos.X,
                    TilePos.Y,
                    WorldGridCell.X,
                    WorldGridCell.Y,
                    *Zone->CropRowName.ToString(),
                    *CropActor->GetName());
            }
            else
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("[FarmingSubsystem] OnPlantCompleted failed to spawn crop patch. Zone=%d LocalTile=(%d,%d) WorldGridCell=(%d,%d) Crop=%s"),
                    ZoneID,
                    TilePos.X,
                    TilePos.Y,
                    WorldGridCell.X,
                    WorldGridCell.Y,
                    *Zone->CropRowName.ToString());
            }

            break;
        }
    }
}

void UFarmingSubsystem::OnWaterCompleted(int32 ZoneID, FIntPoint TilePos)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnWaterCompleted ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();
    if (!IsValid(SafeAdapter))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnWaterCompleted failed: WildHarvestAdapter invalid. ZoneID=%d Tile=(%d,%d)"),
            ZoneID,
            TilePos.X,
            TilePos.Y);

        return;
    }

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.TilePos != TilePos)
        {
            continue;
        }

        if (WF_IsHarvestLocked(Tile))
        {
            // A water job may already have been queued before the crop reached Ready.
            // Let the worker finish, but do not let this alter a harvest-ready tile.
            Tile.bWaterJobPending = false;
            Tile.LastWaterJobStep = INDEX_NONE;

            UE_LOG(LogTemp, Log,
                TEXT("[FarmingSubsystem] Water completed ignored because tile is harvest-locked. Zone=%d Tile=(%d,%d) Stage=%d HarvestPending=%s"),
                ZoneID,
                TilePos.X,
                TilePos.Y,
                static_cast<int32>(Tile.GrowthStage),
                Tile.bHarvestJobPending ? TEXT("true") : TEXT("false"));

            return;
        }

        AActor* CropActor = Tile.CropActor.Get();

        if (!WF_IsValidCropActor(CropActor))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[FarmingSubsystem] OnWaterCompleted skipped: invalid crop actor. Zone=%d Tile=(%d,%d)"),
                ZoneID,
                TilePos.X,
                TilePos.Y);

            WF_ResetFarmTileRuntimeState(Tile);
            return;
        }

        const bool bWatered = SafeAdapter->WaterCrop(CropActor);

        Tile.bWaterJobPending = false;

        if (bWatered)
        {
            Tile.WaterLevel = 1.0f;

            if (Tile.GrowthStage != ECropGrowthStage::Ready &&
                Tile.GrowthStage != ECropGrowthStage::Harvested &&
                Tile.GrowthStage != ECropGrowthStage::Dead)
            {
                Tile.GrowthStage = ECropGrowthStage::Growing;
            }
        }

        UE_LOG(LogTemp, Log,
            TEXT("[FarmingSubsystem] Water completed. Zone=%d Tile=(%d,%d) Actor=%s Success=%s WaterLevel=%.2f"),
            ZoneID,
            TilePos.X,
            TilePos.Y,
            *CropActor->GetName(),
            bWatered ? TEXT("true") : TEXT("false"),
            Tile.WaterLevel);

        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[FarmingSubsystem] OnWaterCompleted ignored: Tile=(%d,%d) not found in Zone=%d."),
        TilePos.X,
        TilePos.Y,
        ZoneID);
}

void UFarmingSubsystem::OnHarvestCompleted(
    int32 ZoneID,
    FIntPoint TilePos,
    int32 YieldAmount)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnHarvestCompleted ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();

    bool bFoundTile = false;

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.TilePos != TilePos)
        {
            continue;
        }

        bFoundTile = true;

        AActor* CropActor = Tile.CropActor.Get();

        if (IsValid(SafeAdapter) && WF_IsValidCropActor(CropActor))
        {
            const bool bHarvested = SafeAdapter->HarvestCrop(CropActor);

            UE_LOG(LogTemp, Log,
                TEXT("[FarmingSubsystem] Harvest interaction sent. Zone=%d Tile=(%d,%d) Actor=%s Success=%s"),
                ZoneID,
                TilePos.X,
                TilePos.Y,
                *CropActor->GetName(),
                bHarvested ? TEXT("true") : TEXT("false"));
        }

        Tile.GrowthStage = ECropGrowthStage::Harvested;
        Tile.bPlanted = false;
        Tile.bPlantJobPending = false;
        Tile.bWaterJobPending = false;
        Tile.bHarvestJobPending = false;
        Tile.WaterLevel = 0.0f;

        Tile.LastPlantJobStep = INDEX_NONE;
        Tile.LastWaterJobStep = INDEX_NONE;
        Tile.LastHarvestJobStep = INDEX_NONE;
        Tile.SimGrowthStageIndex = INDEX_NONE;
        Tile.LastGrowthAdvanceStep = INDEX_NONE;
        Tile.HarvestedStep = INDEX_NONE;

        // v2: Pre-set replant intent. TickFarmZones will finalize
        // once the visual hold expires.
        Tile.bAwaitingManualReplant = !Zone->bAutoReplant;

        break;
    }

    if (!bFoundTile)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] OnHarvestCompleted ignored: Tile=(%d,%d) not found in Zone=%d."),
            TilePos.X,
            TilePos.Y,
            ZoneID);

        return;
    }

    Zone->TotalYieldThisSeason += YieldAmount;

    UEventBus* SafeEventBus = GetEventBusSafe();

    if (SafeEventBus)
    {
        FSimulationEvent HarvestEvt;
        HarvestEvt.EventType = TAG_Sim_Event_CropHarvested;
        HarvestEvt.SettlementID = Zone->SettlementID;
        HarvestEvt.FloatPayload = static_cast<float>(YieldAmount);
        HarvestEvt.Payload = TAG_Job_Farm_Harvest;
        SafeEventBus->Dispatch(HarvestEvt);
    }

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Harvest completed. Zone=%d Tile=(%d,%d) Yield=%d SeasonTotal=%d"),
        ZoneID,
        TilePos.X,
        TilePos.Y,
        YieldAmount,
        Zone->TotalYieldThisSeason);
}

// ═══════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════

bool UFarmingSubsystem::GetZoneByID(
    int32 ZoneID,
    FWorldForgeFarmZone& OutZone) const
{
    const FWorldForgeFarmZone* Zone = FindZone(ZoneID);
    if (Zone)
    {
        OutZone = *Zone;
        return true;
    }

    return false;
}

TArray<FWorldForgeFarmZone> UFarmingSubsystem::GetZonesForSettlement(
    int32 SettlementID) const
{
    TArray<FWorldForgeFarmZone> Result;

    for (const FWorldForgeFarmZone& Zone : FarmZones)
    {
        if (Zone.SettlementID == SettlementID &&
            Zone.ZoneState != EFarmZoneState::Disbanded)
        {
            Result.Add(Zone);
        }
    }

    return Result;
}

bool UFarmingSubsystem::IsTileInFarmZone(
    FIntPoint TilePos,
    int32 SettlementID) const
{
    for (const FWorldForgeFarmZone& Zone : FarmZones)
    {
        if (Zone.SettlementID != SettlementID ||
            Zone.ZoneState == EFarmZoneState::Disbanded)
        {
            continue;
        }

        if (Zone.ContainsLocalTile(TilePos))
        {
            return true;
        }
    }

    return false;
}

// ═══════════════════════════════════════════════════════════════════
// Save / Load
// ═══════════════════════════════════════════════════════════════════

void UFarmingSubsystem::RestoreZoneRecords(
    const TArray<FWorldForgeFarmZone>& InZones)
{
    FarmZones = InZones;

    int32 MaxID = 0;

    for (FWorldForgeFarmZone& Zone : FarmZones)
    {
        MaxID = FMath::Max(MaxID, Zone.ZoneID);

        // Backward compatibility: older saves may not have ExplicitLocalCells.
        // Rebuild exact-cell membership from saved tile records or rectangle data.
        if (Zone.ExplicitLocalCells.Num() == 0)
        {
            if (Zone.Tiles.Num() > 0)
            {
                for (const FFarmTileRecord& Tile : Zone.Tiles)
                {
                    Zone.ExplicitLocalCells.Add(Tile.TilePos);
                }
            }
            else if (Zone.SizeX > 0 && Zone.SizeY > 0)
            {
                Zone.InitTiles();
            }
        }
    }

    NextZoneID = MaxID + 1;

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Restored %d zone records. NextID=%d"),
        FarmZones.Num(),
        NextZoneID);
}

// ═══════════════════════════════════════════════════════════════════
// v2: Replant Policy
// ═══════════════════════════════════════════════════════════════════

void UFarmingSubsystem::SetAutoReplant(int32 ZoneID, bool bEnable)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] SetAutoReplant ignored: ZoneID=%d not found."),
            ZoneID);

        return;
    }

    Zone->bAutoReplant = bEnable;

    // If enabling auto-replant, clear manual-wait flags on all
    // harvested tiles so they re-enter the plant queue immediately.
    if (bEnable)
    {
        for (FFarmTileRecord& Tile : Zone->Tiles)
        {
            if (Tile.bAwaitingManualReplant)
            {
                Tile.bAwaitingManualReplant = false;
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Zone %d auto-replant=%s"),
        ZoneID,
        bEnable ? TEXT("true") : TEXT("false"));
}

bool UFarmingSubsystem::ManualReplantTile(int32 ZoneID, FIntPoint TilePos)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        return false;
    }

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.TilePos == TilePos && Tile.bAwaitingManualReplant)
        {
            Tile.bAwaitingManualReplant = false;

            // If the tile still has a harvested actor, despawn it now
            // so the next tick sees bare soil and posts a Plant job.
            UWildHarvestAdapter* SafeAdapter = GetWildHarvestAdapterSafe();
            AActor* OldActor = Tile.CropActor.Get();

            if (IsValid(SafeAdapter) && IsValid(OldActor) && !OldActor->IsActorBeingDestroyed())
            {
                SafeAdapter->DespawnCropPatch(OldActor);
            }

            const bool bWasPrepared = Tile.bSoilPrepared;
            const int32 PrevSoilStep = Tile.SoilPreparedStep;

            Tile.CropActor = nullptr;
            Tile.bPlanted = false;
            Tile.bPlantJobPending = false;
            Tile.bWaterJobPending = false;
            Tile.bHarvestJobPending = false;
            Tile.WaterLevel = 0.0f;
            Tile.SimGrowthStageIndex = INDEX_NONE;
            Tile.LastGrowthAdvanceStep = INDEX_NONE;
            Tile.PlantedStep = INDEX_NONE;
            Tile.HarvestedStep = INDEX_NONE;

            Tile.bSoilPrepared = bWasPrepared;
            Tile.SoilPreparedStep = PrevSoilStep;
            Tile.GrowthStage = bWasPrepared
                ? ECropGrowthStage::BareSoil
                : ECropGrowthStage::None;

            UE_LOG(LogTemp, Log,
                TEXT("[FarmingSubsystem] ManualReplantTile Zone=%d Tile=(%d,%d) SoilPrepared=%s"),
                ZoneID,
                TilePos.X,
                TilePos.Y,
                bWasPrepared ? TEXT("true") : TEXT("false"));

            return true;
        }
    }

    return false;
}

int32 UFarmingSubsystem::ManualReplantZone(int32 ZoneID)
{
    FWorldForgeFarmZone* Zone = FindZoneMutable(ZoneID);
    if (!Zone)
    {
        return 0;
    }

    int32 Count = 0;

    for (FFarmTileRecord& Tile : Zone->Tiles)
    {
        if (Tile.bAwaitingManualReplant)
        {
            if (ManualReplantTile(ZoneID, Tile.TilePos))
            {
                ++Count;
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] ManualReplantZone Zone=%d ResetTiles=%d"),
        ZoneID,
        Count);

    return Count;
}

// ═══════════════════════════════════════════════════════════════════
// v2: Seed Resolution
// ═══════════════════════════════════════════════════════════════════

FGameplayTag UFarmingSubsystem::GetSeedTagForCategory(ECropCategory Category)
{
    // Use soft gameplay-tag lookup instead of native TAG_Item_Seed_* constants.
    // This keeps farming compiling even before the seed tags are promoted to native tags.
    const bool bErrorIfNotFound = false;

    switch (Category)
    {
    case ECropCategory::Grains:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Grain")), bErrorIfNotFound);

    case ECropCategory::Berries:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Berry")), bErrorIfNotFound);

    case ECropCategory::Herbs:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Herb")), bErrorIfNotFound);

    case ECropCategory::Mushrooms:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Mushroom")), bErrorIfNotFound);

    case ECropCategory::RootVegetables:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.RootVegetable")), bErrorIfNotFound);

    case ECropCategory::SaladGarden:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.SaladGarden")), bErrorIfNotFound);

    default:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Item.Seed.Grain")), bErrorIfNotFound);
    }
}

bool UFarmingSubsystem::HasSeedsForZone(const FWorldForgeFarmZone& Zone) const
{
    if (!Zone.SeedItemTag.IsValid())
    {
        // No seed tag means seed system is not configured for this zone.
        // Fall back to allowing planting (backward compat).
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (!SM)
    {
        return false;
    }

    return SM->HasStockpile(Zone.SettlementID, Zone.SeedItemTag, 1);
}

// ═══════════════════════════════════════════════════════════════════
// Internal
// ═══════════════════════════════════════════════════════════════════

UWildHarvestAdapter* UFarmingSubsystem::GetWildHarvestAdapterSafe()
{
    if (IsValid(Adapter))
    {
        return Adapter;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] GetWildHarvestAdapterSafe failed: World is null."));

        return nullptr;
    }

    Adapter = World->GetSubsystem<UWildHarvestAdapter>();

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Late-bound WildHarvestAdapter: %s"),
        IsValid(Adapter) ? TEXT("OK") : TEXT("NULL"));

    return Adapter;
}

UEventBus* UFarmingSubsystem::GetEventBusSafe()
{
    if (IsValid(EventBus))
    {
        return EventBus;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[FarmingSubsystem] GetEventBusSafe failed: World is null."));

        return nullptr;
    }

    EventBus = World->GetSubsystem<UEventBus>();

    UE_LOG(LogTemp, Log,
        TEXT("[FarmingSubsystem] Late-bound EventBus: %s"),
        IsValid(EventBus) ? TEXT("OK") : TEXT("NULL"));

    return EventBus;
}

FWorldForgeFarmZone* UFarmingSubsystem::FindZoneMutable(int32 ZoneID)
{
    for (FWorldForgeFarmZone& Zone : FarmZones)
    {
        if (Zone.ZoneID == ZoneID)
        {
            return &Zone;
        }
    }

    return nullptr;
}

const FWorldForgeFarmZone* UFarmingSubsystem::FindZone(int32 ZoneID) const
{
    for (const FWorldForgeFarmZone& Zone : FarmZones)
    {
        if (Zone.ZoneID == ZoneID)
        {
            return &Zone;
        }
    }

    return nullptr;
}