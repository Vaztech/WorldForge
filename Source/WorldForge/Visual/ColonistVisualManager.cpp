#include "ColonistVisualManager.h"

#include "WorldForgeColonist.h"
#include "ProcessorManager.h"
#include "SimulationTimeManager.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/UObjectGlobals.h"
#include "VoxelWorldManager.h"

void UColonistVisualManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Collection.InitializeDependency<UProcessorManager>();
    Collection.InitializeDependency<USimulationTimeManager>();

    Super::Initialize(Collection);

    if (!ColonistActorClass)
    {
        UClass* LoadedBlueprintClass = StaticLoadClass(
            AWorldForgeColonist::StaticClass(),
            nullptr,
            TEXT("/Game/WorldForge/BluePrints/BP_WorldForgeColonist.BP_WorldForgeColonist_C")
        );

        if (LoadedBlueprintClass)
        {
            ColonistActorClass = LoadedBlueprintClass;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[ColonistVisualManager] Using BP_WorldForgeColonist as default spawn class.")
            );
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[ColonistVisualManager] Failed to load BP_WorldForgeColonist at /Game/WorldForge/BluePrints/BP_WorldForgeColonist.BP_WorldForgeColonist_C. Falling back to native class.")
            );
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Initialized."));
}

void UColonistVisualManager::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);

    CachedProcessorManager = InWorld.GetSubsystem<UProcessorManager>();
    CachedSimulationTimeManager = InWorld.GetSubsystem<USimulationTimeManager>();
    CachedVoxelWorldManager = InWorld.GetSubsystem<UVoxelWorldManager>();

    if (CachedSimulationTimeManager)
    {
        CachedSimulationTimeManager->OnSimulationStep.AddDynamic(
            this,
            &UColonistVisualManager::HandleSimulationStep
        );

        UE_LOG(LogTemp, Log, TEXT("[ColonistVisualManager] Bound to SimulationTimeManager."));
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] SimulationTimeManager not found."));
    }

    if (CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager->OnTerrainReady.AddDynamic(
            this,
            &UColonistVisualManager::HandleTerrainReady
        );
        bBoundToTerrainReady = true;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] VoxelWorldManager not found."));
    }
}

void UColonistVisualManager::Deinitialize()
{
    if (CachedSimulationTimeManager)
    {
        CachedSimulationTimeManager->OnSimulationStep.RemoveDynamic(
            this,
            &UColonistVisualManager::HandleSimulationStep
        );
    }

    if (CachedVoxelWorldManager && bBoundToTerrainReady)
    {
        CachedVoxelWorldManager->OnTerrainReady.RemoveDynamic(
            this,
            &UColonistVisualManager::HandleTerrainReady
        );
        bBoundToTerrainReady = false;
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(DeferredSpawnTimerHandle);
    }

    DestroyAllColonistActors();

    CachedProcessorManager = nullptr;
    CachedSimulationTimeManager = nullptr;
    CachedVoxelWorldManager = nullptr;
    PendingSpawnSettlementID = INDEX_NONE;
    bSpawnDeferredScheduled = false;

    Super::Deinitialize();
}

void UColonistVisualManager::SpawnColonistActors(int32 InSettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    ActiveSettlementID = InSettlementID;
    PendingSpawnSettlementID = InSettlementID;

    if (!CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    }

    if (!CachedVoxelWorldManager || !CachedVoxelWorldManager->IsSurfaceReady())
    {
        bSpawnDeferredScheduled = true;

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ColonistVisualManager] Waiting for terrain-ready signal before spawning colonists for settlement=%d."),
            InSettlementID
        );
        return;
    }

    bSpawnDeferredScheduled = false;
    InternalSpawnColonistActors(InSettlementID);
    PendingSpawnSettlementID = INDEX_NONE;
}

void UColonistVisualManager::HandleTerrainReady()
{
    if (!bSpawnDeferredScheduled || PendingSpawnSettlementID == INDEX_NONE)
    {
        return;
    }

    bSpawnDeferredScheduled = false;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Terrain-ready received. Spawning deferred colonists for settlement=%d."),
        PendingSpawnSettlementID
    );

    InternalSpawnColonistActors(PendingSpawnSettlementID);
    PendingSpawnSettlementID = INDEX_NONE;
}

void UColonistVisualManager::InternalSpawnColonistActors(int32 InSettlementID)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    if (!CachedProcessorManager)
    {
        CachedProcessorManager = World->GetSubsystem<UProcessorManager>();
    }

    if (!CachedProcessorManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Cannot spawn actors: ProcessorManager missing."));
        return;
    }

    ActiveSettlementID = InSettlementID;

    const float CellSize = ResolveCellSize();
    const TArray<FAgentData>& Agents = CachedProcessorManager->GetAgentsForSettlement(InSettlementID);

    int32 SpawnedCount = 0;

    for (const FAgentData& AgentData : Agents)
    {
        if (AgentData.State == EAgentState::Dead)
        {
            continue;
        }

        if (ColonistActors.Contains(AgentData.AgentID))
        {
            continue;
        }

        if (SpawnActorFromData(AgentData, CellSize))
        {
            ++SpawnedCount;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] SpawnColonistActors settlement=%d spawned=%d totalTracked=%d"),
        InSettlementID,
        SpawnedCount,
        ColonistActors.Num()
    );
}

void UColonistVisualManager::SyncActorsToAgents(int32 InSettlementID)
{
    if (bIsSyncInProgress)
    {
        return;
    }

    if (bSpawnDeferredScheduled)
    {
        return;
    }

    bIsSyncInProgress = true;

    UWorld* World = GetWorld();
    if (!World)
    {
        bIsSyncInProgress = false;
        return;
    }

    if (!CachedProcessorManager)
    {
        CachedProcessorManager = World->GetSubsystem<UProcessorManager>();
    }

    if (!CachedProcessorManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Cannot sync actors: ProcessorManager missing."));
        bIsSyncInProgress = false;
        return;
    }

    ActiveSettlementID = InSettlementID;

    const float CellSize = ResolveCellSize();
    const TArray<FAgentData>& Agents = CachedProcessorManager->GetAgentsForSettlement(InSettlementID);

    TSet<int32> LiveAgentIDs;
    LiveAgentIDs.Reserve(Agents.Num());

    for (const FAgentData& AgentData : Agents)
    {
        LiveAgentIDs.Add(AgentData.AgentID);

        if (AgentData.State == EAgentState::Dead)
        {
            DestroyActorForAgent(AgentData.AgentID);
            continue;
        }

        AWorldForgeColonist* ExistingActor = GetActorForAgent(AgentData.AgentID);

        if (!ExistingActor)
        {
            ExistingActor = SpawnActorFromData(AgentData, CellSize);
        }

        if (ExistingActor)
        {
            ExistingActor->UpdateVisuals(AgentData, 0.0f, CellSize);
        }
    }

    TArray<int32> TrackedAgentIDs;
    ColonistActors.GetKeys(TrackedAgentIDs);

    for (const int32 AgentID : TrackedAgentIDs)
    {
        if (!LiveAgentIDs.Contains(AgentID))
        {
            DestroyActorForAgent(AgentID);
        }
    }

    bIsSyncInProgress = false;
}

AWorldForgeColonist* UColonistVisualManager::GetActorForAgent(int32 AgentID) const
{
    if (const TObjectPtr<AWorldForgeColonist>* Found = ColonistActors.Find(AgentID))
    {
        return Found->Get();
    }

    return nullptr;
}

void UColonistVisualManager::DestroyAllColonistActors()
{
    TArray<int32> AgentIDs;
    ColonistActors.GetKeys(AgentIDs);

    for (const int32 AgentID : AgentIDs)
    {
        DestroyActorForAgent(AgentID);
    }

    ColonistActors.Empty();
}

void UColonistVisualManager::HandleSimulationStep(int32 StepNumber)
{
    if (ActiveSettlementID == INDEX_NONE)
    {
        return;
    }

    if (bSpawnDeferredScheduled)
    {
        return;
    }

    SyncActorsToAgents(ActiveSettlementID);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] HandleSimulationStep settlement=%d step=%d trackedActors=%d"),
        ActiveSettlementID,
        StepNumber,
        ColonistActors.Num()
    );
}

AWorldForgeColonist* UColonistVisualManager::SpawnActorFromData(const FAgentData& AgentData, float CellSize)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    if (!CachedVoxelWorldManager)
    {
        CachedVoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>();
    }

    UClass* ColonistClass = ColonistActorClass
        ? ColonistActorClass.Get()
        : AWorldForgeColonist::StaticClass();

    if (!ColonistClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Colonist class is invalid."));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    AWorldForgeColonist* NewActor = World->SpawnActor<AWorldForgeColonist>(
        ColonistClass,
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!NewActor)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[ColonistVisualManager] Failed to spawn actor for AgentID=%d"),
            AgentData.AgentID
        );
        return nullptr;
    }

    NewActor->InitializeFromAgentData(AgentData, CellSize);

    ColonistActors.Add(AgentData.AgentID, NewActor);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisualManager] Spawned colonist visual for AgentID=%d SettlementID=%d using class=%s"),
        AgentData.AgentID,
        AgentData.SettlementID,
        *GetNameSafe(ColonistClass)
    );

    return NewActor;
}

void UColonistVisualManager::DestroyActorForAgent(int32 AgentID)
{
    TObjectPtr<AWorldForgeColonist>* Found = ColonistActors.Find(AgentID);
    if (!Found)
    {
        return;
    }

    if (AWorldForgeColonist* Actor = Found->Get())
    {
        Actor->Destroy();
    }

    ColonistActors.Remove(AgentID);

    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[ColonistVisualManager] Destroyed colonist visual for AgentID=%d"),
        AgentID
    );
}

float UColonistVisualManager::ResolveCellSize() const
{
    return 1.0f;
}