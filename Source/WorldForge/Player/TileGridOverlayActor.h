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

	// ── Drag Preview (tentative cells during click-drag) ────

	void SetDragPreviewCells(const TMap<FIntVector, float>& CellsWithTerrainZ);
	void ClearDragPreview();

	/** Clear only the temporary neutral base-grid layer around the cursor.
	 *  Confirmed designation cells/icons are not affected. */
	void ClearBaseGrid();

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
	int32 VisibleGridRadius = 8;

	/** Z offset above terrain surface to prevent Z-fighting (UU). */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Layout")
	float ZOffset = 25.0f;

	/** Extra Z offset for icon plane above tile plane. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Layout")
	float IconZOffset = 6.0f;

	/** Collision channel to trace against for self-sampling terrain Z. */
	UPROPERTY(EditDefaultsOnly, Category = "Overlay|Trace")
	TEnumAsByte<ECollisionChannel> TerrainTraceChannel = ECC_WorldStatic;

private:
	// ── Components ──────────────────────────────────────────

	/** Instanced neutral base grid cells around the cursor. */
	UPROPERTY(VisibleAnywhere, Category = "Overlay")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> BaseGridHISM;

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

	// Cell tracking: VoxelCoord → HISM instance index
	TMap<FIntVector, int32> BaseGridCells;
	TMap<FIntVector, int32> SelectedCells;
	TMap<FIntVector, int32> SelectedIcons;   // parallel icon instances for active preview/selection
	TMap<FIntVector, int32> DragCells;

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

	// ── Internal Helpers ────────────────────────────────────

	/** Build a world-space transform for a cell plane at the given voxel coord + Z. */
	FTransform MakeCellTransform(const FIntVector& VoxelCoord, float TerrainZ) const;

	/** Same but for the icon plane (smaller, higher Z). */
	FTransform MakeIconTransform(const FIntVector& VoxelCoord, float TerrainZ) const;

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

	/** Create a runtime plane mesh if CellPlaneMesh is null. */
	void EnsurePlaneMesh();
};