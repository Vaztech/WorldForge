// VoxelConstructionSystem.cpp

#include "VoxelConstructionSystem.h"
#include "Engine/World.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "VoxelWorldManager.h"
#include "WorldForgeTags.h"

namespace
{
    static bool TryParseExplicitPrefabDimensions(const FString& Source, int32& OutWidth, int32& OutDepth, int32& OutHeight)
    {
        OutWidth = 0;
        OutDepth = 0;
        OutHeight = 0;

        const int32 Length = Source.Len();
        for (int32 Start = 0; Start < Length; ++Start)
        {
            if (!FChar::IsDigit(Source[Start]))
            {
                continue;
            }

            int32 Cursor = Start;

            FString A;
            while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
            {
                A.AppendChar(Source[Cursor]);
                ++Cursor;
            }

            if (A.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
            {
                continue;
            }

            ++Cursor;

            FString B;
            while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
            {
                B.AppendChar(Source[Cursor]);
                ++Cursor;
            }

            if (B.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
            {
                continue;
            }

            ++Cursor;

            FString C;
            while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
            {
                C.AppendChar(Source[Cursor]);
                ++Cursor;
            }

            if (C.IsEmpty())
            {
                continue;
            }

            const int32 ParsedWidth = FCString::Atoi(*A);
            const int32 ParsedDepth = FCString::Atoi(*B);
            const int32 ParsedHeight = FCString::Atoi(*C);

            if (ParsedWidth > 0 && ParsedDepth > 0 && ParsedHeight > 0)
            {
                OutWidth = ParsedWidth;
                OutDepth = ParsedDepth;
                OutHeight = ParsedHeight;
                return true;
            }
        }

        return false;
    }
}

void UVoxelConstructionSystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);
    UE_LOG(LogTemp, Log, TEXT("[VoxelConstruction] Initialized."));
}

int32 UVoxelConstructionSystem::StartBuildTask(int32 SID, FGameplayTag Tag, FIntPoint Origin, int32 AgentID)
{
    if (HasConflictingTaskAtOrigin(SID, Origin))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Already has active task at (%d,%d) for settlement %d"),
            Origin.X,
            Origin.Y,
            SID
        );
        return -1;
    }

    const FGameplayTag DefaultPrefabMaterial =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    const TArray<FIntVector> PrefabBlocks = BuildPrefabBlockList(Tag, Origin);
    return StartBuildTaskFromBlocks(SID, Tag, PrefabBlocks, DefaultPrefabMaterial, AgentID);
}

int32 UVoxelConstructionSystem::StartBuildTaskFromBlocks(
    int32 SID,
    FGameplayTag BuildTag,
    const TArray<FIntVector>& Blocks,
    FGameplayTag MaterialTag,
    int32 AgentID)
{
    if (Blocks.Num() <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start prefab task %s for settlement %d: empty block list"),
            *BuildTag.ToString(),
            SID
        );
        return -1;
    }

    if (HasConflictingTaskInBlockList(SID, Blocks))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start prefab task %s for settlement %d: explicit block list conflicts with an active task"),
            *BuildTag.ToString(),
            SID
        );
        return -1;
    }

    const FGameplayTag EffectiveMaterial = MaterialTag.IsValid()
        ? MaterialTag
        : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    UWorld* World = GetWorld();
    USettlementManager* SettlementManager = World ? World->GetSubsystem<USettlementManager>() : nullptr;

    if (!HasEnoughStockpileForTask(SID, EffectiveMaterial, Blocks.Num(), World))
    {
        const FGameplayTag RequiredItem = ResolveRequiredStockpileItem(EffectiveMaterial);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start prefab task %s for settlement %d: insufficient %s. Required=%d Available=%d"),
            *BuildTag.ToString(),
            SID,
            *RequiredItem.ToString(),
            Blocks.Num(),
            SettlementManager ? SettlementManager->GetStockpileAmount(SID, RequiredItem) : 0
        );

        return -1;
    }

    if (!SettlementManager)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelConstruction] Cannot start prefab task %s: SettlementManager missing"), *BuildTag.ToString());
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
                TEXT("[VoxelConstruction] Failed to consume %d of %s for prefab task %s in settlement %d"),
                Blocks.Num(),
                *RequiredItem.ToString(),
                *BuildTag.ToString(),
                SID
            );

            return -1;
        }

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelConstruction] Consumed %d of %s for prefab task %s"),
            Blocks.Num(),
            *RequiredItem.ToString(),
            *BuildTag.ToString()
        );
    }

    FActiveBuildTask T;
    T.TaskID = NextTaskID++;
    T.SettlementID = SID;
    T.BuildingTag = BuildTag;
    T.AssignedAgentID = AgentID;
    T.TotalBlocks = Blocks.Num();
    T.PlacedBlocks = 0;
    T.bComplete = false;
    T.MaterialTag = EffectiveMaterial;
    T.BlockList3D = Blocks;

    const FIntVector FirstBlock = Blocks[0];
    T.OriginGridCell = FIntPoint(FirstBlock.X, FirstBlock.Y);

    T.BlockList.Reset();
    for (const FIntVector& Block : Blocks)
    {
        if (Block.Z == 0)
        {
            T.BlockList.AddUnique(FIntPoint(Block.X, Block.Y));
        }
    }

    ActiveTasks.Add(T);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelConstruction] Task %d: %s at (%d,%d) -- %d blocks"),
        T.TaskID,
        *T.BuildingTag.ToString(),
        T.OriginGridCell.X,
        T.OriginGridCell.Y,
        T.TotalBlocks
    );

    return T.TaskID;
}

int32 UVoxelConstructionSystem::StartCustomBuildTask(
    int32 SID,
    const TArray<FIntPoint>& Blocks,
    FGameplayTag MaterialTag,
    int32 AgentID)
{
    TArray<FIntVector> Blocks3D;
    Blocks3D.Reserve(Blocks.Num());

    for (const FIntPoint& Block : Blocks)
    {
        Blocks3D.Add(FIntVector(Block.X, Block.Y, 0));
    }

    return StartCustomBuildTask3D(SID, Blocks3D, MaterialTag, AgentID);
}

int32 UVoxelConstructionSystem::StartCustomBuildTask3D(
    int32 SID,
    const TArray<FIntVector>& Blocks,
    FGameplayTag MaterialTag,
    int32 AgentID)
{
    if (Blocks.Num() <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start custom task for settlement %d: empty block list"),
            SID
        );
        return -1;
    }

    if (HasConflictingTaskInBlockList(SID, Blocks))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] Cannot start custom task for settlement %d: explicit block list conflicts with an active task"),
            SID
        );
        return -1;
    }

    const FGameplayTag EffectiveMaterial = MaterialTag.IsValid()
        ? MaterialTag
        : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    UWorld* World = GetWorld();
    USettlementManager* SettlementManager = World ? World->GetSubsystem<USettlementManager>() : nullptr;

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
    T.BuildingTag = FGameplayTag::EmptyTag;
    T.BlockList3D = Blocks;
    T.MaterialTag = EffectiveMaterial;
    T.TotalBlocks = Blocks.Num();
    T.PlacedBlocks = 0;
    T.AssignedAgentID = AgentID;
    T.bComplete = false;

    const FIntVector FirstBlock = Blocks[0];
    T.OriginGridCell = FIntPoint(FirstBlock.X, FirstBlock.Y);

    T.BlockList.Reset();
    for (const FIntVector& Block : Blocks)
    {
        if (Block.Z == 0)
        {
            T.BlockList.AddUnique(FIntPoint(Block.X, Block.Y));
        }
    }

    ActiveTasks.Add(T);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelConstruction] Custom task %d: blocks=%d material=%s"),
        T.TaskID,
        T.TotalBlocks,
        *T.MaterialTag.ToString()
    );

    return T.TaskID;
}

bool UVoxelConstructionSystem::PlaceNextBlock(int32 TaskID, UWorld* World)
{
    FActiveBuildTask* T = GetMutableTask(TaskID);
    if (!T || T->bComplete)
    {
        return false;
    }

    TArray<FIntVector> EffectiveBlocks;
    GetEffectiveBlockList3D(*T, EffectiveBlocks);

    if (EffectiveBlocks.Num() <= 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelConstruction] PlaceNextBlock failed: Task %d has no effective block list"),
            TaskID
        );
        return false;
    }

    if (T->PlacedBlocks >= EffectiveBlocks.Num())
    {
        T->bComplete = true;
        FinalizeBuilding(*T, World);
        return true;
    }

    const FIntVector Block = EffectiveBlocks[T->PlacedBlocks];
    T->PlacedBlocks++;

    if (UVoxelWorldManager* V = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr)
    {
        const FVector WorldPos = V->GridToWorld(FIntPoint(Block.X, Block.Y), Block.Z);

        V->PlaceVoxelBlock(
            WorldPos,
            T->MaterialTag.IsValid()
            ? T->MaterialTag
            : FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")))
        );
    }

    if (T->PlacedBlocks >= EffectiveBlocks.Num())
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

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelConstruction] Complete: %s settlement %d"),
        *T.BuildingTag.ToString(),
        T.SettlementID
    );
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
        {
            return true;
        }
    }
    return false;
}

bool UVoxelConstructionSystem::HasConflictingTaskAtOrigin(int32 SettlementID, FIntPoint Origin) const
{
    for (const FActiveBuildTask& T : ActiveTasks)
    {
        if (T.bComplete)
        {
            continue;
        }

        if (T.SettlementID != SettlementID)
        {
            continue;
        }

        if (T.OriginGridCell == Origin)
        {
            return true;
        }

        if (TaskOccupiesBlock(T, FIntVector(Origin.X, Origin.Y, 0)))
        {
            return true;
        }
    }

    return false;
}

bool UVoxelConstructionSystem::HasConflictingTaskInBlockList(int32 SettlementID, const TArray<FIntVector>& Blocks) const
{
    for (const FIntVector& Block : Blocks)
    {
        for (const FActiveBuildTask& T : ActiveTasks)
        {
            if (T.bComplete)
            {
                continue;
            }

            if (T.SettlementID != SettlementID)
            {
                continue;
            }

            if (TaskOccupiesBlock(T, Block))
            {
                return true;
            }
        }
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

    int32 TrailingDigitStart = INDEX_NONE;
    for (int32 Index = S.Len() - 1; Index >= 0; --Index)
    {
        if (FChar::IsDigit(S[Index]))
        {
            TrailingDigitStart = Index;
        }
        else
        {
            break;
        }
    }

    if (TrailingDigitStart != INDEX_NONE)
    {
        const FString TrailingDigits = S.Mid(TrailingDigitStart);
        const int32 ParsedCount = FCString::Atoi(*TrailingDigits);
        if (ParsedCount > 0)
        {
            return ParsedCount;
        }
    }

    if (S.Contains(TEXT("Stockpile"))) return 8;
    if (S.Contains(TEXT("Kitchen"))) return 16;
    if (S.Contains(TEXT("Carpenter"))) return 20;
    if (S.Contains(TEXT("Barracks"))) return 24;
    if (S.Contains(TEXT("Shrine"))) return 12;

    return 10;
}

TArray<FIntVector> UVoxelConstructionSystem::BuildPrefabBlockList(FGameplayTag BuildTag, FIntPoint Origin) const
{
    TArray<FIntVector> Blocks;

    int32 ExplicitWidth = 0;
    int32 ExplicitDepth = 0;
    int32 ExplicitHeight = 0;

    if (TryParseExplicitPrefabDimensions(BuildTag.ToString(), ExplicitWidth, ExplicitDepth, ExplicitHeight))
    {
        Blocks.Reserve(ExplicitWidth * ExplicitDepth * ExplicitHeight);

        for (int32 Z = 0; Z < ExplicitHeight; ++Z)
        {
            for (int32 Y = 0; Y < ExplicitDepth; ++Y)
            {
                for (int32 X = 0; X < ExplicitWidth; ++X)
                {
                    Blocks.Add(FIntVector(Origin.X + X, Origin.Y + Y, Z));
                }
            }
        }

        return Blocks;
    }

    const int32 RequiredBlocks = GetBlockCount(BuildTag);
    Blocks.Reserve(RequiredBlocks);

    for (int32 BlockNumber = 0; BlockNumber < RequiredBlocks; ++BlockNumber)
    {
        const int32 Layer = BlockNumber / 4;
        const int32 BlockIndexWithinLayer = BlockNumber % 4;

        FIntPoint LocalOffset = FIntPoint::ZeroValue;

        switch (BlockIndexWithinLayer)
        {
        case 0: LocalOffset = FIntPoint(0, 0); break;
        case 1: LocalOffset = FIntPoint(1, 0); break;
        case 2: LocalOffset = FIntPoint(0, 1); break;
        case 3: LocalOffset = FIntPoint(1, 1); break;
        default: break;
        }

        Blocks.Add(FIntVector(
            Origin.X + LocalOffset.X,
            Origin.Y + LocalOffset.Y,
            Layer
        ));
    }

    return Blocks;
}

void UVoxelConstructionSystem::GetEffectiveBlockList3D(const FActiveBuildTask& Task, TArray<FIntVector>& OutBlocks) const
{
    OutBlocks.Reset();

    if (Task.BlockList3D.Num() > 0)
    {
        OutBlocks = Task.BlockList3D;
        return;
    }

    if (Task.BlockList.Num() > 0)
    {
        OutBlocks.Reserve(Task.BlockList.Num());
        for (const FIntPoint& Block2D : Task.BlockList)
        {
            OutBlocks.Add(FIntVector(Block2D.X, Block2D.Y, 0));
        }
        return;
    }

    if (Task.TotalBlocks > 0)
    {
        OutBlocks = BuildPrefabBlockList(Task.BuildingTag, Task.OriginGridCell);
        return;
    }
}

bool UVoxelConstructionSystem::TaskOccupiesBlock(const FActiveBuildTask& Task, const FIntVector& Block) const
{
    TArray<FIntVector> EffectiveBlocks;
    GetEffectiveBlockList3D(Task, EffectiveBlocks);

    for (const FIntVector& ExistingBlock : EffectiveBlocks)
    {
        if (ExistingBlock == Block)
        {
            return true;
        }
    }

    return false;
}

FGameplayTag UVoxelConstructionSystem::ResolveRequiredStockpileItem(FGameplayTag MaterialTag) const
{
    const FString MaterialString = MaterialTag.ToString();

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

int32 UVoxelConstructionSystem::GetRequiredUnitsForPrefab(FGameplayTag BuildTag) const
{
    return BuildTag.IsValid() ? GetBlockCount(BuildTag) : 0;
}

int32 UVoxelConstructionSystem::GetRequiredUnitsForCustom(int32 CellCount) const
{
    return FMath::Max(0, CellCount);
}

FGameplayTag UVoxelConstructionSystem::GetRequiredItemForPrefab(FGameplayTag BuildTag) const
{
    if (!BuildTag.IsValid())
    {
        return FGameplayTag::EmptyTag;
    }

    const FGameplayTag DefaultPrefabMaterial =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    return ResolveRequiredStockpileItem(DefaultPrefabMaterial);
}

FGameplayTag UVoxelConstructionSystem::GetRequiredItemForMaterial(FGameplayTag MaterialTag) const
{
    if (!MaterialTag.IsValid())
    {
        return FGameplayTag::EmptyTag;
    }

    return ResolveRequiredStockpileItem(MaterialTag);
}

int32 UVoxelConstructionSystem::GetAvailableUnitsForPrefab(int32 SettlementID, FGameplayTag BuildTag, UWorld* World) const
{
    if (!World || SettlementID < 0 || !BuildTag.IsValid())
    {
        return 0;
    }

    USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    if (!SettlementManager)
    {
        return 0;
    }

    const FGameplayTag RequiredItem = GetRequiredItemForPrefab(BuildTag);
    return RequiredItem.IsValid() ? SettlementManager->GetStockpileAmount(SettlementID, RequiredItem) : 0;
}

int32 UVoxelConstructionSystem::GetAvailableUnitsForMaterial(int32 SettlementID, FGameplayTag MaterialTag, UWorld* World) const
{
    if (!World || SettlementID < 0 || !MaterialTag.IsValid())
    {
        return 0;
    }

    USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
    if (!SettlementManager)
    {
        return 0;
    }

    const FGameplayTag RequiredItem = GetRequiredItemForMaterial(MaterialTag);
    return RequiredItem.IsValid() ? SettlementManager->GetStockpileAmount(SettlementID, RequiredItem) : 0;
}

bool UVoxelConstructionSystem::CanAffordPrefab(int32 SettlementID, FGameplayTag BuildTag, UWorld* World) const
{
    if (!BuildTag.IsValid())
    {
        return false;
    }

    const FGameplayTag DefaultPrefabMaterial =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    return HasEnoughStockpileForTask(
        SettlementID,
        DefaultPrefabMaterial,
        GetRequiredUnitsForPrefab(BuildTag),
        World
    );
}

bool UVoxelConstructionSystem::CanAffordCustom(int32 SettlementID, FGameplayTag MaterialTag, int32 CellCount, UWorld* World) const
{
    if (!MaterialTag.IsValid())
    {
        return false;
    }

    return HasEnoughStockpileForTask(
        SettlementID,
        MaterialTag,
        GetRequiredUnitsForCustom(CellCount),
        World
    );
}

void UVoxelConstructionSystem::RestoreFromSave(const TArray<FActiveBuildTask>& Saved)
{
    ActiveTasks = Saved;

    for (FActiveBuildTask& T : ActiveTasks)
    {
        if (T.BlockList3D.Num() == 0)
        {
            if (T.BlockList.Num() > 0)
            {
                T.BlockList3D.Reserve(T.BlockList.Num());
                for (const FIntPoint& LegacyBlock : T.BlockList)
                {
                    T.BlockList3D.Add(FIntVector(LegacyBlock.X, LegacyBlock.Y, 0));
                }
            }
            else if (T.TotalBlocks > 0)
            {
                T.BlockList3D = BuildPrefabBlockList(T.BuildingTag, T.OriginGridCell);
            }
        }

        if (T.BlockList3D.Num() > 0)
        {
            T.TotalBlocks = T.BlockList3D.Num();
        }
    }

    NextTaskID = 0;
    for (const FActiveBuildTask& T : ActiveTasks)
    {
        NextTaskID = FMath::Max(NextTaskID, T.TaskID + 1);
    }
}