// DigGhostActor.cpp
// WorldForge — Phase 1: Digging Foundation

#include "Player/DigGhostActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

ADigGhostActor::ADigGhostActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork; // Tick after camera so we're always current

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	WireframeMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WireframeMesh"));
	WireframeMesh->SetupAttachment(RootScene);
	WireframeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WireframeMesh->SetCastShadow(false);
	WireframeMesh->bUseAsyncCooking = false;

	// Ghost should never block anything.
	SetCanBeDamaged(false);

	// Don't replicate — purely local visual.
	bReplicates = false;
}

void ADigGhostActor::BeginPlay()
{
	Super::BeginPlay();

	// ------------------------------------------------------------------
	// Ensure this actor NEVER interacts with gameplay systems.
	// This is purely a visual preview — no collision, no shadows.
	// ------------------------------------------------------------------
	WireframeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WireframeMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	WireframeMesh->SetGenerateOverlapEvents(false);

	// Disable all shadow casting — ghost visuals should not affect lighting.
	WireframeMesh->SetCastShadow(false);
	WireframeMesh->bCastDynamicShadow = false;
	WireframeMesh->bCastStaticShadow = false;

	// ------------------------------------------------------------------
	// Material setup
	//
	// IMPORTANT:
	// If no material is assigned, Unreal will fallback to a default
	// opaque material — which turns our "wireframe" box into a giant
	// solid black cube (bad).
	//
	// So instead of letting that happen, we HIDE the mesh entirely.
	// ------------------------------------------------------------------
	if (BaseMaterial)
	{
		// Create a dynamic instance so we can change color + opacity at runtime
		GhostMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);

		// Apply it to the procedural mesh
		WireframeMesh->SetMaterial(0, GhostMaterial);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigGhostActor] No BaseMaterial assigned — hiding dig ghost mesh. "
				"Assign a translucent/wireframe material in defaults."));

		// Prevent the ugly default material fallback
		WireframeMesh->SetVisibility(false, true);
	}

	// ------------------------------------------------------------------
	// Start hidden — BuildModeManager will activate us when needed
	// ------------------------------------------------------------------
	SetGhostVisible(false);
	SetGhostState(EDigGhostState::Hidden);

	// Build initial geometry (1x1x1 dig volume)
	RebuildWireframeGeometry();
}

void ADigGhostActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentState == EDigGhostState::Hidden)
	{
		return;
	}

	UpdateMaterialState(DeltaTime);
}

// ===================================================================
// Public API
// ===================================================================

void ADigGhostActor::UpdateGhost(const FVector& WorldPosition,
                                  const FIntVector& GridPosition,
                                  int32 Radius, int32 DepthPerAction)
{
	// Snap position to grid-aligned world position.
	// The world position passed in should already be grid-snapped by
	// BuildModeManager (same logic as BuildGhostActor), but we enforce
	// grid alignment here as a safety net.
	const FVector SnappedPos = FVector(
		FMath::RoundToFloat(WorldPosition.X / GridCellSize) * GridCellSize,
		FMath::RoundToFloat(WorldPosition.Y / GridCellSize) * GridCellSize,
		FMath::RoundToFloat(WorldPosition.Z / GridCellSize) * GridCellSize
	);

	SetActorLocation(SnappedPos);
	CurrentGridPos = GridPosition;

	// Rebuild geometry only if dimensions changed.
	const bool bDimensionsChanged = (Radius != CurrentRadius || DepthPerAction != CurrentDepth);
	CurrentRadius = FMath::Max(Radius, 1);
	CurrentDepth = FMath::Max(DepthPerAction, 1);

	if (bDimensionsChanged)
	{
		RebuildWireframeGeometry();
	}
}

void ADigGhostActor::SetGhostState(EDigGhostState NewState)
{
	if (CurrentState == NewState)
	{
		return;
	}

	CurrentState = NewState;

	if (NewState == EDigGhostState::Hidden)
	{
		SetGhostVisible(false);
	}
	else
	{
		SetGhostVisible(true);
	}

	// Immediately apply the new color (don't wait for next tick).
	if (GhostMaterial)
	{
		const FLinearColor Color = GetColorForState();
		GhostMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
		GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Color.A);
	}
}

void ADigGhostActor::SetGhostVisible(bool bVisible)
{
	// ------------------------------------------------------------------
	// We ONLY allow rendering if:
	// 1. The system wants us visible
	// 2. We actually have a valid material
	//
	// This prevents the "giant black box" bug when BaseMaterial is missing.
	// ------------------------------------------------------------------
	const bool bCanRender = bVisible && GhostMaterial != nullptr;

	// Hide/show the entire actor in-game
	SetActorHiddenInGame(!bCanRender);

	// Only tick when visible (saves performance + avoids unnecessary updates)
	SetActorTickEnabled(bVisible);

	// Explicitly control mesh visibility as well
	// (important because component visibility != actor hidden state)
	if (WireframeMesh)
	{
		WireframeMesh->SetVisibility(bCanRender, true);
	}
}

// ===================================================================
// Wireframe geometry generation
// ===================================================================

void ADigGhostActor::RebuildWireframeGeometry()
{
	WireframeMesh->ClearAllMeshSections();

	// Calculate volume dimensions in grid cells.
	const int32 Width = (CurrentRadius * 2) - 1;  // Radius=1 → 1 cell, Radius=2 → 3 cells
	const int32 Height = Width;                     // Square footprint
	const int32 Depth = CurrentDepth;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	GenerateBoxWireframe(Width, Height, Depth, Vertices, Triangles, Normals, UVs);

	if (Vertices.Num() > 0 && Triangles.Num() > 0)
	{
		TArray<FColor> VertexColors;
		VertexColors.Init(FColor::White, Vertices.Num());

		WireframeMesh->CreateMeshSection(
			0,
			Vertices,
			Triangles,
			Normals,
			UVs,
			VertexColors,
			/*Tangents=*/ TArray<FProcMeshTangent>(),
			/*bCreateCollision=*/ false
		);

		if (GhostMaterial)
		{
			WireframeMesh->SetMaterial(0, GhostMaterial);
		}
	}
}

void ADigGhostActor::GenerateBoxWireframe(
	int32 Width, int32 Height, int32 Depth,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs) const
{
	// We render a thin-walled box (not true wireframe — ProceduralMesh doesn't
	// support line topology). Instead we generate a box with slightly inset faces
	// and a translucent material, which reads as a wireframe volume in practice.
	//
	// The box is centered at local origin, extending:
	//   X: -HalfX to +HalfX
	//   Y: -HalfY to +HalfY
	//   Z: 0 (top face) to -TotalDepth (bottom face)
	//
	// This means the actor's location corresponds to the TOP of the dig volume.

	const float HalfX = (Width * GridCellSize) * 0.5f;
	const float HalfY = (Height * GridCellSize) * 0.5f;
	const float TotalZ = Depth * GridCellSize;

	// Slight inset to make edges visible against terrain.
	const float Inset = GridCellSize * 0.02f;

	// 8 corners of the outer box.
	const FVector C0(-HalfX + Inset, -HalfY + Inset, -Inset);           // Top-front-left
	const FVector C1( HalfX - Inset, -HalfY + Inset, -Inset);           // Top-front-right
	const FVector C2( HalfX - Inset,  HalfY - Inset, -Inset);           // Top-back-right
	const FVector C3(-HalfX + Inset,  HalfY - Inset, -Inset);           // Top-back-left
	const FVector C4(-HalfX + Inset, -HalfY + Inset, -TotalZ + Inset);  // Bottom-front-left
	const FVector C5( HalfX - Inset, -HalfY + Inset, -TotalZ + Inset);  // Bottom-front-right
	const FVector C6( HalfX - Inset,  HalfY - Inset, -TotalZ + Inset);  // Bottom-back-right
	const FVector C7(-HalfX + Inset,  HalfY - Inset, -TotalZ + Inset);  // Bottom-back-left

	// Generate 6 faces (24 vertices, 12 triangles).
	// Each face has 4 verts and 2 tris.

	auto AddQuad = [&](const FVector& V0, const FVector& V1,
	                    const FVector& V2, const FVector& V3,
	                    const FVector& Normal)
	{
		const int32 Base = OutVerts.Num();

		OutVerts.Add(V0);
		OutVerts.Add(V1);
		OutVerts.Add(V2);
		OutVerts.Add(V3);

		OutNormals.Add(Normal);
		OutNormals.Add(Normal);
		OutNormals.Add(Normal);
		OutNormals.Add(Normal);

		OutUVs.Add(FVector2D(0.0f, 0.0f));
		OutUVs.Add(FVector2D(1.0f, 0.0f));
		OutUVs.Add(FVector2D(1.0f, 1.0f));
		OutUVs.Add(FVector2D(0.0f, 1.0f));

		// Two triangles: 0-1-2, 0-2-3
		OutTris.Add(Base + 0);
		OutTris.Add(Base + 1);
		OutTris.Add(Base + 2);

		OutTris.Add(Base + 0);
		OutTris.Add(Base + 2);
		OutTris.Add(Base + 3);
	};

	OutVerts.Reserve(24);
	OutTris.Reserve(36);
	OutNormals.Reserve(24);
	OutUVs.Reserve(24);

	// Top face (Z up)
	AddQuad(C0, C1, C2, C3, FVector::UpVector);

	// Bottom face (Z down)
	AddQuad(C7, C6, C5, C4, -FVector::UpVector);

	// Front face (-Y)
	AddQuad(C4, C5, C1, C0, -FVector::ForwardVector);

	// Back face (+Y)
	AddQuad(C3, C2, C6, C7, FVector::ForwardVector);

	// Left face (-X)
	AddQuad(C4, C0, C3, C7, -FVector::RightVector);

	// Right face (+X)
	AddQuad(C1, C5, C6, C2, FVector::RightVector);
}

// ===================================================================
// Material / animation
// ===================================================================

void ADigGhostActor::UpdateMaterialState(float DeltaTime)
{
	if (!GhostMaterial)
	{
		return;
	}

	// Pulse animation.
	PulseTimer += DeltaTime * PulseFrequency * 2.0f * PI;
	if (PulseTimer > 2.0f * PI)
	{
		PulseTimer -= 2.0f * PI;
	}

	// Sinusoidal pulse between PulseMinAlpha and PulseMaxAlpha.
	const float PulseT = (FMath::Sin(PulseTimer) + 1.0f) * 0.5f; // 0..1
	const float Alpha = FMath::Lerp(PulseMinAlpha, PulseMaxAlpha, PulseT);

	const FLinearColor BaseColor = GetColorForState();
	GhostMaterial->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
	GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Alpha);
}

FLinearColor ADigGhostActor::GetColorForState() const
{
	switch (CurrentState)
	{
	case EDigGhostState::Valid:
		return ValidColor;

	case EDigGhostState::Invalid:
		return InvalidColor;

	case EDigGhostState::ResourceDetected:
		return ResourceColor;

	case EDigGhostState::Hidden:
	default:
		return FLinearColor::Transparent;
	}
}
