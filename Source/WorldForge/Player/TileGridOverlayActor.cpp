// TileGridOverlayActor.cpp

#include "Player/TileGridOverlayActor.h"

#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogGridOverlay, Log, All);

// ─────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────

ATileGridOverlayActor::ATileGridOverlayActor()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// --- Tile HISM (selected + drag cells) ---
	TileMeshHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TileMeshHISM"));
	TileMeshHISM->SetupAttachment(RootComponent);
	TileMeshHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TileMeshHISM->SetCastShadow(false);
	TileMeshHISM->NumCustomDataFloats = 5; // R, G, B, A, BorderAlpha
	TileMeshHISM->SetGenerateOverlapEvents(false);
	TileMeshHISM->bDisableCollision = true;

	// --- Icon HISM (per-selected-cell mode icon) ---
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

	EnsurePlaneMesh();

	// Assign mesh to all components
	if (CellPlaneMesh)
	{
		TileMeshHISM->SetStaticMesh(CellPlaneMesh);
		IconMeshHISM->SetStaticMesh(CellPlaneMesh);
		HoverMeshComp->SetStaticMesh(CellPlaneMesh);
	}

	// Create MIDs for icon + hover
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
	if (TileMaterialBase)
	{
		TileMeshHISM->SetMaterial(0, TileMaterialBase);
	}

	// Start hidden
	SetActorHiddenInGame(true);
	bOverlayVisible = false;

	UE_LOG(LogGridOverlay, Log, TEXT("TileGridOverlayActor ready. CellSize=%.0f ZOffset=%.1f"), CellSizeUU, ZOffset);
}

void ATileGridOverlayActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SelectedCells.Empty();
	SelectedIcons.Empty();
	DragCells.Empty();
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

	UE_LOG(LogGridOverlay, Log, TEXT("Mode changed to %d. Selected=%d DragPreview=%d"),
		static_cast<int32>(Mode), SelectedCells.Num(), DragCells.Num());
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
		SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.TileColor);
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
			SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.TileColor);
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
	ClearHISMLayer(TileMeshHISM, DragCells);
}

// ─────────────────────────────────────────────────────────────
// Visibility
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::ShowOverlay()
{
	bOverlayVisible = true;
	SetActorHiddenInGame(false);
	HoverMeshComp->SetVisibility(bHoverActive);
}

void ATileGridOverlayActor::HideOverlay()
{
	bOverlayVisible = false;
	SetActorHiddenInGame(true);
	HoverMeshComp->SetVisibility(false);
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

void ATileGridOverlayActor::SetInstanceColor(UHierarchicalInstancedStaticMeshComponent* HISM, int32 InstanceIndex, const FLinearColor& Color)
{
	if (!HISM || InstanceIndex < 0) return;

	HISM->SetCustomDataValue(InstanceIndex, 0, Color.R);
	HISM->SetCustomDataValue(InstanceIndex, 1, Color.G);
	HISM->SetCustomDataValue(InstanceIndex, 2, Color.B);
	HISM->SetCustomDataValue(InstanceIndex, 3, Color.A);
}

int32 ATileGridOverlayActor::AddCellToHISM(
	const FIntVector& VoxelCoord,
	float TerrainZ,
	const FLinearColor& Color,
	TMap<FIntVector, int32>& TrackingMap)
{
	const FTransform CellXform = MakeCellTransform(VoxelCoord, TerrainZ);
	const int32 Idx = TileMeshHISM->AddInstance(CellXform, /*bWorldSpace=*/true);
	SetInstanceColor(TileMeshHISM, Idx, Color);

	// Also write border alpha into custom data slot 4
	SetInstanceColor(TileMeshHISM, Idx, Color); // slots 0-3
	TileMeshHISM->SetCustomDataValue(Idx, 4, ActiveStyle.BorderColor.A);

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

	UHierarchicalInstancedStaticMeshComponent* TargetHISM =
		(&TrackingMap == &SelectedIcons) ? IconMeshHISM : TileMeshHISM;

	if (!TargetHISM)
	{
		TrackingMap.Remove(VoxelCoord);
		return;
	}

	// HISM::RemoveInstance swaps the last instance into the removed slot.
	// Capture the last index from the actual target component before removal.
	const int32 LastIdx = TargetHISM->GetInstanceCount() - 1;

	TargetHISM->RemoveInstance(RemovedIdx);
	TrackingMap.Remove(VoxelCoord);

	// Fix up the swapped instance's tracked index.
	if (RemovedIdx != LastIdx && LastIdx >= 0)
	{
		for (auto& Pair : TrackingMap)
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

// ─────────────────────────────────────────────────────────────
// Internal: Full rebuild
// ─────────────────────────────────────────────────────────────

void ATileGridOverlayActor::RebuildAllInstances()
{
	// Snapshot current coords + Z before clearing
	TMap<FIntVector, float> SelectedSnapshot;
	for (const auto& Pair : SelectedCells)
	{
		FTransform Xform;
		if (TileMeshHISM->GetInstanceTransform(Pair.Value, Xform, /*bWorldSpace=*/true))
		{
			SelectedSnapshot.Add(Pair.Key, Xform.GetLocation().Z - ZOffset);
		}
	}

	TMap<FIntVector, float> DragSnapshot;
	for (const auto& Pair : DragCells)
	{
		FTransform Xform;
		if (TileMeshHISM->GetInstanceTransform(Pair.Value, Xform, /*bWorldSpace=*/true))
		{
			DragSnapshot.Add(Pair.Key, Xform.GetLocation().Z - ZOffset);
		}
	}

	// Wipe everything
	ClearHISMLayer(TileMeshHISM, SelectedCells);
	ClearHISMLayer(TileMeshHISM, DragCells);
	ClearHISMLayer(IconMeshHISM, SelectedIcons);

	// Re-add with current style colors
	for (const auto& Pair : SelectedSnapshot)
	{
		AddCellToHISM(Pair.Key, Pair.Value, ActiveStyle.TileColor, SelectedCells);

		if (ActiveStyle.IconTexture)
		{
			const FTransform IconXform = MakeIconTransform(Pair.Key, Pair.Value);
			const int32 IconIdx = IconMeshHISM->AddInstance(IconXform, /*bWorldSpace=*/true);
			SetInstanceColor(IconMeshHISM, IconIdx, ActiveStyle.TileColor);
			SelectedIcons.Add(Pair.Key, IconIdx);
		}
	}

	for (const auto& Pair : DragSnapshot)
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
