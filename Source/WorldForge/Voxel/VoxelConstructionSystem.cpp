#include "VoxelConstructionSystem.h"
#include "Engine/World.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "VoxelWorldManager.h"
#include "WorldForgeTags.h"

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

    // Prefab builds currently default to stone unless a richer prefab-material
    // mapping is introduced later. This preserves your existing behavior while
    // adding the Section I stockpile gate/consumption.
    const FGameplayTag DefaultPrefabMaterial =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    const int32 RequiredBlocks = GetBlockCount(Tag);

    UWorld* World = GetWorld();
    USettlementManager* SettlementManager = World ? World->GetSubsystem<USettlementManager>() : nullptr;

    // Section I / Milestone D:
    // insufficient material blocks task creation.
    if (!HasEnoughStockpileForTask(SID, DefaultPrefabMaterial, RequiredBlocks, World))
    {
        const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(DefaultPrefabMaterial);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start prefab task %s for settlement %d: insufficient %s. Required=%d Available=%d"),
            *Tag.ToString(),
            SID,
            *RequiredItem.ToString(),
            RequiredBlocks,
            SettlementManager ? SettlementManager->GetStockpileAmount(SID, RequiredItem) : 0
        );

        return -1;
    }

    // NEW:
    // Consume stockpile up front so the task reserves real materials
    // and cannot be duplicated after passing the gate once.
    if (!SettlementManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelConstruction] Cannot start prefab task %s: SettlementManager missing"), *Tag.ToString());
        return -1;
    }

    {
        const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(DefaultPrefabMaterial);
        const bool bConsumed = SettlementManager->ConsumeFromStockpile(SID, RequiredItem, RequiredBlocks);

        if (!bConsumed)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelConstruction] Failed to consume %d of %s for prefab task %s in settlement %d"),
                RequiredBlocks,
                *RequiredItem.ToString(),
                *Tag.ToString(),
                SID
            );

            return -1;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelConstruction] Consumed %d of %s for prefab task %s"),
            RequiredBlocks,
            *RequiredItem.ToString(),
            *Tag.ToString()
        );
    }

    FActiveBuildTask T;
    T.TaskID = NextTaskID++;
    T.SettlementID = SID;
    T.BuildingTag = Tag;
    T.OriginGridCell = Origin;
    T.AssignedAgentID = AgentID;
    T.TotalBlocks = RequiredBlocks;
    T.PlacedBlocks = 0;
    T.bComplete = false;

    // Preserve current prefab behavior while allowing Section I material support.
    // Existing tasks default to stone unless a custom material is supplied later.
    T.MaterialTag = DefaultPrefabMaterial;

    ActiveTasks.Add(T);

    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Task %d: %s at (%d,%d) -- %d blocks"),
        T.TaskID, *Tag.ToString(), Origin.X, Origin.Y, T.TotalBlocks);

    return T.TaskID;
}

int32 UVoxelConstructionSystem::StartCustomBuildTask(
    int32 SID,
    const TArray<FIntPoint>& Blocks,
    FGameplayTag MaterialTag,
    int32 AgentID)
{
    // Use caller-supplied material if valid; otherwise preserve existing stone fallback.
    const FGameplayTag EffectiveMaterial = MaterialTag.IsValid()
        ? MaterialTag
        : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    UWorld* World = GetWorld();
    USettlementManager* SettlementManager = World ? World->GetSubsystem<USettlementManager>() : nullptr;

    // Section I / Milestone D:
    // insufficient material blocks task creation.
    if (!HasEnoughStockpileForTask(SID, EffectiveMaterial, Blocks.Num(), World))
    {
        const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(EffectiveMaterial);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start custom task for settlement %d: insufficient %s. Required=%d Available=%d"),
            SID,
            *RequiredItem.ToString(),
            Blocks.Num(),
            SettlementManager ? SettlementManager->GetStockpileAmount(SID, RequiredItem) : 0
        );

        return -1;
    }

    // NEW:
    // Consume stockpile up front so custom tasks also reserve real materials.
    if (!SettlementManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelConstruction] Cannot start custom task: SettlementManager missing"));
        return -1;
    }

    {
        const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(EffectiveMaterial);
        const bool bConsumed = SettlementManager->ConsumeFromStockpile(SID, RequiredItem, Blocks.Num());

        if (!bConsumed)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelConstruction] Failed to consume %d of %s for custom task in settlement %d"),
                Blocks.Num(),
                *RequiredItem.ToString(),
                SID
            );

            return -1;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelConstruction] Consumed %d of %s for custom task"),
            Blocks.Num(),
            *RequiredItem.ToString()
        );
    }

    FActiveBuildTask T;
    T.TaskID = NextTaskID++;
    T.SettlementID = SID;

    // Custom builds do not necessarily map to a standard completed building tag yet.
    // Leave BuildingTag empty rather than inventing a new architecture.
    T.BuildingTag = FGameplayTag::EmptyTag;

    // Store the exact cells to place, in order.
    T.BlockList = Blocks;

    T.MaterialTag = EffectiveMaterial;

    T.TotalBlocks = Blocks.Num();
    T.PlacedBlocks = 0;
    T.AssignedAgentID = AgentID;
    T.bComplete = false;

    // For custom tasks, OriginGridCell is not used for placement, but keeping it zero
    // maintains save compatibility and avoids changing task shape semantics elsewhere.
    T.OriginGridCell = FIntPoint::ZeroValue;

    ActiveTasks.Add(T);

    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Custom task %d: blocks=%d material=%s"),
        T.TaskID,
        T.TotalBlocks,
        *T.MaterialTag.ToString());

    return T.TaskID;
}

bool UVoxelConstructionSystem::PlaceNextBlock(int32 TaskID, UWorld* World)
{
    FActiveBuildTask* T = GetMutableTask(TaskID);
    if (!T || T->bComplete) return false;

    // Advance progress first so PlacedBlocks remains the "count of completed placements"
    // exactly like the existing prefab logic.
    T->PlacedBlocks++;

    if (UVoxelWorldManager* V = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr)
    {
        FVector WorldPos = FVector::ZeroVector;

        // Section I hybrid build support:
        // If BlockList exists, place explicit cells directly.
        // Otherwise preserve the old 2x2 layered prefab behavior.
        if (T->BlockList.Num() > 0)
        {
            const int32 BlockIndex = T->PlacedBlocks - 1;

            if (T->BlockList.IsValidIndex(BlockIndex))
            {
                const FIntPoint BuildGridCell = T->BlockList[BlockIndex];

                // Current custom builds place on layer 0 only.
                // Vertical custom layering is deferred to a later targeted step.
                WorldPos = V->GridToWorld(BuildGridCell, 0);
            }
        }
        else
        {
            // ================================
            // Existing prefab grid placement
            // ================================
            const int32 Layer = (T->PlacedBlocks - 1) / 4;
            const int32 BlockIndexWithinLayer = (T->PlacedBlocks - 1) % 4;

            FIntPoint LocalOffset = FIntPoint::ZeroValue;

            switch (BlockIndexWithinLayer)
            {
            case 0: LocalOffset = FIntPoint(0, 0); break;
            case 1: LocalOffset = FIntPoint(1, 0); break;
            case 2: LocalOffset = FIntPoint(0, 1); break;
            case 3: LocalOffset = FIntPoint(1, 1); break;
            default: break;
            }

            const FIntPoint BuildGridCell(
                T->OriginGridCell.X + LocalOffset.X,
                T->OriginGridCell.Y + LocalOffset.Y
            );

            WorldPos = V->GridToWorld(BuildGridCell, Layer);
        }

        // Use task material when present instead of always hardcoding stone.
        // This preserves old behavior through the fallback path.
        V->PlaceVoxelBlock(
            WorldPos,
            T->MaterialTag.IsValid()
            ? T->MaterialTag
            : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone"))));
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

    // Only register a completed building if this task actually represents a tagged building.
    // Custom freeform builds may intentionally leave BuildingTag empty.
    if (T.BuildingTag.IsValid())
    {
        if (USettlementManager* SM = W->GetSubsystem<USettlementManager>())
        {
            SM->AddBuilding(T.SettlementID, T.BuildingTag);
        }
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

FGameplayTag UVoxelConstructionSystem::ResolveRequiredStockpileItem(FGameplayTag MaterialTag) const
{
    const FString MaterialString = MaterialTag.ToString();

    // Keep this intentionally minimal and deterministic until your project
    // defines a richer voxel-material -> item-cost mapping table.
    if (MaterialString.Contains(TEXT("Wood")))
    {
        return TAG_Item_Wood;
    }

    if (MaterialString.Contains(TEXT("Stone")))
    {
        return TAG_Item_Stone;
    }

    return TAG_Item_Stone;
}

bool UVoxelConstructionSystem::HasEnoughStockpileForTask(
    int32 SettlementID,
    FGameplayTag MaterialTag,
    int32 RequiredUnits,
    UWorld* World) const
{
    if (!World)
    {
        return false;
    }

    USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    if (!SettlementManager)
    {
        return false;
    }

    const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(MaterialTag);
    const int32 AvailableUnits = SettlementManager->GetStockpileAmount(SettlementID, RequiredItem);

    return AvailableUnits >= RequiredUnits;
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