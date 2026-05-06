// ════════════════════════════════════════════════════════════════════
// ResourceSpawnSubsystem.cpp — v6.0
//
// Deterministic spawning of Shatter Stone ore node actors.
// See header for expansion guide.
// ════════════════════════════════════════════════════════════════════

#include "Mining/ResourceSpawnSubsystem.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

namespace
{
    // Safety budget: physical ore actors are expensive.
    // Resource records can exist globally, but actors should be limited/streamed.
    constexpr int32 GMaxLiveResourceActors = 512;

    // Safety budget per chunk activation.
    // Prevents a single tile/chunk from spawning too many Shatter Stone actors.
    constexpr int32 GMaxNewActorsPerChunkActivation = 3;

    // Log only a few chunks so PIE does not get hammered by thousands of log lines.
    constexpr int32 GMaxChunkSpawnLogsPerSession = 20;
    static int32 GChunkSpawnLogsThisSession = 0;
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    GChunkSpawnLogsThisSession = 0;

    for (int32 i = 0; i < static_cast<int32>(EResourceCategory::COUNT); ++i)
    {
        CategoryDataTables[i] = nullptr;
    }

    UE_LOG(LogTemp, Log, TEXT("[ResourceSpawn] Initialized."));
}

void UResourceSpawnSubsystem::Deinitialize()
{
    // Destroy any surviving actors
    for (auto& Pair : AllNodes)
    {
        if (Pair.Value.SpawnedActor.IsValid())
        {
            Pair.Value.SpawnedActor->Destroy();
        }
    }

    AllNodes.Reset();
    ChunkNodeMap.Reset();
    GChunkSpawnLogsThisSession = 0;

    UE_LOG(LogTemp, Log, TEXT("[ResourceSpawn] Deinitialized."));
    Super::Deinitialize();
}

// ═══════════════════════════════════════════════════════════════════
// Configuration
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::SetOreDataTable(
    EResourceCategory Category, UDataTable* Table)
{
    const int32 Idx = static_cast<int32>(Category);
    if (Idx >= 0 && Idx < static_cast<int32>(EResourceCategory::COUNT))
    {
        CategoryDataTables[Idx] = Table;
        UE_LOG(LogTemp, Log,
            TEXT("[ResourceSpawn] DataTable set for category %d: %s"),
            Idx, Table ? *Table->GetName() : TEXT("null"));
    }
}

void UResourceSpawnSubsystem::SetNodeBlueprintClass(
    TSubclassOf<AActor> InClass)
{
    NodeBlueprintClass = InClass;
    UE_LOG(LogTemp, Log,
        TEXT("[ResourceSpawn] BP class set: %s"),
        InClass ? *InClass->GetName() : TEXT("null"));
}

// ═══════════════════════════════════════════════════════════════════
// Chunk activation — deterministic spawn
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::OnChunkActivated(
    FIntPoint ChunkCell,
    const FWorldTileData& TileData,
    float ChunkWorldSize,
    float SurfaceHeightWorld)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    auto CountLiveActors = [&]() -> int32
        {
            int32 Count = 0;
            for (const auto& Pair : AllNodes)
            {
                if (Pair.Value.SpawnedActor.IsValid())
                {
                    ++Count;
                }
            }
            return Count;
        };

    if (ChunkNodeMap.Contains(ChunkCell))
    {
        // Already known chunk — re-spawn only exposed, non-depleted nodes,
        // and only while under the global actor budget.
        TArray<int32>& ExistingIDs = ChunkNodeMap[ChunkCell];

        int32 LiveActors = CountLiveActors();
        int32 RespawnedActors = 0;

        for (int32 NodeID : ExistingIDs)
        {
            if (LiveActors >= GMaxLiveResourceActors)
            {
                break;
            }

            if (RespawnedActors >= GMaxNewActorsPerChunkActivation)
            {
                break;
            }

            FSpawnedResourceNode* Node = AllNodes.Find(NodeID);
            if (!Node)
            {
                continue;
            }

            // Buried nodes remain data-only until exposed by digging/mining.
            if (!Node->bExposed)
            {
                continue;
            }

            if (Node->bDepleted || Node->SpawnedActor.IsValid() || !NodeBlueprintClass)
            {
                continue;
            }

            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride =
                ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

            AActor* Actor = World->SpawnActor<AActor>(
                NodeBlueprintClass,
                FTransform(FRotator::ZeroRotator, Node->WorldPosition),
                Params);

            if (Actor)
            {
                Node->SpawnedActor = Actor;
                ++LiveActors;
                ++RespawnedActors;
            }
        }

        return;
    }

    const FResourceVeinConfig& Vein = TileData.VeinConfig;
    if (Vein.PresentCategories.Num() == 0)
    {
        return;
    }

    if (!NodeBlueprintClass)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ResourceSpawn] No BP class set. Call SetNodeBlueprintClass() first."));
        return;
    }

    TArray<int32>& ChunkNodes = ChunkNodeMap.Add(ChunkCell);

    int32 LiveActors = CountLiveActors();
    int32 NewActorsThisChunk = 0;
    int32 NewRecordsThisChunk = 0;
    int32 BuriedRecordsThisChunk = 0;
    int32 SkippedActorsDueToBudget = 0;

    for (int32 i = 0; i < Vein.PresentCategories.Num(); ++i)
    {
        const EResourceCategory Category = Vein.PresentCategories[i];

        TArray<FVector> Positions;
        TArray<bool> Exposed;
        ComputeSpawnPositions(
            Vein, i, ChunkCell, ChunkWorldSize,
            SurfaceHeightWorld, Positions, Exposed);

        for (int32 j = 0; j < Positions.Num(); ++j)
        {
            const float Depth = SurfaceHeightWorld - Positions[j].Z;

            FSpawnedResourceNode Node;
            Node.NodeID = NextNodeID++;
            Node.Category = Category;
            Node.ChunkCell = ChunkCell;
            Node.WorldPosition = Positions[j];
            Node.bExposed = Exposed.IsValidIndex(j) ? Exposed[j] : false;
            Node.DataTableRowName = PickOreRowName(Category, Depth);
            Node.ResourceTag = PickResourceTag(Category, Depth);

            // Important:
            // Buried resource nodes are stored as records only.
            // Physical Shatter Stone actors should only exist for exposed/shallow nodes.
            const bool bCanSpawnActor =
                Node.bExposed &&
                LiveActors < GMaxLiveResourceActors &&
                NewActorsThisChunk < GMaxNewActorsPerChunkActivation;

            if (bCanSpawnActor)
            {
                FActorSpawnParameters SpawnParams;
                SpawnParams.SpawnCollisionHandlingOverride =
                    ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

                AActor* SpawnedActor = World->SpawnActor<AActor>(
                    NodeBlueprintClass,
                    FTransform(FRotator::ZeroRotator, Positions[j]),
                    SpawnParams);

                if (SpawnedActor)
                {
                    Node.SpawnedActor = SpawnedActor;
                    ++LiveActors;
                    ++NewActorsThisChunk;

                    // TODO: Wire this up to your BP_Mining_Node_Master.
                    // Option A: Add a BlueprintCallable SetOreConfig(FName RowName, int32 NodeID)
                    //   on BP_Mining_Node_Master and call it here via interface.
                    // Option B: Use a C++ interface IResourceNodeInterface.
                    // Option C: Set variables directly if the master BP exposes them.
                }
            }
            else
            {
                if (!Node.bExposed)
                {
                    ++BuriedRecordsThisChunk;
                }
                else
                {
                    ++SkippedActorsDueToBudget;
                }
            }

            AllNodes.Add(Node.NodeID, Node);
            ChunkNodes.Add(Node.NodeID);
            ++NewRecordsThisChunk;
        }
    }

    if (ChunkNodes.Num() > 0 && GChunkSpawnLogsThisSession < GMaxChunkSpawnLogsPerSession)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[ResourceSpawn] Chunk (%d,%d): records=%d actors=%d buried=%d skippedByBudget=%d liveActors=%d/%d"),
            ChunkCell.X,
            ChunkCell.Y,
            NewRecordsThisChunk,
            NewActorsThisChunk,
            BuriedRecordsThisChunk,
            SkippedActorsDueToBudget,
            LiveActors,
            GMaxLiveResourceActors);

        ++GChunkSpawnLogsThisSession;
    }
    else if (ChunkNodes.Num() > 0 && GChunkSpawnLogsThisSession == GMaxChunkSpawnLogsPerSession)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ResourceSpawn] Further per-chunk spawn logs suppressed for this session."));
        ++GChunkSpawnLogsThisSession;
    }
}

// ═══════════════════════════════════════════════════════════════════
// Chunk deactivation — despawn actors, keep records
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::OnChunkDeactivated(FIntPoint ChunkCell)
{
    TArray<int32>* NodeIDs = ChunkNodeMap.Find(ChunkCell);
    if (!NodeIDs) return;

    for (int32 NodeID : *NodeIDs)
    {
        FSpawnedResourceNode* Node = AllNodes.Find(NodeID);
        if (Node && Node->SpawnedActor.IsValid())
        {
            Node->SpawnedActor->Destroy();
            Node->SpawnedActor = nullptr;
        }
    }

    // Don't remove from ChunkNodeMap — records persist for re-activation
}

// ═══════════════════════════════════════════════════════════════════
// Queries
// ═══════════════════════════════════════════════════════════════════

TArray<FSpawnedResourceNode> UResourceSpawnSubsystem::GetNodesInRadius(
    FVector WorldPos, float Radius) const
{
    TArray<FSpawnedResourceNode> Result;
    const float RadiusSq = Radius * Radius;

    for (const auto& Pair : AllNodes)
    {
        const FSpawnedResourceNode& Node = Pair.Value;
        if (Node.bDepleted) continue;

        if (FVector::DistSquared(WorldPos, Node.WorldPosition) <= RadiusSq)
        {
            Result.Add(Node);
        }
    }

    return Result;
}

bool UResourceSpawnSubsystem::HasNodeInRadius(
    FVector WorldPos, float Radius) const
{
    const float RadiusSq = Radius * Radius;
    for (const auto& Pair : AllNodes)
    {
        if (Pair.Value.bDepleted) continue;
        if (FVector::DistSquared(WorldPos, Pair.Value.WorldPosition) <= RadiusSq)
        {
            return true;
        }
    }
    return false;
}

bool UResourceSpawnSubsystem::GetNodeByID(
    int32 NodeID, FSpawnedResourceNode& OutNode) const
{
    const FSpawnedResourceNode* Node = AllNodes.Find(NodeID);
    if (Node)
    {
        OutNode = *Node;
        return true;
    }
    return false;
}

// ═══════════════════════════════════════════════════════════════════
// Depletion
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::MarkNodeDepleted(int32 NodeID)
{
    FSpawnedResourceNode* Node = AllNodes.Find(NodeID);
    if (Node)
    {
        Node->bDepleted = true;
        // Actor destruction is handled by Shatter Stone's own shatter logic
        UE_LOG(LogTemp, Log,
            TEXT("[ResourceSpawn] Node %d depleted at (%s)"),
            NodeID, *Node->WorldPosition.ToString());
    }
}

void UResourceSpawnSubsystem::MarkNodeExposed(int32 NodeID)
{
    FSpawnedResourceNode* Node = AllNodes.Find(NodeID);
    if (!Node)
    {
        return;
    }

    Node->bExposed = true;

    if (!NodeBlueprintClass || Node->bDepleted || Node->SpawnedActor.IsValid())
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("[ResourceSpawn] Node %d marked exposed at (%s)"),
            NodeID, *Node->WorldPosition.ToString());
        return;
    }

    int32 LiveActors = 0;
    for (const auto& Pair : AllNodes)
    {
        if (Pair.Value.SpawnedActor.IsValid())
        {
            ++LiveActors;
        }
    }

    if (LiveActors >= GMaxLiveResourceActors)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[ResourceSpawn] Node %d exposed but actor budget is full. LiveActors=%d/%d"),
            NodeID, LiveActors, GMaxLiveResourceActors);
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

    AActor* SpawnedActor = World->SpawnActor<AActor>(
        NodeBlueprintClass,
        FTransform(FRotator::ZeroRotator, Node->WorldPosition),
        SpawnParams);

    if (SpawnedActor)
    {
        Node->SpawnedActor = SpawnedActor;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[ResourceSpawn] Node %d exposed at (%s). ActorSpawned=%s"),
        NodeID,
        *Node->WorldPosition.ToString(),
        SpawnedActor ? TEXT("true") : TEXT("false"));
}

// ═══════════════════════════════════════════════════════════════════
// Save / Load
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::RestoreNodeRecords(
    const TMap<int32, FSpawnedResourceNode>& InRecords)
{
    AllNodes = InRecords;

    // Rebuild chunk→node mapping
    ChunkNodeMap.Reset();
    int32 MaxID = 0;
    for (const auto& Pair : AllNodes)
    {
        TArray<int32>& Nodes = ChunkNodeMap.FindOrAdd(Pair.Value.ChunkCell);
        Nodes.Add(Pair.Key);
        MaxID = FMath::Max(MaxID, Pair.Key);
    }
    NextNodeID = MaxID + 1;

    UE_LOG(LogTemp, Log,
        TEXT("[ResourceSpawn] Restored %d node records. NextID=%d"),
        AllNodes.Num(), NextNodeID);
}

// ═══════════════════════════════════════════════════════════════════
// Spawn position computation (fully deterministic)
// ═══════════════════════════════════════════════════════════════════

void UResourceSpawnSubsystem::ComputeSpawnPositions(
    const FResourceVeinConfig& VeinConfig,
    int32 VeinIndex,
    FIntPoint ChunkCell,
    float ChunkWorldSize,
    float SurfaceHeightWorld,
    TArray<FVector>& OutPositions,
    TArray<bool>& OutExposed)
{
    OutPositions.Reset();
    OutExposed.Reset();

    if (!VeinConfig.SpawnSeeds.IsValidIndex(VeinIndex))
        return;

    const int64 SpawnSeed = VeinConfig.SpawnSeeds[VeinIndex];
    const float Richness = VeinConfig.VeinRichness[VeinIndex];
    const FVector2D DepthRange = VeinConfig.DepthRanges[VeinIndex];

    // Node count scales with richness, but keep this conservative.
    // The world can contain thousands of resource-bearing tiles.
    // Higher density should be enabled later through chunk streaming / mining exposure.
    const int32 MaxNodes = FMath::Clamp(
        FMath::RoundToInt(Richness * 3.0f), 1, 2);

    // Simple LCG for deterministic placement (matches WorldForgeRNG's algorithm)
    int64 State = SpawnSeed;
    auto NextFloat01 = [&State]() -> float
        {
            State = State * 6364136223846793005LL + 1442695040888963407LL;
            const uint64 U = static_cast<uint64>(State);
            return static_cast<float>(
                static_cast<double>(U >> 11) / static_cast<double>(1ULL << 53));
        };

    // Chunk world-space origin
    // NOTE: This assumes chunks are laid out on a grid centered at world origin.
    // Wire up the actual chunk origin from VoxelWorldManager if your layout differs.
    const float ChunkWorldX = static_cast<float>(ChunkCell.X) * ChunkWorldSize;
    const float ChunkWorldY = static_cast<float>(ChunkCell.Y) * ChunkWorldSize;

    for (int32 i = 0; i < MaxNodes; ++i)
    {
        const float LocalX = NextFloat01() * ChunkWorldSize;
        const float LocalY = NextFloat01() * ChunkWorldSize;
        const float DepthFrac = NextFloat01();
        const float Depth = FMath::Lerp(DepthRange.X, DepthRange.Y, DepthFrac);

        const float WorldX = ChunkWorldX + LocalX;
        const float WorldY = ChunkWorldY + LocalY;
        float WorldZ = SurfaceHeightWorld - Depth;

        // "Exposed" — shallow nodes that stick out of the ground
        const bool bIsExposed = (Depth < ExposedDepthThreshold);
        if (bIsExposed)
        {
            WorldZ += ExposedNodeZOffset;
        }

        OutPositions.Add(FVector(WorldX, WorldY, WorldZ));
        OutExposed.Add(bIsExposed);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Ore row / tag mapping
//
// These functions map (Category, Depth) → Shatter Stone Data Table
// row name and gameplay tag.  Expand these when you add new packs.
// ═══════════════════════════════════════════════════════════════════

FName UResourceSpawnSubsystem::PickOreRowName(
    EResourceCategory Category, float Depth)
{
    switch (Category)
    {
    case EResourceCategory::Metal:
        if (Depth < 15.0f) return FName(TEXT("MetalOre1"));  // Copper (shallow)
        if (Depth < 40.0f) return FName(TEXT("MetalOre2"));  // Iron (mid)
        if (Depth < 70.0f) return FName(TEXT("MetalOre3"));  // Silver (deep)
        return FName(TEXT("MetalOre4"));                      // Gold (very deep)

    case EResourceCategory::Crystal:
        if (Depth < 30.0f) return FName(TEXT("MetalOre5"));  // Placeholder — swap to Crystal pack rows
        return FName(TEXT("MetalOre6"));                      // Placeholder

    case EResourceCategory::Fossil:
        return FName(TEXT("MetalOre1"));                      // Placeholder — swap to Fossil pack rows

    default:
        return FName(TEXT("MetalOre1"));
    }
}

FGameplayTag UResourceSpawnSubsystem::PickResourceTag(
    EResourceCategory Category, float Depth)
{
    switch (Category)
    {
    case EResourceCategory::Metal:
        if (Depth < 15.0f)
            return FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Resource.Copper")), false);
        if (Depth < 40.0f)
            return FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Resource.Iron")), false);
        if (Depth < 70.0f)
            return FGameplayTag::RequestGameplayTag(
                FName(TEXT("WorldForge.Resource.Ore")), false);
        return FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Resource.Gold")), false);

    case EResourceCategory::Crystal:
        return FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Resource.Crystal")), false);

    case EResourceCategory::Fossil:
        return FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Resource.Fossil")), false);

    default:
        return FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Resource.Ore")), false);
    }
}