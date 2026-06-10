#include "ColonyManager.h"

#include "SettlementManager.h"
#include "ProcessorManager.h"
#include "NavigationContext.h"
#include "SimulationTimeManager.h"
#include "SaveSubsystem.h"
#include "VoxelConstructionSystem.h"
#include "VoxelWorldManager.h"
#include "WorldGenBootstrapActor.h"
#include "WorldForgeTags.h"
#include "Player/WorldForgeCamera.h"
#include "WorldMapSubsystem.h"
#include "Embark/EmbarkLoadoutSubsystem.h"
#include "Farming/FarmingSubsystem.h"
#include "UI/WorldForgeEmbarkationManager.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"
#include "Traits/WorldForgeTraitGenerator.h"

namespace
{
    FString WF_HumanizeSeedIdentifier(const FString& Identifier)
    {
        FString Result;
        Result.Reserve(Identifier.Len() + 4);

        for (int32 Index = 0; Index < Identifier.Len(); ++Index)
        {
            const TCHAR Character = Identifier[Index];
            const TCHAR PreviousCharacter = Index > 0 ? Identifier[Index - 1] : 0;
            if (Index > 0 && FChar::IsUpper(Character) && (FChar::IsLower(PreviousCharacter) || FChar::IsDigit(PreviousCharacter)))
            {
                Result.AppendChar(TCHAR(' '));
            }
            Result.AppendChar(Character);
        }

        return Result;
    }

    bool WF_TryResolveSeedLoadoutItem(FName ItemId, FName& OutCropRowName, FString& OutSeedIdentifier, FString& OutDisplayName)
    {
        const FString ItemIdString = ItemId.ToString();
        FString Prefix;
        FString SeedIdentifier;
        if (!ItemIdString.Split(TEXT("."), &Prefix, &SeedIdentifier) || !Prefix.Equals(TEXT("Seed"), ESearchCase::CaseSensitive))
        {
            return false;
        }

        SeedIdentifier.TrimStartAndEndInline();
        if (SeedIdentifier.IsEmpty())
        {
            return false;
        }

        OutSeedIdentifier = SeedIdentifier;
        OutCropRowName = FName(*FString::Printf(TEXT("LP_%s"), *SeedIdentifier));
        OutDisplayName = WF_HumanizeSeedIdentifier(SeedIdentifier);
        return true;
    }
}

AColonyManager::AColonyManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AColonyManager::BeginBootstrapNow()
{
    if (bIsDuplicateManagerInstance)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] BeginBootstrapNow ignored: duplicate manager."));
        return;
    }

    if (bBootstrapComplete)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] BeginBootstrapNow ignored: bootstrap already complete."));
        return;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BootstrapTimerHandle);
    }

    BootstrapRetryCount = 0;

    UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Manual bootstrap requested."));
    StartBootstrapAttempt();
}

void AColonyManager::BeginPlay()
{
    Super::BeginPlay();

    if (!IsPrimaryColonyManagerInstance())
    {
        bIsDuplicateManagerInstance = true;

        UE_LOG(
            LogTemp,
            Error,
            TEXT("[ColonyManager] BLOCKED duplicate colony manager instance: %s"),
            *GetName()
        );

        SetActorTickEnabled(false);
        SetActorHiddenInGame(true);
        SetActorEnableCollision(false);
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Primary colony manager accepted: %s"),
        *GetName()
    );

    if (!bBootstrapOnBeginPlay)
    {
        UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Bootstrap disabled."));
        return;
    }

    GetWorldTimerManager().SetTimer(
        BootstrapTimerHandle,
        this,
        &AColonyManager::StartBootstrapAttempt,
        InitialBootstrapDelaySeconds,
        false
    );
}

void AColonyManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BootstrapTimerHandle);
        World->GetTimerManager().ClearTimer(CameraWarmupTimerHandle);
    }

    if (VoxelWorldMgr && bBoundToTerrainReadyEvent)
    {
        VoxelWorldMgr->OnTerrainReady.RemoveDynamic(this, &AColonyManager::HandleTerrainReady);
        bBoundToTerrainReadyEvent = false;
    }

    Super::EndPlay(EndPlayReason);
}

bool AColonyManager::IsPrimaryColonyManagerInstance() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    for (TActorIterator<AColonyManager> It(World); It; ++It)
    {
        AColonyManager* OtherManager = *It;
        if (!IsValid(OtherManager) || OtherManager == this || OtherManager->IsActorBeingDestroyed())
        {
            continue;
        }

        return false;
    }

    return true;
}

void AColonyManager::StartBootstrapAttempt()
{
    if (bIsDuplicateManagerInstance || bBootstrapComplete)
    {
        return;
    }

    if (!CacheSubsystems() || !AreRequiredSubsystemsReady())
    {
        RetryBootstrap();
        return;
    }

    if (!HasWorldGenBootstrapActor())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ColonyManager] Waiting for AWorldGenBootstrapActor before colony bootstrap.")
        );
        RetryBootstrap();
        return;
    }

    if (IsTerrainReadyForSpawn())
    {
        FinishBootstrap();
        return;
    }

    BindToTerrainReadyEvent();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Waiting for terrain-ready event before colony bootstrap.")
    );
}

void AColonyManager::RetryBootstrap()
{
    BootstrapRetryCount++;

    if (BootstrapRetryCount > MaxBootstrapRetries)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[ColonyManager] Bootstrap failed after %d retries."),
            MaxBootstrapRetries
        );
        return;
    }

    GetWorldTimerManager().SetTimer(
        BootstrapTimerHandle,
        this,
        &AColonyManager::StartBootstrapAttempt,
        SubsystemRetryDelaySeconds,
        false
    );
}

bool AColonyManager::CacheSubsystems()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] CacheSubsystems failed: World is null."));
        return false;
    }

    SettlementMgr = World->GetSubsystem<USettlementManager>();
    ProcessorMgr = World->GetSubsystem<UProcessorManager>();
    NavContext = World->GetSubsystem<UNavigationContext>();
    SimTimeMgr = World->GetSubsystem<USimulationTimeManager>();
    VoxelConstructionSys = World->GetSubsystem<UVoxelConstructionSystem>();
    VoxelWorldMgr = World->GetSubsystem<UVoxelWorldManager>();

    if (UGameInstance* GameInstance = GetGameInstance())
    {
        SaveSubsystem = GameInstance->GetSubsystem<USaveSubsystem>();
    }
    else
    {
        SaveSubsystem = nullptr;
    }

    return true;
}

bool AColonyManager::AreRequiredSubsystemsReady() const
{
    const bool bReady =
        SettlementMgr != nullptr &&
        ProcessorMgr != nullptr &&
        NavContext != nullptr &&
        SimTimeMgr != nullptr &&
        VoxelConstructionSys != nullptr &&
        VoxelWorldMgr != nullptr;

    if (!bReady)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ColonyManager] Missing subsystem(s): Settlement=%d Processor=%d Nav=%d SimTime=%d Build=%d Voxel=%d Save=%d"),
            SettlementMgr != nullptr,
            ProcessorMgr != nullptr,
            NavContext != nullptr,
            SimTimeMgr != nullptr,
            VoxelConstructionSys != nullptr,
            VoxelWorldMgr != nullptr,
            SaveSubsystem != nullptr
        );
    }

    return bReady;
}

bool AColonyManager::IsTerrainReadyForSpawn() const
{
    return VoxelWorldMgr && VoxelWorldMgr->IsSurfaceReady();
}

void AColonyManager::BindToTerrainReadyEvent()
{
    if (!VoxelWorldMgr || bBoundToTerrainReadyEvent)
    {
        return;
    }

    VoxelWorldMgr->OnTerrainReady.AddDynamic(this, &AColonyManager::HandleTerrainReady);
    bBoundToTerrainReadyEvent = true;
}

void AColonyManager::HandleTerrainReady()
{
    if (bIsDuplicateManagerInstance || bBootstrapComplete)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Received terrain-ready event."));

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(BootstrapTimerHandle);
    }

    FinishBootstrap();
}

bool AColonyManager::HasWorldGenBootstrapActor() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    for (TActorIterator<AWorldGenBootstrapActor> It(World); It; ++It)
    {
        if (IsValid(*It) && !(*It)->IsActorBeingDestroyed())
        {
            return true;
        }
    }

    return false;
}

void AColonyManager::FinishBootstrap()
{
    if (bIsDuplicateManagerInstance || bBootstrapComplete)
    {
        return;
    }

    if (!CacheSubsystems() || !AreRequiredSubsystemsReady())
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] FinishBootstrap failed: required subsystems missing."));
        return;
    }

    if (!HasWorldGenBootstrapActor())
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] FinishBootstrap aborted: AWorldGenBootstrapActor is missing."));
        return;
    }

    if (!IsTerrainReadyForSpawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] FinishBootstrap called before terrain was ready."));
        BindToTerrainReadyEvent();
        return;
    }

    InitializeSettlement();
    InitializeNavigation();
    SetupAgents();
    SetupInitialResources();
    PositionPlayerCameraOverSettlement();
    BindDebugInput();
    StartSimulation();

    bBootstrapComplete = true;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Bootstrap complete. SettlementID=%d Population=%d"),
        SettlementID,
        SettlementMgr ? SettlementMgr->GetPopulation(SettlementID) : -1
    );
}

void AColonyManager::InitializeSettlement()
{
    if (!SettlementMgr || !ProcessorMgr)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] InitializeSettlement failed: required managers missing."));
        SettlementID = INDEX_NONE;
        return;
    }

    int32 SelectedStartTileX = INDEX_NONE;
    int32 SelectedStartTileY = INDEX_NONE;
    int32 SelectedStartTileIndex = INDEX_NONE;

    if (TryGetSelectedStartTile(SelectedStartTileX, SelectedStartTileY, SelectedStartTileIndex))
    {
        // The selected full-map tile is the strategic colony claim/source tile.
        SettlementWorldTileID = SelectedStartTileIndex;

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonyManager] Selected start tile found. Tile=(%d,%d) Index=%d"),
            SelectedStartTileX,
            SelectedStartTileY,
            SelectedStartTileIndex
        );
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ColonyManager] No selected start tile found. Using fallback SettlementWorldTileID=%d"),
            SettlementWorldTileID
        );
    }

    const FIntPoint ResolvedSpawnCenter = ResolveInitialSettlementWorldOrigin();
    SpawnGridCenter = ResolvedSpawnCenter;
    SettlementWorldOrigin = ComputeSettlementOriginFromSpawnCenter(SpawnGridCenter);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Final settlement bootstrap values. SettlementWorldTileID=%d SettlementWorldOrigin=(%d,%d) SpawnCenter=(%d,%d)"),
        SettlementWorldTileID,
        SettlementWorldOrigin.X,
        SettlementWorldOrigin.Y,
        SpawnGridCenter.X,
        SpawnGridCenter.Y
    );

    SettlementID = SettlementMgr->CreateSettlement(SettlementName, SettlementWorldTileID);
    ProcessorMgr->RegisterSettlement(SettlementID);
    const int32 ActiveSettlementCount = SettlementMgr ? SettlementMgr->GetAllSettlementIDs().Num() : -1;
    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Registered player SettlementID=%d ActiveSettlementCount=%d"),
        SettlementID,
        ActiveSettlementCount
    );
    SettlementMgr->SetPlayerCurrency(SettlementID, TAG_Currency_Barter);
    SettlementMgr->SetPlayerSettlementID(SettlementID);

    if (SelectedStartTileX >= 0 && SelectedStartTileY >= 0 && SelectedStartTileIndex >= 0)
    {
        // Claim around the resolved spawn center, not only the clicked embark tile.
        // ResolveValidSpawnCell may move the actual colony start to nearby land; the
        // starting territory must follow that playable area while CreateSettlement()
        // still keeps the selected embark tile itself claimed.
        const FIntPoint InitialClaimCenter = SpawnGridCenter;
        const int32 EffectiveInitialClaimRadius = FMath::Max(0, InitialPlayerClaimRadius);
        const int32 InitialClaimDiameter = (EffectiveInitialClaimRadius * 2) + 1;

        UE_LOG(LogTemp, Log,
            TEXT("[ColonyManager] Initial player claim request. SelectedTile=(%d,%d) SelectedTileID=%d SpawnCenter=(%d,%d) ClaimCenter=(%d,%d) Radius=%d Footprint=%dx%d"),
            SelectedStartTileX,
            SelectedStartTileY,
            SelectedStartTileIndex,
            SpawnGridCenter.X,
            SpawnGridCenter.Y,
            InitialClaimCenter.X,
            InitialClaimCenter.Y,
            EffectiveInitialClaimRadius,
            InitialClaimDiameter,
            InitialClaimDiameter);

        SettlementMgr->AddInitialPlayerClaimFootprint(
            SettlementID,
            InitialClaimCenter.X,
            InitialClaimCenter.Y,
            EffectiveInitialClaimRadius);
    }

    if (FSettlementState* Settlement = SettlementMgr->GetMutableSettlement(SettlementID))
    {
        // Keep the persisted settlement state aligned with the strategic claim tile.
        Settlement->WorldTileID = SettlementWorldTileID;
        Settlement->WorldOrigin = SettlementWorldOrigin;
    }

    const TArray<int32>* ClaimedWorldTileIDs = SettlementMgr->GetClaimedWorldTilesForSettlement(SettlementID);
    const int32 ClaimedWorldTileCount = ClaimedWorldTileIDs ? ClaimedWorldTileIDs->Num() : 0;
    const bool bStartingTileClaimed = SettlementMgr->DoesSettlementClaimWorldTile(SettlementID, SettlementWorldTileID);
    int32 SpawnCenterWorldTileID = INDEX_NONE;
    bool bSpawnCenterTileResolved = false;
    bool bSpawnCenterTileClaimed = false;
    if (const UWorld* World = GetWorld())
    {
        if (const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>())
        {
            bSpawnCenterTileResolved = WorldMapSubsystem->TileXYToIndex(SpawnGridCenter.X, SpawnGridCenter.Y, SpawnCenterWorldTileID);
            bSpawnCenterTileClaimed = bSpawnCenterTileResolved && SettlementMgr->DoesSettlementClaimWorldTile(SettlementID, SpawnCenterWorldTileID);
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Player settlement claim verified. SettlementID=%d SelectedWorldTileID=%d SelectedClaimed=%s SpawnCenter=(%d,%d) SpawnCenterTileID=%d SpawnCenterResolved=%s SpawnCenterClaimed=%s Claims=%d"),
        SettlementID,
        SettlementWorldTileID,
        bStartingTileClaimed ? TEXT("true") : TEXT("false"),
        SpawnGridCenter.X,
        SpawnGridCenter.Y,
        SpawnCenterWorldTileID,
        bSpawnCenterTileResolved ? TEXT("true") : TEXT("false"),
        bSpawnCenterTileClaimed ? TEXT("true") : TEXT("false"),
        ClaimedWorldTileCount
    );

    FIntPoint ClaimMin(MAX_int32, MAX_int32);
    FIntPoint ClaimMax(MIN_int32, MIN_int32);
    bool bHasClaimBounds = false;

    if (ClaimedWorldTileIDs)
    {
        if (const UWorld* World = GetWorld())
        {
            if (const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>())
            {
                for (const int32 ClaimedWorldTileID : *ClaimedWorldTileIDs)
                {
                    int32 ClaimTileX = INDEX_NONE;
                    int32 ClaimTileY = INDEX_NONE;
                    if (!WorldMapSubsystem->TileIndexToXY(ClaimedWorldTileID, ClaimTileX, ClaimTileY))
                    {
                        continue;
                    }

                    ClaimMin.X = FMath::Min(ClaimMin.X, ClaimTileX);
                    ClaimMin.Y = FMath::Min(ClaimMin.Y, ClaimTileY);
                    ClaimMax.X = FMath::Max(ClaimMax.X, ClaimTileX);
                    ClaimMax.Y = FMath::Max(ClaimMax.Y, ClaimTileY);
                    bHasClaimBounds = true;
                }
            }
        }
    }

    if (bHasClaimBounds)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[CubeSelectionClaimAudit] PlayerClaimSummary SettlementID=%d ClaimedCount=%d SpawnGridCenter=(%d,%d) SettlementOrigin=(%d,%d) ClaimMin=(%d,%d) ClaimMax=(%d,%d)"),
            SettlementID,
            ClaimedWorldTileCount,
            SpawnGridCenter.X,
            SpawnGridCenter.Y,
            SettlementWorldOrigin.X,
            SettlementWorldOrigin.Y,
            ClaimMin.X,
            ClaimMin.Y,
            ClaimMax.X,
            ClaimMax.Y);
    }
    else
    {
        const int32 TileID0 = (ClaimedWorldTileIDs && ClaimedWorldTileIDs->IsValidIndex(0)) ? (*ClaimedWorldTileIDs)[0] : INDEX_NONE;
        const int32 TileID1 = (ClaimedWorldTileIDs && ClaimedWorldTileIDs->IsValidIndex(1)) ? (*ClaimedWorldTileIDs)[1] : INDEX_NONE;
        const int32 TileID2 = (ClaimedWorldTileIDs && ClaimedWorldTileIDs->IsValidIndex(2)) ? (*ClaimedWorldTileIDs)[2] : INDEX_NONE;
        const int32 TileID3 = (ClaimedWorldTileIDs && ClaimedWorldTileIDs->IsValidIndex(3)) ? (*ClaimedWorldTileIDs)[3] : INDEX_NONE;
        const int32 TileID4 = (ClaimedWorldTileIDs && ClaimedWorldTileIDs->IsValidIndex(4)) ? (*ClaimedWorldTileIDs)[4] : INDEX_NONE;

        UE_LOG(LogTemp, Log,
            TEXT("[CubeSelectionClaimAudit] PlayerClaimSample SettlementID=%d ClaimedCount=%d TileID0=%d TileID1=%d TileID2=%d TileID3=%d TileID4=%d"),
            SettlementID,
            ClaimedWorldTileCount,
            TileID0,
            TileID1,
            TileID2,
            TileID3,
            TileID4);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Created player settlement ID=%d Name='%s' WorldTileID=%d WorldOrigin=(%d,%d) SpawnCenter=(%d,%d) ClaimedWorldTileIDs=%d"),
        SettlementID,
        *SettlementName,
        SettlementWorldTileID,
        SettlementWorldOrigin.X,
        SettlementWorldOrigin.Y,
        SpawnGridCenter.X,
        SpawnGridCenter.Y,
        ClaimedWorldTileCount
    );

    {
        int32 SettlementTileX = -1;
        int32 SettlementTileY = -1;
        if (const UWorld* SyncWorld = GetWorld())
        {
            if (const UWorldMapSubsystem* SyncMapSub = SyncWorld->GetSubsystem<UWorldMapSubsystem>())
            {
                SyncMapSub->TileIndexToXY(SettlementWorldTileID, SettlementTileX, SettlementTileY);
            }
        }
        UE_LOG(LogTemp, Log,
            TEXT("[SpawnSync] SelectedTile=(%d,%d) ClaimTile=(%d,%d) SettlementTile=(%d,%d)"),
            SelectedStartTileX, SelectedStartTileY,
            SpawnGridCenter.X, SpawnGridCenter.Y,
            SettlementTileX, SettlementTileY);
        if (SelectedStartTileIndex >= 0 && SettlementWorldTileID != SelectedStartTileIndex)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[SpawnSync] WARNING: Player settlement not using selected tile."));
        }
    }
}

void AColonyManager::InitializeNavigation()
{
    if (!NavContext || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] InitializeNavigation failed."));
        return;
    }

    NavContext->InitializeGrid(SettlementID, NavigationGridWidth, NavigationGridHeight);

    // CUBE-TERRAIN-04U-STEP1: stamp initial walkability onto the nav grid from
    // the current terrain (CubePrototype-aware). Does NOT change movement, pathfinding,
    // colonist spawn, or terrain generation. JobProcessor still bypasses FindPath.
    if (!VoxelWorldMgr)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[CubeTerrainNavBuild] SettlementID=%d skipped (VoxelWorldMgr null)."),
            SettlementID);
        return;
    }

    const int32 TotalCells = NavigationGridWidth * NavigationGridHeight;
    int32 Walkable = 0;
    int32 Blocked = 0;
    int32 BlockedWater = 0;
    int32 BlockedLava = 0;
    int32 BlockedAir = 0;
    int32 BlockedNoSurface = 0;
    int32 BlockedOther = 0;

    constexpr int32 MaxDetailLogs = 16;
    int32 EmittedDetailLogs = 0;

    for (int32 Y = 0; Y < NavigationGridHeight; ++Y)
    {
        for (int32 X = 0; X < NavigationGridWidth; ++X)
        {
            const FIntPoint LocalCell(X, Y);
            const FIntPoint WorldCell = SettlementWorldOrigin + LocalCell;

            FString Reason;
            int32 SurfaceZ = 0;
            const bool bWalkable =
                VoxelWorldMgr->IsTerrainCellWalkableForNavigation(WorldCell, &Reason, &SurfaceZ);

            NavContext->SetCellWalkable(SettlementID, LocalCell, bWalkable);

            if (bWalkable)
            {
                ++Walkable;
            }
            else
            {
                ++Blocked;
                if      (Reason == TEXT("Water"))     { ++BlockedWater; }
                else if (Reason == TEXT("Lava"))      { ++BlockedLava; }
                else if (Reason == TEXT("Air"))       { ++BlockedAir; }
                else if (Reason == TEXT("NoSurface")) { ++BlockedNoSurface; }
                else                                   { ++BlockedOther; }

                if (EmittedDetailLogs < MaxDetailLogs)
                {
                    UE_LOG(LogTemp, Log,
                        TEXT("[CubeTerrainNavBlocked] WorldCell=(%d,%d) LocalCell=(%d,%d) Reason=%s SurfaceZ=%d"),
                        WorldCell.X, WorldCell.Y,
                        LocalCell.X, LocalCell.Y,
                        *Reason, SurfaceZ);
                    ++EmittedDetailLogs;
                }
            }
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("[CubeTerrainNavBuild] SettlementID=%d Origin=(%d,%d) Cells=%d Walkable=%d Blocked=%d BlockedWater=%d BlockedLava=%d BlockedAir=%d BlockedNoSurface=%d BlockedOther=%d"),
        SettlementID,
        SettlementWorldOrigin.X, SettlementWorldOrigin.Y,
        TotalCells, Walkable, Blocked,
        BlockedWater, BlockedLava, BlockedAir, BlockedNoSurface, BlockedOther);

    // CUBE-TERRAIN-04U-STEP3: diagnostic — log all active agent settlements vs nav grids.
    if (ProcessorMgr)
    {
        TMap<int32, int32> AgentCountBySettlement;
        for (const FAgentData& Agent : ProcessorMgr->GetAllAgents())
        {
            AgentCountBySettlement.FindOrAdd(Agent.SettlementID)++;
        }

        const TArray<int32> NavGridSettlements = NavContext->GetRegisteredSettlementIDs();

        for (const TPair<int32, int32>& Pair : AgentCountBySettlement)
        {
            const bool bHasNavGrid = NavGridSettlements.Contains(Pair.Key);
            UE_LOG(LogTemp, Log,
                TEXT("[CubeTerrainNavSettlement] ActiveSettlementID=%d Agents=%d HasNavGrid=%s"),
                Pair.Key,
                Pair.Value,
                bHasNavGrid ? TEXT("true") : TEXT("false"));
        }
    }
}

void AColonyManager::SetupAgents()
{
    if (!ProcessorMgr || !SettlementMgr || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] SetupAgents failed."));
        return;
    }

    const int32 StartingAgentID = GetNextAgentID();
    const int32 GridSize = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InitialAgentCount))));
    const int32 RowCount = FMath::Max(1, FMath::CeilToInt(static_cast<float>(InitialAgentCount) / static_cast<float>(GridSize)));
    const FIntPoint FormationCenterOffset((GridSize - 1) / 2, (RowCount - 1) / 2);
    TSet<FIntPoint> ClaimedWorldCells;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SpawnSync] SetupAgents starting. SpawnGridCenter=(%d,%d) Origin=(%d,%d) GridSize=%d Rows=%d FormationCenterOffset=(%d,%d)"),
        SpawnGridCenter.X,
        SpawnGridCenter.Y,
        SettlementWorldOrigin.X,
        SettlementWorldOrigin.Y,
        GridSize,
        RowCount,
        FormationCenterOffset.X,
        FormationCenterOffset.Y
    );

    for (int32 i = 0; i < InitialAgentCount; ++i)
    {
        const int32 AgentID = StartingAgentID + i;

        const FIntPoint DesiredLocalCell = ComputeInitialSpawnCell(i);
        const FIntPoint FormationOffset = DesiredLocalCell - FormationCenterOffset;
        const FIntPoint DesiredWorldCell = SpawnGridCenter + FormationOffset;
        const FIntPoint ValidWorldCell = ResolveUniqueValidSpawnCell(DesiredWorldCell, ClaimedWorldCells);
        const FIntPoint SpawnLocalCell = WorldCellToSettlementLocalCell(ValidWorldCell);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[SpawnSync] InitialAgentSpawn AgentID=%d SpawnCenter=(%d,%d) Origin=(%d,%d) FormationOffset=(%d,%d) DesiredWorldCell=(%d,%d) ValidWorldCell=(%d,%d) SpawnLocalCell=(%d,%d)"),
            AgentID,
            SpawnGridCenter.X,
            SpawnGridCenter.Y,
            SettlementWorldOrigin.X,
            SettlementWorldOrigin.Y,
            FormationOffset.X,
            FormationOffset.Y,
            DesiredWorldCell.X,
            DesiredWorldCell.Y,
            ValidWorldCell.X,
            ValidWorldCell.Y,
            SpawnLocalCell.X,
            SpawnLocalCell.Y
        );

        ClaimedWorldCells.Add(ValidWorldCell);

        FAgentData NewAgent;
        NewAgent.AgentID = AgentID;
        NewAgent.SettlementID = SettlementID;
        NewAgent.DisplayName = ResolveStartingNameForAgentIndex(i, AgentID);
        NewAgent.State = EAgentState::Idle;
        NewAgent.RaceTag = ResolveStartingRaceForAgentIndex(i);

        // Roll starting personality/gameplay traits for this colonist.
        // Future us: this same generator can later be reused for NPCs, animals,
        // children, inherited traits, and world-spawned entities.
        NewAgent.TraitTags = UWorldForgeTraitGenerator::RollRandomTraitsForNewEntity(
            NewAgent.RaceTag,
            NewAgent.AgentID + 12345,
            1,
            3
        );

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[TraitGen] Agent %d Traits: %s"),
            NewAgent.AgentID,
            *NewAgent.TraitTags.ToString()
        );
        NewAgent.FoodNeed = 100.0f;
        NewAgent.SleepNeed = 100.0f;
        NewAgent.SocialNeed = 70.0f;
        NewAgent.SafetyNeed = 70.0f;
        NewAgent.PurposeNeed = 70.0f;
        NewAgent.MoodScore = 50.0f;
        NewAgent.EthicsPacifism = 50.0f;
        NewAgent.EthicsCompassion = 50.0f;
        NewAgent.EthicsHonour = 50.0f;
        NewAgent.Health = 100.0f;
        NewAgent.bInjured = false;
        NewAgent.ActiveReservationID = -1;
        NewAgent.CurrentGridPos = SpawnLocalCell;
        NewAgent.CurrentNavTarget = SpawnLocalCell;

        ApplyRaceSkillDefaults(NewAgent);
        ApplyDefaultJobPriorities(NewAgent);

        ProcessorMgr->RegisterAgent(NewAgent);
        SettlementMgr->IncrementPopulation(SettlementID);
        SettlementMgr->IncrementRacePopulation(SettlementID, NewAgent.RaceTag);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonyManager] Registered AgentID=%d at LocalCell=(%d,%d) WorldCell=(%d,%d)"),
            NewAgent.AgentID,
            SpawnLocalCell.X,
            SpawnLocalCell.Y,
            ValidWorldCell.X,
            ValidWorldCell.Y
        );
    }

    TMap<int32, int32> AgentCountBySettlement;
    for (const FAgentData& Agent : ProcessorMgr->GetAllAgents())
    {
        AgentCountBySettlement.FindOrAdd(Agent.SettlementID)++;
    }
    AgentCountBySettlement.FindOrAdd(SettlementID);

    const int32 AgentsRegistered = AgentCountBySettlement.FindRef(SettlementID);
    UE_LOG(LogTemp, Log,
        TEXT("[ColonyManager] SetupAgents complete. SettlementID=%d AgentsRegistered=%d"),
        SettlementID,
        AgentsRegistered);

    const TArray<int32> NavGridSettlements = NavContext ? NavContext->GetRegisteredSettlementIDs() : TArray<int32>();
    for (const TPair<int32, int32>& Pair : AgentCountBySettlement)
    {
        const bool bHasNavGrid = NavGridSettlements.Contains(Pair.Key);
        UE_LOG(LogTemp, Log,
            TEXT("[CubeTerrainNavSettlement] Final ActiveSettlementID=%d Agents=%d HasNavGrid=%s"),
            Pair.Key,
            Pair.Value,
            bHasNavGrid ? TEXT("true") : TEXT("false"));
    }
}

FGameplayTag AColonyManager::ResolveStartingRaceForAgentIndex(int32 AgentIndex) const
{
    // Future embark UI path:
    // - If the player explicitly supplies starting colonists, respect those race choices.
    // - If the player later chooses a single default race, respect that default race.
    //
    // MVP/testing fallback:
    // - When no explicit race data exists, spawn a mixed-race starter colony so both
    //   Dwarf and HighElf visual/data paths are exercised immediately.
    if (const UGameInstance* GI = GetGameInstance())
    {
        if (const UWorldForgeEmbarkationManager* Embark = GI->GetSubsystem<UWorldForgeEmbarkationManager>())
        {
            const FWorldForgeEmbarkConfig Config = Embark->GetEmbarkConfig();
            if (Config.StartingColonists.IsValidIndex(AgentIndex) && Config.StartingColonists[AgentIndex].RaceTag.IsValid())
            {
                return Config.StartingColonists[AgentIndex].RaceTag;
            }

            if (Config.StartingColonists.Num() > 0 && Config.DefaultRaceTag.IsValid())
            {
                return Config.DefaultRaceTag;
            }
        }
    }

    const int32 HalfPoint = FMath::Max(1, InitialAgentCount / 2);
    return (AgentIndex < HalfPoint) ? TAG_Race_Dwarf : TAG_Race_HighElf;
}

FString AColonyManager::ResolveStartingNameForAgentIndex(int32 AgentIndex, int32 AgentID) const
{
    if (const UGameInstance* GI = GetGameInstance())
    {
        if (const UWorldForgeEmbarkationManager* Embark = GI->GetSubsystem<UWorldForgeEmbarkationManager>())
        {
            const FWorldForgeEmbarkConfig Config = Embark->GetEmbarkConfig();
            if (Config.StartingColonists.IsValidIndex(AgentIndex) && !Config.StartingColonists[AgentIndex].Name.IsEmpty())
            {
                return Config.StartingColonists[AgentIndex].Name;
            }
        }
    }

    return FString::Printf(TEXT("Colonist %d"), AgentID);
}

void AColonyManager::ApplyRaceSkillDefaults(FAgentData& Agent) const
{
    Agent.Skills.Add(TAG_Skill_Mining, 25.0f);
    Agent.Skills.Add(TAG_Skill_Hauling, 25.0f);
    Agent.Skills.Add(TAG_Skill_Farming, 25.0f);
    Agent.Skills.Add(TAG_Skill_Cooking, 25.0f);
    Agent.Skills.Add(TAG_Skill_Crafting, 25.0f);
    Agent.Skills.Add(TAG_Skill_Building, 25.0f);
    Agent.Skills.Add(TAG_Skill_Research, 25.0f);
    Agent.Skills.Add(TAG_Skill_Medicine, 25.0f);
    Agent.Skills.Add(TAG_Skill_Combat, 25.0f);

    // Lightweight starter flavor until race DataAssets drive full modifiers.
    if (Agent.RaceTag == TAG_Race_Dwarf)
    {
        Agent.Skills.FindOrAdd(TAG_Skill_Mining) += 10.0f;
        Agent.Skills.FindOrAdd(TAG_Skill_Crafting) += 5.0f;
        Agent.MaxHealth = 110.0f;
        Agent.Health = Agent.MaxHealth;
    }
    else if (Agent.RaceTag == TAG_Race_HighElf)
    {
        Agent.Skills.FindOrAdd(TAG_Skill_Farming) += 5.0f;
        Agent.Skills.FindOrAdd(TAG_Skill_Research) += 10.0f;
        Agent.SocialNeed = 75.0f;
    }
    else if (Agent.RaceTag == TAG_Race_Human)
    {
        Agent.Skills.FindOrAdd(TAG_Skill_Building) += 5.0f;
        Agent.Skills.FindOrAdd(TAG_Skill_Hauling) += 5.0f;
    }

    for (TPair<FGameplayTag, float>& Pair : Agent.Skills)
    {
        Pair.Value = FMath::Clamp(Pair.Value, 0.0f, 100.0f);
    }
}

void AColonyManager::ApplyDefaultJobPriorities(FAgentData& Agent) const
{
    // MVP/testing: every known job starts at the highest priority so any
    // player/system-created work designation is picked up immediately.
    // Future UI can lower/disable these per colonist.
    Agent.JobPriorities.Add(TAG_Job_Eat, 4);
    Agent.JobPriorities.Add(TAG_Job_Sleep, 4);
    Agent.JobPriorities.Add(TAG_Job_Haul, 4);
    Agent.JobPriorities.Add(TAG_Job_Build, 4);
    Agent.JobPriorities.Add(TAG_Job_Mine, 4);
    Agent.JobPriorities.Add(TAG_Job_Harvest_Food, 4);
    Agent.JobPriorities.Add(TAG_Job_Harvest_Wood, 4);
    Agent.JobPriorities.Add(TAG_Job_Harvest_Stone, 4);
    Agent.JobPriorities.Add(TAG_Job_Cook, 4);
    Agent.JobPriorities.Add(TAG_Job_Craft, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_PrepSoil, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_Plant, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_Water, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_Harvest, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_Clear, 4);
    Agent.JobPriorities.Add(TAG_Job_Farm_Fertilize, 4);
}

void AColonyManager::SetupInitialResources()
{
    if (!SettlementMgr || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            TEXT("Settlement manager missing or invalid settlement"));
        return;
    }

    // Tiny legacy fallback stockpile so old tests still work even if embark loadout fails.
    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Food, 5);
    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Wood, 2);
    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Stone, 2);

    ApplyEmbarkLoadoutSeeds();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] Embark loadout skipped: World is null."));
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            TEXT("World is null"));
        return;
    }

    UEmbarkLoadoutSubsystem* EmbarkLoadout = World->GetSubsystem<UEmbarkLoadoutSubsystem>();
    if (!EmbarkLoadout)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] Embark loadout skipped: UEmbarkLoadoutSubsystem missing."));
    }
    else
    {
        const bool bApplied = EmbarkLoadout->ApplyDebugStarterLoadout(SettlementID);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonyManager] Embark starter loadout applied=%s SettlementID=%d"),
            bApplied ? TEXT("true") : TEXT("false"),
            SettlementID
        );
    }

    UFarmingSubsystem* FarmingSubsystem = World->GetSubsystem<UFarmingSubsystem>();
    if (!FarmingSubsystem)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            TEXT("FarmingSubsystem missing"));
        return;
    }

    constexpr int32 StarterFarmSizeX = 3;
    constexpr int32 StarterFarmSizeY = 3;

    if (NavigationGridWidth < StarterFarmSizeX || NavigationGridHeight < StarterFarmSizeY)
    {
        const FString Reason = FString::Printf(
            TEXT("Navigation grid too small for %dx%d farm"),
            StarterFarmSizeX,
            StarterFarmSizeY);
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            *Reason);
        return;
    }

    ECropCategory StarterFarmCategory = ECropCategory::Grains;
    const FName StarterFarmCropRow = FarmingSubsystem->ResolveDefaultPlayerFarmCropRow(
        SettlementID,
        StarterFarmCategory);

    if (StarterFarmCropRow.IsNone())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            TEXT("No valid starter crop row"));
        return;
    }

    const FIntPoint DesiredFarmWorldOrigin = SpawnGridCenter + FIntPoint(5, 0);
    FIntPoint StarterFarmOrigin = WorldCellToSettlementLocalCell(DesiredFarmWorldOrigin);
    StarterFarmOrigin.X = FMath::Clamp(StarterFarmOrigin.X, 0, NavigationGridWidth - StarterFarmSizeX);
    StarterFarmOrigin.Y = FMath::Clamp(StarterFarmOrigin.Y, 0, NavigationGridHeight - StarterFarmSizeY);

    const int32 ZoneID = FarmingSubsystem->CreateFarmZone(
        SettlementID,
        StarterFarmOrigin,
        StarterFarmSizeX,
        StarterFarmSizeY,
        StarterFarmCategory,
        StarterFarmCropRow,
        true);

    if (ZoneID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[PlayerStarterFarm] Failed to create starter farm zone. SettlementID=%d Reason=%s"),
            SettlementID,
            TEXT("CreateFarmZone returned INDEX_NONE"));
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[PlayerStarterFarm] Created starter farm zone. SettlementID=%d ZoneID=%d Origin=(%d,%d) Size=%dx%d CropRow=%s"),
        SettlementID,
        ZoneID,
        StarterFarmOrigin.X,
        StarterFarmOrigin.Y,
        StarterFarmSizeX,
        StarterFarmSizeY,
        *StarterFarmCropRow.ToString());
}


void AColonyManager::ApplyEmbarkLoadoutSeeds()
{
    if (!SettlementMgr || SettlementID == INDEX_NONE)
    {
        return;
    }

    const UGameInstance* GameInstance = GetGameInstance();
    const UWorldForgeEmbarkationManager* EmbarkationManager = GameInstance
        ? GameInstance->GetSubsystem<UWorldForgeEmbarkationManager>()
        : nullptr;

    if (!EmbarkationManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] Starting seed inventory skipped: EmbarkationManager missing."));
        return;
    }

    const FWorldForgeEmbarkConfig Config = EmbarkationManager->GetEmbarkConfig();
    bool bAppliedAnySeed = false;

    for (const FEmbarkSelectedItem& Item : Config.SelectedLoadoutItems)
    {
        if (Item.Quantity <= 0)
        {
            continue;
        }

        FName CropRowName = NAME_None;
        FString SeedIdentifier;
        FString CropDisplayName;
        if (!WF_TryResolveSeedLoadoutItem(Item.ItemId, CropRowName, SeedIdentifier, CropDisplayName))
        {
            continue;
        }

        if (!SettlementMgr->AddStartingSeedInventory(SettlementID, CropRowName, Item.Quantity))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[ColonyManager] Failed to add starting seed inventory: %s Qty=%d SettlementID=%d"),
                *CropDisplayName,
                Item.Quantity,
                SettlementID);
            continue;
        }

        const FGameplayTag SpecificSeedTag = FGameplayTag::RequestGameplayTag(
            FName(*FString::Printf(TEXT("WorldForge.Item.Seed.%s"), *SeedIdentifier)),
            false);
        if (SpecificSeedTag.IsValid())
        {
            SettlementMgr->AddToStockpile(SettlementID, SpecificSeedTag, Item.Quantity);
        }

        bAppliedAnySeed = true;

        UE_LOG(LogTemp, Log,
            TEXT("[ColonyManager] Starting seed inventory: %s Qty=%d"),
            *CropDisplayName,
            Item.Quantity);

        UE_LOG(LogTemp, Log,
            TEXT("[EmbarkLoadout] Applying seed item %s Qty=%d CropRow=%s"),
            *Item.ItemId.ToString(),
            Item.Quantity,
            *CropRowName.ToString());
    }

    if (!bAppliedAnySeed)
    {
        UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Starting seed inventory: none selected; player farms will use fallback crop."));
    }
}

void AColonyManager::StartSimulation()
{
    if (!SimTimeMgr)
    {
        return;
    }

    if (!SimTimeMgr->IsRunning())
    {
        SimTimeMgr->StartSimulation();
    }
}

void AColonyManager::BindDebugInput()
{
    if (!bEnableDebugKeyInput)
    {
        return;
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0);
    if (!PC)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] No PlayerController found for debug input."));
        return;
    }

    EnableInput(PC);

    if (!InputComponent)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] InputComponent was not created after EnableInput."));
        return;
    }

    InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AColonyManager::HandleSaveInput);
    InputComponent->BindKey(EKeys::L, IE_Pressed, this, &AColonyManager::HandleLoadInput);
    InputComponent->BindKey(EKeys::T, IE_Pressed, this, &AColonyManager::HandlePlaceBuildingInput);
    InputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AColonyManager::HandleBuildTaskInput);
}

bool AColonyManager::TryGetSelectedStartTile(int32& OutTileX, int32& OutTileY, int32& OutTileIndex) const
{
    OutTileX = INDEX_NONE;
    OutTileY = INDEX_NONE;
    OutTileIndex = INDEX_NONE;

    const UWorld* World = GetWorld();
    if (!World)
    {
        return false;
    }

    const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>();
    if (!WorldMapSubsystem)
    {
        return false;
    }

    if (!WorldMapSubsystem->GetSelectedStartTile(OutTileX, OutTileY, OutTileIndex))
    {
        return false;
    }

    return OutTileX >= 0 && OutTileY >= 0 && OutTileIndex >= 0;
}

FIntPoint AColonyManager::ResolveInitialSettlementWorldOrigin() const
{
    FIntPoint DesiredWorldCell(
        FMath::Max(0, NavigationGridWidth / 2),
        FMath::Max(0, NavigationGridHeight / 2)
    );
    const TCHAR* SourceLabel = TEXT("MapCenter");

    int32 SelectedTileX = INDEX_NONE;
    int32 SelectedTileY = INDEX_NONE;
    int32 SelectedTileIndex = INDEX_NONE;

    if (TryGetSelectedStartTile(SelectedTileX, SelectedTileY, SelectedTileIndex))
    {
        DesiredWorldCell = FIntPoint(SelectedTileX, SelectedTileY);
        SourceLabel = TEXT("PlayerSelectedTile");
    }

    if (!bPreferMapCenterOnStart && FCString::Strcmp(SourceLabel, TEXT("PlayerSelectedTile")) != 0)
    {
        DesiredWorldCell = SettlementWorldOrigin;
        SourceLabel = TEXT("Default");
    }

    if (bAnchorSettlementToPlayerViewOnStart && FCString::Strcmp(SourceLabel, TEXT("PlayerSelectedTile")) != 0 && VoxelWorldMgr)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

            DesiredWorldCell = VoxelWorldMgr->WorldToTerrainCell(ViewLocation);
            SourceLabel = TEXT("PlayerView");
        }
    }

    if (VoxelWorldMgr && VoxelWorldMgr->HasCachedTerrainData())
    {
        DesiredWorldCell = VoxelWorldMgr->ClampTerrainCellToBounds(DesiredWorldCell);

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonyManager] Light clamp applied. Result=(%d,%d)"),
            DesiredWorldCell.X,
            DesiredWorldCell.Y
        );
    }

    const FIntPoint ResolvedWorldCell = ResolveValidSpawnCell(DesiredWorldCell);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Initial settlement origin resolved. Source=%s Desired=(%d,%d) Resolved=(%d,%d)"),
        SourceLabel,
        DesiredWorldCell.X,
        DesiredWorldCell.Y,
        ResolvedWorldCell.X,
        ResolvedWorldCell.Y
    );

    return ResolvedWorldCell;
}

FIntPoint AColonyManager::ComputeSettlementOriginFromSpawnCenter(const FIntPoint& InSpawnCenter) const
{
    const int32 GridSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InitialAgentCount)));
    const int32 Spacing = FMath::Max(1, SpawnSpacingCells);

    const int32 FormationWidth = FMath::Max(1, GridSize * Spacing);
    const int32 FormationHeight = FMath::Max(1, FMath::CeilToInt(static_cast<float>(InitialAgentCount) / GridSize) * Spacing);

    FIntPoint Origin(
        InSpawnCenter.X - (FormationWidth / 2),
        InSpawnCenter.Y - (FormationHeight / 2)
    );

    Origin.X = FMath::Clamp(Origin.X, 0, FMath::Max(0, NavigationGridWidth - 1));
    Origin.Y = FMath::Clamp(Origin.Y, 0, FMath::Max(0, NavigationGridHeight - 1));

    return Origin;
}

void AColonyManager::PositionPlayerCameraOverSettlement()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    for (TActorIterator<AWorldGenBootstrapActor> It(World); It; ++It)
    {
        AWorldGenBootstrapActor* BootstrapActor = *It;
        if (IsValid(BootstrapActor) &&
            !BootstrapActor->IsActorBeingDestroyed() &&
            BootstrapActor->IsPrePlayBlocking())
        {
            UE_LOG(LogTemp, Log, TEXT("[TerrainVisibility] ColonyManager camera snap deferred; Bootstrap pre-play block active."));
            return;
        }
    }

    APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
    if (!PC)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] No PlayerController found."));
        return;
    }

    AWorldForgeCamera* CameraPawn = Cast<AWorldForgeCamera>(PC->GetPawn());

    if (!CameraPawn)
    {
        for (TActorIterator<AWorldForgeCamera> It(World); It; ++It)
        {
            CameraPawn = *It;
            break;
        }
    }

    if (!CameraPawn)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] No WorldForgeCamera found."));
        return;
    }

    FVector CameraAnchorWorld = FVector::ZeroVector;
    FIntPoint CameraFocusTile(INDEX_NONE, INDEX_NONE);
    bool bHasCameraWarmupTarget = false;

    if (bUseManualCameraStart)
    {
        CameraAnchorWorld = ManualCameraLocation;
    }
    else if (VoxelWorldMgr)
    {
        const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>();

        struct FSpawnSyncFocusTile
        {
            const TCHAR* Source = TEXT("WorldCenter");
            FIntPoint Tile = FIntPoint::ZeroValue;
            bool bValid = false;
        };

        FSpawnSyncFocusTile FocusTile;

        auto TryUseFocusTile = [&](const TCHAR* Source, const FIntPoint& CandidateTile) -> bool
            {
                if (!VoxelWorldMgr)
                {
                    return false;
                }

                FIntPoint ResolvedTile = CandidateTile;
                if (VoxelWorldMgr->HasCachedTerrainData())
                {
                    ResolvedTile = VoxelWorldMgr->ClampTerrainCellToBounds(ResolvedTile);
                }

                if (WorldMapSubsystem)
                {
                    int32 TileIndex = INDEX_NONE;
                    if (!WorldMapSubsystem->TileXYToIndex(ResolvedTile.X, ResolvedTile.Y, TileIndex))
                    {
                        return false;
                    }
                }
                else if (ResolvedTile.X < 0 || ResolvedTile.Y < 0)
                {
                    return false;
                }

                FocusTile.Source = Source;
                FocusTile.Tile = ResolvedTile;
                FocusTile.bValid = true;
                return true;
            };

        int32 SelectedTileX = INDEX_NONE;
        int32 SelectedTileY = INDEX_NONE;
        int32 SelectedTileIndex = INDEX_NONE;
        if (TryGetSelectedStartTile(SelectedTileX, SelectedTileY, SelectedTileIndex) && SelectedTileIndex != INDEX_NONE)
        {
            TryUseFocusTile(TEXT("SelectedEmbarkTile"), FIntPoint(SelectedTileX, SelectedTileY));
        }

        if (!FocusTile.bValid && SettlementMgr && SettlementID != INDEX_NONE && WorldMapSubsystem)
        {
            const FSettlementState* Settlement = SettlementMgr->GetSettlement(SettlementID);
            int32 SettlementTileX = INDEX_NONE;
            int32 SettlementTileY = INDEX_NONE;

            if (Settlement &&
                Settlement->WorldTileID != INDEX_NONE &&
                WorldMapSubsystem->TileIndexToXY(Settlement->WorldTileID, SettlementTileX, SettlementTileY))
            {
                TryUseFocusTile(TEXT("PlayerSettlementTile"), FIntPoint(SettlementTileX, SettlementTileY));
            }
        }

        if (!FocusTile.bValid)
        {
            TryUseFocusTile(TEXT("InitialClaimTile"), SpawnGridCenter);
        }

        if (!FocusTile.bValid)
        {
            const FIntPoint WorldCenter(
                WorldMapSubsystem ? FMath::Max(0, WorldMapSubsystem->GetGridWidth() / 2) : FMath::Max(0, NavigationGridWidth / 2),
                WorldMapSubsystem ? FMath::Max(0, WorldMapSubsystem->GetGridHeight() / 2) : FMath::Max(0, NavigationGridHeight / 2)
            );

            TryUseFocusTile(TEXT("WorldCenter"), WorldCenter);
        }

        if (!FocusTile.bValid)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[TerrainVisibility] WARNING: Camera warmup target invalid; falling back to immediate camera focus."));
            CameraAnchorWorld = CameraPawn->GetActorLocation();
        }
        else
        {
            CameraFocusTile = FocusTile.Tile;
            bHasCameraWarmupTarget = true;

            const bool bHasSurfaceWorld = VoxelWorldMgr->TryGetSurfaceWorldLocation(
                FocusTile.Tile,
                CameraAnchorWorld,
                VoxelWorldMgr->TerrainSurfaceZOffset);

            if (!bHasSurfaceWorld)
            {
                CameraAnchorWorld = VoxelWorldMgr->TerrainCellToWorld(FocusTile.Tile, 0.0f, true);
            }

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[SpawnSync] ZCheck CameraSurfaceZ=%.1f"),
                CameraAnchorWorld.Z
            );

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[SpawnSync] CameraFocus Source=%s Tile=(%d,%d) World=(%.1f,%.1f,%.1f)"),
                FocusTile.Source,
                FocusTile.Tile.X,
                FocusTile.Tile.Y,
                CameraAnchorWorld.X,
                CameraAnchorWorld.Y,
                CameraAnchorWorld.Z
            );
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] VoxelWorldMgr null. Cannot resolve camera position."));
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[SpawnSync] CameraAnchor=(X=%.1f,Y=%.1f,Z=%.1f) SettlementOrigin=(%d,%d) SpawnGridCenter=(%d,%d) WorldTile=(%d,%d)"),
        CameraAnchorWorld.X,
        CameraAnchorWorld.Y,
        CameraAnchorWorld.Z,
        SettlementWorldOrigin.X,
        SettlementWorldOrigin.Y,
        SpawnGridCenter.X,
        SpawnGridCenter.Y,
        CameraFocusTile.X,
        CameraFocusTile.Y);

    if (ProcessorMgr)
    {
        for (const FAgentData& Agent : ProcessorMgr->GetAllAgents())
        {
            if (Agent.SettlementID == SettlementID)
            {
                const FIntPoint ExpectedWorldCell = SettlementWorldOrigin + Agent.CurrentGridPos;
                UE_LOG(LogTemp, Log,
                    TEXT("[SpawnSync] FirstAgentLocal=(%d,%d) ExpectedWorldCell=(%d,%d)"),
                    Agent.CurrentGridPos.X,
                    Agent.CurrentGridPos.Y,
                    ExpectedWorldCell.X,
                    ExpectedWorldCell.Y);
                break;
            }
        }
    }

    auto SnapCameraNow = [this](APlayerController* InPC, AWorldForgeCamera* InCameraPawn, const FVector& InCameraAnchorWorld)
        {
            if (!InPC || !InCameraPawn)
            {
                return;
            }

            if (InPC->GetPawn() != InCameraPawn)
            {
                InPC->Possess(InCameraPawn);
                UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Possessed camera pawn: %s"), *InCameraPawn->GetName());
            }

            InCameraPawn->SnapToRTSView(
                InCameraAnchorWorld,
                RTSCameraYaw,
                RTSCameraPitch,
                RTSCameraHeight,
                true
            );

            InPC->SetViewTarget(InCameraPawn);
            InPC->SetControlRotation(FRotator(RTSCameraPitch, RTSCameraYaw, 0.0f));

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ColonyManager] Camera positioned at (%.1f, %.1f, %.1f). Manual=%s Camera=%s ViewTarget=%s"),
                InCameraAnchorWorld.X,
                InCameraAnchorWorld.Y,
                InCameraAnchorWorld.Z,
                bUseManualCameraStart ? TEXT("true") : TEXT("false"),
                *InCameraPawn->GetName(),
                InPC->GetViewTarget() ? *InPC->GetViewTarget()->GetName() : TEXT("None")
            );
        };

    const bool bShouldDelayCameraSnap =
        !bUseManualCameraStart &&
        bHasCameraWarmupTarget &&
        CameraMeshWarmupDelaySeconds > KINDA_SMALL_NUMBER;

    if (bShouldDelayCameraSnap)
    {
        World->GetTimerManager().ClearTimer(CameraWarmupTimerHandle);

        if (PC->GetViewTarget() != CameraPawn)
        {
            CameraPawn->SnapToRTSView(
                CameraAnchorWorld,
                RTSCameraYaw,
                RTSCameraPitch,
                RTSCameraHeight,
                true
            );
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[TerrainVisibility] TerrainReady received. Pending camera warmup for Tile=(%d,%d) World=(%.1f,%.1f,%.1f) Delay=%.2f"),
            CameraFocusTile.X,
            CameraFocusTile.Y,
            CameraAnchorWorld.X,
            CameraAnchorWorld.Y,
            CameraAnchorWorld.Z,
            CameraMeshWarmupDelaySeconds
        );

        const TWeakObjectPtr<AColonyManager> WeakSelf(this);
        const TWeakObjectPtr<APlayerController> WeakPC(PC);
        const TWeakObjectPtr<AWorldForgeCamera> WeakCameraPawn(CameraPawn);
        const FTimerDelegate CameraWarmupDelegate = FTimerDelegate::CreateLambda(
            [WeakSelf, WeakPC, WeakCameraPawn, CameraFocusTile, CameraAnchorWorld]()
            {
                AColonyManager* StrongSelf = WeakSelf.Get();
                APlayerController* WarmupPC = WeakPC.Get();
                AWorldForgeCamera* WarmupCameraPawn = WeakCameraPawn.Get();

                const bool bInvalidWarmupTarget =
                    !StrongSelf ||
                    !WarmupPC ||
                    !WarmupCameraPawn ||
                    CameraFocusTile.X == INDEX_NONE ||
                    CameraFocusTile.Y == INDEX_NONE ||
                    CameraAnchorWorld.ContainsNaN();

                if (bInvalidWarmupTarget)
                {
                    UE_LOG(
                        LogTemp,
                        Warning,
                        TEXT("[TerrainVisibility] WARNING: Camera warmup target invalid; falling back to immediate camera focus."));

                    if (StrongSelf && WarmupPC && WarmupCameraPawn)
                    {
                        const FVector FallbackCameraWorld =
                            StrongSelf->bUseManualCameraStart ? StrongSelf->ManualCameraLocation : WarmupCameraPawn->GetActorLocation();

                        if (WarmupPC->GetPawn() != WarmupCameraPawn)
                        {
                            WarmupPC->Possess(WarmupCameraPawn);
                            UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Possessed camera pawn: %s"), *WarmupCameraPawn->GetName());
                        }

                        WarmupCameraPawn->SnapToRTSView(
                            FallbackCameraWorld,
                            StrongSelf->RTSCameraYaw,
                            StrongSelf->RTSCameraPitch,
                            StrongSelf->RTSCameraHeight,
                            true
                        );

                        WarmupPC->SetViewTarget(WarmupCameraPawn);
                        WarmupPC->SetControlRotation(FRotator(StrongSelf->RTSCameraPitch, StrongSelf->RTSCameraYaw, 0.0f));
                    }
                    return;
                }

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[TerrainVisibility] CameraWarmup complete. Snapping camera now. Tile=(%d,%d) World=(%.1f,%.1f,%.1f)"),
                    CameraFocusTile.X,
                    CameraFocusTile.Y,
                    CameraAnchorWorld.X,
                    CameraAnchorWorld.Y,
                    CameraAnchorWorld.Z
                );

                if (WarmupPC->GetPawn() != WarmupCameraPawn)
                {
                    WarmupPC->Possess(WarmupCameraPawn);
                    UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Possessed camera pawn: %s"), *WarmupCameraPawn->GetName());
                }

                WarmupCameraPawn->SnapToRTSView(
                    CameraAnchorWorld,
                    StrongSelf->RTSCameraYaw,
                    StrongSelf->RTSCameraPitch,
                    StrongSelf->RTSCameraHeight,
                    true
                );

                WarmupPC->SetViewTarget(WarmupCameraPawn);
                WarmupPC->SetControlRotation(FRotator(StrongSelf->RTSCameraPitch, StrongSelf->RTSCameraYaw, 0.0f));

                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[ColonyManager] Camera positioned at (%.1f, %.1f, %.1f). Manual=%s Camera=%s ViewTarget=%s"),
                    CameraAnchorWorld.X,
                    CameraAnchorWorld.Y,
                    CameraAnchorWorld.Z,
                    StrongSelf->bUseManualCameraStart ? TEXT("true") : TEXT("false"),
                    *WarmupCameraPawn->GetName(),
                    WarmupPC->GetViewTarget() ? *WarmupPC->GetViewTarget()->GetName() : TEXT("None")
                );
            });

        World->GetTimerManager().SetTimer(
            CameraWarmupTimerHandle,
            CameraWarmupDelegate,
            CameraMeshWarmupDelaySeconds,
            false
        );
        return;
    }

    SnapCameraNow(PC, CameraPawn, CameraAnchorWorld);
}

FIntPoint AColonyManager::ComputeInitialSpawnCell(int32 AgentIndex) const
{
    const int32 GridSize = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(InitialAgentCount)));
    const int32 Spacing = FMath::Max(1, SpawnSpacingCells);

    const int32 X = (AgentIndex % GridSize) * Spacing;
    const int32 Y = (AgentIndex / GridSize) * Spacing;

    return FIntPoint(X, Y);
}

FIntPoint AColonyManager::ResolveValidSpawnCell(const FIntPoint& DesiredCell) const
{
    if (!VoxelWorldMgr)
    {
        return DesiredCell;
    }

    const int32 EffectiveSearchRadius = FMath::Max(SpawnSearchRadius, CameraAnchorSearchRadius);

    FVector CellSnapLocation = FVector::ZeroVector;
    FIntPoint ResolvedCell = DesiredCell;
    if (VoxelWorldMgr->TryGetNearestSpawnLocationForTerrainCell(
        DesiredCell,
        CellSnapLocation,
        ResolvedCell,
        EffectiveSearchRadius,
        VoxelWorldMgr->TerrainSurfaceZOffset))
    {
        if (ResolvedCell != DesiredCell)
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[CubeTerrainSnap] ColonyManager resolved spawn cell via terrain snap. Desired=(%d,%d) Resolved=(%d,%d) World=%s"),
                DesiredCell.X,
                DesiredCell.Y,
                ResolvedCell.X,
                ResolvedCell.Y,
                *CellSnapLocation.ToString()
            );
        }

        return ResolvedCell;
    }

    if (VoxelWorldMgr->IsSpawnableTerrainCell(DesiredCell))
    {
        return DesiredCell;
    }

    FIntPoint FoundCell = DesiredCell;
    if (VoxelWorldMgr->FindNearestSpawnableTerrainCell(DesiredCell, FoundCell, EffectiveSearchRadius))
    {
        return FoundCell;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ColonyManager] Failed to find valid terrain near (%d,%d). Using desired cell."),
        DesiredCell.X,
        DesiredCell.Y
    );

    return DesiredCell;
}

FIntPoint AColonyManager::ResolveUniqueValidSpawnCell(const FIntPoint& DesiredCell, const TSet<FIntPoint>& ClaimedWorldCells) const
{
    if (IsWorldCellAvailableForSpawn(DesiredCell, ClaimedWorldCells))
    {
        return DesiredCell;
    }

    const int32 EffectiveSearchRadius = FMath::Max(SpawnSearchRadius, CameraAnchorSearchRadius);

    for (int32 Radius = 1; Radius <= EffectiveSearchRadius; ++Radius)
    {
        for (int32 Y = DesiredCell.Y - Radius; Y <= DesiredCell.Y + Radius; ++Y)
        {
            for (int32 X = DesiredCell.X - Radius; X <= DesiredCell.X + Radius; ++X)
            {
                const bool bOnRing =
                    (X == DesiredCell.X - Radius) ||
                    (X == DesiredCell.X + Radius) ||
                    (Y == DesiredCell.Y - Radius) ||
                    (Y == DesiredCell.Y + Radius);

                if (!bOnRing)
                {
                    continue;
                }

                const FIntPoint Candidate(X, Y);
                if (IsWorldCellAvailableForSpawn(Candidate, ClaimedWorldCells))
                {
                    return Candidate;
                }
            }
        }
    }

    const FIntPoint FallbackCell = ResolveValidSpawnCell(DesiredCell);

    if (!ClaimedWorldCells.Contains(FallbackCell))
    {
        return FallbackCell;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[ColonyManager] Unique spawn fallback collided near Desired=(%d,%d). Returning desired cell as last resort."),
        DesiredCell.X,
        DesiredCell.Y
    );

    return DesiredCell;
}

bool AColonyManager::IsWorldCellAvailableForSpawn(const FIntPoint& WorldCell, const TSet<FIntPoint>& ClaimedWorldCells) const
{
    if (ClaimedWorldCells.Contains(WorldCell))
    {
        return false;
    }

    if (!VoxelWorldMgr)
    {
        return true;
    }

    FVector CellSnapLocation = FVector::ZeroVector;
    FIntPoint ResolvedCell = WorldCell;
    if (VoxelWorldMgr->TryGetNearestSpawnLocationForTerrainCell(
        WorldCell,
        CellSnapLocation,
        ResolvedCell,
        0,
        VoxelWorldMgr->TerrainSurfaceZOffset))
    {
        return ResolvedCell == WorldCell;
    }

    return VoxelWorldMgr->IsSpawnableTerrainCell(WorldCell);
}

FIntPoint AColonyManager::WorldCellToSettlementLocalCell(const FIntPoint& WorldCell) const
{
    return WorldCell - SettlementWorldOrigin;
}

int32 AColonyManager::GetNextAgentID() const
{
    if (!ProcessorMgr)
    {
        return 0;
    }

    int32 MaxAgentID = -1;
    for (const FAgentData& Agent : ProcessorMgr->GetAllAgents())
    {
        MaxAgentID = FMath::Max(MaxAgentID, Agent.AgentID);
    }

    return MaxAgentID + 1;
}

void AColonyManager::HandleSaveInput()
{
    if (!SaveSubsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] SaveSubsystem missing."));
        return;
    }

    SaveSubsystem->SaveGame(SaveSlotName, GetWorld());
    UKismetSystemLibrary::PrintString(
        this,
        FString::Printf(TEXT("Saved %s"), *SaveSlotName),
        true,
        true,
        FLinearColor(0.0f, 0.66f, 1.0f),
        2.0f
    );
}

void AColonyManager::HandleLoadInput()
{
    if (!SaveSubsystem)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] SaveSubsystem missing."));
        return;
    }

    SaveSubsystem->LoadGame(SaveSlotName, GetWorld());
    UKismetSystemLibrary::PrintString(
        this,
        FString::Printf(TEXT("Loaded %s"), *SaveSlotName),
        true,
        true,
        FLinearColor(0.0f, 0.66f, 1.0f),
        2.0f
    );
}

void AColonyManager::HandlePlaceBuildingInput()
{
    UKismetSystemLibrary::PrintString(
        this,
        TEXT("T pressed"),
        true,
        true,
        FLinearColor(0.0f, 0.66f, 1.0f),
        2.0f
    );
}

void AColonyManager::HandleBuildTaskInput()
{
    if (!VoxelConstructionSys || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] Cannot start build task: construction system missing or settlement invalid."));
        return;
    }

    const FIntPoint BuildOriginWorld = FIntPoint(SpawnGridCenter.X + 6, SpawnGridCenter.Y + 2);
    const FIntPoint BuildOriginLocal = BuildOriginWorld - SettlementWorldOrigin;

    const int32 TaskID = VoxelConstructionSys->StartBuildTask(
        SettlementID,
        TAG_Building_Storage_Stockpile,
        BuildOriginLocal,
        -1
    );

    UKismetSystemLibrary::PrintString(
        this,
        TaskID >= 0
        ? FString::Printf(
            TEXT("Started build task %d at Local=(%d,%d) World=(%d,%d)"),
            TaskID,
            BuildOriginLocal.X,
            BuildOriginLocal.Y,
            BuildOriginWorld.X,
            BuildOriginWorld.Y
        )
        : TEXT("Build task failed"),
        true,
        true,
        FLinearColor(0.0f, 0.66f, 1.0f),
        2.0f
    );
}
