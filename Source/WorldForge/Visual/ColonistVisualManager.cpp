#include "ColonistVisualManager.h"

#include "WorldForgeColonist.h"
#include "ProcessorManager.h"
#include "SimulationTimeManager.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h" // CHANGE MADE:
// Required for StaticLoadClass used to load the Blueprint
// colonist class at runtime from a content path.

// Optional but recommended if you already have this subsystem in your project.
// If the include path is different in your project, adjust it.
// This is only used to read the voxel/cell size so visuals line up with the sim.
#include "VoxelWorldManager.h"

void UColonistVisualManager::Initialize(FSubsystemCollectionBase& Collection)
{
    // Make sure these subsystems are created before we try to bind/use them.
    Collection.InitializeDependency<UProcessorManager>();
    Collection.InitializeDependency<USimulationTimeManager>();

    Super::Initialize(Collection);

    // CHANGE MADE:
    // Try to assign a Blueprint colonist visual class automatically if one has not
    // already been provided through defaults or another setup path.
    //
    // IMPORTANT:
    // This must use StaticLoadClass here because ConstructorHelpers::FClassFinder
    // is only valid inside constructors.
    //
    // Update the asset path below if your BP_WorldForgeColonist asset lives in a different folder.
    // The path format for StaticLoadClass must include the generated class suffix:
    //
    //   /Game/<FolderPath>/<AssetName>.<AssetName>_C
    //
    // Example:
    //   /Game/BP_WorldForgeColonist.BP_WorldForgeColonist_C
    //
    // If lookup fails, the system safely falls back to AWorldForgeColonist::StaticClass()
    // later during spawn.
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
                TEXT("[ColonistVisualManager] Failed to load BP_WorldForgeColonist at /Game/BP_WorldForgeColonist.BP_WorldForgeColonist_C. Falling back to native class.")
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

    DestroyAllColonistActors();

    CachedProcessorManager = nullptr;
    CachedSimulationTimeManager = nullptr;

    Super::Deinitialize();
}

void UColonistVisualManager::SpawnColonistActors(int32 InSettlementID)
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
        // Skip dead agents. They should not get a new visible actor.
        if (AgentData.State == EAgentState::Dead)
        {
            continue;
        }

        // Skip if already spawned.
        if (ColonistActors.Contains(AgentData.AgentID))
        {
            continue;
        }

        if (SpawnActorFromData(AgentData, CellSize))
        {
            SpawnedCount++;
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

    // Build a quick lookup of which agents still exist this step.
    TSet<int32> LiveAgentIDs;
    LiveAgentIDs.Reserve(Agents.Num());

    for (const FAgentData& AgentData : Agents)
    {
        LiveAgentIDs.Add(AgentData.AgentID);

        // Remove dead agents instead of leaving hidden actors around forever.
        if (AgentData.State == EAgentState::Dead)
        {
            DestroyActorForAgent(AgentData.AgentID);
            continue;
        }

        AWorldForgeColonist* ExistingActor = GetActorForAgent(AgentData.AgentID);

        // Spawn any visual actor that does not exist yet.
        if (!ExistingActor)
        {
            ExistingActor = SpawnActorFromData(AgentData, CellSize);
        }

        // Push the latest simulation snapshot into the actor.
        //
        // Important:
        // This actor is presentation-only. UpdateVisuals caches target position/state
        // and the actor handles interpolation locally.
        if (ExistingActor)
        {
            ExistingActor->UpdateVisuals(AgentData, 0.0f, CellSize);
        }
    }

    // Destroy actors whose agents no longer exist in the simulation.
    // We gather keys first so we can safely modify the map during removal.
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
    // If nothing has chosen a settlement yet, do nothing.
    if (ActiveSettlementID == INDEX_NONE)
    {
        return;
    }

    SyncActorsToAgents(ActiveSettlementID);

    if (StepNumber % 25 == 0)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[ColonistVisualManager] Synced settlement=%d at step=%d trackedActors=%d"),
            ActiveSettlementID,
            StepNumber,
            ColonistActors.Num()
        );
    }
}

AWorldForgeColonist* UColonistVisualManager::SpawnActorFromData(const FAgentData& AgentData, float CellSize)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    // Use a configurable actor class if one has been assigned.
    // This allows the visual manager to spawn BP_WorldForgeColonist so the
    // Blueprint graph, Blueprint sprite component, and flipbook renderer all run.
    //
    // If no class is assigned, fall back safely to the native C++ actor.
    UClass* ColonistClass = ColonistActorClass
        ? ColonistActorClass.Get()
        : AWorldForgeColonist::StaticClass();

    if (!ColonistClass)
    {
        UE_LOG(LogTemp, Warning, TEXT("[ColonistVisualManager] Colonist class is invalid."));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

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

    // Initialize from current sim snapshot so first frame starts in the correct place.
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
    if (const UWorld* World = GetWorld())
    {
        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            return VoxelWorldManager->VoxelSize;
        }
    }

    // Matches your current colonist actor default fallback.
    return 200.0f;
}