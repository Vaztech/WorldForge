// TileGridOverlayActor.h
// Replaces TileSelectionOverlayActor with a unified, per-mode grid overlay.
// Uses HISM flat planes Z-sampled to terrain. Per-instance custom data drives
// color/opacity in material — zero material swaps on mode change.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/BuildModeTypes.h"
#include "Grid/GridDesignationTypes.h"
#include "TileGridOverlayActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;
class UDataTable;
class UStaticMesh;
class ULineBatchComponent;

// ─────────────────────────────────────────────────────────────
// FGridOverlayStyle — visual config per placement mode
// ─────────────────────────────────────────────────────────────

USTRUCT(BlueprintType)
struct WORLDFORGE_API FGridOverlayStyle
{
	GENERATED_BODY()

	/** Neutral visible base-grid color shown around the cursor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor NeutralGridColor = FLinearColor(1.f, 1.f, 1.f, 0.12f);

	/** Tile fill color for selected cells (translucent). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor TileColor = FLinearColor(0.2f, 0.6f, 1.0f, 0.35f);

	/** Tile border / outline color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor BorderColor = FLinearColor(0.3f, 0.7f, 1.0f, 0.8f);

	/** Hover highlight tint (blended over TileColor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor HoverColor = FLinearColor(1.f, 1.f, 1.f, 0.5f);

	/** Drag-preview tint (shown while dragging, before confirm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor DragPreviewColor = FLinearColor(0.8f, 0.8f, 0.2f, 0.25f);

	/** Per-mode icon texture (hammer, wheat, pickaxe, wrench, etc.).
	 *  nullptr = no icon rendered. Must be a square texture with alpha. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	TObjectPtr<UTexture2D> IconTexture = nullptr;

	/** Per-mode icon tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style")
	FLinearColor IconTint = FLinearColor(1.f, 1.f, 1.f, 1.f);

	/** Icon scale relative to cell size (0.0–1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Style", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float IconScale = 0.4f;
};

// ─────────────────────────────────────────────────────────────
// EOverlayCellState — per-cell visual state
// ─────────────────────────────────────────────────────────────

UENUM()
enum class EOverlayCellState : uint8
{
	Selected,
	DragPreview,
	Hovered
};

// ─────────────────────────────────────────────────────────────
// ATileGridOverlayActor
// ─────────────────────────────────────────────────────────────

/**
 * Unified grid overlay actor for all WorldForge placement modes.
 *
 * Architecture:
 *   - TileMeshHISM:  flat-plane instances for cell fill + border.
 *                     Per-instance custom data [R, G, B, A] drives color in material.
 *   - IconMeshHISM:  flat-plane instances for mode icons, slightly Z-offset above tiles.
 *                     Uses a MID whose texture param is swapped on mode change.
 *   - HoverMeshComp: single static mesh for the currently hovered cell (always on top).
 *
 * This actor does NO raycasting. BuildModeManager feeds it cell coordinates + terrain Z.
 *
 * Coordinate convention:
 *   VoxelCoord = FIntVector in voxel space (1 unit = 1 voxel = CellSizeUU in world).
 *   TerrainZ   = world-space Z of the terrain surface at that cell (from BuildModeManager's
 *                line trace). If not supplied, the overlay will self-trace.
 */
UCLASS()
class WORLDFORGE_API ATileGridOverlayActor : public AActor
{
	GENERATED_BODY()

public:
	ATileGridOverlayActor();

	// ── Configuration (call before first use) ───────────────

	/** Register a visual style for a tool mode. Safe to call multiple times. */
	void RegisterModeStyle(EBuildToolMode Mode, const FGridOverlayStyle& Style);

	/** Switch active mode. Rebuilds icon instances, recolors tiles. */
	void SetActiveMode(EBuildToolMode Mode);

	/** Override voxel cell size (default 100). */
	void SetCellSizeUU(float InSize);

	// ── Hover ───────────────────────────────────────────────

	/** Move the hover highlight to VoxelCoord. TerrainZ = world Z of surface. */
	void SetHoveredCell(const FIntVector& VoxelCoord, float TerrainZ);

	/** Clear hover (mouse left terrain, etc.). */
	void ClearHover();

	// ── Selection (confirmed cells) ─────────────────────────

	void AddSelectedCell(const FIntVector& VoxelCoord, float TerrainZ);
	void RemoveSelectedCell(const FIntVector& VoxelCoord);
	void ToggleSelectedCell(const FIntVector& VoxelCoord, float TerrainZ);

	/** Batch-replace the entire selection. More efficient than add/remove loops. */
	void SetSelectedCells(const TMap<FIntVector, float>& CellsWithTerrainZ);
	void ClearSelection();

	// ── Mining Volume Preview ──────────────────────────────

	/** Draw translucent 3D voxel cubes for mine selections. */
	void SetMiningVolumeCells(const TArray<FIntVector>& VoxelCoords);

	/** Clear translucent 3D voxel cubes for mine selections. */
	void ClearMiningVolumeCells();

	// ── Drag Preview (tentative cells during click-drag) ────

	void SetDragPreviewCells(const TMap<FIntVector, float>& CellsWithTerrainZ);
	void ClearDragPreview();

	/** Clear only the temporary neutral base-grid layer around the cursor.
	 *  Confirmed designation cells/icons are not affected. */
	void ClearBaseGrid();

	/** Replace the neutral base grid with an explicit voxel->terrain-Z map.
	 *  Used by GridSelectionSubsystem for static, per-cell terrain-following grids. */
	void SetBaseGridCells(const TMap<FIntVector, float>& CellsWithTerrainZ);

	/** Build the neutral base grid once around a fixed anchor.
	 *  Unlike SetHoveredCell(), this does not follow the mouse.
	 *  Used when entering a tool mode so the player gets a stable, non-flickering grid. */
	void BuildStaticBaseGridAround(const FIntVector& CenterCoord, float TerrainZ);

	/** Visual-only: underground mine/dig layers use a white grid; surface/above-ground keeps normal style color. */
	void SetUseUndergroundWhiteGrid(bool bEnabled);

	// ── Underground floor block system ──────────────────────────────────────

	/**
	 * Draw solid voxel block slabs at the active underground layer centered on CenterVoxel.
	 * Uses UndergroundBlockMaterial (assign in BP Class Defaults) so blocks look like
	 * actual terrain material rather than a flat translucent tile.
	 * Only draws within UndergroundBlockViewRadius of CenterVoxel.
	 *
	 * Call from GridSelectionSubsystem::RebuildStaticBaseGridFromHover when underground.
	 * LayerFlatZ = the terrain Z of the active layer (voxel top face).
	 */
	void SetUndergroundFloorBlocks(const FIntVector& CenterVoxel, float LayerFlatZ);

	/** Clear the underground floor block slabs. */
	void ClearUndergroundFloorBlocks();

	/**
	 * Configure the instanced mining selection cubes to match BP_MiningGhostActor.
	 * BuildModeManager calls this after spawning the hover ghost so selected cells
	 * use the same cube mesh/material style while staying performant as instances.
	 */
	void ConfigureMiningGhostVisual(
		UStaticMesh* InCubeMesh,
		UMaterialInterface* InGhostMaterial,
		const FLinearColor& InSurfaceTint,
		const FLinearColor& InUndergroundTint,
		float InSurfaceOpacity,
		float InUndergroundOpacity,
		float InCubeScale);

	// ── Persistent Confirmed Designations ───────────────────

	void AddConfirmedDesignationCell(
		const FIntVector& VoxelCoord,
		float TerrainZ,
		EGridDesignationType DesignationType);

	void SetConfirmedDesignationCells(
		EGridDesignationType DesignationType,
		const TMap<FIntVector, float>& CellsWithTerrainZ);

	void ClearConfirmedDesignationCells(EGridDesignationType DesignationType);
	void ClearAllConfirmedDesignationCells();
	void CommitSelectedCellsAsConfirmed(EGridDesignationType DesignationType);

	bool HasConfirmedDesignationCells() const { return ConfirmedDesignationCells.Num() > 0; }

	// ── Visibility ──────────────────────────────────────────

	void ShowOverlay();
	void HideOverlay();
	bool IsOverlayVisible() const { return bOverlayVisible; }

	// ── Query ───────────────────────────────────────────────

	int32 GetSelectedCount() const { return SelectedCells.Num(); }
	int32 GetDragPreviewCount() const { return DragCells.Num(); }
	bool IsCellSelected(const FIntVector& VoxelCoord) const { return SelectedCells.Contains(VoxelCoord); }

	/** Return all currently selected voxel coordinates. */
	void GetSelectedCoords(TArray<FIntVector>& OutCoords) const;

	/** Return the active mode style (read-only). */
	const FGridOverlayStyle& GetCurrentStyle() const { return ActiveStyle; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ── Editor-Assigned Defaults ────────────────────────────

	/** 100×100 UU flat plane mesh (create in editor: plane scaled to 1×1 UU,
	 *  or use the helper function CreateDefaultPlaneMesh at runtime). */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mesh")
	TObjectPtr<UStaticMesh> CellPlaneMesh;

	/** Translucent unlit material that reads PerInstanceCustomData for RGBA.
	 *  Custom data layout: [0]=R  [1]=G  [2]=B  [3]=A  [4]=BorderAlpha */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Material")
	TObjectPtr<UMaterialInterface> TileMaterialBase;

	/** Translucent unlit material with a Texture2D parameter named 'IconTex'.
	 *  Reads PerInstanceCustomData [0..3] for tint. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Material")
	TObjectPtr<UMaterialInterface> IconMaterialBase;

	/** Material for the single hover highlight cell (brighter, slight pulse). */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Material")
	TObjectPtr<UMaterialInterface> HoverMaterialBase;

	/** Optional DataTable that drives overlay icon/color style by designation type. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Styles")
	TObjectPtr<UDataTable> DesignationStyleTable;

	/** Number of cells around hover to draw as a faint base grid. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Layout")
	int32 VisibleGridRadius = 96;

	/** Z offset above terrain surface to prevent Z-fighting (UU). */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Layout")
	float ZOffset = 25.0f;

	/** Extra Z offset for icon plane above tile plane. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Layout")
	float IconZOffset = 6.0f;

	/** Collision channel to trace against for self-sampling terrain Z. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Trace")
	TEnumAsByte<ECollisionChannel> TerrainTraceChannel = ECC_WorldStatic;

	/** Cube mesh for mining volume preview. Defaults to /Engine/BasicShapes/Cube. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining")
	TObjectPtr<UStaticMesh> MiningVolumeCubeMesh;

	/** Optional translucent material for mining volume cubes. Falls back to TileMaterialBase. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining")
	TObjectPtr<UMaterialInterface> MiningVolumeMaterialBase;

	/** Surface selected mining cube tint. Semi-transparent amber over the SDF surface. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining")
	FLinearColor SurfaceMiningCapColor = FLinearColor(1.0f, 0.70f, 0.03f, 0.55f);

	/** Underground selected mining cube tint. Brighter so it reads against the black cutaway void. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining")
	FLinearColor UndergroundMiningCapColor = FLinearColor(1.0f, 0.95f, 0.02f, 0.88f);

	/** Scale for ghost-style selected mining cubes. 0.97 matches BP_MiningGhostActor. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float MiningVolumeScale = 0.97f;

	/** Thin mining cap thickness in world units. Keeps mining designations readable without surface clipping. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining", meta = (ClampMin = "1.0", ClampMax = "20.0"))
	float MiningCapThicknessUU = 6.0f;

	/** Z lift for mining caps above the active layer/grid plane. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float MiningCapZOffsetUU = 36.0f;

	/** Extra lift for bright outline/diagonal designation lines above the cap. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Mining", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float MiningDesignationLineZOffsetUU = 8.0f;

	// ── Underground depth + floor config ────────────────────────────────────

	/**
	 * Material for underground voxel block slabs.
	 * Assign M_WF_UndergroundBlock (opaque/masked) or your terrain material in BP Class Defaults.
	 * Should be opaque so blocks look solid, not translucent.
	 * If null, falls back to TileMaterialBase with UndergroundFloorColor tint.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground")
	TObjectPtr<UMaterialInterface> UndergroundBlockMaterial;

	/**
	 * When true, underground layer slabs are tinted from the same material-layer index
	 * used by MC_WorldForge_Terrain: Sand=0, Grass=1, Dirt=2, Rock=3, Snow=4,
	 * Water=5, Lava=6, Basalt=7, DeepRock=8. This is the Going Medieval-style
	 * readable slice view and is preferred over the generic depth-only helper material.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground")
	bool bUseTerrainLayerColorsForUnderground = true;

	/**
	 * How many cells in each direction from the hover point to draw solid block slabs.
	 * The full white grid still extends to VisibleGridRadius (96).
	 * Keep this small (6-10) so the material blocks feel like a focused local view.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground", meta = (ClampMin = "2", ClampMax = "20"))
	int32 UndergroundBlockViewRadius = 8;

	/**
	 * Thickness of each block slab as a fraction of voxel size.
	 * 0.15 = 15 UU thick. Gives visible 3D depth without obscuring the grid lines above.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float UndergroundBlockThickness = 1.0f;

	/** How many voxel layers below to draw faint depth-reference grid lines. 0 = disabled. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground", meta = (ClampMin = "0", ClampMax = "6"))
	int32 UndergroundDepthReferenceLayers = 0;

	/** Opacity of the nearest depth-reference grid. Each further layer multiplies by UndergroundDepthOpacityFalloff. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UndergroundDepthBaseOpacity = 0.38f;

	/** Falloff multiplier per depth-reference layer. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float UndergroundDepthOpacityFalloff = 0.55f;

	/** Fallback floor tint used when UndergroundBlockMaterial is null. Semi-transparent dark earth color. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Underground")
	FLinearColor UndergroundFloorColor = FLinearColor(0.12f, 0.08f, 0.04f, 0.85f);

private:
	// ── Components ──────────────────────────────────────────

	/** Instanced neutral base grid cells around the cursor. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BaseGridHISM;

	/** Instanced grid line strips. These make the grid readable even in black cutaway voids. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BaseGridLineHISM;

	/** Runtime line component used for terrain-contoured grid lines. This avoids flat planes floating over SDF terrain. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay|Runtime")
	TObjectPtr<ULineBatchComponent> BaseGridLineBatch;

	/** Faint stacked grids at -1, -2, -3 layers below the active underground layer. Gives visual depth stack. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay|Runtime")
	TObjectPtr<ULineBatchComponent> DepthReferenceLineBatch;

	/** Instanced selected cells for the current active tool session. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TileMeshHISM;

	/** Instanced drag-preview cells while the mouse is held. Kept separate from
	 *  selected cells so clearing preview cannot wipe active selections. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DragPreviewMeshHISM;

	/** Instanced persistent confirmed designation cells. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ConfirmedTileMeshHISM;

	/** Instanced icons: one per selected cell (not drag-preview). */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> IconMeshHISM;

	/** Single-instance hover highlight. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UStaticMeshComponent> HoverMeshComp;

	/** Instanced translucent 3D voxel cubes/caps for mine selections. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MiningVolumeHISM;

	/** Solid block slabs rendered at the active underground layer in a small radius around the cursor.
	 *  Makes the underground feel like a solid voxel layer rather than a black void. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay|Underground")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> UndergroundFloorHISM;

	/** Bright outline/X lines for mining designations. Material-independent, stays visible underground. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay|Runtime")
	TObjectPtr<ULineBatchComponent> MiningDesignationLineBatch;

	// ── Runtime State ───────────────────────────────────────

	float CellSizeUU = 100.0f;
	bool  bOverlayVisible = false;
	bool  bHoverActive = false;

	EBuildToolMode CurrentMode = EBuildToolMode::None;
	FGridOverlayStyle ActiveStyle;
	TMap<EBuildToolMode, FGridOverlayStyle> RegisteredStyles;

	/** MID for icon HISM — texture param swapped on mode change. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> IconMID;

	/** MID for hover mesh — color set on mode change. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> HoverMID;

	/** MID used by selected mining ghost-style instanced cubes. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> MiningVolumeMID;

	// Cell tracking: VoxelCoord → HISM instance index
	TMap<FIntVector, int32> BaseGridCells;

	/** True only when viewing a below-surface mine/dig layer. */
	bool bUseUndergroundWhiteGrid = false;
	TMap<FIntVector, int32> SelectedCells;
	TMap<FIntVector, int32> SelectedIcons;   // parallel icon instances for active preview/selection
	TMap<FIntVector, int32> DragCells;

	/** Surface mine selections use flat marker tiles only; underground selections use ghost cubes. */
	TMap<FIntVector, int32> SurfaceMiningMarkerCells;
	TMap<FIntVector, int32> MiningVolumeCells;

	/** Tracking map for underground floor block slab instances. */
	TMap<FIntVector, int32> UndergroundFloorCells;

	// Persistent confirmed designation state. Survives selection clears.
	TMap<FIntVector, FGridDesignationCellVisual> ConfirmedDesignationCells;
	TMap<FIntVector, float> ConfirmedCellTerrainZ;
	TMap<FIntVector, int32> ConfirmedTileInstances;

	// Separate icon HISM/MID per designation type so Build/Mine/Farm icons can coexist.
	UPROPERTY()
	TMap<EGridDesignationType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> IconHISMsByDesignation;

	UPROPERTY()
	TMap<EGridDesignationType, TObjectPtr<UMaterialInstanceDynamic>> IconMIDsByDesignation;

	FIntVector HoveredCellCoord = FIntVector(MAX_int32);

	// Static base grid state. The base grid is a targeting background, not selection state.
	// It should remain stable while the mouse moves and only rebuild on tool start / explicit refresh.
	FIntVector BaseGridAnchorCoord = FIntVector(MAX_int32);
	float BaseGridAnchorTerrainZ = 0.0f;
	bool bBaseGridBuilt = false;

	// ── Internal Helpers ────────────────────────────────────

	/** Build a world-space transform for a cell plane at the given voxel coord + Z. */
	FTransform MakeCellTransform(const FIntVector& VoxelCoord, float TerrainZ) const;

	/** Same but for the icon plane (smaller, higher Z). */
	FTransform MakeIconTransform(const FIntVector& VoxelCoord, float TerrainZ) const;

	/** Build a world-space transform for a thin mining designation cap. */
	FTransform MakeMiningVolumeTransform(const FIntVector& VoxelCoord) const;

	/** Current cap/outline color based on whether the active layer is underground. */
	FLinearColor GetMiningDesignationColor() const;

	/** Draw bright selected-cell outlines/X markers. This remains visible even if the cap material is subtle. */
	void RebuildMiningDesignationLines(const TArray<FIntVector>& VoxelCoords);

	/** Write RGBA into PerInstanceCustomData for a HISM instance. */
	void SetInstanceColor(UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex, const FLinearColor& Color);

	/** Trace straight down to find terrain surface Z at a voxel XY. */
	float SelfSampleTerrainZ(const FIntVector& VoxelCoord) const;

	/** Add a tile instance to the HISM and tracking map. Returns instance index. */
	int32 AddCellToHISM(const FIntVector& VoxelCoord, float TerrainZ, const FLinearColor& Color, TMap<FIntVector, int32>& TrackingMap);

	/** Remove a cell from the HISM and tracking map. Handles index fixup. */
	void RemoveCellFromHISM(const FIntVector& VoxelCoord, TMap<FIntVector, int32>& TrackingMap);

	/** Wipe a tracking map and its HISM instances. */
	void ClearHISMLayer(UHierarchicalInstancedStaticMeshComponent* HISM, TMap<FIntVector, int32>& TrackingMap);

	FGridOverlayStyle GetStyleForDesignation(EGridDesignationType DesignationType) const;
	EGridDesignationType ToolModeToDesignationType(EBuildToolMode Mode) const;
	void LoadDesignationStylesFromDataTable();
	void RebuildBaseGridAround(const FIntVector& CenterCoord, float TerrainZ);
	void RebuildConfirmedDesignationVisuals();
	void AddConfirmedVisualInstance(const FGridDesignationCellVisual& CellVisual);
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateIconHISM(EGridDesignationType DesignationType, const FGridOverlayStyle& Style);

	/** Full rebuild of tile + icon instances from current state. Called on mode change. */
	void RebuildAllInstances();

	/** Update icon MID texture when mode changes. */
	void ApplyIconTexture();

	/** Create/load runtime meshes if editor assets are null. */
	void EnsurePlaneMesh();
	void EnsureMiningVolumeMesh();

	/** Rebuild visible cube-strip grid lines from the current base-grid cells. */
	void RebuildBaseGridLines(const TMap<FIntVector, float>& CellsWithTerrainZ);
	void RebuildBaseGridLineBatch(const TMap<FIntVector, float>& CellsWithTerrainZ);

	/** Draw faint stacked depth-reference grids at -1..-N layers below the active underground layer. */
	void RebuildDepthReferenceGrids(const TMap<FIntVector, float>& CellsWithTerrainZ, float ActiveLayerFlatZ);
	FVector MakeGridLinePoint(const FIntVector& VoxelCoord, float TerrainZ, float ExtraZOffset) const;
};