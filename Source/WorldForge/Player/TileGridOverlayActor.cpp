// TileGridOverlayActor.cpp

#include "Player/TileGridOverlayActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Engine/DataTable.h"

DEFINE_LOG_CATEGORY_STATIC(LogGridOverlay, Log, All);

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────

ATileGridOverlayActor::ATileGridOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

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
}

// ─────────────────────────────────────────────────────────────
// BeginPlay / EndPlay
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::BeginPlay()
{
	Super::BeginPlay();

	// Keep the overlay close to the voxel surface now that rendering is verified.
	// The values below are intentionally small enough to read as a surface grid
	// while still avoiding most voxel-terrain z-fighting.
	ZOffset = FMath::Max(ZOffset, 25.0f);
	IconZOffset = FMath::Max(IconZOffset, 6.0f);
	VisibleGridRadius = FMath::Max(VisibleGridRadius, 1);

	EnsurePlaneMesh();

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

	if (TileMaterialBase)
	{
		if (BaseGridHISM) { BaseGridHISM->SetMaterial(0, TileMaterialBase); }
		if (TileMeshHISM) { TileMeshHISM->SetMaterial(0, TileMaterialBase); }
		if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetMaterial(0, TileMaterialBase); }
		if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetMaterial(0, TileMaterialBase); }
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
	SelectedCells.Empty();
	SelectedIcons.Empty();
	DragCells.Empty();
	ConfirmedDesignationCells.Empty();
	ConfirmedCellTerrainZ.Empty();
	ConfirmedTileInstances.Empty();
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

	RebuildBaseGridAround(VoxelCoord, TerrainZ);

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

	AddCellToHISM(VoxelCoord, TerrainZ, ActiveStyle.TileColor, SelectedCells);

	// Also add an icon instance if the mode has an icon
	if (ActiveStyle.IconTexture)
	{
		const FTransform IconXform = MakeIconTransform(VoxelCoord, TerrainZ);
		const int32 IconIdx = IconMeshHISM->AddInstance(IconXform, /*bWorldSpace=*/true);
		SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.IconTint);
		SelectedIcons.Add(VoxelCoord, IconIdx);
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
	// Batch replace: clear existing, add all new
	ClearHISMLayer(TileMeshHISM, SelectedCells);
	ClearHISMLayer(IconMeshHISM, SelectedIcons);

	for (const auto& Pair : CellsWithTerrainZ)
	{
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
	ClearHISMLayer(IconMeshHISM, SelectedIcons);
	UE_LOG(LogGridOverlay, Log, TEXT("Selection cleared."));
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

	if (BaseGridHISM)
	{
		BaseGridHISM->MarkRenderStateDirty();
	}

	UE_LOG(LogGridOverlay, Verbose, TEXT("Base grid cleared."));
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

	if (BaseGridHISM) { BaseGridHISM->SetVisibility(true, true); }
	if (TileMeshHISM) { TileMeshHISM->SetVisibility(true, true); }
	if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetVisibility(true, true); }
	if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetVisibility(true, true); }
	if (IconMeshHISM) { IconMeshHISM->SetVisibility(true, true); }

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
	ClearBaseGrid();

	SetActorHiddenInGame(true);

	if (BaseGridHISM) { BaseGridHISM->SetVisibility(false, true); }
	if (TileMeshHISM) { TileMeshHISM->SetVisibility(false, true); }
	if (DragPreviewMeshHISM) { DragPreviewMeshHISM->SetVisibility(false, true); }
	if (ConfirmedTileMeshHISM) { ConfirmedTileMeshHISM->SetVisibility(false, true); }
	if (IconMeshHISM) { IconMeshHISM->SetVisibility(false, true); }

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
	TargetHISM->SetCustomDataValue(Idx, 4, ActiveStyle.BorderColor.A, true);

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
	FLinearColor VisibleNeutralGridColor = ActiveStyle.NeutralGridColor;
	const float VisibleBorderAlpha = ActiveStyle.BorderColor.A;

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
		AddCellToHISM(Pair.Key, Pair.Value, ActiveStyle.TileColor, SelectedCells);

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
