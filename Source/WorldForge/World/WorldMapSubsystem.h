#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldMapSubsystem.generated.h"

class UWorldGenPipeline;

// ------------------------------------------------------------------
// Legacy single-mode enum.
// Kept for backward compatibility with existing Blueprint/C++ code.
// Internally routes through the new bitfield system.
// ------------------------------------------------------------------
UENUM(BlueprintType)
enum class EWorldMapOverlayMode : uint8
{
    Terrain     UMETA(DisplayName = "Terrain"),
    Political   UMETA(DisplayName = "Political"),
    Economy     UMETA(DisplayName = "Economy"),
    Cosmic      UMETA(DisplayName = "Cosmic"),
    Trade       UMETA(DisplayName = "Trade"),
    Underground UMETA(DisplayName = "Underground")
};

// ------------------------------------------------------------------
// Overlay layer bitfield.
//
// Each bit enables one layer.  Multiple layers can be active at once
// and are composited top-to-bottom over terrain.  The blending order
// is fixed:  Terrain → Political → Economy → Cosmic → Trade →
// Underground → Fog.
//
// Usage:
//   ActiveOverlayLayers |=  WF_OVERLAY_POLITICAL;   // turn on
//   ActiveOverlayLayers &= ~WF_OVERLAY_POLITICAL;   // turn off
//   bool bOn = (ActiveOverlayLayers & WF_OVERLAY_POLITICAL) != 0;
// ------------------------------------------------------------------
namespace WorldForgeOverlay
{
    static constexpr int32 Terrain     = 1 << 0;   // Bit 0
    static constexpr int32 Political   = 1 << 1;   // Bit 1
    static constexpr int32 Economy     = 1 << 2;   // Bit 2
    static constexpr int32 Cosmic      = 1 << 3;   // Bit 3
    static constexpr int32 Trade       = 1 << 4;   // Bit 4
    static constexpr int32 Underground = 1 << 5;   // Bit 5
    static constexpr int32 Fog         = 1 << 6;   // Bit 6

    // Default state: terrain + fog visible.
    static constexpr int32 Default     = Terrain | Fog;

    // All overlays enabled at once (useful for debug).
    static constexpr int32 All         = Terrain | Political | Economy | Cosmic | Trade | Underground | Fog;
}

UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldMapSubsystem
    : public UWorldSubsystem
    , public FTickableGameObject
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return bInitialized; }
    virtual bool IsTickableInEditor() const override { return false; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldMapSubsystem, STATGROUP_Tickables);
    }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void InitializeFromPipeline(UWorldGenPipeline* Pipeline);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    bool GetTileData(int32 TileID, FWorldTileData& OutTile) const;
    const FWorldTileData* GetTileDataPtr(int32 TileID) const;
    FWorldTileData* GetMutableTileDataPtr(int32 TileID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    bool GetCivData(int32 CivID, FCivilizationData& OutCiv) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy = 1.0f);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    EKnowledgeLevel GetTileKnowledge(int32 TileID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void SetSelectedStartTile(int32 TileX, int32 TileY, int32 TileIndex);

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool GetSelectedStartTile(int32& OutTileX, int32& OutTileY, int32& OutTileIndex) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void ClearSelectedStartTile();

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool HasSelectedStartTile() const { return bHasSelectedStartTile; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    int32 GetGridWidth() const { return GridWidth; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    int32 GetGridHeight() const { return GridHeight; }

    int32 GetTileCount() const { return WorldTiles.Num(); }
    bool IsInitialized() const { return bInitialized; }

    const TArray<FWorldTileData>& GetWorldTiles()        const { return WorldTiles; }
    const TArray<FCivilizationData>& GetCivilizations()     const { return Civilizations; }
    const TArray<FHistoryEventData>& GetHistoryLog()        const { return HistoryLog; }
    const TArray<FMountainRange>& GetMountainRanges()    const { return MountainRanges; }
    const TArray<FMountainProvince>& GetMountainProvinces() const { return MountainProvinces; }

    TArray<FCivilizationData>& GetMutableCivilizations() { return Civilizations; }

    int32 AddHistoryEvent(const FHistoryEventData& Event);

    // ------------------------------------------------------------------
    // Standard (256x256) map texture — kept for overlays / minimap use.
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    UTexture2D* GetOrCreateMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void RebuildMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void MarkMapTextureDirty();

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    UTexture2D* GetCachedMapTexture() const { return CachedMapTexture; }

    // ------------------------------------------------------------------
    // Detailed map texture — higher resolution, bilinear colour blending
    // between cells.  Use this for the full-screen map widget.
    //
    // Resolution is set by DetailedMapResolution (default 1024).
    // Each pixel samples the terrain colour grid and bilinearly blends
    // the four surrounding cell colours, producing smooth gradients
    // instead of the hard 256x256 tile boundaries.
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    UTexture2D* GetOrCreateDetailedMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void RebuildDetailedMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void MarkDetailedMapTextureDirty();

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    UTexture2D* GetCachedDetailedMapTexture() const { return DetailedMapTexture; }

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap")
    int32 DetailedMapResolution = 2048;

    // ------------------------------------------------------------------
    // Overlay layer bitfield — the DF-style multi-overlay system.
    //
    // Each bit corresponds to one overlay layer (see WorldForgeOverlay
    // namespace).  Multiple layers can be active simultaneously.
    //
    // The rendering pipeline always starts with terrain as the base,
    // then composites enabled overlays in fixed order using per-layer
    // alpha values.
    // ------------------------------------------------------------------
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|WorldMap|Overlay")
    int32 ActiveOverlayLayers = WorldForgeOverlay::Default;

    // ------------------------------------------------------------------
    // Per-layer alpha values.
    //
    // Each overlay blends on top of the layers below it using these
    // strength values.  0.0 = invisible, 1.0 = fully opaque.
    // Tune these to keep the map readable when multiple overlays are
    // active at once.
    // ------------------------------------------------------------------
    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CivTerritoryTintAlpha = 0.45f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PoliticalAlpha = 0.45f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float EconomyAlpha = 0.35f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CosmicAlpha = 0.30f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TradeAlpha = 0.35f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float UndergroundAlpha = 0.50f;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap|Overlay", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FogAlpha = 0.65f;

    // ------------------------------------------------------------------
    // Overlay layer toggle API.
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap|Overlay")
    void SetOverlayLayerActive(int32 LayerBit, bool bActive);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap|Overlay")
    void ToggleOverlayLayer(int32 LayerBit);

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap|Overlay")
    bool IsOverlayLayerActive(int32 LayerBit) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap|Overlay")
    void SetAllOverlayLayers(int32 NewLayerMask);

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap|Overlay")
    int32 GetActiveOverlayLayers() const { return ActiveOverlayLayers; }

    // ------------------------------------------------------------------
    // Progressive world-gen updates (DF-style live map feedback).
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void UpdateFromPartialPipeline(
        const TArray<FWorldTileData>& InTiles,
        const TArray<FCivilizationData>& InCivs,
        int32 InGridWidth,
        int32 InGridHeight);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void NotifyCivTerritoryChanged();

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    int32 CountCivControlledTiles() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    FColor GetCivColor(int32 CivID) const;

    // ------------------------------------------------------------------
    // Legacy single-mode API.
    //
    // SetOverlayMode() clears all layer bits, enables Terrain, then
    // enables the requested mode's bit.  This lets old code/Blueprints
    // that call SetOverlayMode() work without changes.
    //
    // GetOverlayMode() returns the "dominant" active overlay, or
    // Terrain if multiple overlays are on (since there is no single
    // mode that represents a multi-layer state).
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void SetOverlayMode(EWorldMapOverlayMode NewMode);

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    EWorldMapOverlayMode GetOverlayMode() const;

    // ------------------------------------------------------------------
    // Tile coordinate helpers
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool TileXYToIndex(int32 TileX, int32 TileY, int32& OutTileIndex) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool TileIndexToXY(int32 TileIndex, int32& OutTileX, int32& OutTileY) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool NormalizedMapUVToTile(
        const FVector2D& NormalizedUV,
        int32& OutTileX,
        int32& OutTileY,
        int32& OutTileIndex) const;

    UFUNCTION()
    void HandleVoxelTerrainReady();

    bool bBoundToTerrainReady = false;

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool WorldUVToVoxelXY(const FVector2D& WorldUV, FIntPoint& OutVoxelXY) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    bool WorldUVToBestSpawnTile(
        const FVector2D& WorldUV,
        FIntPoint& OutTileXY,
        int32& OutTileIndex,
        FIntPoint& OutVoxelXY) const;

private:
    void TickLivingWorld();

    // ------------------------------------------------------------------
    // Per-layer color computation.
    //
    // Each returns the raw overlay color for a single tile.  These do
    // NOT include the terrain base — that is applied by the central
    // blending pipeline in ComputeBlendedTileColor().
    // ------------------------------------------------------------------
    FColor MakeTileMapColor(const FWorldTileData& Tile) const;
    FColor ApplyKnowledgeToColor(const FColor& InColor, const FWorldTileData& Tile) const;
    FColor MakePoliticalLayerColor(const FWorldTileData& Tile) const;
    FColor MakeEconomyLayerColor(const FWorldTileData& Tile) const;
    FColor MakeCosmicLayerColor(const FWorldTileData& Tile) const;
    FColor MakeTradeLayerColor(const FWorldTileData& Tile) const;
    FColor MakeUndergroundLayerColor(const FWorldTileData& Tile) const;
    FColor MakeFogLayerColor(const FWorldTileData& Tile) const;

    // ------------------------------------------------------------------
    // Central blending pipeline.
    //
    // Composites all active overlay layers over terrain in fixed order.
    // This is the ONLY function that should be called when computing
    // the final pixel color for map rendering.
    // ------------------------------------------------------------------
    FColor ComputeBlendedTileColor(const FWorldTileData& Tile) const;
    FColor ComputeBlendedTileColorAtCell(int32 TileX, int32 TileY, const FWorldTileData& Tile) const;
    FColor MakeTerrainColorFromVoxelData(int32 TileX, int32 TileY, const FWorldTileData& FallbackTile) const;

    // Alpha blend: lerp SrcColor over DstColor at the given alpha.
    static FColor BlendLayer(const FColor& DstColor, const FColor& SrcColor, float Alpha);

    // Blends civilization ownership color on top of the base terrain color.
    FColor BlendCivTerritoryTint(const FColor& BaseColor, const FWorldTileData& Tile) const;

    void AllocateMapTextureIfNeeded();
    void UploadMapPixels(const TArray<FColor>& PixelData);

    TArray<FWorldTileData>    WorldTiles;
    TArray<FCivilizationData> Civilizations;
    TArray<FHistoryEventData> HistoryLog;
    TArray<FMountainRange>    MountainRanges;
    TArray<FMountainProvince> MountainProvinces;

    int32 GridWidth = 256;
    int32 GridHeight = 256;
    bool  bInitialized = false;
    float TickAccumulator = 0.0f;
    static constexpr float LiveWorldInterval = 30.0f;

    UPROPERTY(Transient)
    UTexture2D* CachedMapTexture = nullptr;

    UPROPERTY(Transient)
    UTexture2D* DetailedMapTexture = nullptr;

    UPROPERTY(Transient)
    bool bMapTextureDirty = true;

    UPROPERTY(Transient)
    bool bDetailedMapTextureDirty = true;

    UPROPERTY(Transient)
    bool bHasSelectedStartTile = false;

    UPROPERTY(Transient)
    int32 SelectedStartTileX = -1;

    UPROPERTY(Transient)
    int32 SelectedStartTileY = -1;

    UPROPERTY(Transient)
    int32 SelectedStartTileIndex = -1;
};
