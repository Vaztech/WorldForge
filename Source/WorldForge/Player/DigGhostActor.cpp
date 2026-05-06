// DigGhostActor.cpp
// WorldForge — Phase 1: Digging Foundation (v5.0 SDF Brush Path)

#include "Player/DigGhostActor.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Mining/ResourceSpawnSubsystem.h"
#include "Player/PlayerBuildContext.h"
#include "Engine/World.h"

ADigGhostActor::ADigGhostActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);

	WireframeMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WireframeMesh"));
	WireframeMesh->SetupAttachment(RootScene);
	WireframeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WireframeMesh->SetCastShadow(false);
	WireframeMesh->bUseAsyncCooking = false;

	XRayMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("XRayMesh"));
	XRayMesh->SetupAttachment(RootScene);
	XRayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	XRayMesh->SetCastShadow(false);
	XRayMesh->bUseAsyncCooking = false;
	XRayMesh->SetAbsolute(true, false, false); // absolute position so outlines stay at node world positions

	SetCanBeDamaged(false);
	bReplicates = false;
}

void ADigGhostActor::BeginPlay()
{
	Super::BeginPlay();

	WireframeMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	WireframeMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	WireframeMesh->SetGenerateOverlapEvents(false);

	WireframeMesh->SetCastShadow(false);
	WireframeMesh->bCastDynamicShadow = false;
	WireframeMesh->bCastStaticShadow = false;

	if (BaseMaterial)
	{
		GhostMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		WireframeMesh->SetMaterial(0, GhostMaterial);

		// X-ray uses the same base material with a distinct cyan color.
		XRayMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		XRayMaterial->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.0f, 0.85f, 1.0f, 0.5f));
		XRayMaterial->SetScalarParameterValue(TEXT("Opacity"), 0.5f);
		XRayMesh->SetMaterial(0, XRayMaterial);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DigGhostActor] No BaseMaterial assigned — hiding dig ghost mesh. "
				"Assign a translucent/wireframe material in defaults."));
		WireframeMesh->SetVisibility(false, true);
	}

	SetGhostVisible(false);
	SetGhostState(EDigGhostState::Hidden);
	RebuildWireframeGeometry();
}

void ADigGhostActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (CurrentState == EDigGhostState::Hidden)
	{
		return;
	}

	// ── Resource Detection: when ghost is in Valid state, periodically check
	// for nearby hidden resource nodes and auto-transition to ResourceDetected.
	if (CurrentState == EDigGhostState::Valid || CurrentState == EDigGhostState::ResourceDetected)
	{
		CheckForNearbyResources(DeltaTime);
	}

	// ── X-Ray outlines: when Ctrl is held (bXRayResourcesActive), render
	// wireframe spheres at nearby hidden node positions through terrain.
	{
		UPlayerBuildContext* BuildCtx = GetPlayerBuildContext();
		const bool bWantXRay = BuildCtx && BuildCtx->bXRayResourcesActive
			&& (CurrentState != EDigGhostState::Hidden);

		if (bWantXRay && !bXRayOutlinesActive)
		{
			RebuildXRayOutlines();
			bXRayOutlinesActive = true;
		}
		else if (bWantXRay && bXRayOutlinesActive)
		{
			// Rebuild periodically — reuse the resource check timer cadence.
			// The outlines were already built; only refresh when the resource
			// check fires (every ResourceCheckInterval seconds) so we don't
			// rebuild geometry every frame.
			if (ResourceCheckTimer < KINDA_SMALL_NUMBER)
			{
				RebuildXRayOutlines();
			}
		}
		else if (!bWantXRay && bXRayOutlinesActive)
		{
			ClearXRayOutlines();
			bXRayOutlinesActive = false;
		}
	}

	UpdateMaterialState(DeltaTime);
}

// ===================================================================
// Public API
// ===================================================================

void ADigGhostActor::UpdateGhost(const FVector& WorldPosition,
	const FIntVector& GridPosition,
	int32 Radius, int32 DepthPerAction,
	EDigBrushShape BrushShape)
{
	const FVector SnappedPos = FVector(
		FMath::RoundToFloat(WorldPosition.X / GridCellSize) * GridCellSize,
		FMath::RoundToFloat(WorldPosition.Y / GridCellSize) * GridCellSize,
		FMath::RoundToFloat(WorldPosition.Z / GridCellSize) * GridCellSize
	);

	SetActorLocation(SnappedPos);
	CurrentGridPos = GridPosition;

	const bool bDimensionsChanged =
		(Radius != CurrentRadius) ||
		(DepthPerAction != CurrentDepth) ||
		(BrushShape != CurrentBrushShape);

	CurrentRadius = FMath::Max(Radius, 1);
	CurrentDepth = FMath::Max(DepthPerAction, 1);
	CurrentBrushShape = BrushShape;

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

	if (GhostMaterial)
	{
		const FLinearColor Color = GetColorForState();
		GhostMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
		GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Color.A);
	}
}

void ADigGhostActor::SetGhostVisible(bool bVisible)
{
	const bool bCanRender = bVisible && GhostMaterial != nullptr;

	SetActorHiddenInGame(!bCanRender);
	SetActorTickEnabled(bVisible);

	if (WireframeMesh)
	{
		WireframeMesh->SetVisibility(bCanRender, true);
	}

	// Clear X-ray outlines when hiding the ghost entirely.
	if (!bVisible && bXRayOutlinesActive)
	{
		ClearXRayOutlines();
		bXRayOutlinesActive = false;
	}
}

// ===================================================================
// Wireframe geometry generation — shape router
// ===================================================================

void ADigGhostActor::RebuildWireframeGeometry()
{
	WireframeMesh->ClearAllMeshSections();

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	const int32 Width = (CurrentRadius * 2) - 1;
	const int32 Height = Width;
	const int32 Depth = CurrentDepth;

	switch (CurrentBrushShape)
	{
	case EDigBrushShape::Box:
		GenerateBoxWireframe(Width, Height, Depth, Vertices, Triangles, Normals, UVs);
		break;

	case EDigBrushShape::Sphere:
	{
		// Sphere radius = max of XY extent and depth extent (matches ComputeBrushParameters).
		const float XYExtent = (Width * GridCellSize) * 0.5f;
		const float ZExtent = (Depth * GridCellSize) * 0.5f;
		const float Radius = FMath::Max(XYExtent, ZExtent);
		GenerateSphereWireframe(Radius, Vertices, Triangles, Normals, UVs);
		break;
	}

	case EDigBrushShape::Cylinder:
	{
		const float CylRadius = (Width * GridCellSize) * 0.5f;
		const float CylHalfHeight = (Depth * GridCellSize) * 0.5f;
		GenerateCylinderWireframe(CylRadius, CylHalfHeight, Vertices, Triangles, Normals, UVs);
		break;
	}
	}

	if (Vertices.Num() > 0 && Triangles.Num() > 0)
	{
		TArray<FColor> VertexColors;
		VertexColors.Init(FColor::White, Vertices.Num());

		WireframeMesh->CreateMeshSection(
			0, Vertices, Triangles, Normals, UVs, VertexColors,
			TArray<FProcMeshTangent>(), false);

		if (GhostMaterial)
		{
			WireframeMesh->SetMaterial(0, GhostMaterial);
		}
	}
}

// ===================================================================
// Box wireframe (original implementation, preserved)
// ===================================================================

void ADigGhostActor::GenerateBoxWireframe(
	int32 Width, int32 Height, int32 Depth,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs) const
{
	const float HalfX = (Width * GridCellSize) * 0.5f;
	const float HalfY = (Height * GridCellSize) * 0.5f;
	const float TotalZ = Depth * GridCellSize;

	const float Inset = GridCellSize * 0.02f;

	const FVector C0(-HalfX + Inset, -HalfY + Inset, -Inset);
	const FVector C1(HalfX - Inset, -HalfY + Inset, -Inset);
	const FVector C2(HalfX - Inset, HalfY - Inset, -Inset);
	const FVector C3(-HalfX + Inset, HalfY - Inset, -Inset);
	const FVector C4(-HalfX + Inset, -HalfY + Inset, -TotalZ + Inset);
	const FVector C5(HalfX - Inset, -HalfY + Inset, -TotalZ + Inset);
	const FVector C6(HalfX - Inset, HalfY - Inset, -TotalZ + Inset);
	const FVector C7(-HalfX + Inset, HalfY - Inset, -TotalZ + Inset);

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

	AddQuad(C0, C1, C2, C3, FVector::UpVector);           // Top
	AddQuad(C7, C6, C5, C4, -FVector::UpVector);          // Bottom
	AddQuad(C4, C5, C1, C0, -FVector::ForwardVector);     // Front
	AddQuad(C3, C2, C6, C7, FVector::ForwardVector);      // Back
	AddQuad(C4, C0, C3, C7, -FVector::RightVector);       // Left
	AddQuad(C1, C5, C6, C2, FVector::RightVector);        // Right
}

// ===================================================================
// Sphere wireframe (UV sphere for translucent preview)
// ===================================================================

void ADigGhostActor::GenerateSphereWireframe(
	float Radius,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs) const
{
	// UV sphere with CircleSegments longitude and half that for latitude.
	// Centered at local origin, offset downward by Radius so the top
	// of the sphere sits at Z=0 (consistent with box ghost placement).
	const int32 LongSegs = FMath::Max(CircleSegments, 8);
	const int32 LatSegs = FMath::Max(LongSegs / 2, 4);

	const FVector Offset(0.0f, 0.0f, -Radius);

	const int32 NumVerts = (LatSegs + 1) * (LongSegs + 1);
	OutVerts.Reserve(NumVerts);
	OutNormals.Reserve(NumVerts);
	OutUVs.Reserve(NumVerts);

	for (int32 Lat = 0; Lat <= LatSegs; ++Lat)
	{
		const float Theta = PI * static_cast<float>(Lat) / static_cast<float>(LatSegs);
		const float SinTheta = FMath::Sin(Theta);
		const float CosTheta = FMath::Cos(Theta);

		for (int32 Lon = 0; Lon <= LongSegs; ++Lon)
		{
			const float Phi = 2.0f * PI * static_cast<float>(Lon) / static_cast<float>(LongSegs);
			const float SinPhi = FMath::Sin(Phi);
			const float CosPhi = FMath::Cos(Phi);

			const FVector Normal(SinTheta * CosPhi, SinTheta * SinPhi, CosTheta);
			OutVerts.Add(Normal * Radius + Offset);
			OutNormals.Add(Normal);
			OutUVs.Add(FVector2D(
				static_cast<float>(Lon) / static_cast<float>(LongSegs),
				static_cast<float>(Lat) / static_cast<float>(LatSegs)));
		}
	}

	// Triangles.
	OutTris.Reserve(LatSegs * LongSegs * 6);
	for (int32 Lat = 0; Lat < LatSegs; ++Lat)
	{
		for (int32 Lon = 0; Lon < LongSegs; ++Lon)
		{
			const int32 A = Lat * (LongSegs + 1) + Lon;
			const int32 B = A + (LongSegs + 1);

			OutTris.Add(A);
			OutTris.Add(B);
			OutTris.Add(A + 1);

			OutTris.Add(A + 1);
			OutTris.Add(B);
			OutTris.Add(B + 1);
		}
	}
}

// ===================================================================
// Cylinder wireframe (cap + barrel for shaft preview)
// ===================================================================

void ADigGhostActor::GenerateCylinderWireframe(
	float Radius, float HalfHeight,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs) const
{
	// Cylinder centered at local origin, extending from Z=0 (top) to
	// Z = -2*HalfHeight (bottom), matching the box ghost convention.
	const int32 Segs = FMath::Max(CircleSegments, 8);

	const float TopZ = 0.0f;
	const float BottomZ = -2.0f * HalfHeight;

	// ------------------------------------------------------------------
	// 1. Barrel (side faces)
	// ------------------------------------------------------------------
	const int32 BarrelBase = OutVerts.Num();

	for (int32 i = 0; i <= Segs; ++i)
	{
		const float Angle = 2.0f * PI * static_cast<float>(i) / static_cast<float>(Segs);
		const float CosA = FMath::Cos(Angle);
		const float SinA = FMath::Sin(Angle);

		const FVector Normal(CosA, SinA, 0.0f);
		const float U = static_cast<float>(i) / static_cast<float>(Segs);

		// Top ring vertex.
		OutVerts.Add(FVector(CosA * Radius, SinA * Radius, TopZ));
		OutNormals.Add(Normal);
		OutUVs.Add(FVector2D(U, 0.0f));

		// Bottom ring vertex.
		OutVerts.Add(FVector(CosA * Radius, SinA * Radius, BottomZ));
		OutNormals.Add(Normal);
		OutUVs.Add(FVector2D(U, 1.0f));
	}

	for (int32 i = 0; i < Segs; ++i)
	{
		const int32 T0 = BarrelBase + i * 2;
		const int32 B0 = T0 + 1;
		const int32 T1 = T0 + 2;
		const int32 B1 = T0 + 3;

		OutTris.Add(T0); OutTris.Add(B0); OutTris.Add(T1);
		OutTris.Add(T1); OutTris.Add(B0); OutTris.Add(B1);
	}

	// ------------------------------------------------------------------
	// 2. Top cap (fan from center)
	// ------------------------------------------------------------------
	auto AddCap = [&](float Z, const FVector& CapNormal)
		{
			const int32 CenterIdx = OutVerts.Num();
			OutVerts.Add(FVector(0.0f, 0.0f, Z));
			OutNormals.Add(CapNormal);
			OutUVs.Add(FVector2D(0.5f, 0.5f));

			const int32 RingStart = OutVerts.Num();
			for (int32 i = 0; i <= Segs; ++i)
			{
				const float Angle = 2.0f * PI * static_cast<float>(i) / static_cast<float>(Segs);
				const float CosA = FMath::Cos(Angle);
				const float SinA = FMath::Sin(Angle);

				OutVerts.Add(FVector(CosA * Radius, SinA * Radius, Z));
				OutNormals.Add(CapNormal);
				OutUVs.Add(FVector2D(CosA * 0.5f + 0.5f, SinA * 0.5f + 0.5f));
			}

			for (int32 i = 0; i < Segs; ++i)
			{
				if (CapNormal.Z > 0.0f)
				{
					// Top cap: CCW when viewed from above.
					OutTris.Add(CenterIdx);
					OutTris.Add(RingStart + i);
					OutTris.Add(RingStart + i + 1);
				}
				else
				{
					// Bottom cap: CW when viewed from above.
					OutTris.Add(CenterIdx);
					OutTris.Add(RingStart + i + 1);
					OutTris.Add(RingStart + i);
				}
			}
		};

	AddCap(TopZ, FVector::UpVector);
	AddCap(BottomZ, -FVector::UpVector);
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

	// Use faster pulse frequency when a resource node is detected nearby.
	const float EffectivePulseFreq = (CurrentState == EDigGhostState::ResourceDetected)
		? ResourcePulseFrequency
		: PulseFrequency;

	PulseTimer += DeltaTime * EffectivePulseFreq * 2.0f * PI;
	if (PulseTimer > 2.0f * PI)
	{
		PulseTimer -= 2.0f * PI;
	}

	const float PulseT = (FMath::Sin(PulseTimer) + 1.0f) * 0.5f;
	const float Alpha = FMath::Lerp(PulseMinAlpha, PulseMaxAlpha, PulseT);

	const FLinearColor BaseColor = GetColorForState();
	GhostMaterial->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
	GhostMaterial->SetScalarParameterValue(TEXT("Opacity"), Alpha);
}

FLinearColor ADigGhostActor::GetColorForState() const
{
	switch (CurrentState)
	{
	case EDigGhostState::Valid:            return ValidColor;
	case EDigGhostState::Invalid:          return InvalidColor;
	case EDigGhostState::ResourceDetected: return ResourceColor;
	case EDigGhostState::Hidden:
	default:                               return FLinearColor::Transparent;
	}
}

// ===================================================================
// Resource detection
// ===================================================================

UResourceSpawnSubsystem* ADigGhostActor::GetResourceSpawnSubsystem()
{
	if (CachedResourceSpawn)
	{
		return CachedResourceSpawn;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		CachedResourceSpawn = World->GetSubsystem<UResourceSpawnSubsystem>();
	}

	return CachedResourceSpawn;
}

void ADigGhostActor::CheckForNearbyResources(float DeltaTime)
{
	ResourceCheckTimer += DeltaTime;

	if (ResourceCheckTimer < ResourceCheckInterval)
	{
		return;
	}

	ResourceCheckTimer = 0.0f;

	UResourceSpawnSubsystem* ResourceSpawn = GetResourceSpawnSubsystem();
	if (!ResourceSpawn)
	{
		// No subsystem available — stay in current state.
		// If we were ResourceDetected without a subsystem, fall back to Valid.
		if (CurrentState == EDigGhostState::ResourceDetected)
		{
			SetGhostState(EDigGhostState::Valid);
		}
		return;
	}

	const FVector GhostWorldPos = GetActorLocation();
	const bool bNearResource = ResourceSpawn->HasNodeInRadius(GhostWorldPos, ResourceDetectionRadius);

	if (bNearResource && CurrentState == EDigGhostState::Valid)
	{
		SetGhostState(EDigGhostState::ResourceDetected);
	}
	else if (!bNearResource && CurrentState == EDigGhostState::ResourceDetected)
	{
		SetGhostState(EDigGhostState::Valid);
	}
}

// ===================================================================
// X-Ray resource outlines (Batch 3)
// ===================================================================

UPlayerBuildContext* ADigGhostActor::GetPlayerBuildContext()
{
	if (CachedBuildContext)
	{
		return CachedBuildContext;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		CachedBuildContext = World->GetSubsystem<UPlayerBuildContext>();
	}

	return CachedBuildContext;
}

void ADigGhostActor::RebuildXRayOutlines()
{
	if (!XRayMesh)
	{
		return;
	}

	XRayMesh->ClearAllMeshSections();

	UResourceSpawnSubsystem* ResourceSpawn = GetResourceSpawnSubsystem();
	if (!ResourceSpawn)
	{
		return;
	}

	const FVector GhostWorldPos = GetActorLocation();
	TArray<FSpawnedResourceNode> NearbyNodes = ResourceSpawn->GetNodesInRadius(
		GhostWorldPos, ResourceDetectionRadius);

	if (NearbyNodes.Num() == 0)
	{
		return;
	}

	TArray<FVector> AllVerts;
	TArray<int32> AllTris;
	TArray<FVector> AllNormals;
	TArray<FVector2D> AllUVs;

	// Reserve rough estimates.
	constexpr int32 VertsPerSphere = 100;
	constexpr int32 TrisPerSphere = 150;
	AllVerts.Reserve(NearbyNodes.Num() * VertsPerSphere);
	AllTris.Reserve(NearbyNodes.Num() * TrisPerSphere);
	AllNormals.Reserve(NearbyNodes.Num() * VertsPerSphere);
	AllUVs.Reserve(NearbyNodes.Num() * VertsPerSphere);

	constexpr float SphereRadius = 75.0f; // World units — slightly smaller than a voxel.

	for (const FSpawnedResourceNode& Node : NearbyNodes)
	{
		// Only show hidden (non-exposed) nodes — that's the whole point
		// of X-ray vision. Already-exposed nodes are visible on the surface.
		if (Node.bExposed || Node.bDepleted)
		{
			continue;
		}

		// XRayMesh uses absolute world positioning, so pass world-space center.
		GenerateXRaySphere(Node.WorldPosition, SphereRadius,
			AllVerts, AllTris, AllNormals, AllUVs);
	}

	if (AllVerts.Num() > 0 && AllTris.Num() > 0)
	{
		TArray<FColor> VertexColors;
		VertexColors.Init(FColor::White, AllVerts.Num());

		XRayMesh->CreateMeshSection(
			0, AllVerts, AllTris, AllNormals, AllUVs, VertexColors,
			TArray<FProcMeshTangent>(), false);

		if (XRayMaterial)
		{
			XRayMesh->SetMaterial(0, XRayMaterial);
		}

		XRayMesh->SetVisibility(true, true);

		// Enable rendering through occluding geometry via custom depth stencil.
		XRayMesh->SetRenderCustomDepth(true);
		XRayMesh->SetCustomDepthStencilValue(1);
	}
}

void ADigGhostActor::ClearXRayOutlines()
{
	if (XRayMesh)
	{
		XRayMesh->ClearAllMeshSections();
		XRayMesh->SetVisibility(false, true);
		XRayMesh->SetRenderCustomDepth(false);
	}
}

void ADigGhostActor::GenerateXRaySphere(
	const FVector& WorldCenter, float Radius,
	TArray<FVector>& OutVerts,
	TArray<int32>& OutTris,
	TArray<FVector>& OutNormals,
	TArray<FVector2D>& OutUVs) const
{
	// Low-poly UV sphere (8 segments) — enough to read as a sphere outline.
	constexpr int32 LongSegs = 8;
	constexpr int32 LatSegs = 6;

	const int32 BaseVertIndex = OutVerts.Num();

	for (int32 Lat = 0; Lat <= LatSegs; ++Lat)
	{
		const float Theta = PI * static_cast<float>(Lat) / static_cast<float>(LatSegs);
		const float SinTheta = FMath::Sin(Theta);
		const float CosTheta = FMath::Cos(Theta);

		for (int32 Lon = 0; Lon <= LongSegs; ++Lon)
		{
			const float Phi = 2.0f * PI * static_cast<float>(Lon) / static_cast<float>(LongSegs);
			const float SinPhi = FMath::Sin(Phi);
			const float CosPhi = FMath::Cos(Phi);

			const FVector Normal(SinTheta * CosPhi, SinTheta * SinPhi, CosTheta);
			OutVerts.Add(Normal * Radius + WorldCenter);
			OutNormals.Add(Normal);
			OutUVs.Add(FVector2D(
				static_cast<float>(Lon) / static_cast<float>(LongSegs),
				static_cast<float>(Lat) / static_cast<float>(LatSegs)));
		}
	}

	for (int32 Lat = 0; Lat < LatSegs; ++Lat)
	{
		for (int32 Lon = 0; Lon < LongSegs; ++Lon)
		{
			const int32 A = BaseVertIndex + Lat * (LongSegs + 1) + Lon;
			const int32 B = A + (LongSegs + 1);

			OutTris.Add(A);
			OutTris.Add(B);
			OutTris.Add(A + 1);

			OutTris.Add(A + 1);
			OutTris.Add(B);
			OutTris.Add(B + 1);
		}
	}
}