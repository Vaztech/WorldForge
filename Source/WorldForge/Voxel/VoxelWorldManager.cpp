#include "VoxelWorldManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameplayTagContainer.h"
#include "VoxelWorld.h"
#include "VoxelTools/Gen/VoxelBoxTools.h"
#include "VoxelIntBox.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

namespace
{
    static FGameplayTag GetBiomeCoastalTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    }

    static FGameplayTag GetBiomeLavaTag()
    {
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
    }
}

const int32 UVoxelWorldManager::PermutationTable[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
    140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
    247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
    57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
    74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
    60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
    65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
    200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
    52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
    207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
    119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
    129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
    218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
    81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
    184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
    222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

float UVoxelWorldManager::Fade(float T)
{
    return T * T * T * (T * (T * 6.0f - 15.0f) + 10.0f);
}

float UVoxelWorldManager::Lerp(float T, float A, float B)
{
    return A + T * (B - A);
}

float UVoxelWorldManager::Grad(int32 Hash, float X, float Y)
{
    const int32 H = Hash & 3;
    const float U = (H < 2) ? X : -X;
    const float V = (H == 0 || H == 3) ? Y : -Y;
    return U + V;
}

float UVoxelWorldManager::PerlinNoise2D(float X, float Y)
{
    const int32 Xi = static_cast<int32>(FMath::FloorToFloat(X)) & 255;
    const int32 Yi = static_cast<int32>(FMath::FloorToFloat(Y)) & 255;

    const float Xf = X - FMath::FloorToFloat(X);
    const float Yf = Y - FMath::FloorToFloat(Y);

    const float U = Fade(Xf);
    const float V = Fade(Yf);

    const int32 AA = PermutationTable[PermutationTable[Xi] + Yi];
    const int32 AB = PermutationTable[PermutationTable[Xi] + Yi + 1];
    const int32 BA = PermutationTable[PermutationTable[Xi + 1] + Yi];
    const int32 BB = PermutationTable[PermutationTable[Xi + 1] + Yi + 1];

    const float X1 = Lerp(U, Grad(AA, Xf, Yf), Grad(BA, Xf - 1.0f, Yf));
    const float X2 = Lerp(U, Grad(AB, Xf, Yf - 1.0f), Grad(BB, Xf - 1.0f, Yf - 1.0f));

    return Lerp(V, X1, X2);
}

float UVoxelWorldManager::FractalNoise2D(float X, float Y, const FPerlinOctaveSettings& Settings)
{
    const int32 ClampedOctaves = FMath::Clamp(Settings.OctaveCount, 1, 8);
    const float SafeFreq = FMath::Max(0.0001f, Settings.BaseFrequency);
    const float SafeAmp = FMath::Max(0.1f, Settings.BaseAmplitude);

    float Total = 0.0f;
    float Frequency = SafeFreq;
    float Amplitude = SafeAmp;
    float MaxAmplitude = 0.0f;

    for (int32 i = 0; i < ClampedOctaves; ++i)
    {
        const float OffsetX = static_cast<float>(Settings.SeedOffset * 17 + i * 31);
        const float OffsetY = static_cast<float>(Settings.SeedOffset * 13 + i * 47);

        Total += PerlinNoise2D(
            (X + OffsetX) * Frequency,
            (Y + OffsetY) * Frequency
        ) * Amplitude;

        MaxAmplitude += Amplitude;
        Frequency *= Settings.Lacunarity;
        Amplitude *= Settings.Persistence;
    }

    const float Normalized = Total / FMath::Max(0.001f, MaxAmplitude);
    return Settings.MinHeight + (((Normalized + 1.0f) * 0.5f) * SafeAmp);
}

void UVoxelWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Initialized."));
}

void UVoxelWorldManager::Deinitialize()
{
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(TerrainRetryTimerHandle);
    }

    ClearPendingTerrainRequest();
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

float UVoxelWorldManager::GetTerrainCellWorldSize() const
{
    return FMath::Max(1.0f, static_cast<float>(GetChunkSizeInVoxels()) * GetRenderVoxelSize());
}

bool UVoxelWorldManager::HasPendingTerrainRequest() const
{
    return PendingWorldTiles.Num() > 0 && PendingGridWidth > 0 && PendingGridHeight > 0;
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

FPerlinOctaveSettings UVoxelWorldManager::GetOctaveSettingsForProfile(const FVoxelBiomeProfile& Profile) const
{
    FPerlinOctaveSettings Settings = DefaultPerlinSettings;

    if (Profile.NoiseFrequency > KINDA_SMALL_NUMBER)
    {
        Settings.BaseFrequency = Profile.NoiseFrequency;
    }

    if (Profile.NoiseAmplitude > KINDA_SMALL_NUMBER)
    {
        const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
        Settings.BaseAmplitude = FMath::Max(12.0f, Profile.NoiseAmplitude / (SafeVoxelSize * 0.2f));
    }

    if (Profile.OctaveCount > 0)
    {
        Settings.OctaveCount = FMath::Clamp(Profile.OctaveCount, 1, 8);
    }

    return Settings;
}

int32 UVoxelWorldManager::ComputeSmoothSurfaceHeightAtVoxel(int32 VoxelX, int32 VoxelY, const FVoxelBiomeProfile& Profile) const
{
    const FPerlinOctaveSettings Settings = GetOctaveSettingsForProfile(Profile);

    const float HeightVoxels = FractalNoise2D(
        static_cast<float>(VoxelX),
        static_cast<float>(VoxelY),
        Settings
    );

    return FMath::RoundToInt(HeightVoxels);
}

float UVoxelWorldManager::ComputeSmoothSurfaceHeightAtWorldGrid(FIntPoint GridCell, const FVoxelBiomeProfile& Profile) const
{
    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    const int32 MinX = GridCell.X * ChunkSizeVoxels;
    const int32 MinY = GridCell.Y * ChunkSizeVoxels;

    const int32 Sample00 = ComputeSmoothSurfaceHeightAtVoxel(MinX, MinY, Profile);
    const int32 Sample10 = ComputeSmoothSurfaceHeightAtVoxel(MinX + ChunkSizeVoxels - 1, MinY, Profile);
    const int32 Sample01 = ComputeSmoothSurfaceHeightAtVoxel(MinX, MinY + ChunkSizeVoxels - 1, Profile);
    const int32 Sample11 = ComputeSmoothSurfaceHeightAtVoxel(MinX + ChunkSizeVoxels - 1, MinY + ChunkSizeVoxels - 1, Profile);

    return (static_cast<float>(Sample00) +
        static_cast<float>(Sample10) +
        static_cast<float>(Sample01) +
        static_cast<float>(Sample11)) * 0.25f;
}

int32 UVoxelWorldManager::ComputeChunkSurfaceHeightVoxels(
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation
) const
{
    const int32 NoiseHeight = FMath::RoundToInt(
        ComputeSmoothSurfaceHeightAtWorldGrid(ChunkCoord, Profile)
    );

    // 🔥 FIXED (was 500.0f)
    const int32 BaseHeight = FMath::RoundToInt(
        FMath::Pow(FMath::Clamp(TileElevation, 0.0f, 1.0f), 1.5f) * 80.0f
    );

    int32 FinalHeight = BaseHeight + NoiseHeight;

    if (CurrentTerrainMode == ETerrainMode::Blocky)
    {
        FinalHeight = FMath::GridSnap(FinalHeight, GetEffectiveBlockHeightStep());
    }

    return FMath::Max(4, FinalHeight);
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

bool UVoxelWorldManager::ApplyBiomeChunkNative(
    AVoxelWorld* VoxelWorld,
    FIntPoint ChunkCoord,
    const FVoxelBiomeProfile& Profile,
    float TileElevation)
{
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyBiomeChunkNative failed: VoxelWorld is null."));
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyBiomeChunkNative deferred: VoxelWorld runtime is not created yet.")
        );
        return false;
    }

    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    const int32 MinX = ChunkCoord.X * ChunkSizeVoxels;
    const int32 MinY = ChunkCoord.Y * ChunkSizeVoxels;
    const int32 MaxX = MinX + ChunkSizeVoxels;
    const int32 MaxY = MinY + ChunkSizeVoxels;

    const int32 BaseDepth = ComputeChunkBaseDepthVoxels(Profile);
    const int32 MacroBaseHeight = FMath::RoundToInt(
        FMath::Pow(FMath::Clamp(TileElevation, 0.0f, 1.0f), 1.5f) * 80.0f
    );

    int32 MinSampleHeight = TNumericLimits<int32>::Max();
    int32 MaxSampleHeight = TNumericLimits<int32>::Lowest();

    for (int32 X = MinX; X < MaxX; ++X)
    {
        for (int32 Y = MinY; Y < MaxY; ++Y)
        {
            int32 SurfaceHeight = MacroBaseHeight + ComputeSmoothSurfaceHeightAtVoxel(X, Y, Profile);

            if (CurrentTerrainMode == ETerrainMode::Blocky)
            {
                SurfaceHeight = FMath::GridSnap(SurfaceHeight, GetEffectiveBlockHeightStep());
                SurfaceHeight = FMath::Max(4, SurfaceHeight);
            }

            MinSampleHeight = FMath::Min(MinSampleHeight, SurfaceHeight);
            MaxSampleHeight = FMath::Max(MaxSampleHeight, SurfaceHeight);

            const FVoxelIntBox ColumnBounds(
                FIntVector(X, Y, SurfaceHeight - BaseDepth), // ✅ FIXED
                FIntVector(X + 1, Y + 1, SurfaceHeight)
            );

            UVoxelBoxTools::AddBox(
                VoxelWorld,
                ColumnBounds,
                nullptr,
                nullptr,
                false,
                true
            );
        }
    }

    if (bLogTerrainGeneration && ((ChunkCoord.X + ChunkCoord.Y) % 64 == 0))
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[VoxelWorldManager] Sample chunk (%d,%d) HeightRange=[%d..%d] Depth=%d Mode=%s Step=%d Elev=%.3f"),
            ChunkCoord.X,
            ChunkCoord.Y,
            MinSampleHeight,
            MaxSampleHeight,
            BaseDepth,
            CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
            GetEffectiveBlockHeightStep(),
            TileElevation
        );
    }

    return true;
}

bool UVoxelWorldManager::ApplyGlobalFoundationNative(AVoxelWorld* VoxelWorld, int32 GridWidth, int32 GridHeight)
{
    if (!bAddGlobalFoundation)
    {
        return true;
    }

    if (!VoxelWorld || !VoxelWorld->IsCreated())
    {
        return false;
    }

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    const float SafeVoxelSize = (VoxelSize > KINDA_SMALL_NUMBER) ? VoxelSize : 100.0f;
    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    const int32 FoundationDepthVoxels = FMath::Max(
        8,
        FMath::RoundToInt(GlobalFoundationDepthWorld / SafeVoxelSize)
    );

    const int32 TotalWidthVoxels = GridWidth * ChunkSizeVoxels;
    const int32 TotalHeightVoxels = GridHeight * ChunkSizeVoxels;

    const FVoxelIntBox Bounds(
        FIntVector(0, 0, -FoundationDepthVoxels),
        FIntVector(TotalWidthVoxels, TotalHeightVoxels, 0)
    );

    UVoxelBoxTools::AddBox(
        VoxelWorld,
        Bounds,
        nullptr,
        nullptr,
        false,
        true
    );

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Applied global foundation. Grid=%dx%d DepthVoxels=%d"),
            GridWidth,
            GridHeight,
            FoundationDepthVoxels
        );
    }

    return true;
}

void UVoxelWorldManager::CacheTerrainMetadata(const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight)
{
    CachedTerrainGridWidth = GridWidth;
    CachedTerrainGridHeight = GridHeight;

    CachedSurfaceHeightsVoxels.SetNumZeroed(GridWidth * GridHeight);
    CachedVoxelProfiles.SetNum(GridWidth * GridHeight);
    CachedBiomeTags.SetNum(GridWidth * GridHeight);
    CachedTileElevations.SetNumZeroed(GridWidth * GridHeight);

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = (Y * GridWidth) + X;
            const FWorldTileData& Tile = WorldTiles[Index];

            CachedTileElevations[Index] = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);

            CachedSurfaceHeightsVoxels[Index] = static_cast<float>(
                ComputeChunkSurfaceHeightVoxels(
                    FIntPoint(X, Y),
                    Tile.VoxelProfile,
                    CachedTileElevations[Index]
                )
                );

            CachedVoxelProfiles[Index] = Tile.VoxelProfile;
            CachedBiomeTags[Index] = Tile.BiomeType;
        }
    }

    if (bLogTerrainGeneration)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Cached terrain metadata. Cells=%d Grid=%dx%d Mode=%s Step=%d"),
            CachedSurfaceHeightsVoxels.Num(),
            CachedTerrainGridWidth,
            CachedTerrainGridHeight,
            CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
            GetEffectiveBlockHeightStep()
        );
    }
}

void UVoxelWorldManager::ClearTerrainCache()
{
    CachedSurfaceHeightsVoxels.Reset();
    CachedVoxelProfiles.Reset();
    CachedBiomeTags.Reset();
    CachedTileElevations.Reset();
    CachedTerrainGridWidth = 0;
    CachedTerrainGridHeight = 0;
    bTerrainReadyBroadcasted = false;
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

bool UVoxelWorldManager::IsValidTerrainCell(FIntPoint GridCell) const
{
    return
        GridCell.X >= 0 &&
        GridCell.Y >= 0 &&
        GridCell.X < CachedTerrainGridWidth &&
        GridCell.Y < CachedTerrainGridHeight;
}

FIntPoint UVoxelWorldManager::ClampTerrainCellToBounds(FIntPoint GridCell) const
{
    if (CachedTerrainGridWidth <= 0 || CachedTerrainGridHeight <= 0)
    {
        return GridCell;
    }

    return FIntPoint(
        FMath::Clamp(GridCell.X, 0, CachedTerrainGridWidth - 1),
        FMath::Clamp(GridCell.Y, 0, CachedTerrainGridHeight - 1)
    );
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

int32 UVoxelWorldManager::TerrainIndex(FIntPoint GridCell) const
{
    return (GridCell.Y * CachedTerrainGridWidth) + GridCell.X;
}

bool UVoxelWorldManager::HasCachedTerrainData() const
{
    return
        CachedTerrainGridWidth > 0 &&
        CachedTerrainGridHeight > 0 &&
        CachedSurfaceHeightsVoxels.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedVoxelProfiles.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedBiomeTags.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight) &&
        CachedTileElevations.Num() == (CachedTerrainGridWidth * CachedTerrainGridHeight);
}

bool UVoxelWorldManager::TryGetSurfaceWorldLocation(FIntPoint GridCell, FVector& OutWorldLocation, float ZOffset) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    const float SurfaceHeightWorld = (CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize()) + ZOffset;
    OutWorldLocation = TerrainCellToWorld(GridCell, SurfaceHeightWorld, true);
    return true;
}

bool UVoxelWorldManager::IsSpawnableTerrainCell(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedBiomeTags.IsValidIndex(Index) || !CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    const FGameplayTag& BiomeTag = CachedBiomeTags[Index];
    const FGameplayTag CoastalTag = GetBiomeCoastalTag();
    const FGameplayTag LavaTag = GetBiomeLavaTag();

    if (CoastalTag.IsValid() && BiomeTag.IsValid() && BiomeTag.MatchesTag(CoastalTag))
    {
        return false;
    }

    if (LavaTag.IsValid() && BiomeTag.IsValid() && BiomeTag.MatchesTag(LavaTag))
    {
        return false;
    }

    return CachedSurfaceHeightsVoxels[Index] > 0.0f;
}

bool UVoxelWorldManager::FindNearestSpawnableTerrainCell(FIntPoint StartCell, FIntPoint& OutCell, int32 MaxSearchRadius) const
{
    if (!HasCachedTerrainData())
    {
        return false;
    }

    if (IsSpawnableTerrainCell(StartCell))
    {
        OutCell = StartCell;
        return true;
    }

    const int32 ClampedMaxRadius = FMath::Max(1, MaxSearchRadius);

    for (int32 Radius = 1; Radius <= ClampedMaxRadius; ++Radius)
    {
        for (int32 Y = StartCell.Y - Radius; Y <= StartCell.Y + Radius; ++Y)
        {
            for (int32 X = StartCell.X - Radius; X <= StartCell.X + Radius; ++X)
            {
                const bool bOnRing =
                    (X == StartCell.X - Radius) ||
                    (X == StartCell.X + Radius) ||
                    (Y == StartCell.Y - Radius) ||
                    (Y == StartCell.Y + Radius);

                if (!bOnRing)
                {
                    continue;
                }

                const FIntPoint Candidate(X, Y);
                if (IsSpawnableTerrainCell(Candidate))
                {
                    OutCell = Candidate;
                    return true;
                }
            }
        }
    }

    return false;
}

bool UVoxelWorldManager::TryGetCachedSurfaceHeightWorldForVoxelCell(FIntPoint VoxelCell, float& OutSurfaceHeightWorld) const
{
    OutSurfaceHeightWorld = 0.0f;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelCell);
    if (!IsValidTerrainCell(ChunkCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(ChunkCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return false;
    }

    OutSurfaceHeightWorld = CachedSurfaceHeightsVoxels[Index] * GetRenderVoxelSize();
    return true;
}

bool UVoxelWorldManager::TryGetTerrainSurfaceVoxelZ(FIntPoint VoxelXY, int32& OutVoxelZ) const
{
    OutVoxelZ = 0;

    if (!HasCachedTerrainData())
    {
        return false;
    }

    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    if (!IsValidTerrainCell(ChunkCell))
    {
        return false;
    }

    const int32 Index = TerrainIndex(ChunkCell);
    if (!CachedVoxelProfiles.IsValidIndex(Index) || !CachedTileElevations.IsValidIndex(Index))
    {
        return false;
    }

    const FVoxelBiomeProfile& Profile = CachedVoxelProfiles[Index];
    const float TileElevation = CachedTileElevations[Index];
    const int32 MacroBaseHeight = FMath::RoundToInt(
        FMath::Pow(FMath::Clamp(TileElevation, 0.0f, 1.0f), 1.5f) * 80.0f
    );

    const int32 ChunkSizeVoxels = GetChunkSizeInVoxels();
    const int32 LocalVoxelX = VoxelXY.X;
    const int32 LocalVoxelY = VoxelXY.Y;

    int32 SurfaceVoxelZ = MacroBaseHeight + ComputeSmoothSurfaceHeightAtVoxel(LocalVoxelX, LocalVoxelY, Profile);

    if (CurrentTerrainMode == ETerrainMode::Blocky)
    {
        SurfaceVoxelZ = FMath::GridSnap(SurfaceVoxelZ, GetEffectiveBlockHeightStep());
        SurfaceVoxelZ = FMath::Max(4, SurfaceVoxelZ);
    }

    OutVoxelZ = SurfaceVoxelZ;
    return true;
}

FVector UVoxelWorldManager::ResolvePlacedBlockVisualWorldPosition(const FVector& RequestedWorldPosition) const
{
    FVector Result = RequestedWorldPosition;
    Result.Z += FallbackPlacedBlockVerticalNudge;
    return Result;
}

AStaticMeshActor* UVoxelWorldManager::SpawnFallbackPlacedBlockActor(const FIntVector& GridCell3D, const FVector& SpawnWorldPosition, FGameplayTag MaterialTag)
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

bool UVoxelWorldManager::ApplyTerrainFromTilesInternal(AVoxelWorld* VoxelWorld, const TArray<FWorldTileData>& WorldTiles, int32 GridWidth, int32 GridHeight)
{
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld is null."));
        return false;
    }

    if (!VoxelWorld->IsCreated())
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: VoxelWorld runtime is not created."));
        return false;
    }

    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: invalid grid size %d x %d."), GridWidth, GridHeight);
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] ApplyTerrainFromTilesInternal failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );
        return false;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation start (Perlin fBm). Tiles=%d Grid=%dx%d ChunkSizeVoxels=%d Mode=%s Step=%d"),
        WorldTiles.Num(),
        GridWidth,
        GridHeight,
        GetChunkSizeInVoxels(),
        CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
        GetEffectiveBlockHeightStep()
    );

    CacheTerrainMetadata(WorldTiles, GridWidth, GridHeight);

    if (!ApplyGlobalFoundationNative(VoxelWorld, GridWidth, GridHeight))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Failed to apply global foundation."));
    }

    int32 AppliedCount = 0;

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 TileIndex = (Y * GridWidth) + X;
            const FWorldTileData& Tile = WorldTiles[TileIndex];

            if (!ValidateSmoothSDF(Tile.VoxelProfile))
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("[VoxelWorldManager] Skipping tile %d at (%d,%d): profile is not Smooth SDF."),
                    TileIndex,
                    X,
                    Y
                );
                continue;
            }

            if (ApplyBiomeChunkNative(VoxelWorld, FIntPoint(X, Y), Tile.VoxelProfile, Tile.Elevation))
            {
                ++AppliedCount;
            }
        }

        if (bLogTerrainGeneration && (((Y + 1) % 16) == 0 || (Y + 1) == GridHeight))
        {
            UE_LOG(
                LogTemp,
                Log,
                TEXT("[VoxelWorldManager] Terrain generation progress: %d / %d rows"),
                Y + 1,
                GridHeight
            );
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[VoxelWorldManager] Terrain generation complete (Perlin fBm). Applied=%d / %d"),
        AppliedCount,
        ExpectedTileCount
    );

    const bool bSuccess = AppliedCount > 0;
    if (bSuccess)
    {
        BroadcastTerrainReadyIfNeeded();
    }

    return bSuccess;
}

void UVoxelWorldManager::ScheduleTerrainRetry()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] ScheduleTerrainRetry failed: GetWorld() returned null."));
        return;
    }

    if (bTerrainRetryScheduled)
    {
        return;
    }

    bTerrainRetryScheduled = true;

    World->GetTimerManager().SetTimer(
        TerrainRetryTimerHandle,
        this,
        &UVoxelWorldManager::RetryPendingTerrainGeneration,
        TerrainRetryIntervalSeconds,
        false
    );

    if (bLogTerrainRetry)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[VoxelWorldManager] Scheduled terrain retry in %.2f seconds."),
            TerrainRetryIntervalSeconds
        );
    }
}

void UVoxelWorldManager::RetryPendingTerrainGeneration()
{
    bTerrainRetryScheduled = false;

    if (PendingWorldTiles.Num() == 0 || PendingGridWidth <= 0 || PendingGridHeight <= 0)
    {
        return;
    }

    if (!IsReady())
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: VoxelWorld actor never became ready."));
            ClearPendingTerrainRequest();
            return;
        }

        if (bLogTerrainRetry)
        {
            UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Terrain retry %d: VoxelWorld actor still not ready."), TerrainRetryCount);
        }

        ScheduleTerrainRetry();
        return;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld || !VoxelWorld->IsCreated())
    {
        ++TerrainRetryCount;

        if (TerrainRetryCount > MaxTerrainRetryCount)
        {
            UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: VoxelWorld runtime was never created."));
            ClearPendingTerrainRequest();
            return;
        }

        if (bLogTerrainRetry)
        {
            UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] Terrain retry %d: VoxelWorld runtime not created yet."), TerrainRetryCount);
        }

        ScheduleTerrainRetry();
        return;
    }

    if (ApplyTerrainFromTilesInternal(VoxelWorld, PendingWorldTiles, PendingGridWidth, PendingGridHeight))
    {
        UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Pending terrain generation applied successfully on retry %d."), TerrainRetryCount);
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
        return;
    }

    ++TerrainRetryCount;

    if (TerrainRetryCount > MaxTerrainRetryCount)
    {
        UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] Terrain retry failed: native terrain application never succeeded."));
        ClearPendingTerrainRequest();
        return;
    }

    ScheduleTerrainRetry();
}

void UVoxelWorldManager::ClearPendingTerrainRequest()
{
    PendingWorldTiles.Reset();
    PendingGridWidth = 0;
    PendingGridHeight = 0;
    TerrainRetryCount = 0;
    bTerrainRetryScheduled = false;
}

void UVoxelWorldManager::BroadcastTerrainReadyIfNeeded()
{
    if (bTerrainReadyBroadcasted)
    {
        return;
    }

    if (!IsTerrainRuntimeStable())
    {
        return;
    }

    bTerrainReadyBroadcasted = true;

    UE_LOG(LogTemp, Log, TEXT("[VoxelWorldManager] Terrain is ready for gameplay."));

    if (HasCachedTerrainData())
    {
        const FIntPoint CenterCell(
            FMath::Max(0, CachedTerrainGridWidth / 2),
            FMath::Max(0, CachedTerrainGridHeight / 2)
        );

        const FIntPoint TopLeft(0, 0);
        const FIntPoint TopRight(FMath::Max(0, CachedTerrainGridWidth - 1), 0);
        const FIntPoint BottomLeft(0, FMath::Max(0, CachedTerrainGridHeight - 1));
        const FIntPoint BottomRight(
            FMath::Max(0, CachedTerrainGridWidth - 1),
            FMath::Max(0, CachedTerrainGridHeight - 1)
        );

        FVector CenterWorld = FVector::ZeroVector;
        FVector TopLeftWorld = FVector::ZeroVector;
        FVector TopRightWorld = FVector::ZeroVector;
        FVector BottomLeftWorld = FVector::ZeroVector;
        FVector BottomRightWorld = FVector::ZeroVector;

        const bool bHasCenter = TryGetSurfaceWorldLocation(CenterCell, CenterWorld, TerrainSurfaceZOffset);
        const bool bHasTL = TryGetSurfaceWorldLocation(TopLeft, TopLeftWorld, TerrainSurfaceZOffset);
        const bool bHasTR = TryGetSurfaceWorldLocation(TopRight, TopRightWorld, TerrainSurfaceZOffset);
        const bool bHasBL = TryGetSurfaceWorldLocation(BottomLeft, BottomLeftWorld, TerrainSurfaceZOffset);
        const bool bHasBR = TryGetSurfaceWorldLocation(BottomRight, BottomRightWorld, TerrainSurfaceZOffset);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] Terrain samples: CenterCell=(%d,%d) CenterWorld=(%.1f, %.1f, %.1f) TL=(%.1f, %.1f, %.1f) TR=(%.1f, %.1f, %.1f) BL=(%.1f, %.1f, %.1f) BR=(%.1f, %.1f, %.1f)"),
            CenterCell.X,
            CenterCell.Y,
            bHasCenter ? CenterWorld.X : -999999.0f,
            bHasCenter ? CenterWorld.Y : -999999.0f,
            bHasCenter ? CenterWorld.Z : -999999.0f,
            bHasTL ? TopLeftWorld.X : -999999.0f,
            bHasTL ? TopLeftWorld.Y : -999999.0f,
            bHasTL ? TopLeftWorld.Z : -999999.0f,
            bHasTR ? TopRightWorld.X : -999999.0f,
            bHasTR ? TopRightWorld.Y : -999999.0f,
            bHasTR ? TopRightWorld.Z : -999999.0f,
            bHasBL ? BottomLeftWorld.X : -999999.0f,
            bHasBL ? BottomLeftWorld.Y : -999999.0f,
            bHasBL ? BottomLeftWorld.Z : -999999.0f,
            bHasBR ? BottomRightWorld.X : -999999.0f,
            bHasBR ? BottomRightWorld.Y : -999999.0f,
            bHasBR ? BottomRightWorld.Z : -999999.0f
        );

        if (APlayerController* PC = UGameplayStatics::GetPlayerController(GetWorld(), 0))
        {
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

            const double DistanceToCenter = bHasCenter
                ? FVector::Dist(ViewLocation, CenterWorld)
                : -1.0;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[VoxelWorldManager] Player view at terrain-ready: View=(%.1f, %.1f, %.1f) Rot=(%.1f, %.1f, %.1f) DistToCenter=%.1f"),
                ViewLocation.X,
                ViewLocation.Y,
                ViewLocation.Z,
                ViewRotation.Pitch,
                ViewRotation.Yaw,
                ViewRotation.Roll,
                DistanceToCenter
            );
        }
    }

    OnTerrainReady.Broadcast();
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

bool UVoxelWorldManager::GenerateTerrainFromTiles(
    const TArray<FWorldTileData>& WorldTiles,
    int32 GridWidth,
    int32 GridHeight,
    ETerrainMode TerrainMode,
    int32 BlockHeightStep)
{
    if (GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: invalid grid size %d x %d."), GridWidth, GridHeight);
        return false;
    }

    const int32 ExpectedTileCount = GridWidth * GridHeight;
    if (WorldTiles.Num() != ExpectedTileCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: tile count mismatch. Expected=%d Actual=%d"),
            ExpectedTileCount,
            WorldTiles.Num()
        );
        return false;
    }

    CurrentTerrainMode = TerrainMode;
    CurrentBlockHeightStep = FMath::Max(1, BlockHeightStep);

    ClearTerrainCache();

    PendingWorldTiles = WorldTiles;
    PendingGridWidth = GridWidth;
    PendingGridHeight = GridHeight;
    TerrainRetryCount = 0;

    if (!IsReady() || !IsVoxelRuntimeReady())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[VoxelWorldManager] GenerateTerrainFromTiles deferred: VoxelWorld runtime is not ready yet. Mode=%s Step=%d"),
            CurrentTerrainMode == ETerrainMode::Blocky ? TEXT("Blocky") : TEXT("Smooth"),
            GetEffectiveBlockHeightStep()
        );

        ScheduleTerrainRetry();
        return true;
    }

    AVoxelWorld* VoxelWorld = Cast<AVoxelWorld>(VoxelWorldActor);
    if (!VoxelWorld)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VoxelWorldManager] GenerateTerrainFromTiles failed: cached actor is not AVoxelWorld."));
        return false;
    }

    const bool bSuccess = ApplyTerrainFromTilesInternal(VoxelWorld, WorldTiles, GridWidth, GridHeight);
    if (bSuccess)
    {
        ClearPendingTerrainRequest();
        BroadcastTerrainReadyIfNeeded();
    }
    else
    {
        ScheduleTerrainRetry();
    }

    return true;
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

float UVoxelWorldManager::GetNormalizedElevationForTerrainCell(FIntPoint GridCell) const
{
    if (!IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const int32 Index = TerrainIndex(GridCell);
    if (!CachedSurfaceHeightsVoxels.IsValidIndex(Index))
    {
        return 0.0f;
    }

    const float Height = CachedSurfaceHeightsVoxels[Index];
    const float MinH = 0.0f;
    const float MaxH = 256.0f;

    return FMath::Clamp((Height - MinH) / (MaxH - MinH), 0.0f, 1.0f);
}

EWorldForgeTerrainType UVoxelWorldManager::ClassifyTerrainCellInternal(FIntPoint GridCell) const
{
    if (!IsValidTerrainCell(GridCell))
    {
        return EWorldForgeTerrainType::Grass;
    }

    const int32 Index = TerrainIndex(GridCell);

    if (CachedBiomeTags.IsValidIndex(Index))
    {
        const FGameplayTag& Biome = CachedBiomeTags[Index];

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")))))
        {
            return EWorldForgeTerrainType::Sand;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")))))
        {
            return EWorldForgeTerrainType::Rock;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")))))
        {
            return EWorldForgeTerrainType::Snow;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")))))
        {
            return EWorldForgeTerrainType::Sand;
        }

        if (Biome.MatchesTagExact(FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")))))
        {
            return EWorldForgeTerrainType::Grass;
        }
    }

    const float Elev = GetNormalizedElevationForTerrainCell(GridCell);

    if (Elev < SandMaxElevationNormalized)
    {
        return EWorldForgeTerrainType::Sand;
    }

    if (Elev > SnowMinElevationNormalized)
    {
        return EWorldForgeTerrainType::Snow;
    }

    if (Elev > RockMinElevationNormalized)
    {
        return EWorldForgeTerrainType::Rock;
    }

    return EWorldForgeTerrainType::Grass;
}

bool UVoxelWorldManager::GetTerrainTypeAtCell(FIntPoint GridCell, EWorldForgeTerrainType& OutTerrainType) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutTerrainType = ClassifyTerrainCellInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainTypeAtVoxel(FIntPoint VoxelXY, EWorldForgeTerrainType& OutTerrainType) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainTypeAtCell(ChunkCell, OutTerrainType);
}

float UVoxelWorldManager::ComputeTerrainSlopeDegreesInternal(FIntPoint GridCell) const
{
    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return 0.0f;
    }

    const FIntPoint Center = ClampTerrainCellToBounds(GridCell);
    const FIntPoint North = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y - 1));
    const FIntPoint South = ClampTerrainCellToBounds(FIntPoint(Center.X, Center.Y + 1));
    const FIntPoint West = ClampTerrainCellToBounds(FIntPoint(Center.X - 1, Center.Y));
    const FIntPoint East = ClampTerrainCellToBounds(FIntPoint(Center.X + 1, Center.Y));

    const int32 CenterIndex = TerrainIndex(Center);
    const int32 NorthIndex = TerrainIndex(North);
    const int32 SouthIndex = TerrainIndex(South);
    const int32 WestIndex = TerrainIndex(West);
    const int32 EastIndex = TerrainIndex(East);

    if (!CachedSurfaceHeightsVoxels.IsValidIndex(CenterIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(NorthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(SouthIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(WestIndex) ||
        !CachedSurfaceHeightsVoxels.IsValidIndex(EastIndex))
    {
        return 0.0f;
    }

    const float RVS = GetRenderVoxelSize();
    const float CenterHeightWorld = CachedSurfaceHeightsVoxels[CenterIndex] * RVS;
    const float NorthHeightWorld = CachedSurfaceHeightsVoxels[NorthIndex] * RVS;
    const float SouthHeightWorld = CachedSurfaceHeightsVoxels[SouthIndex] * RVS;
    const float WestHeightWorld = CachedSurfaceHeightsVoxels[WestIndex] * RVS;
    const float EastHeightWorld = CachedSurfaceHeightsVoxels[EastIndex] * RVS;

    const float CellRunWorld = GetTerrainCellWorldSize();

    const float GradientX = (EastHeightWorld - WestHeightWorld) / (2.0f * CellRunWorld);
    const float GradientY = (SouthHeightWorld - NorthHeightWorld) / (2.0f * CellRunWorld);

    const float GradientMagnitude = FMath::Sqrt((GradientX * GradientX) + (GradientY * GradientY));
    return FMath::RadiansToDegrees(FMath::Atan(GradientMagnitude));
}

bool UVoxelWorldManager::GetTerrainSlopeAtCell(FIntPoint GridCell, float& OutSlopeDegrees) const
{
    OutSlopeDegrees = 0.0f;

    if (!HasCachedTerrainData() || !IsValidTerrainCell(GridCell))
    {
        return false;
    }

    OutSlopeDegrees = ComputeTerrainSlopeDegreesInternal(GridCell);
    return true;
}

bool UVoxelWorldManager::GetTerrainSlopeAtVoxel(FIntPoint VoxelXY, float& OutSlopeDegrees) const
{
    const FIntPoint ChunkCell = VoxelCellToChunkCell(VoxelXY);
    return GetTerrainSlopeAtCell(ChunkCell, OutSlopeDegrees);
}