#include "WorldMapSubsystem.h"
#include "../World/WorldGenPipeline.h"
#include "../Voxel/VoxelWorldManager.h"

namespace
{
    static FColor ScaleColor(const FColor& InColor, float Scalar)
    {
        Scalar = FMath::Clamp(Scalar, 0.0f, 1.0f);

        return FColor(
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InColor.R * Scalar), 0, 255)),
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InColor.G * Scalar), 0, 255)),
            static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(InColor.B * Scalar), 0, 255)),
            InColor.A);
    }
}

void UWorldMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Initialized."));
}

void UWorldMapSubsystem::Deinitialize()
{
    bInitialized = false;
    CachedMapTexture = nullptr;
    DetailedMapTexture = nullptr;
    bMapTextureDirty = true;
    bDetailedMapTextureDirty = true;

    Super::Deinitialize();
}

void UWorldMapSubsystem::InitializeFromPipeline(UWorldGenPipeline* Pipeline)
{
    if (!Pipeline || !Pipeline->IsComplete())
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldMapSubsystem] Pipeline null or incomplete!"));
        return;
    }

    WorldTiles = Pipeline->GetWorldTiles();
    Civilizations = Pipeline->GetCivilizations();
    HistoryLog = Pipeline->GetHistoryLog();
    MountainRanges = Pipeline->GetMountainRanges();
    MountainProvinces = Pipeline->GetMountainProvinces();
    GridWidth = Pipeline->GetGridWidth();
    GridHeight = Pipeline->GetGridHeight();

    bInitialized = true;
    bMapTextureDirty = true;
    bDetailedMapTextureDirty = true;

    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Loaded %d tiles, %d civs, %d events, %d mountain ranges"),
        WorldTiles.Num(), Civilizations.Num(), HistoryLog.Num(), MountainRanges.Num());

    RebuildMapTexture();
    RebuildDetailedMapTexture();

    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (!bBoundToTerrainReady)
            {
                VoxelMgr->OnTerrainReady.AddDynamic(this, &UWorldMapSubsystem::HandleVoxelTerrainReady);
                bBoundToTerrainReady = true;
            }
        }
    }
}

void UWorldMapSubsystem::HandleVoxelTerrainReady()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Terrain-ready: rebuilding detailed map texture only."));

    // Keep the main selectable map based on pipeline tile colors so it stays
    // visually consistent with the world-gen result the player saw at startup.
    // Only the detailed texture is refreshed from voxel terrain data.
    MarkDetailedMapTextureDirty();
    RebuildDetailedMapTexture();
}

void UWorldMapSubsystem::Tick(float DeltaTime)
{
    if (!bInitialized)
    {
        return;
    }

    TickAccumulator += DeltaTime;

    if (TickAccumulator >= LiveWorldInterval)
    {
        TickAccumulator = 0.0f;
        TickLivingWorld();
    }
}

void UWorldMapSubsystem::TickLivingWorld()
{
    UE_LOG(LogTemp, Verbose, TEXT("[WorldMapSubsystem] Living world tick"));
}

bool UWorldMapSubsystem::GetTileData(int32 TileID, FWorldTileData& OutTile) const
{
    if (!WorldTiles.IsValidIndex(TileID))
    {
        return false;
    }

    OutTile = WorldTiles[TileID];
    return true;
}

const FWorldTileData* UWorldMapSubsystem::GetTileDataPtr(int32 TileID) const
{
    return WorldTiles.IsValidIndex(TileID) ? &WorldTiles[TileID] : nullptr;
}

FWorldTileData* UWorldMapSubsystem::GetMutableTileDataPtr(int32 TileID)
{
    return WorldTiles.IsValidIndex(TileID) ? &WorldTiles[TileID] : nullptr;
}

bool UWorldMapSubsystem::GetCivData(int32 CivID, FCivilizationData& OutCiv) const
{
    for (const FCivilizationData& C : Civilizations)
    {
        if (C.CivID == CivID)
        {
            OutCiv = C;
            return true;
        }
    }

    return false;
}

void UWorldMapSubsystem::RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy)
{
    FWorldTileData* T = GetMutableTileDataPtr(TileID);
    if (!T)
    {
        return;
    }

    T->KnowledgeState.KnowledgeLevel = NewLevel;
    T->KnowledgeState.DataAccuracy = FMath::Clamp(Accuracy, 0.0f, 1.0f);

    MarkMapTextureDirty();
}

EKnowledgeLevel UWorldMapSubsystem::GetTileKnowledge(int32 TileID) const
{
    const FWorldTileData* T = GetTileDataPtr(TileID);
    return T ? T->KnowledgeState.KnowledgeLevel : EKnowledgeLevel::Unexplored;
}

int32 UWorldMapSubsystem::AddHistoryEvent(const FHistoryEventData& Event)
{
    FHistoryEventData NewEvent = Event;
    NewEvent.EventID = HistoryLog.Num();

    HistoryLog.Add(NewEvent);

    UE_LOG(LogTemp, Log,
        TEXT("[History] EventID=%d Type=%s"),
        NewEvent.EventID,
        *NewEvent.EventType.ToString());

    return NewEvent.EventID;
}

// -----------------------------------------------------------------------
// Standard 256x256 map texture
// -----------------------------------------------------------------------

UTexture2D* UWorldMapSubsystem::GetOrCreateMapTexture()
{
    if (!CachedMapTexture || bMapTextureDirty)
    {
        RebuildMapTexture();
    }

    return CachedMapTexture;
}

void UWorldMapSubsystem::RebuildMapTexture()
{
    if (!bInitialized || WorldTiles.Num() == 0 || GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldMapSubsystem] RebuildMapTexture skipped: map not initialized."));
        return;
    }

    AllocateMapTextureIfNeeded();

    if (!CachedMapTexture || !CachedMapTexture->GetPlatformData() || CachedMapTexture->GetPlatformData()->Mips.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldMapSubsystem] RebuildMapTexture failed: texture allocation invalid."));
        return;
    }

    TArray<FColor> PixelData;
    PixelData.SetNumZeroed(GridWidth * GridHeight);

    for (int32 Y = 0; Y < GridHeight; ++Y)
    {
        for (int32 X = 0; X < GridWidth; ++X)
        {
            const int32 Index = Y * GridWidth + X;
            const FWorldTileData& Tile = WorldTiles[Index];
            PixelData[Index] = GetOverlayAwareTileColorAtCell(X, Y, Tile);
        }
    }

    UploadMapPixels(PixelData);

    UE_LOG(LogTemp, Warning, TEXT("[WorldMapSubsystem] Map texture valid: %s Size=%dx%d"),
        CachedMapTexture ? TEXT("YES") : TEXT("NO"),
        GridWidth, GridHeight);

    bMapTextureDirty = false;

    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Map texture rebuilt. Size=%dx%d Mode=%d"),
        GridWidth, GridHeight, static_cast<int32>(OverlayMode));
}

void UWorldMapSubsystem::MarkMapTextureDirty()
{
    bMapTextureDirty = true;
}

// -----------------------------------------------------------------------
// Detailed map texture
// -----------------------------------------------------------------------

UTexture2D* UWorldMapSubsystem::GetOrCreateDetailedMapTexture()
{
    if (!DetailedMapTexture || bDetailedMapTextureDirty)
    {
        RebuildDetailedMapTexture();
    }
    return DetailedMapTexture;
}

void UWorldMapSubsystem::MarkDetailedMapTextureDirty()
{
    bDetailedMapTextureDirty = true;
}

void UWorldMapSubsystem::RebuildDetailedMapTexture()
{
    if (!bInitialized || WorldTiles.IsEmpty() || GridWidth <= 0 || GridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldMapSubsystem] RebuildDetailedMapTexture skipped: not initialized."));
        return;
    }

    const int32 Res = FMath::Clamp(DetailedMapResolution, 64, 4096);

    if (!DetailedMapTexture ||
        DetailedMapTexture->GetSizeX() != Res ||
        DetailedMapTexture->GetSizeY() != Res)
    {
        DetailedMapTexture = UTexture2D::CreateTransient(Res, Res, PF_B8G8R8A8);
        if (!DetailedMapTexture)
        {
            UE_LOG(LogTemp, Error,
                TEXT("[WorldMapSubsystem] Failed to allocate detailed map texture (%dx%d)."), Res, Res);
            return;
        }

        DetailedMapTexture->SRGB = true;
        DetailedMapTexture->Filter = TF_Nearest;
        DetailedMapTexture->AddressX = TA_Clamp;
        DetailedMapTexture->AddressY = TA_Clamp;
        DetailedMapTexture->UpdateResource();
    }

    if (!DetailedMapTexture->GetPlatformData() ||
        DetailedMapTexture->GetPlatformData()->Mips.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldMapSubsystem] RebuildDetailedMapTexture: mip data not ready."));
        return;
    }

    TArray<FColor> Pixels;
    Pixels.SetNumUninitialized(Res * Res);

    const UWorld* World = GetWorld();
    const UVoxelWorldManager* VoxelMgr = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;

    const int32 ChunkSizeVoxels =
        VoxelMgr ? FMath::Max(1, VoxelMgr->GetChunkSizeInVoxels()) : 32;

    const int32 TotalVoxelWidth = GridWidth * ChunkSizeVoxels;
    const int32 TotalVoxelHeight = GridHeight * ChunkSizeVoxels;

    auto TerrainTypeToColor = [](EWorldForgeTerrainType TerrainType) -> FColor
        {
            switch (TerrainType)
            {
            case EWorldForgeTerrainType::Water: return FColor(35, 100, 190);
            case EWorldForgeTerrainType::Sand:  return FColor(210, 196, 145);
            case EWorldForgeTerrainType::Grass: return FColor(90, 140, 80);
            case EWorldForgeTerrainType::Dirt:  return FColor(125, 110, 80);
            case EWorldForgeTerrainType::Rock:  return FColor(135, 135, 135);
            case EWorldForgeTerrainType::Snow:  return FColor(230, 230, 235);
            default:                            return FColor(255, 0, 255);
            }
        };

    const float InvRes = 1.0f / static_cast<float>(Res);

    for (int32 PY = 0; PY < Res; ++PY)
    {
        for (int32 PX = 0; PX < Res; ++PX)
        {
            const float U = (static_cast<float>(PX) + 0.5f) * InvRes;
            const float V = (static_cast<float>(PY) + 0.5f) * InvRes;

            const int32 HalfVoxelWidth = TotalVoxelWidth / 2;
            const int32 HalfVoxelHeight = TotalVoxelHeight / 2;

            const int32 VoxelX = FMath::Clamp(
                FMath::RoundToInt(U * static_cast<float>(TotalVoxelWidth - 1)) - HalfVoxelWidth,
                -HalfVoxelWidth,
                HalfVoxelWidth - 1);

            const int32 VoxelY = FMath::Clamp(
                FMath::RoundToInt(V * static_cast<float>(TotalVoxelHeight - 1)) - HalfVoxelHeight,
                -HalfVoxelHeight,
                HalfVoxelHeight - 1);

            FColor PixelColor = FColor(255, 0, 255);

            if (VoxelMgr)
            {
                EWorldForgeTerrainType TerrainType;
                if (VoxelMgr->GetTerrainTypeAtVoxelSample(FIntPoint(VoxelX, VoxelY), TerrainType))
                {
                    PixelColor = TerrainTypeToColor(TerrainType);
                }
                else
                {
                    const int32 HalfGridX = GridWidth / 2;
                    const int32 HalfGridY = GridHeight / 2;

                    const int32 TileX = FMath::Clamp(
                        FMath::FloorToInt(static_cast<float>(VoxelX) / static_cast<float>(ChunkSizeVoxels)) + HalfGridX,
                        0,
                        GridWidth - 1);

                    const int32 TileY = FMath::Clamp(
                        FMath::FloorToInt(static_cast<float>(VoxelY) / static_cast<float>(ChunkSizeVoxels)) + HalfGridY,
                        0,
                        GridHeight - 1);

                    const FWorldTileData& FallbackTile = WorldTiles[(TileY * GridWidth) + TileX];
                    PixelColor = MakeTerrainColorFromVoxelData(TileX, TileY, FallbackTile);
                }
            }
            else
            {
                const int32 HalfGridX = GridWidth / 2;
                const int32 HalfGridY = GridHeight / 2;

                const int32 TileX = FMath::Clamp(
                    FMath::FloorToInt(static_cast<float>(VoxelX) / static_cast<float>(ChunkSizeVoxels)) + HalfGridX,
                    0,
                    GridWidth - 1);

                const int32 TileY = FMath::Clamp(
                    FMath::FloorToInt(static_cast<float>(VoxelY) / static_cast<float>(ChunkSizeVoxels)) + HalfGridY,
                    0,
                    GridHeight - 1);

                const FWorldTileData& FallbackTile = WorldTiles[(TileY * GridWidth) + TileX];
                PixelColor = MakeTerrainColorFromVoxelData(TileX, TileY, FallbackTile);
            }

            Pixels[PY * Res + PX] = PixelColor;
        }
    }

    FTexture2DMipMap& Mip = DetailedMapTexture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(
        Data,
        Pixels.GetData(),
        static_cast<SIZE_T>(Res * Res) * sizeof(FColor));
    Mip.BulkData.Unlock();
    DetailedMapTexture->UpdateResource();

    bDetailedMapTextureDirty = false;

    const float WorldWidthMeters = static_cast<float>(GridWidth) * 32.0f;
    const float WorldHeightMeters = static_cast<float>(GridHeight) * 32.0f;
    const float MetersPerPixelX = WorldWidthMeters / static_cast<float>(Res);
    const float MetersPerPixelY = WorldHeightMeters / static_cast<float>(Res);

    UE_LOG(LogTemp, Log,
        TEXT("[WorldMapSubsystem] Detailed map texture rebuilt. Resolution=%dx%d VoxelSpan=%dx%d ApproxMetersPerPixel=(%.2f, %.2f)"),
        Res,
        Res,
        TotalVoxelWidth,
        TotalVoxelHeight,
        MetersPerPixelX,
        MetersPerPixelY);
}

// -----------------------------------------------------------------------

void UWorldMapSubsystem::SetOverlayMode(EWorldMapOverlayMode NewMode)
{
    if (OverlayMode != NewMode)
    {
        OverlayMode = NewMode;
        MarkMapTextureDirty();
        MarkDetailedMapTextureDirty();
    }
}

bool UWorldMapSubsystem::TileXYToIndex(int32 TileX, int32 TileY, int32& OutTileIndex) const
{
    OutTileIndex = INDEX_NONE;

    if (!bInitialized || GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    if (TileX < 0 || TileX >= GridWidth || TileY < 0 || TileY >= GridHeight)
    {
        return false;
    }

    OutTileIndex = TileY * GridWidth + TileX;
    return WorldTiles.IsValidIndex(OutTileIndex);
}

bool UWorldMapSubsystem::TileIndexToXY(int32 TileIndex, int32& OutTileX, int32& OutTileY) const
{
    OutTileX = INDEX_NONE;
    OutTileY = INDEX_NONE;

    if (!bInitialized || GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    if (!WorldTiles.IsValidIndex(TileIndex))
    {
        return false;
    }

    OutTileX = TileIndex % GridWidth;
    OutTileY = TileIndex / GridWidth;
    return true;
}

bool UWorldMapSubsystem::NormalizedMapUVToTile(
    const FVector2D& NormalizedUV,
    int32& OutTileX,
    int32& OutTileY,
    int32& OutTileIndex) const
{
    OutTileX = INDEX_NONE;
    OutTileY = INDEX_NONE;
    OutTileIndex = INDEX_NONE;

    if (!bInitialized || GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    if (NormalizedUV.X < 0.0f || NormalizedUV.X > 1.0f ||
        NormalizedUV.Y < 0.0f || NormalizedUV.Y > 1.0f)
    {
        return false;
    }

    const float ClampedU = FMath::Clamp(NormalizedUV.X, 0.0f, 0.999999f);
    const float ClampedV = FMath::Clamp(NormalizedUV.Y, 0.0f, 0.999999f);

    OutTileX = FMath::FloorToInt(ClampedU * static_cast<float>(GridWidth));
    OutTileY = FMath::FloorToInt(ClampedV * static_cast<float>(GridHeight));

    return TileXYToIndex(OutTileX, OutTileY, OutTileIndex);
}

bool UWorldMapSubsystem::WorldUVToVoxelXY(const FVector2D& WorldUV, FIntPoint& OutVoxelXY) const
{
    OutVoxelXY = FIntPoint::ZeroValue;

    if (!bInitialized || GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const UVoxelWorldManager* VoxelMgr = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VoxelMgr)
    {
        return false;
    }

    const int32 ChunkSizeVoxels = FMath::Max(1, VoxelMgr->GetChunkSizeInVoxels());
    const int32 TotalVoxelWidth = GridWidth * ChunkSizeVoxels;
    const int32 TotalVoxelHeight = GridHeight * ChunkSizeVoxels;

    const float U = FMath::Clamp(WorldUV.X, 0.0f, 0.999999f);
    const float V = FMath::Clamp(WorldUV.Y, 0.0f, 0.999999f);

    const int32 HalfVoxelWidth = TotalVoxelWidth / 2;
    const int32 HalfVoxelHeight = TotalVoxelHeight / 2;

    const int32 VoxelX = FMath::Clamp(
        FMath::FloorToInt(U * static_cast<float>(TotalVoxelWidth)) - HalfVoxelWidth,
        -HalfVoxelWidth,
        HalfVoxelWidth - 1);

    const int32 VoxelY = FMath::Clamp(
        FMath::FloorToInt(V * static_cast<float>(TotalVoxelHeight)) - HalfVoxelHeight,
        -HalfVoxelHeight,
        HalfVoxelHeight - 1);

    OutVoxelXY = FIntPoint(VoxelX, VoxelY);
    return true;
}

bool UWorldMapSubsystem::WorldUVToBestSpawnTile(
    const FVector2D& WorldUV,
    FIntPoint& OutTileXY,
    int32& OutTileIndex,
    FIntPoint& OutVoxelXY) const
{
    OutTileXY = FIntPoint(INDEX_NONE, INDEX_NONE);
    OutTileIndex = INDEX_NONE;
    OutVoxelXY = FIntPoint::ZeroValue;

    if (!bInitialized || GridWidth <= 0 || GridHeight <= 0)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const UVoxelWorldManager* VoxelMgr = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VoxelMgr)
    {
        return false;
    }

    if (!WorldUVToVoxelXY(WorldUV, OutVoxelXY))
    {
        return false;
    }

    const int32 ChunkSizeVoxels = FMath::Max(1, VoxelMgr->GetChunkSizeInVoxels());

    const int32 HalfGridX = GridWidth / 2;
    const int32 HalfGridY = GridHeight / 2;

    const FIntPoint RawTileXY(
        FMath::Clamp(
            FMath::FloorToInt(static_cast<float>(OutVoxelXY.X) / static_cast<float>(ChunkSizeVoxels)) + HalfGridX,
            0,
            GridWidth - 1),
        FMath::Clamp(
            FMath::FloorToInt(static_cast<float>(OutVoxelXY.Y) / static_cast<float>(ChunkSizeVoxels)) + HalfGridY,
            0,
            GridHeight - 1));

    FIntPoint FinalTileXY = RawTileXY;

    if (!VoxelMgr->IsSpawnableTerrainCell(FinalTileXY))
    {
        FIntPoint NearestTile;
        if (VoxelMgr->FindNearestSpawnableTerrainCell(RawTileXY, NearestTile, 8))
        {
            FinalTileXY = NearestTile;

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[WorldMapSubsystem] Clicked non-spawnable tile (%d,%d) from voxel (%d,%d). Snapped to nearest spawnable (%d,%d)."),
                RawTileXY.X,
                RawTileXY.Y,
                OutVoxelXY.X,
                OutVoxelXY.Y,
                FinalTileXY.X,
                FinalTileXY.Y
            );
        }
    }

    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(FinalTileXY.X, FinalTileXY.Y, TileIndex))
    {
        return false;
    }

    OutTileXY = FinalTileXY;
    OutTileIndex = TileIndex;
    return true;
}

FColor UWorldMapSubsystem::MakeTileMapColor(const FWorldTileData& Tile) const
{
    const float Elevation = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);

    const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")), false);
    const FGameplayTag ForestTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Forest")), false);
    const FGameplayTag DesertTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Desert")), false);
    const FGameplayTag TundraTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Tundra")), false);
    const FGameplayTag CoastalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
    const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
    const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
    const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
    const FGameplayTag LakeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
    const FGameplayTag WaterTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Water")), false);
    const FGameplayTag PlainsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Plains")), false);
    const FGameplayTag GrasslandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Grassland")), false);
    const FGameplayTag HillsTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Hills")), false);

    FColor BaseColor;

    if (Tile.BiomeType.MatchesTag(DeepOceanTag))
    {
        BaseColor = FColor(8, 30, 90);
    }
    else if (Tile.BiomeType.MatchesTag(OceanTag))
    {
        BaseColor = FColor(18, 55, 135);
    }
    else if (Tile.BiomeType.MatchesTag(SeaTag))
    {
        BaseColor = FColor(30, 85, 160);
    }
    else if (Tile.BiomeType.MatchesTag(LakeTag) || Tile.BiomeType.MatchesTag(WaterTag))
    {
        BaseColor = FColor(45, 110, 185);
    }
    else if (Tile.BiomeType.MatchesTag(CoastalTag))
    {
        BaseColor = FColor(210, 196, 145);
    }
    else if (Tile.BiomeType.MatchesTag(MountainTag))
    {
        const uint8 V = static_cast<uint8>(FMath::Lerp(140.0f, 235.0f, Elevation));
        BaseColor = FColor(V, V, V);
    }
    else if (Tile.BiomeType.MatchesTag(HillsTag))
    {
        BaseColor = FColor(120, 110, 85);
    }
    else if (Tile.BiomeType.MatchesTag(ForestTag))
    {
        const uint8 G = static_cast<uint8>(FMath::Lerp(90.0f, 180.0f, Elevation));
        BaseColor = FColor(40, G, 50);
    }
    else if (Tile.BiomeType.MatchesTag(DesertTag))
    {
        BaseColor = FColor(194, 178, 96);
    }
    else if (Tile.BiomeType.MatchesTag(TundraTag))
    {
        const uint8 V = static_cast<uint8>(FMath::Lerp(150.0f, 220.0f, Elevation));
        BaseColor = FColor(V - 10, V, V - 5);
    }
    else if (Tile.BiomeType.MatchesTag(PlainsTag) || Tile.BiomeType.MatchesTag(GrasslandTag))
    {
        BaseColor = FColor(110, 155, 90);
    }
    else
    {
        if (Elevation < 0.20f)
        {
            BaseColor = FColor(25, 70, 150);
        }
        else if (Elevation < 0.30f)
        {
            BaseColor = FColor(210, 196, 145);
        }
        else if (Elevation < 0.55f)
        {
            BaseColor = FColor(90, 140, 80);
        }
        else if (Elevation < 0.75f)
        {
            BaseColor = FColor(125, 115, 90);
        }
        else
        {
            BaseColor = FColor(180, 180, 180);
        }
    }

    const float Brightness = FMath::Lerp(0.75f, 1.15f, Elevation);
    return ScaleColor(BaseColor, Brightness);
}

FColor UWorldMapSubsystem::GetOverlayAwareTileColorAtCell(int32 TileX, int32 TileY, const FWorldTileData& Tile) const
{
    switch (OverlayMode)
    {
    case EWorldMapOverlayMode::Political:
        return MakePoliticalOverlayColor(Tile);

    case EWorldMapOverlayMode::Economy:
        return MakeEconomyOverlayColor(Tile);

    case EWorldMapOverlayMode::Cosmic:
        return MakeCosmicOverlayColor(Tile);

    case EWorldMapOverlayMode::Trade:
        return MakeTradeOverlayColor(Tile);

    case EWorldMapOverlayMode::Terrain:
    default:
        // Main world map should stay faithful to pipeline-generated tile/biome data.
        // This preserves the green grass / sandy coast look the player expects.
        return ApplyKnowledgeToColor(MakeTileMapColor(Tile), Tile);
    }
}

FColor UWorldMapSubsystem::MakeTerrainColorFromVoxelData(int32 TileX, int32 TileY, const FWorldTileData& FallbackTile) const
{
    if (const UWorld* World = GetWorld())
    {
        if (const UVoxelWorldManager* VoxelMgr = World->GetSubsystem<UVoxelWorldManager>())
        {
            EWorldForgeTerrainType TerrainType;
            if (VoxelMgr->GetTerrainTypeAtCell(FIntPoint(TileX, TileY), TerrainType))
            {
                switch (TerrainType)
                {
                case EWorldForgeTerrainType::Water: return FColor(35, 100, 190);
                case EWorldForgeTerrainType::Sand:  return FColor(210, 196, 145);
                case EWorldForgeTerrainType::Grass: return FColor(90, 140, 80);
                case EWorldForgeTerrainType::Dirt:  return FColor(125, 110, 80);
                case EWorldForgeTerrainType::Rock:  return FColor(135, 135, 135);
                case EWorldForgeTerrainType::Snow:  return FColor(230, 230, 235);
                default: break;
                }
            }
        }
    }

    return ApplyKnowledgeToColor(MakeTileMapColor(FallbackTile), FallbackTile);
}

FColor UWorldMapSubsystem::ApplyKnowledgeToColor(const FColor& InColor, const FWorldTileData& Tile) const
{
    return InColor;
}

FColor UWorldMapSubsystem::MakePoliticalOverlayColor(const FWorldTileData& Tile) const
{
    if (Tile.ControllingCivID < 0)
    {
        return ApplyKnowledgeToColor(MakeTileMapColor(Tile), Tile);
    }

    const uint8 R = static_cast<uint8>((Tile.ControllingCivID * 67 + 80) % 255);
    const uint8 G = static_cast<uint8>((Tile.ControllingCivID * 131 + 110) % 255);
    const uint8 B = static_cast<uint8>((Tile.ControllingCivID * 197 + 140) % 255);

    return ApplyKnowledgeToColor(FColor(R, G, B, 255), Tile);
}

FColor UWorldMapSubsystem::MakeEconomyOverlayColor(const FWorldTileData& Tile) const
{
    const float Commerce = FMath::Clamp(Tile.CommercePressure, 0.0f, 1.0f);

    const FColor Low(70, 50, 35);
    const FColor High(240, 210, 80);

    return ApplyKnowledgeToColor(FColor(
        static_cast<uint8>(FMath::Lerp(float(Low.R), float(High.R), Commerce)),
        static_cast<uint8>(FMath::Lerp(float(Low.G), float(High.G), Commerce)),
        static_cast<uint8>(FMath::Lerp(float(Low.B), float(High.B), Commerce)),
        255), Tile);
}

FColor UWorldMapSubsystem::MakeCosmicOverlayColor(const FWorldTileData& Tile) const
{
    const float Sacred = FMath::Clamp(Tile.Sacredness, 0.0f, 1.0f);
    const float Corrupt = FMath::Clamp(Tile.Corruption, 0.0f, 1.0f);
    const float Fear = FMath::Clamp(Tile.FearPressure, 0.0f, 1.0f);

    const uint8 R = static_cast<uint8>(FMath::Clamp(Fear * 255.0f + Corrupt * 120.0f, 0.0f, 255.0f));
    const uint8 G = static_cast<uint8>(FMath::Clamp(Sacred * 220.0f, 0.0f, 255.0f));
    const uint8 B = static_cast<uint8>(FMath::Clamp(Corrupt * 255.0f + Sacred * 80.0f, 0.0f, 255.0f));

    return ApplyKnowledgeToColor(FColor(R, G, B, 255), Tile);
}

FColor UWorldMapSubsystem::MakeTradeOverlayColor(const FWorldTileData& Tile) const
{
    const float Trade = FMath::Clamp(Tile.CommercePressure, 0.0f, 1.0f);
    const uint8 V = static_cast<uint8>(FMath::Lerp(30.0f, 230.0f, Trade));

    return ApplyKnowledgeToColor(FColor(V, V, 40, 255), Tile);
}

FColor UWorldMapSubsystem::GetOverlayAwareTileColor(const FWorldTileData& Tile) const
{
    switch (OverlayMode)
    {
    case EWorldMapOverlayMode::Political: return MakePoliticalOverlayColor(Tile);
    case EWorldMapOverlayMode::Economy:   return MakeEconomyOverlayColor(Tile);
    case EWorldMapOverlayMode::Cosmic:    return MakeCosmicOverlayColor(Tile);
    case EWorldMapOverlayMode::Trade:     return MakeTradeOverlayColor(Tile);
    case EWorldMapOverlayMode::Terrain:
    default:
        return ApplyKnowledgeToColor(MakeTileMapColor(Tile), Tile);
    }
}

void UWorldMapSubsystem::SetSelectedStartTile(int32 TileX, int32 TileY, int32 TileIndex)
{
    if (TileX < 0 || TileY < 0 || TileX >= GridWidth || TileY >= GridHeight)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldMapSubsystem] Rejecting selected start tile. Invalid XY=(%d,%d) Grid=%dx%d"),
            TileX, TileY, GridWidth, GridHeight);
        return;
    }

    int32 VerifiedIndex = TileIndex;
    if (VerifiedIndex < 0 || VerifiedIndex >= WorldTiles.Num())
    {
        VerifiedIndex = TileY * GridWidth + TileX;
    }

    if (!WorldTiles.IsValidIndex(VerifiedIndex))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[WorldMapSubsystem] Rejecting selected start tile. Invalid Index=%d"), VerifiedIndex);
        return;
    }

    SelectedStartTileX = TileX;
    SelectedStartTileY = TileY;
    SelectedStartTileIndex = VerifiedIndex;
    bHasSelectedStartTile = true;

    UE_LOG(LogTemp, Log,
        TEXT("[WorldMapSubsystem] Selected start tile set: X=%d Y=%d Index=%d"),
        SelectedStartTileX, SelectedStartTileY, SelectedStartTileIndex);
}

bool UWorldMapSubsystem::GetSelectedStartTile(int32& OutTileX, int32& OutTileY, int32& OutTileIndex) const
{
    if (!bHasSelectedStartTile)
    {
        return false;
    }

    OutTileX = SelectedStartTileX;
    OutTileY = SelectedStartTileY;
    OutTileIndex = SelectedStartTileIndex;
    return true;
}

void UWorldMapSubsystem::ClearSelectedStartTile()
{
    bHasSelectedStartTile = false;
    SelectedStartTileX = -1;
    SelectedStartTileY = -1;
    SelectedStartTileIndex = -1;

    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Selected start tile cleared."));
}

void UWorldMapSubsystem::AllocateMapTextureIfNeeded()
{
    if (CachedMapTexture &&
        CachedMapTexture->GetSizeX() == GridWidth &&
        CachedMapTexture->GetSizeY() == GridHeight)
    {
        return;
    }

    CachedMapTexture = UTexture2D::CreateTransient(GridWidth, GridHeight, PF_B8G8R8A8);
    if (!CachedMapTexture)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldMapSubsystem] Failed to create transient map texture."));
        return;
    }

    CachedMapTexture->SRGB = true;
    CachedMapTexture->Filter = TF_Nearest;
    CachedMapTexture->AddressX = TA_Clamp;
    CachedMapTexture->AddressY = TA_Clamp;
    CachedMapTexture->UpdateResource();
}

void UWorldMapSubsystem::UploadMapPixels(const TArray<FColor>& PixelData)
{
    if (!CachedMapTexture || !CachedMapTexture->GetPlatformData() ||
        CachedMapTexture->GetPlatformData()->Mips.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldMapSubsystem] UploadMapPixels failed: invalid texture."));
        return;
    }

    FTexture2DMipMap& Mip = CachedMapTexture->GetPlatformData()->Mips[0];
    void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);

    const SIZE_T ExpectedBytes = static_cast<SIZE_T>(GridWidth * GridHeight) * sizeof(FColor);
    FMemory::Memcpy(Data, PixelData.GetData(), ExpectedBytes);

    Mip.BulkData.Unlock();
    CachedMapTexture->UpdateResource();
}