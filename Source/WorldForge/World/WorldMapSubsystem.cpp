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

    // ─── Stable civ color palette ─────────────────────────────────
    static const FColor GCivPalette[] =
    {
        FColor(220,  50,  50, 255),   //  0  Red
        FColor(50, 120, 220, 255),   //  1  Blue
        FColor(50, 190,  70, 255),   //  2  Green
        FColor(220, 180,  40, 255),   //  3  Gold
        FColor(180,  60, 200, 255),   //  4  Purple
        FColor(240, 130,  30, 255),   //  5  Orange
        FColor(30, 200, 200, 255),   //  6  Teal
        FColor(200,  80, 120, 255),   //  7  Rose
        FColor(100, 180,  40, 255),   //  8  Lime
        FColor(100,  80, 180, 255),   //  9  Indigo
        FColor(210, 150, 100, 255),   // 10  Tan
        FColor(60, 160, 140, 255),   // 11  Jade
        FColor(180, 120,  60, 255),   // 12  Bronze
        FColor(140, 200, 220, 255),   // 13  Sky
        FColor(220, 100, 180, 255),   // 14  Pink
        FColor(160, 210, 100, 255),   // 15  Chartreuse
    };
    static constexpr int32 GCivPaletteCount = UE_ARRAY_COUNT(GCivPalette);

    static FColor GetStableCivColor(int32 CivID)
    {
        if (CivID >= 0 && CivID < GCivPaletteCount)
        {
            return GCivPalette[CivID];
        }

        // Golden-ratio hash fallback for >16 civs
        const uint8 R = static_cast<uint8>((CivID * 67 + 80) % 255);
        const uint8 G = static_cast<uint8>((CivID * 131 + 110) % 255);
        const uint8 B = static_cast<uint8>((CivID * 197 + 140) % 255);
        return FColor(R, G, B, 255);
    }
}

// ═══════════════════════════════════════════════════════════════════
// Lifecycle
// ═══════════════════════════════════════════════════════════════════

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

    MarkMapTextureDirty();
    MarkDetailedMapTextureDirty();

    UE_LOG(LogTemp, Log,
        TEXT("[WorldMapSubsystem] Loaded %d tiles, %d civs, %d history events, CivTiles=%d"),
        WorldTiles.Num(),
        Civilizations.Num(),
        HistoryLog.Num(),
        CountCivControlledTiles());
}

void UWorldMapSubsystem::UpdateFromPartialPipeline(
    const TArray<FWorldTileData>& InTiles,
    const TArray<FCivilizationData>& InCivs,
    int32 InGridWidth,
    int32 InGridHeight)
{
    if (InTiles.Num() <= 0 || InGridWidth <= 0 || InGridHeight <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldMapSubsystem] UpdateFromPartialPipeline ignored: invalid input."));
        return;
    }

    WorldTiles = InTiles;
    Civilizations = InCivs;
    GridWidth = InGridWidth;
    GridHeight = InGridHeight;
    bInitialized = true;

    MarkMapTextureDirty();
    MarkDetailedMapTextureDirty();

    UE_LOG(LogTemp, Log,
        TEXT("[WorldMapSubsystem] Partial update received. Tiles=%d Civs=%d Grid=%dx%d CivTiles=%d"),
        WorldTiles.Num(),
        Civilizations.Num(),
        GridWidth,
        GridHeight,
        CountCivControlledTiles());
}

void UWorldMapSubsystem::NotifyCivTerritoryChanged()
{
    MarkMapTextureDirty();
    MarkDetailedMapTextureDirty();

    UE_LOG(LogTemp, Log,
        TEXT("[WorldMapSubsystem] Civ territory changed. CivTiles=%d"),
        CountCivControlledTiles());
}

int32 UWorldMapSubsystem::CountCivControlledTiles() const
{
    int32 Count = 0;

    for (const FWorldTileData& Tile : WorldTiles)
    {
        if (Tile.ControllingCivID >= 0)
        {
            ++Count;
        }
    }

    return Count;
}

FColor UWorldMapSubsystem::GetCivColor(int32 CivID) const
{
    if (CivID < 0)
    {
        return FColor::Transparent;
    }

    return GetStableCivColor(CivID);
}

void UWorldMapSubsystem::HandleVoxelTerrainReady()
{
    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Terrain-ready: rebuilding detailed map texture only."));

    MarkDetailedMapTextureDirty();
    RebuildDetailedMapTexture();
}

// ═══════════════════════════════════════════════════════════════════
// Tick
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Data accessors
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Overlay layer toggle API
// ═══════════════════════════════════════════════════════════════════

void UWorldMapSubsystem::SetOverlayLayerActive(int32 LayerBit, bool bActive)
{
    const int32 OldMask = ActiveOverlayLayers;

    if (bActive)
    {
        ActiveOverlayLayers |= LayerBit;
    }
    else
    {
        ActiveOverlayLayers &= ~LayerBit;
    }

    // Terrain is always on — enforce invariant.
    ActiveOverlayLayers |= WorldForgeOverlay::Terrain;

    if (OldMask != ActiveOverlayLayers)
    {
        MarkMapTextureDirty();
        MarkDetailedMapTextureDirty();

        UE_LOG(LogTemp, Log,
            TEXT("[WorldMapSubsystem] Overlay layers changed: 0x%02X -> 0x%02X"),
            OldMask, ActiveOverlayLayers);
    }
}

void UWorldMapSubsystem::ToggleOverlayLayer(int32 LayerBit)
{
    // Don't allow toggling terrain off.
    if (LayerBit == WorldForgeOverlay::Terrain)
    {
        return;
    }

    SetOverlayLayerActive(LayerBit, !IsOverlayLayerActive(LayerBit));
}

bool UWorldMapSubsystem::IsOverlayLayerActive(int32 LayerBit) const
{
    return (ActiveOverlayLayers & LayerBit) != 0;
}

void UWorldMapSubsystem::SetAllOverlayLayers(int32 NewLayerMask)
{
    // Enforce terrain always on.
    ActiveOverlayLayers = NewLayerMask | WorldForgeOverlay::Terrain;
    MarkMapTextureDirty();
    MarkDetailedMapTextureDirty();
}

// ═══════════════════════════════════════════════════════════════════
// Legacy single-mode API — routes through the bitfield
// ═══════════════════════════════════════════════════════════════════

void UWorldMapSubsystem::SetOverlayMode(EWorldMapOverlayMode NewMode)
{
    // Clear all non-terrain bits, then enable the requested layer.
    int32 NewMask = WorldForgeOverlay::Terrain | WorldForgeOverlay::Fog;

    switch (NewMode)
    {
    case EWorldMapOverlayMode::Political:   NewMask |= WorldForgeOverlay::Political;   break;
    case EWorldMapOverlayMode::Economy:     NewMask |= WorldForgeOverlay::Economy;     break;
    case EWorldMapOverlayMode::Cosmic:      NewMask |= WorldForgeOverlay::Cosmic;      break;
    case EWorldMapOverlayMode::Trade:       NewMask |= WorldForgeOverlay::Trade;       break;
    case EWorldMapOverlayMode::Underground: NewMask |= WorldForgeOverlay::Underground; break;
    case EWorldMapOverlayMode::Terrain:
    default:
        break;
    }

    SetAllOverlayLayers(NewMask);
}

EWorldMapOverlayMode UWorldMapSubsystem::GetOverlayMode() const
{
    // Return the highest-priority non-terrain overlay that is active.
    // If multiple are on, the first one in priority order wins.
    if (ActiveOverlayLayers & WorldForgeOverlay::Political)   return EWorldMapOverlayMode::Political;
    if (ActiveOverlayLayers & WorldForgeOverlay::Economy)     return EWorldMapOverlayMode::Economy;
    if (ActiveOverlayLayers & WorldForgeOverlay::Cosmic)      return EWorldMapOverlayMode::Cosmic;
    if (ActiveOverlayLayers & WorldForgeOverlay::Trade)       return EWorldMapOverlayMode::Trade;
    if (ActiveOverlayLayers & WorldForgeOverlay::Underground) return EWorldMapOverlayMode::Underground;
    return EWorldMapOverlayMode::Terrain;
}

// ═══════════════════════════════════════════════════════════════════
// Standard 256x256 map texture
// ═══════════════════════════════════════════════════════════════════

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
            PixelData[Index] = ComputeBlendedTileColorAtCell(X, Y, Tile);
        }
    }

    UploadMapPixels(PixelData);

    bMapTextureDirty = false;

    UE_LOG(LogTemp, Log, TEXT("[WorldMapSubsystem] Map texture rebuilt. Size=%dx%d Layers=0x%02X CivTiles=%d"),
        GridWidth, GridHeight, ActiveOverlayLayers, CountCivControlledTiles());
}

void UWorldMapSubsystem::MarkMapTextureDirty()
{
    bMapTextureDirty = true;
}

// ═══════════════════════════════════════════════════════════════════
// Detailed map texture
// ═══════════════════════════════════════════════════════════════════

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
        DetailedMapTexture->Filter = TF_Nearest; // Keep the overworld map crisp/readable instead of blurry.
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
            case EWorldForgeTerrainType::Lava:  return FColor(200, 60, 20);
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

            // ── Apply overlay blending on the detailed map ─────────
            // Resolve which world tile this pixel falls in and composite
            // all active overlay layers, same as the standard map.
            {
                const int32 HalfGridX2 = GridWidth / 2;
                const int32 HalfGridY2 = GridHeight / 2;

                const int32 CivTileX = FMath::Clamp(
                    FMath::FloorToInt(static_cast<float>(VoxelX) / static_cast<float>(ChunkSizeVoxels)) + HalfGridX2,
                    0,
                    GridWidth - 1);

                const int32 CivTileY = FMath::Clamp(
                    FMath::FloorToInt(static_cast<float>(VoxelY) / static_cast<float>(ChunkSizeVoxels)) + HalfGridY2,
                    0,
                    GridHeight - 1);

                const int32 CivTileIdx = CivTileY * GridWidth + CivTileX;
                if (WorldTiles.IsValidIndex(CivTileIdx))
                {
                    const FWorldTileData& OverlayTile = WorldTiles[CivTileIdx];

                    // Civ territory tint (always applied on terrain base).
                    if (OverlayTile.ControllingCivID >= 0)
                    {
                        PixelColor = BlendLayer(
                            PixelColor,
                            GetStableCivColor(OverlayTile.ControllingCivID),
                            CivTerritoryTintAlpha);
                    }

                    // Composite each active overlay layer.
                    if (IsOverlayLayerActive(WorldForgeOverlay::Political) && OverlayTile.ControllingCivID >= 0)
                    {
                        PixelColor = BlendLayer(PixelColor, MakePoliticalLayerColor(OverlayTile), PoliticalAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Economy))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeEconomyLayerColor(OverlayTile), EconomyAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Cosmic))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeCosmicLayerColor(OverlayTile), CosmicAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Trade))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeTradeLayerColor(OverlayTile), TradeAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Underground))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeUndergroundLayerColor(OverlayTile), UndergroundAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Resource))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeResourceLayerColor(OverlayTile), ResourceAlpha);
                    }
                    if (IsOverlayLayerActive(WorldForgeOverlay::Fog))
                    {
                        PixelColor = BlendLayer(PixelColor, MakeFogLayerColor(OverlayTile), FogAlpha);
                    }
                }
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

// ═══════════════════════════════════════════════════════════════════
// Selection
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Texture allocation / upload
// ═══════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════
// Coordinate helpers
// ═══════════════════════════════════════════════════════════════════

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

bool UWorldMapSubsystem::WorldPositionToWorldTileIndex(const FVector& WorldPosition, int32& OutWorldTileIndex) const
{
    OutWorldTileIndex = INDEX_NONE;

    const UWorld* World = GetWorld();
    const UVoxelWorldManager* VoxelMgr = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VoxelMgr)
    {
        return false;
    }

    const FIntPoint WorldCell = VoxelMgr->WorldToTerrainCell(WorldPosition);
    return WorldCellToWorldTileIndex(WorldCell, OutWorldTileIndex);
}

bool UWorldMapSubsystem::WorldCellToWorldTileIndex(const FIntPoint& WorldCell, int32& OutWorldTileIndex) const
{
    OutWorldTileIndex = INDEX_NONE;

    // Terrain/world cells are the strategic world-map tile X/Y space used by embark selection.
    return TileXYToIndex(WorldCell.X, WorldCell.Y, OutWorldTileIndex);
}

bool UWorldMapSubsystem::VoxelCellToWorldTileIndex(const FIntPoint& VoxelCell, int32& OutWorldTileIndex) const
{
    OutWorldTileIndex = INDEX_NONE;

    const UWorld* World = GetWorld();
    const UVoxelWorldManager* VoxelMgr = World ? World->GetSubsystem<UVoxelWorldManager>() : nullptr;
    if (!VoxelMgr)
    {
        return false;
    }

    const FIntPoint WorldCell = VoxelMgr->VoxelCellToChunkCell(VoxelCell);
    return WorldCellToWorldTileIndex(WorldCell, OutWorldTileIndex);
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

// ═══════════════════════════════════════════════════════════════════
// Blending utilities
// ═══════════════════════════════════════════════════════════════════

FColor UWorldMapSubsystem::BlendLayer(const FColor& DstColor, const FColor& SrcColor, float Alpha)
{
    const float A = FMath::Clamp(Alpha, 0.0f, 1.0f);
    const float InvA = 1.0f - A;

    return FColor(
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(DstColor.R * InvA + SrcColor.R * A), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(DstColor.G * InvA + SrcColor.G * A), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(DstColor.B * InvA + SrcColor.B * A), 0, 255)),
        255);
}

FColor UWorldMapSubsystem::BlendCivTerritoryTint(const FColor& BaseColor, const FWorldTileData& Tile) const
{
    if (Tile.ControllingCivID < 0)
    {
        return BaseColor;
    }

    return BlendLayer(BaseColor, GetStableCivColor(Tile.ControllingCivID), CivTerritoryTintAlpha);
}

// ═══════════════════════════════════════════════════════════════════
// Central blending pipeline
//
// Composites all active overlay layers over terrain in fixed order:
//   Terrain (always) → Political → Economy → Cosmic → Trade →
//   Underground → Fog
//
// This is the ONLY function that should be called when computing the
// final pixel color for the standard (256x256) map.
// ═══════════════════════════════════════════════════════════════════

FColor UWorldMapSubsystem::ComputeBlendedTileColor(const FWorldTileData& Tile) const
{
    // 1. Start with terrain base.
    FColor Color = ApplyKnowledgeToColor(MakeTileMapColor(Tile), Tile);

    // 2. Civ territory tint (always applied on terrain, not gated by
    //    the Political overlay bit — territory borders should always
    //    be visible on the terrain layer).
    Color = BlendCivTerritoryTint(Color, Tile);

    // 3. Overlay layers in fixed order.
    if (IsOverlayLayerActive(WorldForgeOverlay::Political) && Tile.ControllingCivID >= 0)
    {
        Color = BlendLayer(Color, MakePoliticalLayerColor(Tile), PoliticalAlpha);
    }

    if (IsOverlayLayerActive(WorldForgeOverlay::Economy))
    {
        Color = BlendLayer(Color, MakeEconomyLayerColor(Tile), EconomyAlpha);
    }

    if (IsOverlayLayerActive(WorldForgeOverlay::Cosmic))
    {
        Color = BlendLayer(Color, MakeCosmicLayerColor(Tile), CosmicAlpha);
    }

    if (IsOverlayLayerActive(WorldForgeOverlay::Trade))
    {
        Color = BlendLayer(Color, MakeTradeLayerColor(Tile), TradeAlpha);
    }

    if (IsOverlayLayerActive(WorldForgeOverlay::Underground))
    {
        Color = BlendLayer(Color, MakeUndergroundLayerColor(Tile), UndergroundAlpha);
    }

    if (IsOverlayLayerActive(WorldForgeOverlay::Resource))
    {
        Color = BlendLayer(Color, MakeResourceLayerColor(Tile), ResourceAlpha);
    }

    // 4. Fog is always last — it darkens unknown areas on top of
    //    everything else.
    if (IsOverlayLayerActive(WorldForgeOverlay::Fog))
    {
        Color = BlendLayer(Color, MakeFogLayerColor(Tile), FogAlpha);
    }

    return Color;
}

FColor UWorldMapSubsystem::ComputeBlendedTileColorAtCell(int32 TileX, int32 TileY, const FWorldTileData& Tile) const
{
    // For the standard map the cell coordinates are unused (no voxel
    // sampling), so we just delegate to the tile-only blender.
    return ComputeBlendedTileColor(Tile);
}

// ═══════════════════════════════════════════════════════════════════
// Per-layer color functions
//
// Each returns the RAW overlay color — no terrain base, no alpha
// blending.  The central pipeline (ComputeBlendedTileColor) handles
// compositing.
// ═══════════════════════════════════════════════════════════════════

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
    const FGameplayTag AlpineTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Alpine")), false);
    const FGameplayTag SnowPeakTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.SnowPeak")), false);
    const FGameplayTag IceSheetTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.IceSheet")), false);
    const FGameplayTag TaigaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Taiga")), false);
    const FGameplayTag WoodlandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Woodland")), false);
    const FGameplayTag ShrublandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Shrubland")), false);
    const FGameplayTag SteppeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Steppe")), false);
    const FGameplayTag SavannaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Savanna")), false);
    const FGameplayTag MarshTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Marsh")), false);
    const FGameplayTag JungleTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Jungle")), false);
    const FGameplayTag RiverTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.River")), false);
    const FGameplayTag CanyonTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Canyon")), false);
    const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
    const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);

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
    else if (Tile.BiomeType.MatchesTag(RiverTag))
    {
        BaseColor = FColor(45, 130, 200);
    }
    else if (Tile.BiomeType.MatchesTag(LakeTag) || Tile.BiomeType.MatchesTag(WaterTag))
    {
        BaseColor = FColor(45, 110, 185);
    }
    else if (Tile.BiomeType.MatchesTag(LavaTag))
    {
        BaseColor = FColor(230, 55, 15);
    }
    else if (Tile.BiomeType.MatchesTag(CoastalTag))
    {
        BaseColor = FColor(210, 196, 145);
    }
    else if (Tile.BiomeType.MatchesTag(IceSheetTag) || Tile.BiomeType.MatchesTag(SnowPeakTag))
    {
        BaseColor = FColor(230, 235, 240);
    }
    else if (Tile.BiomeType.MatchesTag(MountainTag) || Tile.BiomeType.MatchesTag(AlpineTag))
    {
        const uint8 V = static_cast<uint8>(FMath::Lerp(135.0f, 235.0f, Elevation));
        BaseColor = FColor(V, V, V);
    }
    else if (Tile.BiomeType.MatchesTag(CanyonTag))
    {
        BaseColor = FColor(150, 95, 65);
    }
    else if (Tile.BiomeType.MatchesTag(HillsTag))
    {
        BaseColor = FColor(120, 110, 85);
    }
    else if (Tile.BiomeType.MatchesTag(VolcanicTag))
    {
        BaseColor = FColor(80, 75, 70);
    }
    else if (Tile.BiomeType.MatchesTag(JungleTag))
    {
        BaseColor = FColor(20, 120, 45);
    }
    else if (Tile.BiomeType.MatchesTag(ForestTag) || Tile.BiomeType.MatchesTag(WoodlandTag))
    {
        const uint8 G = static_cast<uint8>(FMath::Lerp(90.0f, 180.0f, Elevation));
        BaseColor = FColor(40, G, 50);
    }
    else if (Tile.BiomeType.MatchesTag(TaigaTag))
    {
        BaseColor = FColor(55, 115, 90);
    }
    else if (Tile.BiomeType.MatchesTag(MarshTag))
    {
        BaseColor = FColor(55, 105, 75);
    }
    else if (Tile.BiomeType.MatchesTag(ShrublandTag))
    {
        BaseColor = FColor(125, 145, 85);
    }
    else if (Tile.BiomeType.MatchesTag(SavannaTag))
    {
        BaseColor = FColor(155, 165, 80);
    }
    else if (Tile.BiomeType.MatchesTag(SteppeTag))
    {
        BaseColor = FColor(135, 150, 90);
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
                case EWorldForgeTerrainType::Lava:  return FColor(200, 60, 20);
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

// ─── Political layer ──────────────────────────────────────────────
// Returns the civ's territory color.  The central blender applies
// the PoliticalAlpha blend.
FColor UWorldMapSubsystem::MakePoliticalLayerColor(const FWorldTileData& Tile) const
{
    if (Tile.ControllingCivID < 0)
    {
        // No civ — transparent black (won't affect blend).
        return FColor(0, 0, 0, 0);
    }

    return GetStableCivColor(Tile.ControllingCivID);
}

// ─── Economy layer ────────────────────────────────────────────────
// Heat map from CommercePressure: brown (low) → gold (high).
FColor UWorldMapSubsystem::MakeEconomyLayerColor(const FWorldTileData& Tile) const
{
    const float Commerce = FMath::Clamp(Tile.CommercePressure, 0.0f, 1.0f);

    const FColor Low(70, 50, 35);
    const FColor High(240, 210, 80);

    return FColor(
        static_cast<uint8>(FMath::Lerp(float(Low.R), float(High.R), Commerce)),
        static_cast<uint8>(FMath::Lerp(float(Low.G), float(High.G), Commerce)),
        static_cast<uint8>(FMath::Lerp(float(Low.B), float(High.B), Commerce)),
        255);
}

// ─── Cosmic layer ─────────────────────────────────────────────────
// Sacredness → green, Corruption → purple, Fear → red.
FColor UWorldMapSubsystem::MakeCosmicLayerColor(const FWorldTileData& Tile) const
{
    const float Sacred = FMath::Clamp(Tile.Sacredness, 0.0f, 1.0f);
    const float Corrupt = FMath::Clamp(Tile.Corruption, 0.0f, 1.0f);
    const float Fear = FMath::Clamp(Tile.FearPressure, 0.0f, 1.0f);

    const uint8 R = static_cast<uint8>(FMath::Clamp(Fear * 255.0f + Corrupt * 120.0f, 0.0f, 255.0f));
    const uint8 G = static_cast<uint8>(FMath::Clamp(Sacred * 220.0f, 0.0f, 255.0f));
    const uint8 B = static_cast<uint8>(FMath::Clamp(Corrupt * 255.0f + Sacred * 80.0f, 0.0f, 255.0f));

    return FColor(R, G, B, 255);
}

// ─── Trade layer ──────────────────────────────────────────────────
// Yellow-gold intensity based on commerce.
FColor UWorldMapSubsystem::MakeTradeLayerColor(const FWorldTileData& Tile) const
{
    const float Trade = FMath::Clamp(Tile.CommercePressure, 0.0f, 1.0f);
    const uint8 V = static_cast<uint8>(FMath::Lerp(30.0f, 230.0f, Trade));

    return FColor(V, V, 40, 255);
}

// ─── Underground layer ────────────────────────────────────────────
// Caves → blue, Canyons → red, Lava tubes → orange, None → dark gray.
FColor UWorldMapSubsystem::MakeUndergroundLayerColor(const FWorldTileData& Tile) const
{
    const float Elev = FMath::Clamp(Tile.Elevation, 0.0f, 1.0f);

    // Water tiles → dark blue
    if (Elev < 0.48f)
    {
        return FColor(8, 20, 50, 255);
    }

    const float CaveIntensity = FMath::Clamp(Tile.VoxelProfile.CaveIntensity, 0.0f, 3.0f);
    const float CanyonIntensity = FMath::Clamp(Tile.VoxelProfile.CanyonIntensity, 0.0f, 3.0f);
    const float CaveZScale = FMath::Clamp(Tile.VoxelProfile.CaveZScale, 0.0f, 1.0f);
    const bool bIsLavaTube = (CaveIntensity > 0.0f && CaveZScale < 0.5f);

    // No underground features → dark gray
    if (CaveIntensity <= 0.0f && CanyonIntensity <= 0.0f)
    {
        return FColor(35, 35, 35, 255);
    }

    // Lava tubes → orange gradient
    if (bIsLavaTube)
    {
        const float T = FMath::Clamp(CaveIntensity / 2.0f, 0.0f, 1.0f);
        const uint8 R = static_cast<uint8>(FMath::Lerp(80.0f, 230.0f, T));
        const uint8 G = static_cast<uint8>(FMath::Lerp(30.0f, 100.0f, T));
        return FColor(R, G, 10, 255);
    }

    // Cave + canyon blend
    const float CaveT = FMath::Clamp(CaveIntensity / 2.5f, 0.0f, 1.0f);
    const float CanyonT = FMath::Clamp(CanyonIntensity / 2.0f, 0.0f, 1.0f);

    const uint8 R = static_cast<uint8>(FMath::Lerp(25.0f, 200.0f, CanyonT));
    const uint8 G = static_cast<uint8>(FMath::Lerp(25.0f, 60.0f, FMath::Max(CaveT, CanyonT)));
    const uint8 B = static_cast<uint8>(FMath::Lerp(25.0f, 220.0f, CaveT));

    return FColor(R, G, B, 255);
}

// ─── Fog layer ────────────────────────────────────────────────────
// Unexplored tiles → near-black.  Surveyed/known tiles → transparent
// (returns base color unchanged when blended at FogAlpha).
FColor UWorldMapSubsystem::MakeFogLayerColor(const FWorldTileData& Tile) const
{
    switch (Tile.KnowledgeState.KnowledgeLevel)
    {
    case EKnowledgeLevel::Unexplored:
        // Full fog — dark.
        return FColor(10, 10, 12, 255);

    case EKnowledgeLevel::Rumored:
        // Partial fog — dim.
        return FColor(30, 28, 35, 255);

    case EKnowledgeLevel::Surveyed:
    default:
        // No fog — return the "identity" color for this tile's terrain
        // so the blend is a no-op.  This avoids needing a separate
        // "skip this pixel" path in the blender.
        return ApplyKnowledgeToColor(MakeTileMapColor(Tile), Tile);
    }
}

// ─── Resource layer (Batch 3) ────────────────────────────────────
// Reads FResourceSurveyData from each tile and renders a category-
// blended heatmap.  Colors per spec:
//   Metal  = orange  (#FF8C00)
//   Crystal = purple (#9B30FF)
//   Fossil = brown   (#8B4513)
// Opacity proportional to density; tiles with no resources → dark.
FColor UWorldMapSubsystem::MakeResourceLayerColor(const FWorldTileData& Tile) const
{
    const int32 IdxMetal = static_cast<int32>(EResourceCategory::Metal);
    const int32 IdxCrystal = static_cast<int32>(EResourceCategory::Crystal);
    const int32 IdxFossil = static_cast<int32>(EResourceCategory::Fossil);

    const float MetalDensity = (Tile.ResourceSurvey.DensityByCategory.IsValidIndex(IdxMetal))
        ? FMath::Clamp(Tile.ResourceSurvey.DensityByCategory[IdxMetal], 0.0f, 1.0f) : 0.0f;
    const float CrystalDensity = (Tile.ResourceSurvey.DensityByCategory.IsValidIndex(IdxCrystal))
        ? FMath::Clamp(Tile.ResourceSurvey.DensityByCategory[IdxCrystal], 0.0f, 1.0f) : 0.0f;
    const float FossilDensity = (Tile.ResourceSurvey.DensityByCategory.IsValidIndex(IdxFossil))
        ? FMath::Clamp(Tile.ResourceSurvey.DensityByCategory[IdxFossil], 0.0f, 1.0f) : 0.0f;

    const float TotalDensity = MetalDensity + CrystalDensity + FossilDensity;

    // No resources at all → dark background.
    if (TotalDensity <= KINDA_SMALL_NUMBER)
    {
        return FColor(20, 18, 15, 255);
    }

    // Category colors.
    const FLinearColor MetalColor(1.0f, 0.549f, 0.0f);     // #FF8C00 orange
    const FLinearColor CrystalColor(0.608f, 0.188f, 1.0f);  // #9B30FF purple
    const FLinearColor FossilColor(0.545f, 0.271f, 0.075f);  // #8B4513 brown

    // Weighted blend by relative density.
    const float InvTotal = 1.0f / TotalDensity;
    const FLinearColor Blended =
        MetalColor * (MetalDensity * InvTotal) +
        CrystalColor * (CrystalDensity * InvTotal) +
        FossilColor * (FossilDensity * InvTotal);

    // Scale brightness by overall richness so dense areas glow brighter.
    const float Richness = FMath::Clamp(Tile.ResourceSurvey.OverallRichness, 0.0f, 1.0f);
    const float Brightness = FMath::Lerp(0.3f, 1.0f, Richness);

    return FColor(
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Blended.R * Brightness * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Blended.G * Brightness * 255.0f), 0, 255)),
        static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Blended.B * Brightness * 255.0f), 0, 255)),
        255);
}
// ═══════════════════════════════════════════════════════════════════
// Embark / tile selection helpers
// ═══════════════════════════════════════════════════════════════════

bool UWorldMapSubsystem::IsTileClaimed(int32 TileX, int32 TileY) const
{
    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(TileX, TileY, TileIndex) || !WorldTiles.IsValidIndex(TileIndex))
    {
        return false;
    }

    return WorldTiles[TileIndex].ControllingCivID >= 0;
}

bool UWorldMapSubsystem::IsTileWaterLike(int32 TileX, int32 TileY) const
{
    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(TileX, TileY, TileIndex) || !WorldTiles.IsValidIndex(TileIndex))
    {
        return true;
    }

    const FWorldTileData& Tile = WorldTiles[TileIndex];
    const FString Biome = Tile.BiomeType.ToString();

    if (Tile.WaterDepth > KINDA_SMALL_NUMBER)
    {
        return true;
    }

    if (Biome.Contains(TEXT("DeepOcean")) ||
        Biome.Contains(TEXT("Ocean")) ||
        Biome.Contains(TEXT("Sea")) ||
        Biome.Contains(TEXT("Lake")) ||
        Biome.Contains(TEXT("River")) ||
        Biome.Contains(TEXT("Underwater")))
    {
        return true;
    }

    // Step02 sea threshold is roughly 0.48.  Keep a small safety floor so
    // borderline coast/water tiles are not selected as colony starts.
    return Tile.Elevation < 0.48f;
}

bool UWorldMapSubsystem::IsTileLandLike(int32 TileX, int32 TileY) const
{
    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(TileX, TileY, TileIndex) || !WorldTiles.IsValidIndex(TileIndex))
    {
        return false;
    }

    return !IsTileWaterLike(TileX, TileY);
}

bool UWorldMapSubsystem::IsTileValidForEmbark(int32 TileX, int32 TileY, FString& OutReason) const
{
    OutReason.Empty();

    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(TileX, TileY, TileIndex) || !WorldTiles.IsValidIndex(TileIndex))
    {
        OutReason = TEXT("Invalid tile.");
        return false;
    }

    if (IsTileClaimed(TileX, TileY))
    {
        OutReason = TEXT("Claimed by another civilization.");
        return false;
    }

    if (IsTileWaterLike(TileX, TileY))
    {
        OutReason = TEXT("Water tile. Select land.");
        return false;
    }

    OutReason = TEXT("Valid embark region.");
    return true;
}

FString UWorldMapSubsystem::BuildTileSurveyText(int32 TileX, int32 TileY, bool bDebugReveal) const
{
    int32 TileIndex = INDEX_NONE;
    if (!TileXYToIndex(TileX, TileY, TileIndex) || !WorldTiles.IsValidIndex(TileIndex))
    {
        return TEXT("Invalid tile.");
    }

    const FWorldTileData& Tile = WorldTiles[TileIndex];
    FString BlockReason;
    const bool bValidEmbark = IsTileValidForEmbark(TileX, TileY, BlockReason);

    FString Out = FString::Printf(
        TEXT("Tile (%d,%d)  Index=%d\nBiome: %s\nElevation: %.2f  WaterDepth: %.2f\nClaimed: %s\nEmbark: %s"),
        TileX,
        TileY,
        TileIndex,
        *Tile.BiomeType.ToString(),
        Tile.Elevation,
        Tile.WaterDepth,
        Tile.ControllingCivID >= 0 ? *FString::Printf(TEXT("Civ %d"), Tile.ControllingCivID) : TEXT("No"),
        bValidEmbark ? TEXT("Valid") : *BlockReason);

    const bool bCanRevealSurvey = bDebugReveal ||
        Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Surveyed ||
        Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Live ||
        Tile.KnowledgeState.KnowledgeLevel == EKnowledgeLevel::Rumored;

    if (!bCanRevealSurvey)
    {
        Out += TEXT("\nResources: Unknown (survey required)");
        return Out;
    }

    const int32 IdxMetal = static_cast<int32>(EResourceCategory::Metal);
    const int32 IdxCrystal = static_cast<int32>(EResourceCategory::Crystal);
    const int32 IdxFossil = static_cast<int32>(EResourceCategory::Fossil);

    const TArray<float>& Density = Tile.ResourceSurvey.DensityByCategory;
    const TArray<float>& Depth = Tile.ResourceSurvey.PeakDepthByCategory;

    const float MetalPct = Density.IsValidIndex(IdxMetal) ? FMath::Clamp(Density[IdxMetal] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float CrystalPct = Density.IsValidIndex(IdxCrystal) ? FMath::Clamp(Density[IdxCrystal] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float FossilPct = Density.IsValidIndex(IdxFossil) ? FMath::Clamp(Density[IdxFossil] * 100.0f, 0.0f, 100.0f) : 0.0f;
    const float RichnessPct = FMath::Clamp(Tile.ResourceSurvey.OverallRichness * 100.0f, 0.0f, 100.0f);

    Out += FString::Printf(TEXT("\nResources: Richness %.0f%%"), RichnessPct);

    if (MetalPct > 0.1f)
    {
        Out += FString::Printf(TEXT("\n  Metal/Fe: %.1f%%  PeakDepth: %.0f"), MetalPct, Depth.IsValidIndex(IdxMetal) ? Depth[IdxMetal] : 0.0f);
    }
    if (CrystalPct > 0.1f)
    {
        Out += FString::Printf(TEXT("\n  Crystal: %.1f%%  PeakDepth: %.0f"), CrystalPct, Depth.IsValidIndex(IdxCrystal) ? Depth[IdxCrystal] : 0.0f);
    }
    if (FossilPct > 0.1f)
    {
        Out += FString::Printf(TEXT("\n  Fossil/Stone: %.1f%%  PeakDepth: %.0f"), FossilPct, Depth.IsValidIndex(IdxFossil) ? Depth[IdxFossil] : 0.0f);
    }

    if (MetalPct <= 0.1f && CrystalPct <= 0.1f && FossilPct <= 0.1f)
    {
        Out += TEXT("\n  No strong mineral signal detected.");
    }

    Out += FString::Printf(
        TEXT("\nWorld Effects: Sacred %.0f%% | Corruption %.0f%% | Fear %.0f%% | Commerce %.0f%%"),
        FMath::Clamp(Tile.Sacredness * 100.0f, 0.0f, 100.0f),
        FMath::Clamp(Tile.Corruption * 100.0f, 0.0f, 100.0f),
        FMath::Clamp(Tile.FearPressure * 100.0f, 0.0f, 100.0f),
        FMath::Clamp(Tile.CommercePressure * 100.0f, 0.0f, 100.0f));

    return Out;
}
