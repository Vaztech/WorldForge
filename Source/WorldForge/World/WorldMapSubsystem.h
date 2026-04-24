#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "GameplayTagContainer.h"
#include "Engine/Texture2D.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldMapSubsystem.generated.h"

class UWorldGenPipeline;

UENUM(BlueprintType)
enum class EWorldMapOverlayMode : uint8
{
    Terrain     UMETA(DisplayName = "Terrain"),
    Political   UMETA(DisplayName = "Political"),
    Economy     UMETA(DisplayName = "Economy"),
    Cosmic      UMETA(DisplayName = "Cosmic"),
    Trade       UMETA(DisplayName = "Trade")
};

UCLASS()
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
    // between cells. Use this for the full-screen map widget.
    //
    // Resolution is set by DetailedMapResolution (default 1024).
    // Each pixel samples the terrain colour grid and bilinearly blends
    // the four surrounding cell colours, producing smooth gradients
    // instead of the hard 256×256 tile boundaries.
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    UTexture2D* GetOrCreateDetailedMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void RebuildDetailedMapTexture();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void MarkDetailedMapTextureDirty();

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    UTexture2D* GetCachedDetailedMapTexture() const { return DetailedMapTexture; }

    // Resolution of the detailed texture. 1024 is a good default.
    // 2048 is sharper but takes ~4× longer to build.
    // Change before InitializeFromPipeline() or call RebuildDetailedMapTexture()
    // manually after changing at runtime.
    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldMap")
    int32 DetailedMapResolution = 1024;

    // ------------------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category = "WorldForge|WorldMap")
    void SetOverlayMode(EWorldMapOverlayMode NewMode);

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    EWorldMapOverlayMode GetOverlayMode() const { return OverlayMode; }

    // ------------------------------------------------------------------
    // Tile coordinate helpers
    // ------------------------------------------------------------------
    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool TileXYToIndex(int32 TileX, int32 TileY, int32& OutTileIndex) const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|WorldMap")
    bool TileIndexToXY(int32 TileIndex, int32& OutTileX, int32& OutTileY) const;

    // UV is normalized map-space in [0..1].
    // (0,0) = top-left tile, (1,1) = bottom-right tile.
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

    FColor MakeTileMapColor(const FWorldTileData& Tile) const;
    FColor ApplyKnowledgeToColor(const FColor& InColor, const FWorldTileData& Tile) const;
    FColor MakePoliticalOverlayColor(const FWorldTileData& Tile) const;
    FColor MakeEconomyOverlayColor(const FWorldTileData& Tile) const;
    FColor MakeCosmicOverlayColor(const FWorldTileData& Tile) const;
    FColor MakeTradeOverlayColor(const FWorldTileData& Tile) const;
    FColor GetOverlayAwareTileColor(const FWorldTileData& Tile) const;
    FColor GetOverlayAwareTileColorAtCell(int32 TileX, int32 TileY, const FWorldTileData& Tile) const;
    FColor MakeTerrainColorFromVoxelData(int32 TileX, int32 TileY, const FWorldTileData& FallbackTile) const;

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

    UPROPERTY(Transient)
    EWorldMapOverlayMode OverlayMode = EWorldMapOverlayMode::Terrain;
};