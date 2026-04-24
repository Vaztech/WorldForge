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

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

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

    const FIntPoint ResolvedSpawnCenter = ResolveInitialSettlementWorldOrigin();
    SpawnGridCenter = ResolvedSpawnCenter;
    SettlementWorldOrigin = ComputeSettlementOriginFromSpawnCenter(SpawnGridCenter);

    SettlementID = SettlementMgr->CreateSettlement(SettlementName, SettlementWorldTileID);
    ProcessorMgr->RegisterSettlement(SettlementID);
    SettlementMgr->SetPlayerCurrency(SettlementID, TAG_Currency_Barter);

    if (FSettlementState* Settlement = SettlementMgr->GetMutableSettlement(SettlementID))
    {
        Settlement->WorldOrigin = SettlementWorldOrigin;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Created settlement '%s' with ID=%d at WorldOrigin=(%d,%d) SpawnCenter=(%d,%d)"),
        *SettlementName,
        SettlementID,
        SettlementWorldOrigin.X,
        SettlementWorldOrigin.Y,
        SpawnGridCenter.X,
        SpawnGridCenter.Y
    );
}

void AColonyManager::InitializeNavigation()
{
    if (!NavContext || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] InitializeNavigation failed."));
        return;
    }

    NavContext->InitializeGrid(SettlementID, NavigationGridWidth, NavigationGridHeight);
}

void AColonyManager::SetupAgents()
{
    if (!ProcessorMgr || !SettlementMgr || SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] SetupAgents failed."));
        return;
    }

    const int32 StartingAgentID = GetNextAgentID();
    TSet<FIntPoint> ClaimedWorldCells;

    for (int32 i = 0; i < InitialAgentCount; ++i)
    {
        const int32 AgentID = StartingAgentID + i;

        const FIntPoint DesiredLocalCell = ComputeInitialSpawnCell(i);
        const FIntPoint DesiredWorldCell = SettlementWorldOrigin + DesiredLocalCell;
        const FIntPoint ValidWorldCell = ResolveUniqueValidSpawnCell(DesiredWorldCell, ClaimedWorldCells);
        const FIntPoint SpawnLocalCell = WorldCellToSettlementLocalCell(ValidWorldCell);

        ClaimedWorldCells.Add(ValidWorldCell);

        FAgentData NewAgent;
        NewAgent.AgentID = AgentID;
        NewAgent.SettlementID = SettlementID;
        NewAgent.State = EAgentState::Idle;
        NewAgent.RaceTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")));
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

        NewAgent.Skills.Add(TAG_Skill_Mining, 25.0f);
        NewAgent.Skills.Add(TAG_Skill_Hauling, 25.0f);
        NewAgent.Skills.Add(TAG_Skill_Farming, 25.0f);
        NewAgent.Skills.Add(TAG_Skill_Cooking, 25.0f);
        NewAgent.Skills.Add(TAG_Skill_Crafting, 25.0f);
        NewAgent.Skills.Add(TAG_Skill_Building, 25.0f);

        ProcessorMgr->RegisterAgent(NewAgent);
        SettlementMgr->IncrementPopulation(SettlementID);

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
}

void AColonyManager::SetupInitialResources()
{
    if (!SettlementMgr || SettlementID == INDEX_NONE)
    {
        return;
    }

    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Food, 5);
    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Wood, 2);
    SettlementMgr->AddToStockpile(SettlementID, TAG_Item_Stone, 2);
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

FIntPoint AColonyManager::ResolveInitialSettlementWorldOrigin() const
{
    FIntPoint DesiredWorldCell(
        FMath::Max(0, NavigationGridWidth / 2),
        FMath::Max(0, NavigationGridHeight / 2)
    );
    const TCHAR* SourceLabel = TEXT("MapCenter");

    if (const UWorld* World = GetWorld())
    {
        if (const UWorldMapSubsystem* WorldMapSubsystem = World->GetSubsystem<UWorldMapSubsystem>())
        {
            int32 SelectedTileX = INDEX_NONE;
            int32 SelectedTileY = INDEX_NONE;
            int32 SelectedTileIndex = INDEX_NONE;

            if (WorldMapSubsystem->GetSelectedStartTile(SelectedTileX, SelectedTileY, SelectedTileIndex))
            {
                DesiredWorldCell = FIntPoint(SelectedTileX, SelectedTileY);
                SourceLabel = TEXT("PlayerSelectedTile");
            }
        }
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

void AColonyManager::PositionPlayerCameraOverSettlement() const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
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

    if (PC->GetPawn() != CameraPawn)
    {
        PC->Possess(CameraPawn);
        UE_LOG(LogTemp, Log, TEXT("[ColonyManager] Possessed camera pawn: %s"), *CameraPawn->GetName());
    }

    FVector CameraAnchorWorld = FVector::ZeroVector;

    if (bUseManualCameraStart)
    {
        CameraAnchorWorld = ManualCameraLocation;
    }
    else if (VoxelWorldMgr)
    {
        // IMPORTANT:
        // Use the player's resolved spawn-center cell, not SettlementWorldOrigin.
        // SettlementWorldOrigin is the formation origin/top-left area for agents,
        // which is why the camera was offset into the wrong place.
        const FIntPoint DesiredCameraCell = SpawnGridCenter;
        FIntPoint ResolvedCameraCell = DesiredCameraCell;

        if (!VoxelWorldMgr->TryGetNearestSpawnLocationForTerrainCell(
            DesiredCameraCell,
            CameraAnchorWorld,
            ResolvedCameraCell,
            FMath::Max(SpawnSearchRadius, CameraAnchorSearchRadius),
            VoxelWorldMgr->TerrainSurfaceZOffset))
        {
            if (!VoxelWorldMgr->TryGetSurfaceWorldLocation(DesiredCameraCell, CameraAnchorWorld, VoxelWorldMgr->TerrainSurfaceZOffset))
            {
                CameraAnchorWorld = VoxelWorldMgr->TerrainCellToWorld(DesiredCameraCell, 0.0f, true);

                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[ColonyManager] Camera anchor fallback used. DesiredCell=(%d,%d) World=(%.1f, %.1f, %.1f)"),
                    DesiredCameraCell.X,
                    DesiredCameraCell.Y,
                    CameraAnchorWorld.X,
                    CameraAnchorWorld.Y,
                    CameraAnchorWorld.Z
                );
            }
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonyManager] Camera anchor resolved from selected spawn. Desired=(%d,%d) Resolved=(%d,%d) Surface=(%.1f, %.1f, %.1f)"),
            DesiredCameraCell.X,
            DesiredCameraCell.Y,
            ResolvedCameraCell.X,
            ResolvedCameraCell.Y,
            CameraAnchorWorld.X,
            CameraAnchorWorld.Y,
            CameraAnchorWorld.Z
        );
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] VoxelWorldMgr null. Cannot resolve camera position."));
        return;
    }

    // Keep the RTS anchor safely above the surface.
    const float MinimumCameraAnchorZ = 3000.0f;
    CameraAnchorWorld.Z = FMath::Max(CameraAnchorWorld.Z, MinimumCameraAnchorZ);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Camera anchor after clamp: (%.1f, %.1f, %.1f)"),
        CameraAnchorWorld.X,
        CameraAnchorWorld.Y,
        CameraAnchorWorld.Z
    );

    CameraPawn->SnapToRTSView(
        CameraAnchorWorld,
        RTSCameraYaw,
        RTSCameraPitch,
        RTSCameraHeight,
        true
    );

    PC->SetViewTarget(CameraPawn);
    PC->SetControlRotation(FRotator(RTSCameraPitch, RTSCameraYaw, 0.0f));

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonyManager] Camera positioned at (%.1f, %.1f, %.1f). Manual=%s Camera=%s ViewTarget=%s"),
        CameraAnchorWorld.X,
        CameraAnchorWorld.Y,
        CameraAnchorWorld.Z,
        bUseManualCameraStart ? TEXT("true") : TEXT("false"),
        *CameraPawn->GetName(),
        PC->GetViewTarget() ? *PC->GetViewTarget()->GetName() : TEXT("None")
    );
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

    if (VoxelWorldMgr->IsSpawnableTerrainCell(DesiredCell))
    {
        return DesiredCell;
    }

    const int32 EffectiveSearchRadius = FMath::Max(SpawnSearchRadius, CameraAnchorSearchRadius);

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