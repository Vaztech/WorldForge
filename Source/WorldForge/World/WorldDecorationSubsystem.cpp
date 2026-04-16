#include "WorldDecorationSubsystem.h"

#include "Actors/WorldResourceActor.h"
#include "NavigationContext.h"
#include "Voxel/VoxelWorldManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
    static FGameplayTag GetForestBiomeTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")), false);
    }

    static FGameplayTag GetCoastalBiomeTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    }

    static FGameplayTag GetWoodResourceTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Wood")), false);
    }

    static FGameplayTag GetStoneResourceTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Resource.Stone")), false);
    }

    static uint32 Hash32(uint32 Value)
    {
        Value ^= 2747636419u;
        Value *= 2654435769u;
        Value ^= Value >> 16;
        Value *= 2654435769u;
        Value ^= Value >> 16;
        Value *= 2654435769u;
        return Value;
    }

    static float HashToUnitFloat(int32 X, int32 Y, int64 Seed)
    {
        uint32 H = Hash32(static_cast<uint32>(X) * 73856093u);
        H ^= Hash32(static_cast<uint32>(Y) * 19349663u);
        H ^= Hash32(static_cast<uint32>(Seed & 0xFFFFFFFF));
        return static_cast<float>(H) / static_cast<float>(MAX_uint32);
    }

    static int32 GetFootprintSize(EWorldDecorationFootprint Footprint)
    {
        switch (Footprint)
        {
        case EWorldDecorationFootprint::TwoByTwo:
            return 2;
        case EWorldDecorationFootprint::ThreeByThree:
            return 3;
        case EWorldDecorationFootprint::OneByOne:
        default:
            return 1;
        }
    }

    static void BuildFootprintOffsets(EWorldDecorationFootprint Footprint, TArray<FIntPoint>& OutOffsets)
    {
        OutOffsets.Reset();

        switch (Footprint)
        {
        case EWorldDecorationFootprint::OneByOne:
            OutOffsets.Add(FIntPoint(0, 0));
            break;

        case EWorldDecorationFootprint::TwoByTwo:
            OutOffsets.Add(FIntPoint(0, 0));
            OutOffsets.Add(FIntPoint(1, 0));
            OutOffsets.Add(FIntPoint(0, 1));
            OutOffsets.Add(FIntPoint(1, 1));
            break;

        case EWorldDecorationFootprint::ThreeByThree:
            for (int32 Y = -1; Y <= 1; ++Y)
            {
                for (int32 X = -1; X <= 1; ++X)
                {
                    OutOffsets.Add(FIntPoint(X, Y));
                }
            }
            break;
        }
    }

    static bool IsGridCellValid(FIntPoint Cell, int32 GridWidth, int32 GridHeight)
    {
        return
            Cell.X >= 0 &&
            Cell.Y >= 0 &&
            Cell.X < GridWidth &&
            Cell.Y < GridHeight;
    }

    static int32 GridIndex(FIntPoint Cell, int32 GridWidth)
    {
        return (Cell.Y * GridWidth) + Cell.X;
    }

    static bool IsCellWithinTolerance(const FIntPoint& A, const FIntPoint& B, int32 Tolerance = 1)
    {
        return
            FMath::Abs(A.X - B.X) <= Tolerance &&
            FMath::Abs(A.Y - B.Y) <= Tolerance;
    }
}

UWorldDecorationSubsystem::UWorldDecorationSubsystem()
{
}

void UWorldDecorationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    RegisterDefaultEntries();

    UE_LOG(LogTemp, Log, TEXT("[WorldDecoration] Initialized."));
}

void UWorldDecorationSubsystem::Deinitialize()
{
    ClearGeneratedDecorations();
    DefaultEntries.Reset();

    Super::Deinitialize();
}

void UWorldDecorationSubsystem::RegisterDefaultEntries()
{
    if (bDefaultsRegistered)
    {
        return;
    }

    bDefaultsRegistered = true;
    DefaultEntries.Reset();

    {
        FWorldDecorationEntry Entry;
        Entry.EntryName = TEXT("Tree_2x2");
        Entry.MeshAsset = TSoftObjectPtr<UStaticMesh>(
            FSoftObjectPath(TEXT("/Script/Engine.StaticMesh'/Game/MedCastle/Mesh/Foliage_Rocks/SM_TreeOakFix.SM_TreeOakFix'"))
        );
        Entry.AllowedBiomeTag = GetForestBiomeTag();
        Entry.ResourceTag = GetWoodResourceTag();
        Entry.Footprint = EWorldDecorationFootprint::TwoByTwo;
        Entry.MaxHeightDeltaVoxels = 1;
        Entry.BiomeTransitionAllowanceCells = 2;
        Entry.bRejectWaterOrCoastAtAnchor = true;
        Entry.bBlocksNavigation = true;
        Entry.bChoppable = true;
        Entry.ClusterCellSize = 14;
        Entry.ClusterChance = 0.35f;
        Entry.Density = 0.015f;
        Entry.SeedOffset = 101;
        DefaultEntries.Add(Entry);
    }

    {
        FWorldDecorationEntry Entry;
        Entry.EntryName = TEXT("Bush_1x1");
        Entry.MeshAsset = TSoftObjectPtr<UStaticMesh>(
            FSoftObjectPath(TEXT("/Script/Engine.StaticMesh'/Game/A_Surface_Footstep/Geometry/SM_Bush.SM_Bush'"))
        );
        Entry.AllowedBiomeTag = GetForestBiomeTag();
        Entry.ResourceTag = GetWoodResourceTag();
        Entry.Footprint = EWorldDecorationFootprint::OneByOne;
        Entry.MaxHeightDeltaVoxels = 1;
        Entry.BiomeTransitionAllowanceCells = 2;
        Entry.bRejectWaterOrCoastAtAnchor = true;
        Entry.bBlocksNavigation = true;
        Entry.bChoppable = true;
        Entry.ClusterCellSize = 12;
        Entry.ClusterChance = 0.35f;
        Entry.Density = 0.025f;
        Entry.SeedOffset = 202;
        DefaultEntries.Add(Entry);
    }

    {
        FWorldDecorationEntry Entry;
        Entry.EntryName = TEXT("Rock_3x3");
        Entry.MeshAsset = TSoftObjectPtr<UStaticMesh>(
            FSoftObjectPath(TEXT("/Script/Engine.StaticMesh'/Game/MedCastle/Mesh/Foliage_Rocks/SM_Boulder6.SM_Boulder6'"))
        );
        Entry.AllowedBiomeTag = GetForestBiomeTag();
        Entry.ResourceTag = GetStoneResourceTag();
        Entry.Footprint = EWorldDecorationFootprint::ThreeByThree;
        Entry.MaxHeightDeltaVoxels = 1;
        Entry.BiomeTransitionAllowanceCells = 2;
        Entry.bRejectWaterOrCoastAtAnchor = true;
        Entry.bBlocksNavigation = true;
        Entry.bChoppable = true;
        Entry.ClusterCellSize = 16;
        Entry.ClusterChance = 0.30f;
        Entry.Density = 0.008f;
        Entry.SeedOffset = 303;
        DefaultEntries.Add(Entry);
    }
}

void UWorldDecorationSubsystem::ClearGeneratedDecorations()
{
    for (AActor* Actor : SpawnedActors)
    {
        if (IsValid(Actor))
        {
            Actor->Destroy();
        }
    }

    SpawnedActors.Reset();
    GeneratedRecords.Reset();
}

bool UWorldDecorationSubsystem::IsCandidateNearAllowedBiome(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    FIntPoint AnchorCell,
    const FWorldDecorationEntry& Entry
) const
{
    if (!Entry.AllowedBiomeTag.IsValid())
    {
        return true;
    }

    const int32 Radius = FMath::Max(0, Entry.BiomeTransitionAllowanceCells);
    for (int32 Y = AnchorCell.Y - Radius; Y <= AnchorCell.Y + Radius; ++Y)
    {
        for (int32 X = AnchorCell.X - Radius; X <= AnchorCell.X + Radius; ++X)
        {
            const FIntPoint Candidate(X, Y);
            if (!IsGridCellValid(Candidate, GridWidth, GridHeight))
            {
                continue;
            }

            const int32 Index = GridIndex(Candidate, GridWidth);
            if (!WorldTiles.IsValidIndex(Index))
            {
                continue;
            }

            if (WorldTiles[Index].BiomeType.IsValid() &&
                WorldTiles[Index].BiomeType.MatchesTag(Entry.AllowedBiomeTag))
            {
                return true;
            }
        }
    }

    return false;
}

bool UWorldDecorationSubsystem::PassesAnchorTerrainRules(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    FIntPoint AnchorCell,
    const FWorldDecorationEntry& Entry
) const
{
    if (!IsGridCellValid(AnchorCell, GridWidth, GridHeight))
    {
        return false;
    }

    const int32 Index = GridIndex(AnchorCell, GridWidth);
    if (!WorldTiles.IsValidIndex(Index))
    {
        return false;
    }

    const FWorldTileData& Tile = WorldTiles[Index];

    if (!IsCandidateNearAllowedBiome(WorldTiles, GridWidth, GridHeight, AnchorCell, Entry))
    {
        return false;
    }

    if (!Entry.bRejectWaterOrCoastAtAnchor)
    {
        return true;
    }

    if (Tile.WaterDepth > KINDA_SMALL_NUMBER)
    {
        return false;
    }

    const FGameplayTag CoastalBiomeTag = GetCoastalBiomeTag();
    if (CoastalBiomeTag.IsValid() &&
        Tile.BiomeType.IsValid() &&
        Tile.BiomeType.MatchesTag(CoastalBiomeTag))
    {
        return false;
    }

    return true;
}

bool UWorldDecorationSubsystem::PassesDistributionRules(
    FIntPoint AnchorCell,
    int64 WorldSeed,
    const FWorldDecorationEntry& Entry
) const
{
    const int64 EntrySeed = WorldSeed + static_cast<int64>(Entry.SeedOffset);

    const int32 SafeClusterSize = FMath::Max(1, Entry.ClusterCellSize);
    const FIntPoint ClusterCoord(
        FMath::FloorToInt(static_cast<float>(AnchorCell.X) / static_cast<float>(SafeClusterSize)),
        FMath::FloorToInt(static_cast<float>(AnchorCell.Y) / static_cast<float>(SafeClusterSize))
    );

    const float ClusterRoll = HashToUnitFloat(ClusterCoord.X, ClusterCoord.Y, EntrySeed);
    if (ClusterRoll > Entry.ClusterChance)
    {
        return false;
    }

    const float DensityRoll = HashToUnitFloat(AnchorCell.X, AnchorCell.Y, EntrySeed ^ 0x4D2);
    return DensityRoll <= Entry.Density;
}

bool UWorldDecorationSubsystem::TryEvaluatePlacement(
    UVoxelWorldManager* VoxelManager,
    int32 GridWidth,
    int32 GridHeight,
    FIntPoint AnchorCell,
    const FWorldDecorationEntry& Entry,
    FVector& OutWorldLocation,
    TArray<FIntPoint>& OutOccupiedWorldCells
) const
{
    OutWorldLocation = FVector::ZeroVector;
    OutOccupiedWorldCells.Reset();

    if (!VoxelManager || !VoxelManager->HasCachedTerrainData())
    {
        return false;
    }

    TArray<FIntPoint> Offsets;
    BuildFootprintOffsets(Entry.Footprint, Offsets);

    float MinZ = TNumericLimits<float>::Max();
    float MaxZ = TNumericLimits<float>::Lowest();

    for (const FIntPoint& Offset : Offsets)
    {
        const FIntPoint WorldCell = AnchorCell + Offset;

        if (!IsGridCellValid(WorldCell, GridWidth, GridHeight))
        {
            return false;
        }

        FVector CellWorldLocation = FVector::ZeroVector;
        if (!VoxelManager->TryGetSurfaceWorldLocation(WorldCell, CellWorldLocation, 0.0f))
        {
            return false;
        }

        const FIntPoint RoundTripCell = VoxelManager->WorldToTerrainCell(CellWorldLocation);
        if (!IsCellWithinTolerance(RoundTripCell, WorldCell, 1))
        {
            UE_LOG(
                LogTemp,
                VeryVerbose,
                TEXT("[WorldDecoration] Reject placement cell round-trip mismatch Entry=%s Anchor=(%d,%d) Cell=(%d,%d) RoundTrip=(%d,%d)"),
                *Entry.EntryName,
                AnchorCell.X,
                AnchorCell.Y,
                WorldCell.X,
                WorldCell.Y,
                RoundTripCell.X,
                RoundTripCell.Y
            );
            return false;
        }

        MinZ = FMath::Min(MinZ, CellWorldLocation.Z);
        MaxZ = FMath::Max(MaxZ, CellWorldLocation.Z);

        OutOccupiedWorldCells.Add(WorldCell);
    }

    const float VoxelSize = FMath::Max(1.0f, VoxelManager->VoxelSize);
    const int32 HeightDeltaVoxels = FMath::RoundToInt((MaxZ - MinZ) / VoxelSize);

    if (HeightDeltaVoxels > Entry.MaxHeightDeltaVoxels)
    {
        return false;
    }

    FVector AnchorWorldLocation = FVector::ZeroVector;
    if (!VoxelManager->TryGetSurfaceWorldLocation(AnchorCell, AnchorWorldLocation, 0.0f))
    {
        return false;
    }

    const FIntPoint AnchorRoundTrip = VoxelManager->WorldToTerrainCell(AnchorWorldLocation);
    if (!IsCellWithinTolerance(AnchorRoundTrip, AnchorCell, 1))
    {
        UE_LOG(
            LogTemp,
            VeryVerbose,
            TEXT("[WorldDecoration] Reject anchor round-trip mismatch Entry=%s Anchor=(%d,%d) RoundTrip=(%d,%d)"),
            *Entry.EntryName,
            AnchorCell.X,
            AnchorCell.Y,
            AnchorRoundTrip.X,
            AnchorRoundTrip.Y
        );
        return false;
    }

    OutWorldLocation = AnchorWorldLocation;
    return true;
}

void UWorldDecorationSubsystem::ApplyNavigationBlocking(
    UNavigationContext* NavigationContext,
    const TArray<FIntPoint>& OccupiedWorldCells
)
{
    if (!NavigationContext || OccupiedWorldCells.Num() == 0)
    {
        return;
    }

    TSet<FIntPoint> BlockedSet;
    for (const FIntPoint& Cell : OccupiedWorldCells)
    {
        BlockedSet.Add(Cell);
    }

    const TArray<int32> SettlementIDs = NavigationContext->GetRegisteredSettlementIDs();
    for (const int32 SettlementID : SettlementIDs)
    {
        const TMap<FIntPoint, FNavCell>& Grid = NavigationContext->GetGrid(SettlementID);

        for (const TPair<FIntPoint, FNavCell>& Pair : Grid)
        {
            const FIntPoint WorldCell = NavigationContext->NavToWorld(SettlementID, Pair.Key);
            if (BlockedSet.Contains(WorldCell))
            {
                NavigationContext->SetCellWalkable(SettlementID, Pair.Key, false);
            }
        }
    }
}

bool UWorldDecorationSubsystem::GenerateDecorations(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    int64 WorldSeed
)
{
    ClearGeneratedDecorations();

    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldDecoration] GenerateDecorations failed: world is null."));
        return false;
    }

    UVoxelWorldManager* VoxelManager = World->GetSubsystem<UVoxelWorldManager>();
    if (!VoxelManager || !VoxelManager->IsTerrainRuntimeStable())
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldDecoration] GenerateDecorations failed: terrain is not runtime-stable."));
        return false;
    }

    UNavigationContext* NavigationContext = World->GetSubsystem<UNavigationContext>();

    TSet<FIntPoint> OccupiedCells;

    for (const FWorldDecorationEntry& Entry : DefaultEntries)
    {
        if (Entry.EntryName.IsEmpty())
        {
            continue;
        }

        UClass* SpawnClass = Entry.ActorClass.IsNull()
            ? AWorldResourceActor::StaticClass()
            : Entry.ActorClass.LoadSynchronous();

        if (!SpawnClass)
        {
            UE_LOG(LogTemp, Warning, TEXT("[WorldDecoration] Entry '%s' skipped: ActorClass failed to load."), *Entry.EntryName);
            continue;
        }

        UStaticMesh* MeshAsset = Entry.MeshAsset.IsNull() ? nullptr : Entry.MeshAsset.LoadSynchronous();
        if (!MeshAsset)
        {
            UE_LOG(LogTemp, Warning, TEXT("[WorldDecoration] Entry '%s' skipped: MeshAsset failed to load."), *Entry.EntryName);
            continue;
        }

        for (int32 Index = 0; Index < WorldTiles.Num(); ++Index)
        {
            const FIntPoint AnchorCell(Index % GridWidth, Index / GridWidth);

            if (!PassesAnchorTerrainRules(WorldTiles, GridWidth, GridHeight, AnchorCell, Entry))
            {
                continue;
            }

            if (!PassesDistributionRules(AnchorCell, WorldSeed, Entry))
            {
                continue;
            }

            FVector SpawnWorldLocation = FVector::ZeroVector;
            TArray<FIntPoint> FootprintCells;
            if (!TryEvaluatePlacement(VoxelManager, GridWidth, GridHeight, AnchorCell, Entry, SpawnWorldLocation, FootprintCells))
            {
                continue;
            }

            bool bConflicts = false;
            for (const FIntPoint& Cell : FootprintCells)
            {
                if (OccupiedCells.Contains(Cell))
                {
                    bConflicts = true;
                    break;
                }
            }

            if (bConflicts)
            {
                continue;
            }

            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            SpawnParams.Name = NAME_None;

            AActor* SpawnedActor = World->SpawnActor<AActor>(
                SpawnClass,
                SpawnWorldLocation,
                FRotator::ZeroRotator,
                SpawnParams
            );

            if (!SpawnedActor)
            {
                continue;
            }

            SpawnedActor->Tags.AddUnique(FName(TEXT("WorldForgeGeneratedDecoration")));

            if (Entry.ResourceTag.IsValid())
            {
                SpawnedActor->Tags.AddUnique(FName(*Entry.ResourceTag.ToString()));
            }
            if (Entry.bChoppable)
            {
                SpawnedActor->Tags.AddUnique(FName(TEXT("WorldForgeChoppable")));
            }
            if (Entry.bBlocksNavigation)
            {
                SpawnedActor->Tags.AddUnique(FName(TEXT("WorldForgeBlocksNavigation")));
            }

            if (AWorldResourceActor* ResourceActor = Cast<AWorldResourceActor>(SpawnedActor))
            {
                ResourceActor->ApplyDecorationMesh(MeshAsset);
                ResourceActor->ApplyDecorationMetadata(
                    Entry.ResourceTag,
                    Entry.bBlocksNavigation,
                    Entry.bChoppable,
                    Entry.Footprint
                );
            }

            FWorldDecorationSpawnRecord Record;
            Record.EntryName = Entry.EntryName;
            Record.ActorClassPath = SpawnClass ? SpawnClass->GetPathName() : FString();
            Record.MeshAssetPath = Entry.MeshAsset.ToSoftObjectPath().ToString();
            Record.ResourceTag = Entry.ResourceTag;
            Record.AnchorCell = AnchorCell;
            Record.FootprintSize = GetFootprintSize(Entry.Footprint);
            Record.bBlocksNavigation = Entry.bBlocksNavigation;
            Record.bChoppable = Entry.bChoppable;
            Record.WorldLocation = SpawnWorldLocation;
            Record.Rotation = FRotator::ZeroRotator;
            Record.Scale = FVector(1.0f, 1.0f, 1.0f);
            GeneratedRecords.Add(Record);

            SpawnedActors.Add(SpawnedActor);

            for (const FIntPoint& Cell : FootprintCells)
            {
                OccupiedCells.Add(Cell);
            }

            if (Entry.bBlocksNavigation)
            {
                ApplyNavigationBlocking(NavigationContext, FootprintCells);
            }
        }
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldDecoration] Generated %d world decoration actors."), SpawnedActors.Num());
    return SpawnedActors.Num() > 0;
}