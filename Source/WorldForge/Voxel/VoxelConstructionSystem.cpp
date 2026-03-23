#include "VoxelConstructionSystem.h"
#include "Engine/World.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "VoxelWorldManager.h"

void UVoxelConstructionSystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);
    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Initialized."));
}

int32 UVoxelConstructionSystem::StartBuildTask(int32 SID, FGameplayTag Tag, FIntPoint Origin, int32 AgentID)
{
    if (HasActiveTask(SID, Tag))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelConstruction] Already has active %s task"), *Tag.ToString());
        return -1;
    }

    FActiveBuildTask T;
    T.TaskID = NextTaskID++;
    T.SettlementID = SID;
    T.BuildingTag = Tag;
    T.OriginGridCell = Origin;
    T.AssignedAgentID = AgentID;
    T.TotalBlocks = GetBlockCount(Tag);
    T.PlacedBlocks = 0;
    T.bComplete = false;

    ActiveTasks.Add(T);

    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Task %d: %s at (%d,%d) -- %d blocks"),
        T.TaskID, *Tag.ToString(), Origin.X, Origin.Y, T.TotalBlocks);

    return T.TaskID;
}

bool UVoxelConstructionSystem::PlaceNextBlock(int32 TaskID, UWorld* World)
{
    FActiveBuildTask* T = GetMutableTask(TaskID);
    if (!T || T->bComplete) return false;

    T->PlacedBlocks++;

    // ================================
    // MODIFIED: grid-based placement
    // ================================
    if (UVoxelWorldManager* V = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr)
    {
        // ADDED: layer logic (unchanged behavior)
        const int32 Layer = (T->PlacedBlocks - 1) / 4;

        // ADDED: index within layer
        const int32 BlockIndexWithinLayer = (T->PlacedBlocks - 1) % 4;

        // ADDED: 2x2 footprint
        FIntPoint LocalOffset = FIntPoint::ZeroValue;

        switch (BlockIndexWithinLayer)
        {
        case 0: LocalOffset = FIntPoint(0, 0); break; // ADDED
        case 1: LocalOffset = FIntPoint(1, 0); break; // ADDED
        case 2: LocalOffset = FIntPoint(0, 1); break; // ADDED
        case 3: LocalOffset = FIntPoint(1, 1); break; // ADDED
        default: break;
        }

        // ADDED: apply offset
        const FIntPoint BuildGridCell(
            T->OriginGridCell.X + LocalOffset.X,
            T->OriginGridCell.Y + LocalOffset.Y
        );

        // MODIFIED: use grid cell
        const FVector WorldPos = V->GridToWorld(BuildGridCell, Layer);

        V->PlaceVoxelBlock(
            WorldPos,
            FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone"))));
    }

    if (T->PlacedBlocks >= T->TotalBlocks)
    {
        T->bComplete = true;
        FinalizeBuilding(*T, World);
        return true;
    }

    return false;
}

void UVoxelConstructionSystem::FinalizeBuilding(const FActiveBuildTask& T, UWorld* W)
{
    if (!W) return;

    if (USettlementManager* SM = W->GetSubsystem<USettlementManager>())
    {
        SM->AddBuilding(T.SettlementID, T.BuildingTag);
    }

    if (UEventBus* Bus = W->GetSubsystem<UEventBus>())
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Sim.Event.BuildingComplete")));
        E.SettlementID = T.SettlementID;
        E.AgentID = T.AssignedAgentID;
        E.Payload = T.BuildingTag;
        Bus->Dispatch(E);
    }

    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Complete: %s settlement %d"),
        *T.BuildingTag.ToString(), T.SettlementID);
}

// ================================
// RESTORED FUNCTIONS (CRITICAL)
// ================================

bool UVoxelConstructionSystem::IsTaskComplete(int32 ID) const
{
    for (const FActiveBuildTask& T : ActiveTasks)
    {
        if (T.TaskID == ID) return T.bComplete;
    }
    return false;
}

bool UVoxelConstructionSystem::HasActiveTask(int32 SID, FGameplayTag Tag) const
{
    for (const FActiveBuildTask& T : ActiveTasks)
    {
        if (T.SettlementID == SID && T.BuildingTag == Tag && !T.bComplete)
            return true;
    }
    return false;
}

FActiveBuildTask* UVoxelConstructionSystem::GetMutableTask(int32 ID)
{
    for (FActiveBuildTask& T : ActiveTasks)
    {
        if (T.TaskID == ID) return &T;
    }
    return nullptr;
}

FActiveBuildTask* UVoxelConstructionSystem::GetMutableTaskForAgent(int32 AgentID)
{
    for (FActiveBuildTask& T : ActiveTasks)
    {
        if (T.AssignedAgentID == AgentID && !T.bComplete)
        {
            return &T;
        }
    }
    return nullptr;
}

int32 UVoxelConstructionSystem::GetBlockCount(FGameplayTag Tag) const
{
    const FString S = Tag.ToString();

    if (S.Contains(TEXT("Stockpile"))) return 8;
    if (S.Contains(TEXT("Kitchen"))) return 16;
    if (S.Contains(TEXT("Carpenter"))) return 20;
    if (S.Contains(TEXT("Barracks"))) return 24;
    if (S.Contains(TEXT("Shrine"))) return 12;

    return 10;
}

void UVoxelConstructionSystem::RestoreFromSave(const TArray<FActiveBuildTask>& Saved)
{
    ActiveTasks = Saved;

    NextTaskID = 0;
    for (const FActiveBuildTask& T : ActiveTasks)
    {
        NextTaskID = FMath::Max(NextTaskID, T.TaskID + 1);
    }
}