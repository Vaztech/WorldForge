// Source/WorldForge/Player/BuildGhostActor.cpp
#include "BuildGhostActor.h"
#include "VoxelWorldManager.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

ABuildGhostActor::ABuildGhostActor()
{
	PrimaryActorTick.bCanEverTick = false;

	// Root scene component
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	// Prefab ghost mesh — starts hidden
	GhostMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("GhostMesh"));
	GhostMesh->SetupAttachment(Root);
	GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GhostMesh->SetCastShadow(false);
	GhostMesh->SetVisibility(false);

	// Load the default cube mesh used throughout WorldForge
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
	{
		GhostMesh->SetStaticMesh(CubeFinder.Object);
	}
}

void ABuildGhostActor::BeginPlay()
{
	Super::BeginPlay();

	// Create the translucent material for the prefab ghost.
	// Uses a basic unlit translucent material with a color parameter.
	UMaterialInterface* BaseMat = GhostMesh->GetMaterial(0);
	if (BaseMat)
	{
		GhostMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (GhostMaterial)
		{
			GhostMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
			GhostMesh->SetMaterial(0, GhostMaterial);
		}
	}

	// Pre-create material instances for cell highlights
	if (BaseMat)
	{
		CellValidMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (CellValidMaterial)
		{
			CellValidMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity));
			CellValidMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
		}

		CellCursorMaterial = UMaterialInstanceDynamic::Create(BaseMat, this);
		if (CellCursorMaterial)
		{
			CellCursorMaterial->SetVectorParameterValue(FName("BaseColor"), FLinearColor(0.2f, 0.5f, 1.0f, GhostOpacity));
			CellCursorMaterial->SetScalarParameterValue(FName("Opacity"), GhostOpacity);
		}
	}
}

// ================================
// State
// ================================

void ABuildGhostActor::SetGhostState(EBuildGhostState State)
{
	CurrentState = State;

	if (State == EBuildGhostState::Hidden)
	{
		GhostMesh->SetVisibility(false);
		for (int32 i = 0; i < ActiveHighlightCount; ++i)
		{
			if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
			{
				CellHighlightPool[i]->SetVisibility(false);
			}
		}
		return;
	}

	// Update ghost mesh color based on state
	if (GhostMaterial)
	{
		GhostMaterial->SetVectorParameterValue(FName("BaseColor"), GetColorForState(State));
	}
}

FLinearColor ABuildGhostActor::GetColorForState(EBuildGhostState State) const
{
	switch (State)
	{
	case EBuildGhostState::Valid:
		return FLinearColor(0.1f, 0.8f, 0.2f, GhostOpacity);  // Green
	case EBuildGhostState::Invalid:
		return FLinearColor(0.9f, 0.1f, 0.1f, GhostOpacity);  // Red
	default:
		return FLinearColor(0.5f, 0.5f, 0.5f, 0.0f);          // Transparent
	}
}

// ================================
// Prefab Ghost
// ================================

void ABuildGhostActor::UpdatePrefabGhost(FVector WorldPosition, int32 RotationIndex)
{
	// Hide cell highlights in prefab mode
	for (int32 i = 0; i < ActiveHighlightCount; ++i)
	{
		if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
		{
			CellHighlightPool[i]->SetVisibility(false);
		}
	}
	ActiveHighlightCount = 0;

	// Position and rotate the ghost mesh
	GhostMesh->SetVisibility(true);
	GhostMesh->SetWorldLocation(WorldPosition);
	GhostMesh->SetWorldRotation(FRotator(0.f, RotationIndex * 90.f, 0.f));

	// Scale to voxel size (default cube is 100x100x100, VoxelSize is typically 100)
	// The mesh is already 1:1 at default scale for VoxelSize=100.
	// If VoxelSize changes, this needs to scale: VoxelSize / 100.f
	GhostMesh->SetWorldScale3D(FVector(1.0f));
}

// ================================
// Custom Draw Cell Highlights
// ================================

void ABuildGhostActor::UpdateCustomDrawCells(
	const TArray<FIntPoint>& SelectedCells,
	FIntPoint CursorCell,
	UVoxelWorldManager* VoxelManager)
{
	if (!VoxelManager) return;

	// Hide prefab ghost in custom draw mode
	GhostMesh->SetVisibility(false);

	// Total highlights = selected cells + 1 cursor cell
	const int32 TotalNeeded = SelectedCells.Num() + 1;
	EnsurePoolSize(TotalNeeded);

	int32 Idx = 0;

	// Highlight each selected cell (green)
	for (const FIntPoint& Cell : SelectedCells)
	{
		UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
		if (Comp)
		{
			const FVector Pos = VoxelManager->GridToWorld(Cell, 0);
			Comp->SetWorldLocation(Pos);
			Comp->SetVisibility(true);

			if (CellValidMaterial)
			{
				Comp->SetMaterial(0, CellValidMaterial);
			}
		}
		++Idx;
	}

	// Highlight cursor cell (blue)
	{
		UStaticMeshComponent* Comp = GetOrCreateHighlight(Idx);
		if (Comp)
		{
			const FVector Pos = VoxelManager->GridToWorld(CursorCell, 0);
			Comp->SetWorldLocation(Pos);
			Comp->SetVisibility(true);

			if (CellCursorMaterial)
			{
				Comp->SetMaterial(0, CellCursorMaterial);
			}
		}
		++Idx;
	}

	// Hide unused highlights
	for (int32 i = Idx; i < ActiveHighlightCount; ++i)
	{
		if (CellHighlightPool.IsValidIndex(i) && CellHighlightPool[i])
		{
			CellHighlightPool[i]->SetVisibility(false);
		}
	}

	ActiveHighlightCount = Idx;
}

void ABuildGhostActor::ClearAllHighlights()
{
	for (int32 i = 0; i < CellHighlightPool.Num(); ++i)
	{
		if (CellHighlightPool[i])
		{
			CellHighlightPool[i]->SetVisibility(false);
		}
	}
	ActiveHighlightCount = 0;

	GhostMesh->SetVisibility(false);
}

// ================================
// Pool Management
// ================================

UStaticMeshComponent* ABuildGhostActor::GetOrCreateHighlight(int32 Index)
{
	if (CellHighlightPool.IsValidIndex(Index) && CellHighlightPool[Index])
	{
		return CellHighlightPool[Index];
	}

	// Create new mesh component
	UStaticMeshComponent* NewComp = NewObject<UStaticMeshComponent>(this);
	if (!NewComp) return nullptr;

	NewComp->SetupAttachment(GetRootComponent());
	NewComp->RegisterComponent();
	NewComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	NewComp->SetCastShadow(false);
	NewComp->SetVisibility(false);

	// Use the same cube mesh as the ghost
	if (GhostMesh && GhostMesh->GetStaticMesh())
	{
		NewComp->SetStaticMesh(GhostMesh->GetStaticMesh());
	}

	// Flatten the cube to a thin slab for cell highlighting
	NewComp->SetWorldScale3D(FVector(1.0f, 1.0f, 0.1f));

	if (Index < CellHighlightPool.Num())
	{
		CellHighlightPool[Index] = NewComp;
	}
	else
	{
		CellHighlightPool.Add(NewComp);
	}

	return NewComp;
}

void ABuildGhostActor::EnsurePoolSize(int32 RequiredSize)
{
	while (CellHighlightPool.Num() < RequiredSize)
	{
		CellHighlightPool.Add(nullptr);
	}
}
