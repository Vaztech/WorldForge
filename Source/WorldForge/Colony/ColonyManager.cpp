#include "ColonyManager.h"

#include "SettlementManager.h"
#include "ProcessorManager.h"
#include "NavigationContext.h"
#include "SimulationTimeManager.h"
#include "SaveSubsystem.h"
#include "VoxelConstructionSystem.h"
#include "VoxelWorldManager.h"
#include "WorldForgeTags.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

AColonyManager::AColonyManager()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AColonyManager::BeginPlay()
{
    Super::BeginPlay();

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

void AColonyManager::StartBootstrapAttempt()
{
    if (bBootstrapComplete)
    {
        return;
    }

    if (!CacheSubsystems() || !AreRequiredSubsystemsReady())
    {
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
    return VoxelWorldMgr && VoxelWorldMgr->IsTerrainReadyForGameplay();
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
    if (bBootstrapComplete)
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

void AColonyManager::FinishBootstrap()
{
    if (bBootstrapComplete)
    {
        return;
    }

    if (!CacheSubsystems() || !AreRequiredSubsystemsReady())
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] FinishBootstrap failed: required subsystems missing."));
        return;
    }

    if (!IsTerrainReadyForSpawn())
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonyManager] FinishBootstrap called before terrain was ready."));
        BindToTerrainReadyEvent();
        return;
    }

    InitializeSettlement();

    if (SettlementID == INDEX_NONE)
    {
        UE_LOG(LogTemp, Error, TEXT("[ColonyManager] Invalid SettlementID, aborting bootstrap."));
        return;
    }

    InitializeNavigation();
    SetupAgents();
    SetupInitialResources();
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

    if (bAnchorSettlementToPlayerViewOnStart)
    {
        SettlementWorldOrigin = ResolveInitialSettlementWorldOrigin();
        SpawnGridCenter = SettlementWorldOrigin;
    }

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

    for (int32 i = 0; i < InitialAgentCount; ++i)
    {
        const int32 AgentID = StartingAgentID + i;

        const FIntPoint DesiredLocalCell = ComputeInitialSpawnCell(i);
        const FIntPoint DesiredWorldCell = SettlementWorldOrigin + DesiredLocalCell;
        const FIntPoint ValidWorldCell = ResolveValidSpawnCell(DesiredWorldCell);
        const FIntPoint SpawnLocalCell = ValidWorldCell - SettlementWorldOrigin;

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
    FIntPoint DesiredWorldCell = SettlementWorldOrigin;
    bool bDerivedFromPlayerView = false;

    if (VoxelWorldMgr)
    {
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

            DesiredWorldCell = VoxelWorldMgr->WorldToTerrainCell(ViewLocation);
            bDerivedFromPlayerView = true;
        }
    }

    // 🚨 FIX: REMOVE HEAVY CLAMPING
    // We only lightly clamp to world bounds (NOT push inward by 24+ cells)

    if (VoxelWorldMgr && VoxelWorldMgr->HasCachedTerrainData())
    {
        const int32 MaxCellX = NavigationGridWidth - 1;
        const int32 MaxCellY = NavigationGridHeight - 1;

        DesiredWorldCell.X = FMath::Clamp(DesiredWorldCell.X, 0, MaxCellX);
        DesiredWorldCell.Y = FMath::Clamp(DesiredWorldCell.Y, 0, MaxCellY);

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
        bDerivedFromPlayerView ? TEXT("PlayerView") : TEXT("Default"),
        DesiredWorldCell.X,
        DesiredWorldCell.Y,
        ResolvedWorldCell.X,
        ResolvedWorldCell.Y
    );

    return ResolvedWorldCell;
}

FIntPoint AColonyManager::ComputeInitialSpawnCell(int32 AgentIndex) const
{
    const int32 SafeColumns = FMath::Max(1, SpawnGridColumns);
    const int32 SafeSpacing = FMath::Max(1, SpawnSpacingCells);

    const int32 LocalOffsetX = (AgentIndex % SafeColumns) * SafeSpacing;
    const int32 LocalOffsetY = (AgentIndex / SafeColumns) * SafeSpacing;

    const FIntPoint LocalCenter = SpawnGridCenter - SettlementWorldOrigin;

    return FIntPoint(
        LocalCenter.X + LocalOffsetX,
        LocalCenter.Y + LocalOffsetY
    );
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