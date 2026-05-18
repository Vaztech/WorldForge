// TileGridOverlayActor.cpp

#include "Player/TileGridOverlayActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"
#include "Voxel/VoxelWorldManager.h"
#include "Data/WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogGridOverlay, Log, All);

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────

ATileGridOverlayActor::ATileGridOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	BaseGridLineBatch = CreateDefaultSubobject<ULineBatchComponent>(TEXT("BaseGridLineBatch"));
	BaseGridLineBatch->SetupAttachment(RootComponent);
	BaseGridLineBatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BaseGridLineBatch->SetCastShadow(false);
	BaseGridLineBatch->SetGenerateOverlapEvents(false);

	DepthReferenceLineBatch = CreateDefaultSubobject<ULineBatchComponent>(TEXT("DepthReferenceLineBatch"));
	DepthReferenceLineBatch->SetupAttachment(RootComponent);
	DepthReferenceLineBatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DepthReferenceLineBatch->SetCastShadow(false);
	DepthReferenceLineBatch->SetGenerateOverlapEvents(false);

	// --- Underground floor block slabs ---
	UndergroundFloorHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("UndergroundFloorHISM"));
	UndergroundFloorHISM->SetupAttachment(RootComponent);
	UndergroundFloorHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UndergroundFloorHISM->SetCastShadow(false);
	UndergroundFloorHISM->NumCustomDataFloats = 5;
	UndergroundFloorHISM->SetGenerateOverlapEvents(false);
	UndergroundFloorHISM->bDisableCollision = true;

	MiningDesignationLineBatch = CreateDefaultSubobject<ULineBatchComponent>(TEXT("MiningDesignationLineBatch"));
	MiningDesignationLineBatch->SetupAttachment(RootComponent);
	MiningDesignationLineBatch->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MiningDesignationLineBatch->SetCastShadow(false);
	MiningDesignationLineBatch->SetGenerateOverlapEvents(false);

	auto ConfigureTileHISM = [this](UHierarchicalInstancedStaticMeshComponent* HISM)
		{
			if (!HISM)
			{
				return;
			}

			HISM->SetupAttachment(RootComponent);
			HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			HISM->SetCastShadow(false);
			HISM->NumCustomDataFloats = 5; // R, G, B, A, BorderAlpha
			HISM->SetGenerateOverlapEvents(false);
			HISM->bDisableCollision = true;
		};

	// --- Neutral base grid cells around hover ---
	BaseGridHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BaseGridHISM"));
	ConfigureTileHISM(BaseGridHISM);

	// --- Strong visible grid-line strips. This is what the player reads as the grid. ---
	BaseGridLineHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("BaseGridLineHISM"));
	ConfigureTileHISM(BaseGridLineHISM);

	// --- Tile HISM (active selected cells only) ---
	TileMeshHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TileMeshHISM"));
	ConfigureTileHISM(TileMeshHISM);

	// --- Drag-preview HISM (mouse-held preview only) ---
	DragPreviewMeshHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("DragPreviewMeshHISM"));
	ConfigureTileHISM(DragPreviewMeshHISM);

	// --- Persistent confirmed designation tile layer ---
	ConfirmedTileMeshHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("ConfirmedTileMeshHISM"));
	ConfigureTileHISM(ConfirmedTileMeshHISM);

	// --- Icon HISM (active preview/selection mode only) ---
	IconMeshHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("IconMeshHISM"));
	IconMeshHISM->SetupAttachment(RootComponent);
	IconMeshHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	IconMeshHISM->SetCastShadow(false);
	IconMeshHISM->NumCustomDataFloats = 4; // R, G, B, A (tint)
	IconMeshHISM->SetGenerateOverlapEvents(false);
	IconMeshHISM->bDisableCollision = true;

	// --- Single hover highlight ---
	HoverMeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HoverMesh"));
	HoverMeshComp->SetupAttachment(RootComponent);
	HoverMeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HoverMeshComp->SetCastShadow(false);
	HoverMeshComp->SetVisibility(false);

	// --- 3D mining volume preview cubes ---
	MiningVolumeHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("MiningVolumeHISM"));
	MiningVolumeHISM->SetupAttachment(RootComponent);
	MiningVolumeHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MiningVolumeHISM->SetCastShadow(false);
	MiningVolumeHISM->NumCustomDataFloats = 5; // R, G, B, A, BorderAlpha
	MiningVolumeHISM->SetGenerateOverlapEvents(false);
	MiningVolumeHISM->bDisableCollision = true;
}

// ─────────────────────────────────────────────────────────────
// BeginPlay / EndPlay
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::BeginPlay()
{
	Super::BeginPlay();

	// Keep the overlay close to the voxel surface now that rendering is verified.
	// Tune these values if terrain z-fighting appears; avoid high floating overlays.
	ZOffset = FMath::Max(ZOffset, 3.0f);
	IconZOffset = FMath::Max(IconZOffset, 5.0f);
	VisibleGridRadius = FMath::Max(VisibleGridRadius, 96);

	EnsurePlaneMesh();
	EnsureMiningVolumeMesh();

	if (!TileMaterialBase)
	{
		TileMaterialBase = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/WorldForge/Visual/UI/M_GridTile.M_GridTile"));

		UE_LOG(LogGridOverlay, Warning,
			TEXT("TileMaterialBase fallback load: %s"),
			TileMaterialBase ? TEXT("OK") : TEXT("FAILED"));
	}

	if (!IconMaterialBase)
	{
		IconMaterialBase = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/WorldForge/Visual/UI/M_GridIcon.M_GridIcon"));

		UE_LOG(LogGridOverlay, Warning,
			TEXT("IconMaterialBase fallback load: %s"),
			IconMaterialBase ? TEXT("OK") : TEXT("FAILED"));
	}

	if (!HoverMaterialBase)
	{
		HoverMaterialBase = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/WorldForge/Visual/UI/M_GridHover.M_GridHover"));

		UE_LOG(LogGridOverlay, Warning,
			TEXT("HoverMaterialBase fallback load: %s"),
			HoverMaterialBase ? TEXT("OK") : TEXT("FAILED"));
	}

	if (!UndergroundBlockMaterial)
	{
		UndergroundBlockMaterial = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Game/WorldForge/Materials/M_WF_UndergroundBlock.M_WF_UndergroundBlock"));

		UE_LOG(LogGridOverlay, Warning,
			TEXT("UndergroundBlockMaterial fallback load: %s"),
			UndergroundBlockMaterial ? TEXT("OK") : TEXT("FAILED"));
	}

	if (!DesignationStyleTable)
	{
		DesignationStyleTable = LoadObject<UDataTable>(
			nullptr,
			TEXT("/Game/WorldForge/Visual/UI/DT_GridDesignationStyles.DT_GridDesignationStyles"));
	}

	// Assign mesh to all components
	if (CellPlaneMesh)
	{
		if (BaseGridHISM) { BaseGridHISM->SetStaticMesh(CellPlaneMesh); }
		if (TileMeshHISM) { TileMeshHISM->SetStaticMesh(CellPlaneMesh); }
		if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetStaticMesh(CellPlaneMesh); }
		if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetStaticMesh(CellPlaneMesh); }
		if (IconMeshHISM) { IconMeshHISM->SetStaticMesh(CellPlaneMesh); }
		if (HoverMeshComp) { HoverMeshComp->SetStaticMesh(CellPlaneMesh); }
	}

	if (MiningVolumeHISM && MiningVolumeCubeMesh)
	{
		MiningVolumeHISM->SetStaticMesh(MiningVolumeCubeMesh);
	}

	// Underground floor block slabs use the cube mesh (scaled thin) with user-assigned material.
	if (UndergroundFloorHISM && MiningVolumeCubeMesh)
	{
		UndergroundFloorHISM->SetStaticMesh(MiningVolumeCubeMesh);

		// Apply material. Priority: UndergroundBlockMaterial > TileMaterialBase > none.
		UMaterialInterface* FloorMat = UndergroundBlockMaterial
			? UndergroundBlockMaterial.Get()
			: (TileMaterialBase ? TileMaterialBase.Get() : nullptr);

		if (FloorMat)
		{
			UndergroundFloorHISM->SetMaterial(0, FloorMat);
		}
	}

	if (BaseGridLineHISM && MiningVolumeCubeMesh)
	{
		BaseGridLineHISM->SetStaticMesh(MiningVolumeCubeMesh);
	}

	if (TileMaterialBase)
	{
		if (BaseGridHISM) { BaseGridHISM->SetMaterial(0, TileMaterialBase); }
		if (BaseGridLineHISM) { BaseGridLineHISM->SetMaterial(0, TileMaterialBase); }
		if (TileMeshHISM) { TileMeshHISM->SetMaterial(0, TileMaterialBase); }
		if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetMaterial(0, TileMaterialBase); }
		if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetMaterial(0, TileMaterialBase); }
	}

	if (MiningVolumeHISM)
	{
		UMaterialInterface* SourceMiningMaterial = MiningVolumeMaterialBase ? MiningVolumeMaterialBase.Get() : TileMaterialBase.Get();
		if (SourceMiningMaterial)
		{
			MiningVolumeMID = UMaterialInstanceDynamic::Create(SourceMiningMaterial, this);
			MiningVolumeHISM->SetMaterial(0, MiningVolumeMID ? MiningVolumeMID.Get() : SourceMiningMaterial);
		}
	}

	if (IconMaterialBase)
	{
		IconMID = UMaterialInstanceDynamic::Create(IconMaterialBase, this);
		IconMeshHISM->SetMaterial(0, IconMID);
	}

	if (HoverMaterialBase)
	{
		HoverMID = UMaterialInstanceDynamic::Create(HoverMaterialBase, this);
		HoverMeshComp->SetMaterial(0, HoverMID);
	}

	LoadDesignationStylesFromDataTable();

	// Start hidden
	SetActorHiddenInGame(true);
	bOverlayVisible = false;

	UE_LOG(LogGridOverlay, Log,
		TEXT("TileGridOverlayActor ready. CellSize=%.0f ZOffset=%.1f VisibleGridRadius=%d"),
		CellSizeUU,
		ZOffset,
		VisibleGridRadius);
}

void ATileGridOverlayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	BaseGridCells.Empty();
	if (BaseGridLineHISM) { BaseGridLineHISM->ClearInstances(); }
	SelectedCells.Empty();
	SelectedIcons.Empty();
	DragCells.Empty();
	ConfirmedDesignationCells.Empty();
	ConfirmedCellTerrainZ.Empty();
	ConfirmedTileInstances.Empty();
	SurfaceMiningMarkerCells.Empty();
	MiningVolumeCells.Empty();
	if (MiningDesignationLineBatch) { MiningDesignationLineBatch->Flush(); }
	if (DepthReferenceLineBatch) { DepthReferenceLineBatch->Flush(); }
	UndergroundFloorCells.Empty();
	IconHISMsByDesignation.Empty();
	IconMIDsByDesignation.Empty();
	Super::EndPlay(EndPlayReason);
}

// ─────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::RegisterModeStyle(EBuildToolMode Mode, const FGridOverlayStyle& Style)
{
	RegisteredStyles.Add(Mode, Style);
	UE_LOG(LogGridOverlay, Verbose, TEXT("Registered style for mode %d"), static_cast<int32>(Mode));
}

void ATileGridOverlayActor::SetActiveMode(EBuildToolMode Mode)
{
	if (CurrentMode == Mode) return;

	CurrentMode = Mode;

	// DataTable styles are authoritative if present. This call is cheap after load
	// and prevents default hardcoded styles from overwriting editor data.
	LoadDesignationStylesFromDataTable();

	if (const FGridOverlayStyle* Found = RegisteredStyles.Find(Mode))
	{
		ActiveStyle = *Found;
	}
	else
	{
		UE_LOG(LogGridOverlay, Warning, TEXT("No style registered for mode %d — using defaults."), static_cast<int32>(Mode));
		ActiveStyle = FGridOverlayStyle();
	}

	// Update hover material color
	if (HoverMID)
	{
		HoverMID->SetVectorParameterValue(FName("TintColor"), ActiveStyle.HoverColor);
	}

	// Update icon texture
	ApplyIconTexture();

	// Recolor all existing instances
	RebuildAllInstances();

	UE_LOG(LogGridOverlay, Log, TEXT("Designation changed to %d. Selected=%d DragPreview=%d BaseGrid=%d"),
		static_cast<int32>(ToolModeToDesignationType(Mode)),
		SelectedCells.Num(),
		DragCells.Num(),
		BaseGridCells.Num());
}

void ATileGridOverlayActor::SetCellSizeUU(float InSize)
{
	if (FMath::IsNearlyEqual(CellSizeUU, InSize)) return;
	CellSizeUU = FMath::Max(InSize, 1.0f);
	RebuildAllInstances();
}

// ─────────────────────────────────────────────────────────────
// Hover
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::SetHoveredCell(const FIntVector& VoxelCoord, float TerrainZ)
{
	HoveredCellCoord = VoxelCoord;
	bHoverActive = true;

	// Do not rebuild the base grid here. The base grid is static per tool session
	// and should not swim/flicker while the mouse moves.
	const FTransform CellXform = MakeCellTransform(VoxelCoord, TerrainZ);
	HoverMeshComp->SetWorldTransform(CellXform);
	HoverMeshComp->SetVisibility(bOverlayVisible);
}

void ATileGridOverlayActor::ClearHover()
{
	bHoverActive = false;
	HoveredCellCoord = FIntVector(MAX_int32);
	HoverMeshComp->SetVisibility(false);
}

// ─────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::AddSelectedCell(const FIntVector& VoxelCoord, float TerrainZ)
{
	if (SelectedCells.Contains(VoxelCoord)) return;

	const bool bMiningSelection = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);

	if (bMiningSelection)
	{
		// Mine mode uses the dedicated cap/outline preview in SetMiningVolumeCells().
		// Do not add the normal flat selected-cell tile here; on smooth SDF terrain it reads
		// as a grey/black clipped square and makes the selection look offset.
		SelectedCells.Add(VoxelCoord, INDEX_NONE);
	}
	else
	{
		AddCellToHISM(VoxelCoord, TerrainZ, ActiveStyle.TileColor, SelectedCells);

		if (ActiveStyle.IconTexture)
		{
			const FTransform IconXform = MakeIconTransform(VoxelCoord, TerrainZ);
			const int32 IconIdx = IconMeshHISM->AddInstance(IconXform, /*bWorldSpace=*/true);
			SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.IconTint);
			SelectedIcons.Add(VoxelCoord, IconIdx);
		}
	}

	UE_LOG(LogGridOverlay, Verbose, TEXT("Selected cell (%d,%d,%d) — total=%d"),
		VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z, SelectedCells.Num());
}

void ATileGridOverlayActor::RemoveSelectedCell(const FIntVector& VoxelCoord)
{
	RemoveCellFromHISM(VoxelCoord, SelectedCells);

	// Remove paired icon
	if (SelectedIcons.Contains(VoxelCoord))
	{
		RemoveCellFromHISM(VoxelCoord, SelectedIcons);
	}
}

void ATileGridOverlayActor::ToggleSelectedCell(const FIntVector& VoxelCoord, float TerrainZ)
{
	if (SelectedCells.Contains(VoxelCoord))
	{
		RemoveSelectedCell(VoxelCoord);
	}
	else
	{
		AddSelectedCell(VoxelCoord, TerrainZ);
	}
}

void ATileGridOverlayActor::SetSelectedCells(const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	// Batch replace: clear existing, add all new.
	ClearHISMLayer(TileMeshHISM, SelectedCells);
	ClearHISMLayer(TileMeshHISM, SurfaceMiningMarkerCells);
	ClearHISMLayer(IconMeshHISM, SelectedIcons);

	const bool bMiningSelection = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);

	for (const auto& Pair : CellsWithTerrainZ)
	{
		if (bMiningSelection)
		{
			// Store logical selection only. The real mine visual is the dedicated
			// MiningVolumeHISM cap plus the material-independent yellow outline/X lines.
			SelectedCells.Add(Pair.Key, INDEX_NONE);
			continue;
		}

		AddCellToHISM(Pair.Key, Pair.Value, ActiveStyle.TileColor, SelectedCells);

		if (ActiveStyle.IconTexture)
		{
			const FTransform IconXform = MakeIconTransform(Pair.Key, Pair.Value);
			const int32 IconIdx = IconMeshHISM->AddInstance(IconXform, /*bWorldSpace=*/true);
			SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.IconTint);
			SelectedIcons.Add(Pair.Key, IconIdx);
		}
	}

	UE_LOG(LogGridOverlay, Log, TEXT("Batch set %d selected cells."), SelectedCells.Num());
}

void ATileGridOverlayActor::ClearSelection()
{
	ClearHISMLayer(TileMeshHISM, SelectedCells);
	ClearHISMLayer(TileMeshHISM, SurfaceMiningMarkerCells);
	ClearHISMLayer(IconMeshHISM, SelectedIcons);
	UE_LOG(LogGridOverlay, Log, TEXT("Selection cleared."));
}

void ATileGridOverlayActor::SetMiningVolumeCells(const TArray<FIntVector>& VoxelCoords)
{
	if (!MiningVolumeHISM)
	{
		return;
	}

	// Mine selection has two different visual modes:
	//   Surface:    flat marker tiles only, so the player marks terrain from above.
	//   Underground: M_WF_MiningGhost cube instances, so the exact voxel for removal is clear.
	ClearHISMLayer(MiningVolumeHISM, MiningVolumeCells);
	ClearHISMLayer(TileMeshHISM, SurfaceMiningMarkerCells);

	if (MiningDesignationLineBatch)
	{
		MiningDesignationLineBatch->Flush();
		MiningDesignationLineBatch->SetVisibility(false, true);
	}

	const FLinearColor EffectiveColor = GetMiningDesignationColor();

	if (!bUseUndergroundWhiteGrid)
	{
		// Surface mine mode: no voxel cube, no yellow X-line clutter. Just a flat tile marker
		// at the active terrain layer. Confirm/commit can still create deeper designations,
		// but the surface interaction should read as a terrain marker.
		for (const FIntVector& VoxelCoord : VoxelCoords)
		{
			if (SurfaceMiningMarkerCells.Contains(VoxelCoord))
			{
				continue;
			}

			// Surface marker should follow the actual terrain surface one-to-one.
			const float MarkerWorldZ = SelfSampleTerrainZ(VoxelCoord);
			AddCellToHISM(VoxelCoord, MarkerWorldZ, EffectiveColor, SurfaceMiningMarkerCells);
		}

		if (TileMeshHISM)
		{
			TileMeshHISM->SetVisibility(bOverlayVisible && SurfaceMiningMarkerCells.Num() > 0, true);
			TileMeshHISM->SetHiddenInGame(!(bOverlayVisible && SurfaceMiningMarkerCells.Num() > 0));
			TileMeshHISM->SetCullDistances(0, 0);
			TileMeshHISM->MarkRenderStateDirty();
		}

		MiningVolumeHISM->SetVisibility(false, true);
		MiningVolumeHISM->SetHiddenInGame(true);
		MiningVolumeHISM->MarkRenderStateDirty();

		UE_LOG(LogGridOverlay, Log, TEXT("Mining surface marker preview set. Tiles=%d"), SurfaceMiningMarkerCells.Num());
		return;
	}

	// Underground mine mode: use the same ghost material/mesh style as BP_MiningGhostActor.
	if (MiningVolumeMID)
	{
		MiningVolumeMID->SetVectorParameterValue(TEXT("GhostTint"), EffectiveColor);
		MiningVolumeMID->SetScalarParameterValue(TEXT("GhostOpacity"), EffectiveColor.A);
	}

	for (const FIntVector& VoxelCoord : VoxelCoords)
	{
		if (MiningVolumeCells.Contains(VoxelCoord))
		{
			continue;
		}

		const int32 Idx = MiningVolumeHISM->AddInstance(MakeMiningVolumeTransform(VoxelCoord), true);
		SetInstanceColor(MiningVolumeHISM, Idx, EffectiveColor);
		MiningVolumeHISM->SetCustomDataValue(Idx, 4, EffectiveColor.A, true);
		MiningVolumeCells.Add(VoxelCoord, Idx);
	}

	MiningVolumeHISM->SetVisibility(bOverlayVisible && MiningVolumeCells.Num() > 0, true);
	MiningVolumeHISM->SetHiddenInGame(!(bOverlayVisible && MiningVolumeCells.Num() > 0));
	MiningVolumeHISM->SetCullDistances(0, 0);
	MiningVolumeHISM->MarkRenderStateDirty();

	// No additional line/X overlay underground: the ghost cube itself is the voxel removal marker.
	UE_LOG(LogGridOverlay, Log, TEXT("Mining underground ghost preview set. Voxels=%d"), MiningVolumeCells.Num());
}

void ATileGridOverlayActor::ClearMiningVolumeCells()
{
	ClearHISMLayer(MiningVolumeHISM, MiningVolumeCells);
	ClearHISMLayer(TileMeshHISM, SurfaceMiningMarkerCells);
	if (MiningVolumeHISM)
	{
		MiningVolumeHISM->SetVisibility(false, true);
	}
	if (TileMeshHISM)
	{
		TileMeshHISM->SetVisibility(false, true);
	}
	if (MiningDesignationLineBatch)
	{
		MiningDesignationLineBatch->Flush();
		MiningDesignationLineBatch->SetVisibility(false, true);
	}
}

void ATileGridOverlayActor::GetSelectedCoords(TArray<FIntVector>& OutCoords) const
{
	SelectedCells.GetKeys(OutCoords);
}

// ─────────────────────────────────────────────────────────────
// Drag Preview
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::SetDragPreviewCells(const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	// Remove old drag instances that are NOT in the new set
	TArray<FIntVector> ToRemove;
	for (const auto& Pair : DragCells)
	{
		if (!CellsWithTerrainZ.Contains(Pair.Key))
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FIntVector& Coord : ToRemove)
	{
		RemoveCellFromHISM(Coord, DragCells);
	}

	// Add new drag cells not already present
	for (const auto& Pair : CellsWithTerrainZ)
	{
		if (!DragCells.Contains(Pair.Key) && !SelectedCells.Contains(Pair.Key))
		{
			AddCellToHISM(Pair.Key, Pair.Value, ActiveStyle.DragPreviewColor, DragCells);
		}
	}
}

void ATileGridOverlayActor::ClearDragPreview()
{
	ClearHISMLayer(DragPreviewMeshHISM, DragCells);
}

void ATileGridOverlayActor::ClearBaseGrid()
{
	ClearHISMLayer(BaseGridHISM, BaseGridCells);
	if (BaseGridLineHISM)
	{
		BaseGridLineHISM->ClearInstances();
		BaseGridLineHISM->SetVisibility(false, true);
		BaseGridLineHISM->MarkRenderStateDirty();
	}

	if (BaseGridLineBatch)
	{
		BaseGridLineBatch->Flush();
		BaseGridLineBatch->SetVisibility(false, true);
	}

	if (DepthReferenceLineBatch)
	{
		DepthReferenceLineBatch->Flush();
		DepthReferenceLineBatch->SetVisibility(false, true);
	}

	ClearUndergroundFloorBlocks();

	BaseGridAnchorCoord = FIntVector(MAX_int32);
	BaseGridAnchorTerrainZ = 0.0f;
	bBaseGridBuilt = false;

	if (BaseGridHISM)
	{
		BaseGridHISM->MarkRenderStateDirty();
	}

	UE_LOG(LogGridOverlay, Verbose, TEXT("Base grid cleared."));
}


void ATileGridOverlayActor::SetUseUndergroundWhiteGrid(bool bEnabled)
{
	bUseUndergroundWhiteGrid = bEnabled;
}

void ATileGridOverlayActor::ConfigureMiningGhostVisual(
	UStaticMesh* InCubeMesh,
	UMaterialInterface* InGhostMaterial,
	const FLinearColor& InSurfaceTint,
	const FLinearColor& InUndergroundTint,
	float InSurfaceOpacity,
	float InUndergroundOpacity,
	float InCubeScale)
{
	if (InCubeMesh)
	{
		MiningVolumeCubeMesh = InCubeMesh;
		if (MiningVolumeHISM)
		{
			MiningVolumeHISM->SetStaticMesh(MiningVolumeCubeMesh);
		}
	}

	if (InGhostMaterial)
	{
		MiningVolumeMaterialBase = InGhostMaterial;
		if (MiningVolumeHISM)
		{
			MiningVolumeMID = UMaterialInstanceDynamic::Create(InGhostMaterial, this);
			MiningVolumeHISM->SetMaterial(0, MiningVolumeMID ? MiningVolumeMID.Get() : InGhostMaterial);
		}
	}

	SurfaceMiningCapColor = FLinearColor(InSurfaceTint.R, InSurfaceTint.G, InSurfaceTint.B, FMath::Clamp(InSurfaceOpacity, 0.0f, 1.0f));
	UndergroundMiningCapColor = FLinearColor(InUndergroundTint.R, InUndergroundTint.G, InUndergroundTint.B, FMath::Clamp(InUndergroundOpacity, 0.0f, 1.0f));
	MiningVolumeScale = FMath::Clamp(InCubeScale, 0.1f, 1.0f);

	UE_LOG(LogGridOverlay, Log,
		TEXT("Configured mining ghost visual. Mesh=%s Material=%s SurfaceOpacity=%.2f UndergroundOpacity=%.2f Scale=%.2f"),
		MiningVolumeCubeMesh ? *MiningVolumeCubeMesh->GetName() : TEXT("None"),
		MiningVolumeMaterialBase ? *MiningVolumeMaterialBase->GetName() : TEXT("None"),
		SurfaceMiningCapColor.A,
		UndergroundMiningCapColor.A,
		MiningVolumeScale);
}

void ATileGridOverlayActor::SetBaseGridCells(const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	// Base grid = LineBatch only (no HISM flat tiles for the grid).
	// Underground floor blocks are handled separately by SetUndergroundFloorBlocks.
	if (BaseGridHISM)
	{
		ClearHISMLayer(BaseGridHISM, BaseGridCells);
		BaseGridHISM->ClearInstances();
		BaseGridHISM->SetVisibility(false, true);
		BaseGridHISM->SetHiddenInGame(true);
		BaseGridHISM->MarkRenderStateDirty();
	}

	if (BaseGridLineHISM)
	{
		BaseGridLineHISM->ClearInstances();
		BaseGridLineHISM->SetVisibility(false, true);
		BaseGridLineHISM->SetHiddenInGame(true);
		BaseGridLineHISM->MarkRenderStateDirty();
	}

	BaseGridCells.Reset();
	BaseGridCells.Reserve(CellsWithTerrainZ.Num());

	for (const TPair<FIntVector, float>& Pair : CellsWithTerrainZ)
	{
		BaseGridCells.Add(Pair.Key, INDEX_NONE);
	}

	if (CellsWithTerrainZ.Num() > 0)
	{
		BaseGridAnchorCoord = CellsWithTerrainZ.CreateConstIterator()->Key;
		BaseGridAnchorTerrainZ = CellsWithTerrainZ.CreateConstIterator()->Value;
		bBaseGridBuilt = true;
	}
	else
	{
		BaseGridAnchorCoord = FIntVector(MAX_int32);
		BaseGridAnchorTerrainZ = 0.0f;
		bBaseGridBuilt = false;
	}

	// Primary grid lines (white/styled, edge-aligned, full radius).
	RebuildBaseGridLineBatch(CellsWithTerrainZ);

	// Depth reference: faint amber stacked grids below the active underground layer.
	if (false && bUseUndergroundWhiteGrid && CellsWithTerrainZ.Num() > 0 && UndergroundDepthReferenceLayers > 0)
	{
		const float ActiveZ = CellsWithTerrainZ.CreateConstIterator()->Value;
		RebuildDepthReferenceGrids(CellsWithTerrainZ, ActiveZ);
	}
	else if (DepthReferenceLineBatch)
	{
		DepthReferenceLineBatch->Flush();
		DepthReferenceLineBatch->SetVisibility(false, true);
	}

	SetActorHiddenInGame(false);

	UE_LOG(LogGridOverlay, Log,
		TEXT("Static base line grid set. Cells=%d UndergroundWhite=%s Renderer=LineBatchOnly EdgeAligned=true"),
		BaseGridCells.Num(),
		bUseUndergroundWhiteGrid ? TEXT("true") : TEXT("false"));
}

void ATileGridOverlayActor::BuildStaticBaseGridAround(const FIntVector& CenterCoord, float TerrainZ)
{
	BaseGridAnchorCoord = CenterCoord;
	BaseGridAnchorTerrainZ = TerrainZ;
	bBaseGridBuilt = true;
	RebuildBaseGridAround(CenterCoord, TerrainZ);

	UE_LOG(LogGridOverlay, Log,
		TEXT("Static base grid built. Anchor=(%d,%d,%d) Radius=%d Cells=%d TerrainZ=%.1f"),
		CenterCoord.X,
		CenterCoord.Y,
		CenterCoord.Z,
		VisibleGridRadius,
		BaseGridCells.Num(),
		TerrainZ);
}

// ─────────────────────────────────────────────────────────────
// Persistent confirmed designations
// ─────────────────────────────────────────────────────────────

EGridDesignationType ATileGridOverlayActor::ToolModeToDesignationType(EBuildToolMode Mode) const
{
	switch (Mode)
	{
	case EBuildToolMode::Place:
		return EGridDesignationType::Build;
	case EBuildToolMode::Mine:
		return EGridDesignationType::Mine;
	case EBuildToolMode::Farm:
		return EGridDesignationType::Farm;
	case EBuildToolMode::Dig:
		return EGridDesignationType::Mine;
	case EBuildToolMode::Remove:
		return EGridDesignationType::Erase;
	default:
		return EGridDesignationType::None;
	}
}

FGridOverlayStyle ATileGridOverlayActor::GetStyleForDesignation(EGridDesignationType DesignationType) const
{
	EBuildToolMode Mode = EBuildToolMode::None;

	switch (DesignationType)
	{
	case EGridDesignationType::Build:
		Mode = EBuildToolMode::Place;
		break;
	case EGridDesignationType::Mine:
		Mode = EBuildToolMode::Mine;
		break;
	case EGridDesignationType::Farm:
		Mode = EBuildToolMode::Farm;
		break;
	case EGridDesignationType::Erase:
		Mode = EBuildToolMode::Remove;
		break;
	default:
		break;
	}

	if (const FGridOverlayStyle* FoundStyle = RegisteredStyles.Find(Mode))
	{
		return *FoundStyle;
	}

	return ActiveStyle;
}

void ATileGridOverlayActor::AddConfirmedDesignationCell(
	const FIntVector& VoxelCoord,
	float TerrainZ,
	EGridDesignationType DesignationType)
{
	if (DesignationType == EGridDesignationType::None)
	{
		return;
	}

	FGridDesignationCellVisual CellVisual;
	CellVisual.VoxelCoord = VoxelCoord;
	CellVisual.TerrainZ = TerrainZ;
	CellVisual.DesignationType = DesignationType;
	CellVisual.VisualState = EGridDesignationVisualState::Confirmed;

	ConfirmedDesignationCells.Add(VoxelCoord, CellVisual);
	ConfirmedCellTerrainZ.Add(VoxelCoord, TerrainZ);

	AddConfirmedVisualInstance(CellVisual);
	SetActorHiddenInGame(false);
	bOverlayVisible = true;

	UE_LOG(LogGridOverlay, Log,
		TEXT("Confirmed designation cell (%d,%d,%d) Type=%d ConfirmedTotal=%d"),
		VoxelCoord.X,
		VoxelCoord.Y,
		VoxelCoord.Z,
		static_cast<int32>(DesignationType),
		ConfirmedDesignationCells.Num());
}

void ATileGridOverlayActor::SetConfirmedDesignationCells(
	EGridDesignationType DesignationType,
	const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	if (DesignationType == EGridDesignationType::None)
	{
		return;
	}

	for (const TPair<FIntVector, float>& Pair : CellsWithTerrainZ)
	{
		AddConfirmedDesignationCell(Pair.Key, Pair.Value, DesignationType);
	}

	UE_LOG(LogGridOverlay, Log,
		TEXT("Batch confirmed %d cells for designation %d"),
		CellsWithTerrainZ.Num(),
		static_cast<int32>(DesignationType));
}

void ATileGridOverlayActor::CommitSelectedCellsAsConfirmed(EGridDesignationType DesignationType)
{
	if (DesignationType == EGridDesignationType::None)
	{
		DesignationType = ToolModeToDesignationType(CurrentMode);
	}

	if (DesignationType == EGridDesignationType::None)
	{
		return;
	}

	TMap<FIntVector, float> Snapshot;

	for (const TPair<FIntVector, int32>& Pair : SelectedCells)
	{
		// Mine mode stores INDEX_NONE (no flat tile instance). Use voxel top face Z.
		if (Pair.Value == INDEX_NONE)
		{
			Snapshot.Add(Pair.Key, static_cast<float>(Pair.Key.Z + 1) * CellSizeUU);
			continue;
		}
		FTransform Xform;
		if (TileMeshHISM && TileMeshHISM->GetInstanceTransform(Pair.Value, Xform, true))
		{
			Snapshot.Add(Pair.Key, Xform.GetLocation().Z - ZOffset);
		}
		else
		{
			Snapshot.Add(Pair.Key, SelfSampleTerrainZ(Pair.Key));
		}
	}

	SetConfirmedDesignationCells(DesignationType, Snapshot);
}

void ATileGridOverlayActor::ClearConfirmedDesignationCells(EGridDesignationType DesignationType)
{
	if (DesignationType == EGridDesignationType::None)
	{
		return;
	}

	TArray<FIntVector> ToRemove;

	for (const TPair<FIntVector, FGridDesignationCellVisual>& Pair : ConfirmedDesignationCells)
	{
		if (Pair.Value.DesignationType == DesignationType)
		{
			ToRemove.Add(Pair.Key);
		}
	}

	for (const FIntVector& Coord : ToRemove)
	{
		ConfirmedDesignationCells.Remove(Coord);
		ConfirmedCellTerrainZ.Remove(Coord);
	}

	RebuildAllInstances();
}

void ATileGridOverlayActor::ClearAllConfirmedDesignationCells()
{
	ConfirmedDesignationCells.Empty();
	ConfirmedCellTerrainZ.Empty();
	RebuildAllInstances();
}

void ATileGridOverlayActor::AddConfirmedVisualInstance(const FGridDesignationCellVisual& CellVisual)
{
	if (!ConfirmedTileMeshHISM || !CellPlaneMesh)
	{
		return;
	}

	const FGridOverlayStyle Style = GetStyleForDesignation(CellVisual.DesignationType);
	const FTransform CellXform = MakeCellTransform(CellVisual.VoxelCoord, CellVisual.TerrainZ);
	const int32 TileIdx = ConfirmedTileMeshHISM->AddInstance(CellXform, true);
	SetInstanceColor(ConfirmedTileMeshHISM, TileIdx, Style.TileColor);
	ConfirmedTileMeshHISM->SetCustomDataValue(TileIdx, 4, Style.BorderColor.A, true);
	ConfirmedTileInstances.Add(CellVisual.VoxelCoord, TileIdx);

	if (CellVisual.DesignationType == EGridDesignationType::Farm)
	{
		UE_LOG(LogGridOverlay, Log,
			TEXT("[GridOverlay] Farm confirmed marker added. Cell=(%d,%d,%d) TileColor.A=%.2f HasIcon=%s"),
			CellVisual.VoxelCoord.X, CellVisual.VoxelCoord.Y, CellVisual.VoxelCoord.Z,
			Style.TileColor.A,
			(Style.IconTexture != nullptr) ? TEXT("yes") : TEXT("no"));
	}

	if (Style.IconTexture && IconMaterialBase)
	{
		if (UHierarchicalInstancedStaticMeshComponent* IconHISM = GetOrCreateIconHISM(CellVisual.DesignationType, Style))
		{
			const float WorldX = (static_cast<float>(CellVisual.VoxelCoord.X) + 0.5f) * CellSizeUU;
			const float WorldY = (static_cast<float>(CellVisual.VoxelCoord.Y) + 0.5f) * CellSizeUU;
			const float WorldZ = CellVisual.TerrainZ + ZOffset + IconZOffset;
			const float IconWorldSize = CellSizeUU * FMath::Clamp(Style.IconScale, 0.05f, 1.0f);
			const float MeshScale = IconWorldSize / 100.0f;

			const FTransform IconXform(
				FRotator::ZeroRotator,
				FVector(WorldX, WorldY, WorldZ),
				FVector(MeshScale, MeshScale, 1.0f));

			const int32 IconIdx = IconHISM->AddInstance(IconXform, true);
			SetInstanceColor(IconHISM, IconIdx, Style.IconTint);
		}
	}
}

UHierarchicalInstancedStaticMeshComponent* ATileGridOverlayActor::GetOrCreateIconHISM(
	EGridDesignationType DesignationType,
	const FGridOverlayStyle& Style)
{
	if (DesignationType == EGridDesignationType::None || !IconMaterialBase)
	{
		return nullptr;
	}

	if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = IconHISMsByDesignation.Find(DesignationType))
	{
		return Found->Get();
	}

	const FName ComponentName(*FString::Printf(TEXT("IconHISM_%d"), static_cast<int32>(DesignationType)));
	UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, ComponentName);
	if (!NewHISM)
	{
		return nullptr;
	}

	NewHISM->SetupAttachment(RootComponent);
	NewHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewHISM->SetCastShadow(false);
	NewHISM->NumCustomDataFloats = 4;
	NewHISM->SetGenerateOverlapEvents(false);
	NewHISM->bDisableCollision = true;
	if (CellPlaneMesh)
	{
		NewHISM->SetStaticMesh(CellPlaneMesh);
	}
	NewHISM->RegisterComponent();

	UMaterialInstanceDynamic* NewMID = UMaterialInstanceDynamic::Create(IconMaterialBase, this);
	if (NewMID)
	{
		NewMID->SetTextureParameterValue(FName("IconTex"), Style.IconTexture);
		NewHISM->SetMaterial(0, NewMID);
		IconMIDsByDesignation.Add(DesignationType, NewMID);
	}

	IconHISMsByDesignation.Add(DesignationType, NewHISM);
	return NewHISM;
}

// ─────────────────────────────────────────────────────────────
// Visibility
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::ShowOverlay()
{
	bOverlayVisible = true;
	SetActorHiddenInGame(false);

	if (BaseGridHISM) { BaseGridHISM->SetVisibility(false, true); BaseGridHISM->SetHiddenInGame(true); }
	if (BaseGridLineHISM) { BaseGridLineHISM->SetVisibility(false, true); BaseGridLineHISM->SetHiddenInGame(true); }
	if (BaseGridLineBatch) { BaseGridLineBatch->SetVisibility(bOverlayVisible && BaseGridCells.Num() > 0, true); }
	if (DepthReferenceLineBatch) { DepthReferenceLineBatch->SetVisibility(bOverlayVisible && bUseUndergroundWhiteGrid && BaseGridCells.Num() > 0, true); }
	if (UndergroundFloorHISM) { UndergroundFloorHISM->SetVisibility(bOverlayVisible && UndergroundFloorCells.Num() > 0, true); }
	if (TileMeshHISM) { TileMeshHISM->SetVisibility(true, true); }
	if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetVisibility(true, true); }
	if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetVisibility(true, true); }
	if (IconMeshHISM) { IconMeshHISM->SetVisibility(true, true); }
	if (MiningVolumeHISM) { MiningVolumeHISM->SetVisibility(MiningVolumeCells.Num() > 0, true); }
	if (MiningDesignationLineBatch) { MiningDesignationLineBatch->SetVisibility(MiningVolumeCells.Num() > 0, true); }

	for (TPair<EGridDesignationType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : IconHISMsByDesignation)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(true, true);
		}
	}

	if (HoverMeshComp)
	{
		HoverMeshComp->SetVisibility(bHoverActive);
	}
}

void ATileGridOverlayActor::HideOverlay()
{
	bOverlayVisible = false;

	// HideOverlay is a full transient overlay shutdown. It clears the temporary
	// targeting layers so a giant neutral base grid cannot remain after confirm,
	// cancel, or tool exit. Persistent confirmed designations are controlled by
	// ShowOverlay()/EndSelectionTool and are not cleared here.
	ClearHover();
	ClearDragPreview();
	ClearSelection();
	ClearMiningVolumeCells();
	ClearBaseGrid();

	SetActorHiddenInGame(true);

	if (BaseGridHISM) { BaseGridHISM->SetVisibility(false, true); }
	if (BaseGridLineHISM) { BaseGridLineHISM->SetVisibility(false, true); }
	if (BaseGridLineBatch) { BaseGridLineBatch->Flush(); BaseGridLineBatch->SetVisibility(false, true); }
	if (DepthReferenceLineBatch) { DepthReferenceLineBatch->Flush(); DepthReferenceLineBatch->SetVisibility(false, true); }
	if (UndergroundFloorHISM) { UndergroundFloorHISM->ClearInstances(); UndergroundFloorHISM->SetVisibility(false, true); }
	if (TileMeshHISM) { TileMeshHISM->SetVisibility(false, true); }
	if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetVisibility(false, true); }
	if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetVisibility(false, true); }
	if (IconMeshHISM) { IconMeshHISM->SetVisibility(false, true); }
	if (MiningVolumeHISM) { MiningVolumeHISM->SetVisibility(false, true); }

	for (TPair<EGridDesignationType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : IconHISMsByDesignation)
	{
		if (Pair.Value)
		{
			Pair.Value->SetVisibility(false, true);
		}
	}

	if (HoverMeshComp)
	{
		HoverMeshComp->SetVisibility(false);
	}
}


void ATileGridOverlayActor::RebuildBaseGridLines(const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	if (!BaseGridLineHISM || !MiningVolumeCubeMesh)
	{
		return;
	}

	BaseGridLineHISM->ClearInstances();

	const bool bMiningLayer = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);
	const bool bUndergroundWhite = bMiningLayer && bUseUndergroundWhiteGrid;

	FLinearColor LineColor = bUndergroundWhite
		? FLinearColor(0.025f, 0.035f, 0.045f, 0.78f)
		: ActiveStyle.BorderColor;

	LineColor.A = bUndergroundWhite
		? 0.78f
		: FMath::Clamp(LineColor.A, bMiningLayer ? 0.85f : 0.55f, 1.0f);

	const float ThicknessUU = bUndergroundWhite ? 3.0f : 3.0f;
	const float HeightUU = bUndergroundWhite ? 2.0f : 2.0f;
	const float LineZOffset = bUndergroundWhite ? 8.0f : 5.0f;

	const float LengthScale = CellSizeUU / 100.0f;
	const float ThicknessScale = ThicknessUU / 100.0f;
	const float HeightScale = HeightUU / 100.0f;

	auto AddLineInstance = [&](const FVector& Location, const FVector& Scale)
		{
			const int32 Idx = BaseGridLineHISM->AddInstance(FTransform(FRotator::ZeroRotator, Location, Scale), true);
			SetInstanceColor(BaseGridLineHISM, Idx, LineColor);
			BaseGridLineHISM->SetCustomDataValue(Idx, 4, LineColor.A, false);
		};

	for (const TPair<FIntVector, float>& Pair : CellsWithTerrainZ)
	{
		const FIntVector& VoxelCoord = Pair.Key;
		const float Z = Pair.Value + LineZOffset;

		const float MinX = static_cast<float>(VoxelCoord.X) * CellSizeUU;
		const float MaxX = static_cast<float>(VoxelCoord.X + 1) * CellSizeUU;
		const float MinY = static_cast<float>(VoxelCoord.Y) * CellSizeUU;
		const float MaxY = static_cast<float>(VoxelCoord.Y + 1) * CellSizeUU;
		const float CenterX = (MinX + MaxX) * 0.5f;
		const float CenterY = (MinY + MaxY) * 0.5f;

		// Four border strips per cell. Duplicates are intentional: they make the grid readable
		// over bright terrain and over the black underground cutaway void.
		AddLineInstance(FVector(CenterX, MinY, Z), FVector(LengthScale, ThicknessScale, HeightScale));
		AddLineInstance(FVector(CenterX, MaxY, Z), FVector(LengthScale, ThicknessScale, HeightScale));
		AddLineInstance(FVector(MinX, CenterY, Z), FVector(ThicknessScale, LengthScale, HeightScale));
		AddLineInstance(FVector(MaxX, CenterY, Z), FVector(ThicknessScale, LengthScale, HeightScale));
	}

	BaseGridLineHISM->SetHiddenInGame(false);
	BaseGridLineHISM->SetVisibility(bOverlayVisible && CellsWithTerrainZ.Num() > 0, true);
	BaseGridLineHISM->SetCullDistances(0, 0);
	BaseGridLineHISM->MarkRenderStateDirty();
}


namespace
{
	static FLinearColor WF_TerrainLayerColor(int32 MaterialLayer)
	{
		// Matches WorldForgeTerrainGenerator.h material indices.
		switch (MaterialLayer)
		{
		case 0: return FLinearColor(0.80f, 0.72f, 0.45f, 1.0f); // Sand
		case 1: return FLinearColor(0.22f, 0.42f, 0.16f, 1.0f); // Grass
		case 2: return FLinearColor(0.45f, 0.30f, 0.18f, 1.0f); // Dirt
		case 3: return FLinearColor(0.38f, 0.38f, 0.40f, 1.0f); // Rock
		case 4: return FLinearColor(0.85f, 0.90f, 0.95f, 1.0f); // Snow
		case 5: return FLinearColor(0.15f, 0.35f, 0.70f, 1.0f); // Water
		case 6: return FLinearColor(1.00f, 0.25f, 0.05f, 1.0f); // Lava
		case 7: return FLinearColor(0.10f, 0.10f, 0.11f, 1.0f); // Basalt
		case 8: return FLinearColor(0.20f, 0.20f, 0.22f, 1.0f); // DeepRock
		default: return FLinearColor(0.45f, 0.30f, 0.18f, 1.0f);
		}
	}
}

void ATileGridOverlayActor::SetUndergroundFloorBlocks(const FIntVector& CenterVoxel, float LayerFlatZ)
{
	if (!UndergroundFloorHISM || !MiningVolumeCubeMesh)
	{
		return;
	}

	ClearHISMLayer(UndergroundFloorHISM, UndergroundFloorCells);
	UndergroundFloorHISM->NumCustomDataFloats = FMath::Max(UndergroundFloorHISM->NumCustomDataFloats, 5);

	// Runtime visibility note:
	// The real voxel terrain below the cut plane still participates in depth testing.
	// If this helper slice is placed inside/under the solid terrain, the component is
	// valid and visible in logs but hidden by the black cutaway surface in the scene.
	// Draw the helper slice as a thin readable cap just ABOVE the active layer.
	const float RequestedThickUU = CellSizeUU * FMath::Clamp(UndergroundBlockThickness, 0.05f, 1.0f);
	const float ThickUU = FMath::Clamp(RequestedThickUU, 8.0f, 18.0f);
	const float HalfThick = ThickUU * 0.5f;
	const float SlabLiftUU = 8.0f;
	const float SlabCenterZ = LayerFlatZ + SlabLiftUU + HalfThick;
	const float SlabBottomZ = SlabCenterZ - HalfThick;
	const float SlabTopZ = SlabCenterZ + HalfThick;
	const float XYScale = CellSizeUU / 100.0f;
	const float ZScale = ThickUU / 100.0f;

	UVoxelWorldManager* VM = GetWorld() ? GetWorld()->GetSubsystem<UVoxelWorldManager>() : nullptr;

	// Going Medieval-style layer view: draw solid underground cells with the
	// dedicated unlit underground material. Per-cell terrain color is supplied
	// through PerInstanceCustomData[0..3] below. Do not use the normal grid
	// material here; it turns the slice into a black/white debug grid instead
	// of a readable dirt/rock cross-section.
	UMaterialInterface* FloorMat = UndergroundBlockMaterial
		? UndergroundBlockMaterial.Get()
		: (TileMaterialBase ? TileMaterialBase.Get() : nullptr);

	if (FloorMat)
	{
		UndergroundFloorHISM->SetMaterial(0, FloorMat);
	}

	const int32 Radius = FMath::Max(1, UndergroundBlockViewRadius);

	int32 SolidCount = 0;
	int32 EmptyCount = 0;
	int32 LayerCounts[9] = { 0,0,0,0,0,0,0,0,0 };

	for (int32 X = CenterVoxel.X - Radius; X <= CenterVoxel.X + Radius; ++X)
	{
		for (int32 Y = CenterVoxel.Y - Radius; Y <= CenterVoxel.Y + Radius; ++Y)
		{
			const FIntVector CellCoord(X, Y, CenterVoxel.Z);

			if (UndergroundFloorCells.Contains(CellCoord))
			{
				continue;
			}

			int32 MaterialLayer = 2; // Dirt fallback
			FGameplayTag BlockType;
			const bool bSolid = VM
				? VM->GetTerrainMaterialLayerAtVoxel(CellCoord, MaterialLayer, BlockType)
				: true;

			if (!bSolid)
			{
				++EmptyCount;
				continue;
			}

			MaterialLayer = FMath::Clamp(MaterialLayer, 0, 8);
			++SolidCount;
			++LayerCounts[MaterialLayer];

			const float WorldX = (static_cast<float>(X) + 0.5f) * CellSizeUU;
			const float WorldY = (static_cast<float>(Y) + 0.5f) * CellSizeUU;

			const FTransform BlockXform(
				FRotator::ZeroRotator,
				FVector(WorldX, WorldY, SlabCenterZ),
				FVector(XYScale, XYScale, ZScale));

			const int32 Idx = UndergroundFloorHISM->AddInstance(BlockXform, true);

			const FLinearColor CellColor = bUseTerrainLayerColorsForUnderground
				? WF_TerrainLayerColor(MaterialLayer)
				: UndergroundFloorColor;

			SetInstanceColor(UndergroundFloorHISM, Idx, CellColor);
			UndergroundFloorHISM->SetCustomDataValue(Idx, 4, CellColor.A, true);

			UndergroundFloorCells.Add(CellCoord, Idx);
		}
	}

	UndergroundFloorHISM->SetHiddenInGame(false);
	UndergroundFloorHISM->SetVisibility(bOverlayVisible && UndergroundFloorCells.Num() > 0, true);
	UndergroundFloorHISM->SetCullDistances(0, 0);
	UndergroundFloorHISM->MarkRenderStateDirty();

	UE_LOG(LogGridOverlay, Warning,
		TEXT("Underground slice render debug. Instances=%d Tracked=%d Visible=%s Hidden=%s ActorHidden=%s Mesh=%s Material=%s CustomFloats=%d BottomZ=%.1f CenterZ=%.1f TopZ=%.1f LayerFlatZ=%.1f"),
		UndergroundFloorHISM->GetInstanceCount(),
		UndergroundFloorCells.Num(),
		UndergroundFloorHISM->IsVisible() ? TEXT("true") : TEXT("false"),
		UndergroundFloorHISM->bHiddenInGame ? TEXT("true") : TEXT("false"),
		IsHidden() ? TEXT("true") : TEXT("false"),
		UndergroundFloorHISM->GetStaticMesh() ? *UndergroundFloorHISM->GetStaticMesh()->GetName() : TEXT("None"),
		UndergroundFloorHISM->GetMaterial(0) ? *UndergroundFloorHISM->GetMaterial(0)->GetName() : TEXT("None"),
		UndergroundFloorHISM->NumCustomDataFloats,
		SlabBottomZ,
		SlabCenterZ,
		SlabTopZ,
		LayerFlatZ);

	UE_LOG(LogGridOverlay, Log,
		TEXT("Underground layer slice set. Cells=%d Solid=%d Empty=%d Radius=%d LayerFlatZ=%.1f ThickUU=%.1f Material=%s Sand=%d Grass=%d Dirt=%d Rock=%d Snow=%d Water=%d Lava=%d Basalt=%d DeepRock=%d"),
		UndergroundFloorCells.Num(),
		SolidCount,
		EmptyCount,
		Radius,
		LayerFlatZ,
		ThickUU,
		FloorMat ? *FloorMat->GetName() : TEXT("None"),
		LayerCounts[0],
		LayerCounts[1],
		LayerCounts[2],
		LayerCounts[3],
		LayerCounts[4],
		LayerCounts[5],
		LayerCounts[6],
		LayerCounts[7],
		LayerCounts[8]);
}

void ATileGridOverlayActor::ClearUndergroundFloorBlocks()
{
	ClearHISMLayer(UndergroundFloorHISM, UndergroundFloorCells);

	if (UndergroundFloorHISM)
	{
		UndergroundFloorHISM->SetVisibility(false, true);
		UndergroundFloorHISM->SetHiddenInGame(true);
	}
}

void ATileGridOverlayActor::RebuildDepthReferenceGrids(const TMap<FIntVector, float>& CellsWithTerrainZ, float ActiveLayerFlatZ)
{
	if (!DepthReferenceLineBatch || UndergroundDepthReferenceLayers <= 0 || CellsWithTerrainZ.Num() == 0)
	{
		if (DepthReferenceLineBatch)
		{
			DepthReferenceLineBatch->Flush();
			DepthReferenceLineBatch->SetVisibility(false, true);
		}
		return;
	}

	DepthReferenceLineBatch->Flush();

	// Desaturated amber-gold: reads as "same lattice, deeper" without competing
	// with the bright white active-layer lines.
	const FLinearColor DepthBaseColor(0.55f, 0.45f, 0.20f, 1.0f);
	const float Thickness = 0.75f;
	const float LineZOffset = 1.5f;
	const float LifeTime = 0.0f;
	const uint8 DepthPriority = 0;

	// Build unique edge list once from the XY layout (same for all depth layers).
	TMap<FIntPoint, float> CellZByXY;
	CellZByXY.Reserve(CellsWithTerrainZ.Num());
	for (const TPair<FIntVector, float>& Pair : CellsWithTerrainZ)
	{
		CellZByXY.Add(FIntPoint(Pair.Key.X, Pair.Key.Y), Pair.Value);
	}

	TArray<TPair<FIntPoint, FIntPoint>> UniqueEdges;
	{
		TSet<uint64> DrawnKeys;
		DrawnKeys.Reserve(CellsWithTerrainZ.Num() * 2);

		for (const TPair<FIntPoint, float>& Pair : CellZByXY)
		{
			const int32 X = Pair.Key.X;
			const int32 Y = Pair.Key.Y;

			auto TryAddEdge = [&](FIntPoint A, FIntPoint B)
				{
					if (A.X > B.X || (A.X == B.X && A.Y > B.Y)) { Swap(A, B); }
					const uint64 Key = ((uint64)(uint32)A.X << 32) | ((uint64)(uint32)A.Y << 16) |
						((uint64)(uint32)B.X << 8) | (uint64)(uint32)B.Y;
					if (!DrawnKeys.Contains(Key))
					{
						DrawnKeys.Add(Key);
						UniqueEdges.Add(TPair<FIntPoint, FIntPoint>(A, B));
					}
				};

			TryAddEdge(FIntPoint(X, Y), FIntPoint(X + 1, Y));
			TryAddEdge(FIntPoint(X, Y + 1), FIntPoint(X + 1, Y + 1));
			TryAddEdge(FIntPoint(X, Y), FIntPoint(X, Y + 1));
			TryAddEdge(FIntPoint(X + 1, Y), FIntPoint(X + 1, Y + 1));
		}
	}

	int32 TotalLines = 0;

	for (int32 Depth = 1; Depth <= UndergroundDepthReferenceLayers; ++Depth)
	{
		const float LayerZ = ActiveLayerFlatZ - (static_cast<float>(Depth) * CellSizeUU) + LineZOffset;

		float Opacity = UndergroundDepthBaseOpacity;
		for (int32 i = 1; i < Depth; ++i) { Opacity *= UndergroundDepthOpacityFalloff; }
		Opacity = FMath::Clamp(Opacity, 0.02f, 1.0f);

		FLinearColor LineColor = DepthBaseColor;
		LineColor.A = Opacity;

		for (const TPair<FIntPoint, FIntPoint>& Edge : UniqueEdges)
		{
			const FVector P0(static_cast<float>(Edge.Key.X) * CellSizeUU, static_cast<float>(Edge.Key.Y) * CellSizeUU, LayerZ);
			const FVector P1(static_cast<float>(Edge.Value.X) * CellSizeUU, static_cast<float>(Edge.Value.Y) * CellSizeUU, LayerZ);
			DepthReferenceLineBatch->DrawLine(P0, P1, LineColor, DepthPriority, Thickness, LifeTime);
			++TotalLines;
		}
	}

	DepthReferenceLineBatch->SetHiddenInGame(false);
	DepthReferenceLineBatch->SetVisibility(bOverlayVisible && TotalLines > 0, true);
	DepthReferenceLineBatch->MarkRenderStateDirty();

	UE_LOG(LogGridOverlay, Log,
		TEXT("Depth reference grids drawn. Layers=%d UniqueEdges=%d TotalLines=%d BaseOpacity=%.2f Falloff=%.2f"),
		UndergroundDepthReferenceLayers, UniqueEdges.Num(), TotalLines,
		UndergroundDepthBaseOpacity, UndergroundDepthOpacityFalloff);
}


void ATileGridOverlayActor::RebuildBaseGridLineBatch(const TMap<FIntVector, float>& CellsWithTerrainZ)
{
	if (!BaseGridLineBatch)
	{
		return;
	}

	BaseGridLineBatch->Flush();

	if (CellsWithTerrainZ.Num() == 0)
	{
		BaseGridLineBatch->SetVisibility(false, true);
		return;
	}

	const bool bMiningLayer = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);
	const bool bUndergroundWhite = bMiningLayer && bUseUndergroundWhiteGrid;

	FLinearColor LineColor = bUndergroundWhite
		? FLinearColor(0.025f, 0.035f, 0.045f, 0.78f)
		: ActiveStyle.BorderColor;

	LineColor.A = bUndergroundWhite ? 0.78f : FMath::Clamp(LineColor.A, bMiningLayer ? 0.85f : 0.55f, 1.0f);

	// Underground slice blocks are lifted above the cut plane. Put the grid lines
	// above those caps too, otherwise the colored helper blocks can depth-occlude them.
	const float LineZOffset = bUndergroundWhite ? 3.0f : 1.5f;
	const float Thickness = bUndergroundWhite ? 3.0f : (bMiningLayer ? 3.0f : 1.25f);
	const float LifeTime = 0.0f; // persistent until Flush()
	const uint8 DepthPriority = 0;

	// Center samples supplied by GridSelectionSubsystem.
	// We draw grid lines on CELL EDGES, not center-to-center, so selected voxel cubes sit inside squares.
	TMap<FIntPoint, float> CellZByXY;
	CellZByXY.Reserve(CellsWithTerrainZ.Num());
	for (const TPair<FIntVector, float>& Pair : CellsWithTerrainZ)
	{
		CellZByXY.Add(FIntPoint(Pair.Key.X, Pair.Key.Y), Pair.Value);
	}

	float FallbackZ = 0.0f;
	if (CellsWithTerrainZ.Num() > 0)
	{
		FallbackZ = CellsWithTerrainZ.CreateConstIterator()->Value;
	}

	auto GetCornerZ = [&](const FIntPoint& Corner) -> float
		{
			// A grid corner touches up to four cell centers. Average those sampled Z values.
			// This keeps the surface grid terrain-contoured while avoiding the half-cell offset bug.
			float Sum = 0.0f;
			int32 Count = 0;

			const FIntPoint Candidates[4] =
			{
				FIntPoint(Corner.X - 1, Corner.Y - 1),
				FIntPoint(Corner.X,     Corner.Y - 1),
				FIntPoint(Corner.X - 1, Corner.Y),
				FIntPoint(Corner.X,     Corner.Y)
			};

			for (const FIntPoint& Candidate : Candidates)
			{
				if (const float* FoundZ = CellZByXY.Find(Candidate))
				{
					Sum += *FoundZ;
					++Count;
				}
			}

			return Count > 0 ? (Sum / static_cast<float>(Count)) : FallbackZ;
		};

	auto MakeCornerPoint = [&](const FIntPoint& Corner) -> FVector
		{
			return FVector(
				static_cast<float>(Corner.X) * CellSizeUU,
				static_cast<float>(Corner.Y) * CellSizeUU,
				GetCornerZ(Corner) + LineZOffset);
		};

	auto EdgeKey = [](const FIntPoint& A, const FIntPoint& B) -> FString
		{
			if (A.X < B.X || (A.X == B.X && A.Y <= B.Y))
			{
				return FString::Printf(TEXT("%d,%d|%d,%d"), A.X, A.Y, B.X, B.Y);
			}
			return FString::Printf(TEXT("%d,%d|%d,%d"), B.X, B.Y, A.X, A.Y);
		};

	TSet<FString> DrawnEdges;
	DrawnEdges.Reserve(CellsWithTerrainZ.Num() * 2);
	int32 LinesDrawn = 0;

	auto DrawEdge = [&](const FIntPoint& A, const FIntPoint& B)
		{
			const FString Key = EdgeKey(A, B);
			if (DrawnEdges.Contains(Key))
			{
				return;
			}

			DrawnEdges.Add(Key);
			BaseGridLineBatch->DrawLine(MakeCornerPoint(A), MakeCornerPoint(B), LineColor, DepthPriority, Thickness, LifeTime);
			++LinesDrawn;
		};

	for (const TPair<FIntPoint, float>& Pair : CellZByXY)
	{
		const int32 X = Pair.Key.X;
		const int32 Y = Pair.Key.Y;

		// Draw the four true cell edges. These are boundaries, not center lines.
		DrawEdge(FIntPoint(X, Y), FIntPoint(X + 1, Y));
		DrawEdge(FIntPoint(X, Y + 1), FIntPoint(X + 1, Y + 1));
		DrawEdge(FIntPoint(X, Y), FIntPoint(X, Y + 1));
		DrawEdge(FIntPoint(X + 1, Y), FIntPoint(X + 1, Y + 1));
	}

	BaseGridLineBatch->SetHiddenInGame(false);
	BaseGridLineBatch->SetVisibility(bOverlayVisible && LinesDrawn > 0, true);
	BaseGridLineBatch->MarkRenderStateDirty();

	UE_LOG(LogGridOverlay, Log,
		TEXT("Edge-aligned terrain grid rebuilt. Cells=%d Lines=%d UndergroundWhite=%s Thickness=%.2f"),
		CellsWithTerrainZ.Num(),
		LinesDrawn,
		bUndergroundWhite ? TEXT("true") : TEXT("false"),
		Thickness);
}

FVector ATileGridOverlayActor::MakeGridLinePoint(const FIntVector& VoxelCoord, float TerrainZ, float ExtraZOffset) const
{
	return FVector(
		(static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU,
		(static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU,
		TerrainZ + ExtraZOffset);
}

// ─────────────────────────────────────────────────────────────
// Internal: Transform builders
// ─────────────────────────────────────────────────────────────

FTransform ATileGridOverlayActor::MakeCellTransform(const FIntVector& VoxelCoord, float TerrainZ) const
{
	// Voxel coord → world position (cell center)
	const float WorldX = (static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU;
	const float WorldY = (static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU;
	const float WorldZ = TerrainZ + ZOffset;

	// Scale the unit plane (assumed 100×100 base) to match CellSizeUU
	const float MeshScale = CellSizeUU / 100.0f;

	return FTransform(
		FRotator::ZeroRotator,
		FVector(WorldX, WorldY, WorldZ),
		FVector(MeshScale, MeshScale, 1.0f)
	);
}

FTransform ATileGridOverlayActor::MakeIconTransform(const FIntVector& VoxelCoord, float TerrainZ) const
{
	const float WorldX = (static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU;
	const float WorldY = (static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU;
	const float WorldZ = TerrainZ + ZOffset + IconZOffset;

	// Icons are scaled smaller than the full cell
	const float IconWorldSize = CellSizeUU * ActiveStyle.IconScale;
	const float MeshScale = IconWorldSize / 100.0f;

	return FTransform(
		FRotator::ZeroRotator,
		FVector(WorldX, WorldY, WorldZ),
		FVector(MeshScale, MeshScale, 1.0f)
	);
}

FLinearColor ATileGridOverlayActor::GetMiningDesignationColor() const
{
	return bUseUndergroundWhiteGrid ? UndergroundMiningCapColor : SurfaceMiningCapColor;
}

void ATileGridOverlayActor::RebuildMiningDesignationLines(const TArray<FIntVector>& VoxelCoords)
{
    // Disabled by design: underground mining uses M_WF_MiningGhost cube markers.
    // The old line/X batch made the slice visually noisy and confused the selected voxel.
    if (MiningDesignationLineBatch)
    {
        MiningDesignationLineBatch->Flush();
        MiningDesignationLineBatch->SetVisibility(false, true);
        MiningDesignationLineBatch->SetHiddenInGame(true);
        MiningDesignationLineBatch->MarkRenderStateDirty();
    }
}

FTransform ATileGridOverlayActor::MakeMiningVolumeTransform(const FIntVector& VoxelCoord) const
{
	// Match AMiningGhostActor::VoxelToWorldCenter(): integer voxel coord -> centre of cell.
	// The grid is edge-aligned; the cube is centred inside those edges.
	const float WorldX = (static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU;
	const float WorldY = (static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU;
	const float WorldZ = (static_cast<float>(VoxelCoord.Z) + 0.5f) * CellSizeUU;

	const float Scale = (CellSizeUU / 100.0f) * FMath::Clamp(MiningVolumeScale, 0.1f, 1.0f);

	return FTransform(
		FRotator::ZeroRotator,
		FVector(WorldX, WorldY, WorldZ),
		FVector(Scale, Scale, Scale)
	);
}

// ─────────────────────────────────────────────────────────────
// Internal: HISM instance management
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::SetInstanceColor(
	UHierarchicalInstancedStaticMeshComponent* HISM,
	int32 InstanceIndex,
	const FLinearColor& Color)
{
	if (!HISM || InstanceIndex < 0)
	{
		return;
	}

	HISM->SetCustomDataValue(InstanceIndex, 0, Color.R, false);
	HISM->SetCustomDataValue(InstanceIndex, 1, Color.G, false);
	HISM->SetCustomDataValue(InstanceIndex, 2, Color.B, false);

	// Mark render state dirty on the final write so the material actually
	// receives the per-instance color/opacity this frame.
	HISM->SetCustomDataValue(InstanceIndex, 3, Color.A, true);
}

int32 ATileGridOverlayActor::AddCellToHISM(
	const FIntVector& VoxelCoord,
	float TerrainZ,
	const FLinearColor& Color,
	TMap<FIntVector, int32>& TrackingMap)
{
	UHierarchicalInstancedStaticMeshComponent* TargetHISM =
		(&TrackingMap == &DragCells) ? DragPreviewMeshHISM : TileMeshHISM;

	if (!TargetHISM)
	{
		return INDEX_NONE;
	}

	const FTransform CellXform = MakeCellTransform(VoxelCoord, TerrainZ);
	const int32 Idx = TargetHISM->AddInstance(CellXform, /*bWorldSpace=*/true);
	SetInstanceColor(TargetHISM, Idx, Color);
	const bool bMiningSelection = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);
	const float EffectiveBorderAlpha = bMiningSelection && (&TrackingMap == &SelectedCells)
		? 1.0f
		: ActiveStyle.BorderColor.A;
	TargetHISM->SetCustomDataValue(Idx, 4, EffectiveBorderAlpha, true);

	TrackingMap.Add(VoxelCoord, Idx);
	return Idx;
}

void ATileGridOverlayActor::RemoveCellFromHISM(const FIntVector& VoxelCoord, TMap<FIntVector, int32>& TrackingMap)
{
	const int32* FoundIdx = TrackingMap.Find(VoxelCoord);
	if (!FoundIdx)
	{
		return;
	}

	const int32 RemovedIdx = *FoundIdx;

	// Never compare TMap values directly. UE deletes TMap::operator==, and map
	// comparison is not needed here anyway. The reference passed into this function
	// is one of our owned tracking maps, so choose the target HISM by map address.
	UHierarchicalInstancedStaticMeshComponent* TargetHISM = TileMeshHISM;
	if (&TrackingMap == &SelectedIcons)
	{
		TargetHISM = IconMeshHISM;
	}
	else if (&TrackingMap == &DragCells)
	{
		TargetHISM = DragPreviewMeshHISM;
	}
	else if (&TrackingMap == &MiningVolumeCells)
	{
		TargetHISM = MiningVolumeHISM;
	}

	if (!TargetHISM)
	{
		TrackingMap.Remove(VoxelCoord);
		return;
	}

	// HISM::RemoveInstance swaps the last instance into the removed slot. Capture
	// the last index before removal so we can repair the moved instance's map entry.
	const int32 LastIdx = TargetHISM->GetInstanceCount() - 1;

	TargetHISM->RemoveInstance(RemovedIdx);
	TrackingMap.Remove(VoxelCoord);

	// Fix up the swapped instance's tracked index.
	if (RemovedIdx != LastIdx && LastIdx >= 0)
	{
		for (TPair<FIntVector, int32>& Pair : TrackingMap)
		{
			if (Pair.Value == LastIdx)
			{
				Pair.Value = RemovedIdx;
				break;
			}
		}
	}
}

void ATileGridOverlayActor::ClearHISMLayer(
	UHierarchicalInstancedStaticMeshComponent* HISM,
	TMap<FIntVector, int32>& TrackingMap)
{
	if (HISM)
	{
		HISM->ClearInstances();
	}
	TrackingMap.Empty();
}


void ATileGridOverlayActor::LoadDesignationStylesFromDataTable()
{
	if (!DesignationStyleTable)
	{
		return;
	}

	static bool bLoggedTableLoad = false;

	TArray<FGridDesignationStyleRow*> Rows;
	DesignationStyleTable->GetAllRows<FGridDesignationStyleRow>(TEXT("GridDesignationStyles"), Rows);

	for (const FGridDesignationStyleRow* Row : Rows)
	{
		if (!Row || Row->DesignationType == EGridDesignationType::None)
		{
			continue;
		}

		FGridOverlayStyle Style;
		Style.NeutralGridColor = Row->NeutralGridColor;
		Style.TileColor = Row->TileColor;
		Style.BorderColor = Row->BorderColor;
		Style.HoverColor = Row->HoverColor;
		Style.DragPreviewColor = Row->DragPreviewColor;
		Style.IconTexture = Row->IconTexture;
		Style.IconTint = Row->IconTint;
		Style.IconScale = Row->IconScale;

		switch (Row->DesignationType)
		{
		case EGridDesignationType::Build:
			RegisteredStyles.Add(EBuildToolMode::Place, Style);
			break;
		case EGridDesignationType::Mine:
			RegisteredStyles.Add(EBuildToolMode::Mine, Style);
			RegisteredStyles.Add(EBuildToolMode::Dig, Style);
			break;
		case EGridDesignationType::Farm:
			RegisteredStyles.Add(EBuildToolMode::Farm, Style);
			break;
		case EGridDesignationType::Erase:
			RegisteredStyles.Add(EBuildToolMode::Remove, Style);
			break;
		default:
			break;
		}
	}

	if (!bLoggedTableLoad)
	{
		bLoggedTableLoad = true;
		UE_LOG(LogGridOverlay, Log,
			TEXT("Loaded %d grid designation styles from DataTable %s"),
			Rows.Num(),
			*DesignationStyleTable->GetName());

		const bool bFarmStyleFound = RegisteredStyles.Contains(EBuildToolMode::Farm);
		UE_LOG(LogGridOverlay, Log,
			TEXT("[GridOverlay] Farm designation style loaded: %s"),
			bFarmStyleFound ? TEXT("OK") : TEXT("NULL — using hardcoded fallback green"));
	}
}

void ATileGridOverlayActor::RebuildBaseGridAround(const FIntVector& CenterCoord, float TerrainZ)
{
	if (!BaseGridHISM || VisibleGridRadius <= 0)
	{
		return;
	}

	ClearHISMLayer(BaseGridHISM, BaseGridCells);

	// Base grid is only a temporary targeting layer. Keep it driven by the
	// DataTable so it reads as a light voxel-size grid instead of a giant sheet.
	const bool bMiningLayer = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);
	FLinearColor VisibleNeutralGridColor = bMiningLayer
		? FLinearColor(0.10f, 0.36f, 0.85f, FMath::Max(ActiveStyle.NeutralGridColor.A, 0.18f))
		: ActiveStyle.NeutralGridColor;
	const float VisibleBorderAlpha = bMiningLayer ? FMath::Max(ActiveStyle.BorderColor.A, 0.65f) : ActiveStyle.BorderColor.A;

	for (int32 X = CenterCoord.X - VisibleGridRadius; X <= CenterCoord.X + VisibleGridRadius; ++X)
	{
		for (int32 Y = CenterCoord.Y - VisibleGridRadius; Y <= CenterCoord.Y + VisibleGridRadius; ++Y)
		{
			const FIntVector GridCoord(X, Y, CenterCoord.Z);
			const FTransform CellXform = MakeCellTransform(GridCoord, TerrainZ);
			const int32 Idx = BaseGridHISM->AddInstance(CellXform, true);
			SetInstanceColor(BaseGridHISM, Idx, VisibleNeutralGridColor);
			BaseGridHISM->SetCustomDataValue(Idx, 4, VisibleBorderAlpha, true);
			BaseGridCells.Add(GridCoord, Idx);
		}
	}

	UE_LOG(LogGridOverlay, Warning,
		TEXT("Base grid rebuilt. Center=(%d,%d,%d) Radius=%d Cells=%d TerrainZ=%.1f Alpha=%.2f Border=%.2f"),
		CenterCoord.X,
		CenterCoord.Y,
		CenterCoord.Z,
		VisibleGridRadius,
		BaseGridCells.Num(),
		TerrainZ,
		VisibleNeutralGridColor.A,
		VisibleBorderAlpha);
}

void ATileGridOverlayActor::RebuildConfirmedDesignationVisuals()
{
	if (ConfirmedTileMeshHISM)
	{
		ConfirmedTileMeshHISM->ClearInstances();
	}
	ConfirmedTileInstances.Empty();
	MiningVolumeCells.Empty();

	for (TPair<EGridDesignationType, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>>& Pair : IconHISMsByDesignation)
	{
		if (Pair.Value)
		{
			Pair.Value->ClearInstances();
		}
	}

	for (const TPair<FIntVector, FGridDesignationCellVisual>& Pair : ConfirmedDesignationCells)
	{
		AddConfirmedVisualInstance(Pair.Value);
	}
}

// ─────────────────────────────────────────────────────────────
// Internal: Full rebuild
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::RebuildAllInstances()
{
	// Snapshot current coords + Z before clearing selected/drag preview.
	TMap<FIntVector, float> SelectedSnapshot;
	for (const TPair<FIntVector, int32>& Pair : SelectedCells)
	{
		FTransform Xform;
		if (TileMeshHISM && TileMeshHISM->GetInstanceTransform(Pair.Value, Xform, true))
		{
			SelectedSnapshot.Add(Pair.Key, Xform.GetLocation().Z - ZOffset);
		}
	}

	TMap<FIntVector, float> DragSnapshot;
	for (const TPair<FIntVector, int32>& Pair : DragCells)
	{
		FTransform Xform;
		if (DragPreviewMeshHISM && DragPreviewMeshHISM->GetInstanceTransform(Pair.Value, Xform, true))
		{
			DragSnapshot.Add(Pair.Key, Xform.GetLocation().Z - ZOffset);
		}
	}

	// Wipe transient selected/preview layers.
	ClearHISMLayer(TileMeshHISM, SelectedCells);
	ClearHISMLayer(DragPreviewMeshHISM, DragCells);
	ClearHISMLayer(IconMeshHISM, SelectedIcons);

	// Rebuild persistent confirmed designation layer first.
	RebuildConfirmedDesignationVisuals();

	// Re-add transient selected cells with current active style.
	for (const TPair<FIntVector, float>& Pair : SelectedSnapshot)
	{
		const bool bMiningSelection = (CurrentMode == EBuildToolMode::Mine || CurrentMode == EBuildToolMode::Dig);
		const FLinearColor SelectionColor = bMiningSelection
			? FLinearColor(1.0f, 0.78f, 0.05f, 0.85f)
			: ActiveStyle.TileColor;

		AddCellToHISM(Pair.Key, Pair.Value, SelectionColor, SelectedCells);

		if (ActiveStyle.IconTexture)
		{
			const FTransform IconXform = MakeIconTransform(Pair.Key, Pair.Value);
			const int32 IconIdx = IconMeshHISM->AddInstance(IconXform, true);
			SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.IconTint);
			SelectedIcons.Add(Pair.Key, IconIdx);
		}
	}

	for (const TPair<FIntVector, float>& Pair : DragSnapshot)
	{
		if (!SelectedCells.Contains(Pair.Key))
		{
			AddCellToHISM(Pair.Key, Pair.Value, ActiveStyle.DragPreviewColor, DragCells);
		}
	}
}

// ─────────────────────────────────────────────────────────────
// Internal: Icon texture swap
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::ApplyIconTexture()
{
	if (!IconMID) return;

	if (ActiveStyle.IconTexture)
	{
		IconMID->SetTextureParameterValue(FName("IconTex"), ActiveStyle.IconTexture);
		IconMeshHISM->SetVisibility(true);
	}
	else
	{
		IconMeshHISM->SetVisibility(false);
	}
}

// ─────────────────────────────────────────────────────────────
// Internal: Terrain Z self-sampling
// ─────────────────────────────────────────────────────────────

float ATileGridOverlayActor::SelfSampleTerrainZ(const FIntVector& VoxelCoord) const
{
	const float WorldX = (static_cast<float>(VoxelCoord.X) + 0.5f) * CellSizeUU;
	const float WorldY = (static_cast<float>(VoxelCoord.Y) + 0.5f) * CellSizeUU;

	const FVector TraceStart(WorldX, WorldY, 50000.0f);
	const FVector TraceEnd(WorldX, WorldY, -50000.0f);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	if (GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TerrainTraceChannel, Params))
	{
		return Hit.ImpactPoint.Z;
	}

	UE_LOG(LogGridOverlay, Warning, TEXT("Terrain Z trace missed at voxel (%d,%d)."), VoxelCoord.X, VoxelCoord.Y);
	return 0.0f;
}

// ─────────────────────────────────────────────────────────────
// Internal: Fallback plane mesh
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::EnsurePlaneMesh()
{
	if (CellPlaneMesh) return;

	// Attempt to load Engine's built-in plane (100×100 UU)
	CellPlaneMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Plane.Plane"));

	if (!CellPlaneMesh)
	{
		UE_LOG(LogGridOverlay, Error,
			TEXT("No CellPlaneMesh assigned and Engine plane not found. Overlay will be invisible."));
	}
}


void ATileGridOverlayActor::EnsureMiningVolumeMesh()
{
	if (!MiningVolumeCubeMesh)
	{
		MiningVolumeCubeMesh = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	if (!MiningVolumeCubeMesh)
	{
		UE_LOG(LogGridOverlay, Error,
			TEXT("No MiningVolumeCubeMesh assigned and Engine cube not found. Mining volume preview will be invisible."));
	}
}