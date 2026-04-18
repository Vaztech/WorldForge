#include "VoxelWorldManager.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameplayTagContainer.h"
#include "VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Utility/WorldForgeTerrainSettings.h"

void UVoxelWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    const UWorldForgeTerrainSettings* Settings = GetDefault<UWorldForgeTerrainSettings>();
    if (Settings)
    {
        ChunkWorldSize = Settings->ChunkWorldSize;
        VoxelSize = Settings->VoxelSize;
        TerrainSurfaceZOffset = Settings->TerrainSurfaceZOffset;
        bAddGlobalFoundation = Settings->bAddGlobalFoundation;
        GlobalFoundationDepthWorld = Settings->GlobalFoundationDepthWorld;

        DefaultPerlinSettings = Settings->DefaultPerlinSettings;

        SandMaxElevationNormalized = Settings->SandMaxElevationNormalized;
        RockMinElevationNormalized = Settings->RockMinElevationNormalized;
        SnowMinElevationNormalized = Settings->SnowMinElevationNormalized;

        SeaLevelVoxels = Settings->SeaLevelVoxels;

        ContinentFrequency = Settings->ContinentFrequency;
        ContinentHeightRangeVoxels = Settings->ContinentHeightRangeVoxels;
        ContinentExponent = Settings->ContinentExponent;

        BasinFrequency = Settings->BasinFrequency;
        BasinThreshold = Settings->BasinThreshold;
        LakeBasinDepthVoxels = Settings->LakeBasinDepthVoxels;

        RiverFrequency = Settings->RiverFrequency;
        RiverThreshold = Settings->RiverThreshold;
        RiverCarveDepthVoxels = Settings->RiverCarveDepthVoxels;
        RiverSharpness = Settings->RiverSharpness;

        bUseFastTerrainSampling = Settings->bUseFastTerrainSampling;

        bLogGridConversions = Settings->bLogGridConversions;
        bLogTerrainGeneration = Settings->bLogTerrainGeneration;
        bLogTerrainRetry = Settings->bLogTerrainRetry;
        bUseFallbackPlacedBlockVisuals = Settings->bUseFallbackPlacedBlockVisuals;
        bPreferSurfaceAnchoredPlacedBlocks = Settings->bPreferSurfaceAnchoredPlacedBlocks;
        bLogPlacedBlockVisuals = Settings->bLogPlacedBlockVisuals;
        FallbackPlacedBlockVerticalNudge = Settings->FallbackPlacedBlockVerticalNudge;

        TerrainRetryIntervalSeconds = Settings->TerrainRetryIntervalSeconds;
        MaxTerrainRetryCount = Settings->MaxTerrainRetryCount;
    }

    ClearTerrainSnapshot();

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Initialized."));
}

void UVoxelWorldManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TerrainRetryTimerHandle);
    }

    ClearPendingTerrainRequest();
    ClearTerrainSnapshot();
    ClearTerrainCache();
    ClearPlacedBlockVisuals();

    Super::Deinitialize();
}

bool UVoxelWorldManager::IsReady()
{
    if (VoxelWorldActor)
    {
        return true;
    }

    return FindAndCacheVoxelWorld();
}

bool UVoxelWorldManager::IsTerrainReadyForGameplay() const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    if (!HasTerrainSnapshot())
    {
        return false;
    }

    if (!VoxelWorldActor)
    {
        return false;
    }

    const AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    return VoxelWorld && VoxelWorld->IsCreated();
}

bool UVoxelWorldManager::IsTerrainRuntimeStable() const
{
    return IsTerrainReadyForGameplay() && !HasPendingTerrainRequest();
}

bool UVoxelWorldManager::FindAndCacheVoxelWorld()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] FindAndCacheVoxelWorld failed: GetWorld() returned null."));
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Searching for VoxelWorld..."));

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        if (It->GetClass()->GetName().Contains(TEXT("VoxelWorld")))
        {
            VoxelWorldActor = *It;
            UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Found: %s"), *It->GetName());
            return true;
        }
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[VoxelWorldManager] No VoxelWorld actor found. Fix: Place a VoxelWorld actor in the current level.")
    );
    return false;
}

FVector UVoxelWorldManager::GetVoxelWorldOrigin() const
{
    if (!VoxelWorldActor)
    {
        const_cast<UVoxelWorldManager*>(this)->FindAndCacheVoxelWorld();
    }

    return VoxelWorldActor ? VoxelWorldActor->GetActorLocation() : FVector::ZeroVector;
}

float UVoxelWorldManager::GetRenderVoxelSize() const
{
    if (VoxelWorldActor)
    {
        if (const AVoxelWorld* VW = Cast<AVoxelWorld>(VoxelWorldActor))
        {
            return FMath::Max(KINDA_SMALL_NUMBER, VW->VoxelSize);
        }
    }

    return 1.0f;
}

int32 UVoxelWorldManager::GetChunkSizeInVoxels() const
{
    const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
    return FMath::Max(1, FMath::RoundToInt(ChunkWorldSize / SafeVoxelSize));
}

int32 UVoxelWorldManager::ComputeChunkBaseDepthVoxels(const FVoxelBiomeProfile& Profile) const
{
    const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
    const int32 DerivedDepth = FMath::RoundToInt((Profile.NoiseAmplitude * 1.00f) / SafeVoxelSize);
    return FMath::Max(12, DerivedDepth);
}

int32 UVoxelWorldManager::GetEffectiveBlockHeightStep() const
{
    return FMath::Max(1, CurrentBlockHeightStep);
}

float UVoxelWorldManager::GetTerrainCellWorldSize() const
{
    return FMath::Max(1.0f, static_cast<float>(GetChunkSizeInVoxels()) * GetRenderVoxelSize());
}

bool UVoxelWorldManager::HasPendingTerrainRequest() const
{
    return PendingWorldTiles.Num() > 0 && PendingGridWidth > 0 && PendingGridHeight > 0;
}

void UVoxelWorldManager::BuildTerrainSnapshot(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight)
{
    ClearTerrainSnapshot();

    TerrainSnapshot.GridWidth = GridWidth;
    TerrainSnapshot.GridHeight = GridHeight;
    TerrainSnapshot.ChunkSizeVoxels = GetChunkSizeInVoxels();
    TerrainSnapshot.VoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;

    TerrainSnapshot.SeaLevelVoxels = SeaLevelVoxels;
    TerrainSnapshot.bAddGlobalFoundation = bAddGlobalFoundation;
    TerrainSnapshot.GlobalFoundationDepthWorld = GlobalFoundationDepthWorld;

    TerrainSnapshot.ContinentFrequency = ContinentFrequency;
    TerrainSnapshot.ContinentHeightRangeVoxels = ContinentHeightRangeVoxels;
    TerrainSnapshot.ContinentExponent = ContinentExponent;

    TerrainSnapshot.BasinFrequency = BasinFrequency;
    TerrainSnapshot.BasinThreshold = BasinThreshold;
    TerrainSnapshot.LakeBasinDepthVoxels = LakeBasinDepthVoxels;

    TerrainSnapshot.RiverFrequency = RiverFrequency;
    TerrainSnapshot.RiverThreshold = RiverThreshold;
    TerrainSnapshot.RiverCarveDepthVoxels = RiverCarveDepthVoxels;
    TerrainSnapshot.RiverSharpness = RiverSharpness;

    TerrainSnapshot.bUseFastTerrainSampling = bUseFastTerrainSampling;

    TerrainSnapshot.WorldTiles = WorldTiles;
    TerrainSnapshot.CachedTileElevations = CachedTileElevations;
    TerrainSnapshot.CachedVoxelProfiles = CachedVoxelProfiles;
    TerrainSnapshot.CachedBiomeTags = CachedBiomeTags;
    TerrainSnapshot.CachedSurfaceHeightsVoxels = CachedSurfaceHeightsVoxels;

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Built terrain snapshot. Grid=%dx%d Tiles=%d ChunkSizeVoxels=%d VoxelSize=%.2f"),
            TerrainSnapshot.GridWidth,
            TerrainSnapshot.GridHeight,
            TerrainSnapshot.WorldTiles.Num(),
            TerrainSnapshot.ChunkSizeVoxels,
            TerrainSnapshot.VoxelSize
        );
    }
}

void UVoxelWorldManager::ClearTerrainSnapshot()
{
    TerrainSnapshot = FWorldForgeTerrainSnapshot();
}

bool UVoxelWorldManager::HasTerrainSnapshot() const
{
    return TerrainSnapshot.IsValid();
}

FVector UVoxelWorldManager::GridToWorld(FIntPoint GridCell, int32 ZLevel) const
{
    const FVector Origin = GetVoxelWorldOrigin();
    const float RVS = GetRenderVoxelSize();

    const FVector Result(
        Origin.X + (GridCell.X * RVS),
        Origin.Y + (GridCell.Y * RVS),
        Origin.Z + (ZLevel * RVS)
    );

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] GridToWorld Grid=(%d,%d,%d) Origin=(%.0f, %.0f, %.0f) -> World=(%.0f, %.0f, %.0f)"),
            GridCell.X,
            GridCell.Y,
            ZLevel,
            Origin.X,
            Origin.Y,
            Origin.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FVector UVoxelWorldManager::TerrainCellToWorld(FIntPoint GridCell, float WorldZ, bool bCenterInCell) const
{
    const FVector Origin = GetVoxelWorldOrigin();
    const float CellSize = GetTerrainCellWorldSize();
    const float CellOffset = bCenterInCell ? 0.5f : 0.0f;

    const FVector Result(
        Origin.X + ((static_cast<float>(GridCell.X) + CellOffset) * CellSize),
        Origin.Y + ((static_cast<float>(GridCell.Y) + CellOffset) * CellSize),
        Origin.Z + WorldZ
    );

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] TerrainCellToWorld Cell=(%d,%d) CellSize=%.1f Center=%d -> World=(%.1f, %.1f, %.1f)"),
            GridCell.X,
            GridCell.Y,
            CellSize,
            bCenterInCell ? 1 : 0,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FIntVector UVoxelWorldManager::WorldToGrid3D(FVector WorldPosition) const
{
    const float RVS = FMath::Max(KINDA_SMALL_NUMBER, GetRenderVoxelSize());
    const FVector Origin = GetVoxelWorldOrigin();
    const FVector LocalPosition = WorldPosition - Origin;

    const int32 GridX = FMath::FloorToInt((LocalPosition.X + (RVS * 0.5f)) / RVS);
    const int32 GridY = FMath::FloorToInt((LocalPosition.Y + (RVS * 0.5f)) / RVS);
    const int32 GridZ = FMath::FloorToInt((LocalPosition.Z + (RVS * 0.5f)) / RVS);

    const FIntVector Result(GridX, GridY, GridZ);

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] WorldToGrid3D World=(%.1f, %.1f, %.1f) Origin=(%.1f, %.1f, %.1f) Local=(%.1f, %.1f, %.1f) -> Grid=(%d,%d,%d)"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            Origin.X,
            Origin.Y,
            Origin.Z,
            LocalPosition.X,
            LocalPosition.Y,
            LocalPosition.Z,
            Result.X,
            Result.Y,
            Result.Z
        );
    }

    return Result;
}

FIntPoint UVoxelWorldManager::WorldToGrid(FVector WorldPosition) const
{
    const FIntVector Grid3D = WorldToGrid3D(WorldPosition);
    return FIntPoint(Grid3D.X, Grid3D.Y);
}

FIntPoint UVoxelWorldManager::WorldToTerrainCell(FVector WorldPosition) const
{
    const FVector Origin = GetVoxelWorldOrigin();
    const float CellSize = GetTerrainCellWorldSize();
    const FVector LocalPosition = WorldPosition - Origin;

    const int32 CellX = FMath::FloorToInt(LocalPosition.X / CellSize);
    const int32 CellY = FMath::FloorToInt(LocalPosition.Y / CellSize);

    const FIntPoint Result(CellX, CellY);

    if (bLogGridConversions)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] WorldToTerrainCell World=(%.1f, %.1f, %.1f) Origin=(%.1f, %.1f, %.1f) Local=(%.1f, %.1f, %.1f) CellSize=%.1f -> Cell=(%d,%d)"),
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            Origin.X,
            Origin.Y,
            Origin.Z,
            LocalPosition.X,
            LocalPosition.Y,
            LocalPosition.Z,
            CellSize,
            Result.X,
            Result.Y
        );
    }

    return Result;
}

FIntPoint UVoxelWorldManager::VoxelCellToChunkCell(FIntPoint VoxelCell) const
{
    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    if (ChunkSizeVoxels <= 0)
    {
        return VoxelCell;
    }

    const int32 ChunkX = FMath::FloorToInt(static_cast<float>(VoxelCell.X) / static_cast<float>(ChunkSizeVoxels));
    const int32 ChunkY = FMath::FloorToInt(static_cast<float>(VoxelCell.Y) / static_cast<float>(ChunkSizeVoxels));
    return FIntPoint(ChunkX, ChunkY);
}

bool UVoxelWorldManager::IsVoxelRuntimeReady() const
{
    if (!VoxelWorldActor)
    {
        return false;
    }

    const AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        return false;
    }

    return VoxelWorld->IsCreated();
}

void UVoxelWorldManager::ApplyBiomeProfileToChunk(FIntPoint ChunkCoord, const FVoxelBiomeProfile& Profile)
{
    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk rejected: biome profile is not Smooth SDF.")
        );
        return;
    }

    if (!IsReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed: VoxelWorld still not available.")
        );
        return;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed: cached actor is not AVoxelWorld.")
        );
        return;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk skipped: VoxelWorld runtime is not created yet.")
        );
        return;
    }

    const int32 Index = TerrainIndex(ClampTerrainCellToBounds(ChunkCoord));
    const float TileElevation = CachedTileElevations.IsValidIndex(Index) ? CachedTileElevations[Index] : 0.5f;

    const bool bNativeApplied = ApplyBiomeChunkNative(VoxelWorld, ChunkCoord, Profile, TileElevation);
    if (!bNativeApplied)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeProfileToChunk failed during native application.")
        );
        return;
    }

    OnBiomeChunkRequested(ChunkCoord, Profile);
}

void UVoxelWorldManager::PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag)
{
    const FIntVector GridCell3D = WorldToGrid3D(WorldPosition);
    const FVector VisualWorldPosition = ResolvePlacedBlockVisualWorldPosition(WorldPosition);

    if (bUseFallbackPlacedBlockVisuals)
    {
        if (TObjectPtr<AStaticMeshActor>* ExistingActor = FallbackPlacedBlockActors.Find(GridCell3D))
        {
            if (IsValid(*ExistingActor))
            {
                (*ExistingActor)->SetActorLocation(VisualWorldPosition);
            }
            else
            {
                FallbackPlacedBlockActors.Remove(GridCell3D);
                if (AStaticMeshActor* NewActor = SpawnFallbackPlacedBlockActor(GridCell3D, VisualWorldPosition, MaterialTag))
                {
                    FallbackPlacedBlockActors.Add(GridCell3D, NewActor);
                }
            }
        }
        else
        {
            if (AStaticMeshActor* NewActor = SpawnFallbackPlacedBlockActor(GridCell3D, VisualWorldPosition, MaterialTag))
            {
                FallbackPlacedBlockActors.Add(GridCell3D, NewActor);
            }
        }
    }

    if (bLogPlacedBlockVisuals)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] PlaceVoxelBlock Grid=(%d,%d,%d) Requested=(%.1f, %.1f, %.1f) Visual=(%.1f, %.1f, %.1f) Material=%s"),
            GridCell3D.X,
            GridCell3D.Y,
            GridCell3D.Z,
            WorldPosition.X,
            WorldPosition.Y,
            WorldPosition.Z,
            VisualWorldPosition.X,
            VisualWorldPosition.Y,
            VisualWorldPosition.Z,
            *MaterialTag.ToString()
        );
    }

    OnVoxelBlockPlaced(VisualWorldPosition, MaterialTag);
}

void UVoxelWorldManager::NotifyVoxelDig(FVector WorldPosition, int32 AgentID)
{
    UE_LOG(
        LogTemp,
        Verbose,
        TEXT("[VoxelWorldManager] NotifyVoxelDig WorldPosition=(%.1f, %.1f, %.1f) AgentID=%d"),
        WorldPosition.X,
        WorldPosition.Y,
        WorldPosition.Z,
        AgentID
    );

    OnVoxelDigged(WorldPosition);
}

bool UVoxelWorldManager::ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const
{
    return Profile.bSmoothSDF;
}

FVector UVoxelWorldManager::ResolvePlacedBlockVisualWorldPosition(const FVector& RequestedWorldPosition) const
{
    FVector Result = RequestedWorldPosition;
    Result.Z += FallbackPlacedBlockVerticalNudge;
    return Result;
}

AStaticMeshActor* UVoxelWorldManager::SpawnFallbackPlacedBlockActor(
    const FIntVector& GridCell3D,
    const FVector& SpawnWorldPosition,
    FGameplayTag MaterialTag)
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    static UStaticMesh* CubeMesh = nullptr;
    if (!CubeMesh)
    {
        CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    }

    if (!CubeMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Failed to load fallback cube mesh for placed block visuals."));
        return nullptr;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    SpawnParams.Name = NAME_None;

    AStaticMeshActor* BlockActor = World->SpawnActor<AStaticMeshActor>(
        SpawnWorldPosition,
        FRotator::ZeroRotator,
        SpawnParams
    );

    if (!BlockActor)
    {
        return nullptr;
    }

    UStaticMeshComponent* MeshComp = BlockActor->GetStaticMeshComponent();
    if (!MeshComp)
    {
        BlockActor->Destroy();
        return nullptr;
    }

    MeshComp->SetMobility(EComponentMobility::Movable);
    MeshComp->SetStaticMesh(CubeMesh);
    MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
    MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    MeshComp->SetGenerateOverlapEvents(false);

    const float RVS = GetRenderVoxelSize();
    MeshComp->SetWorldScale3D(FVector(
        RVS / 100.0f,
        RVS / 100.0f,
        RVS / 100.0f
    ));

    MeshComp->SetCastShadow(true);

    BlockActor->SetActorEnableCollision(true);
    BlockActor->Tags.Add(FName(TEXT("WorldForgePlacedBlock")));
    BlockActor->Tags.Add(FName(*MaterialTag.ToString()));

    return BlockActor;
}

void UVoxelWorldManager::ClearPlacedBlockVisuals()
{
    for (TPair<FIntVector, TObjectPtr<AStaticMeshActor>>& Pair : FallbackPlacedBlockActors)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }

    FallbackPlacedBlockActors.Reset();
}