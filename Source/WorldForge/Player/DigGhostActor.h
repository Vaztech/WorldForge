// DigGhostActor.h
// WorldForge — Phase 1: Digging Foundation (v5.0 SDF Brush Path)
// Visual preview actor showing where a dig operation will occur.
// Mirrors the BuildGhostActor pattern — spawned and driven by BuildModeManager.
// Renders a wireframe volume at the dig target with shape/depth/validity color coding.
//
// v5.0: Supports sphere, cylinder, and box brush shape previews to match
// the actual SDF brush that will be applied by DigSubsystem.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Player/DigResult.h"
#include "DigGhostActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UDigSubsystem;
class UResourceSpawnSubsystem;
class UPlayerBuildContext;

/**
 * Visual states for the dig ghost.
 * Controls wireframe color and opacity to communicate dig validity.
 */
UENUM(BlueprintType)
enum class EDigGhostState : uint8
{
	/** Target is valid — can dig here. Green wireframe. */
	Valid,

	/** Target is invalid — air, out of bounds, bedrock. Red wireframe. */
	Invalid,

	/** Dig would reveal a resource node. Gold wireframe (Phase 2). */
	ResourceDetected,

	/** Ghost is hidden (not in dig mode). */
	Hidden
};

/**
 * Actor that renders a translucent wireframe volume at the planned dig site.
 *
 * v5.0 changes:
 * - Supports sphere, cylinder, and box preview shapes via EDigBrushShape.
 * - Geometry regenerated when shape, radius, or depth changes.
 * - Shape matches the actual SDF brush DigSubsystem will apply.
 *
 * Lifecycle:
 * - Spawned by BuildModeManager when entering Dig tool mode.
 * - Updated every frame with the current dig target from PlayerBuildContext.
 * - Destroyed when leaving Dig tool mode.
 */
UCLASS(NotBlueprintable)
class WORLDFORGE_API ADigGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ADigGhostActor();

	// -------------------------------------------------------------------
	// Public API (called by BuildModeManager)
	// -------------------------------------------------------------------

	/**
	 * Update the ghost position and volume to reflect the current dig target.
	 *
	 * @param WorldPosition   World-space center of the dig volume.
	 * @param GridPosition    Grid-space center (used for validity checks).
	 * @param Radius          Dig radius in grid cells (1 = single block).
	 * @param DepthPerAction  How many layers the dig will remove.
	 * @param BrushShape      SDF brush shape to preview.
	 */
	void UpdateGhost(const FVector& WorldPosition, const FIntVector& GridPosition,
	                 int32 Radius, int32 DepthPerAction,
	                 EDigBrushShape BrushShape = EDigBrushShape::Box);

	/**
	 * Set the visual state (valid/invalid/resource/hidden).
	 * Drives wireframe color.
	 */
	void SetGhostState(EDigGhostState NewState);

	/** Get current ghost state. */
	EDigGhostState GetGhostState() const { return CurrentState; }

	/** Show or hide the ghost actor entirely. */
	void SetGhostVisible(bool bVisible);

	/** Returns the grid position the ghost is currently snapped to. */
	FIntVector GetCurrentGridPosition() const { return CurrentGridPos; }

	/** Returns the current brush shape being previewed. */
	EDigBrushShape GetCurrentBrushShape() const { return CurrentBrushShape; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// -------------------------------------------------------------------
	// Components
	// -------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> WireframeMesh;

	// -------------------------------------------------------------------
	// Visual state
	// -------------------------------------------------------------------

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	EDigGhostState CurrentState = EDigGhostState::Hidden;
	EDigBrushShape CurrentBrushShape = EDigBrushShape::Box;

	FIntVector CurrentGridPos = FIntVector::ZeroValue;
	int32 CurrentRadius = 1;
	int32 CurrentDepth = 1;

	float PulseTimer = 0.0f;

	// -------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor ValidColor = FLinearColor(0.1f, 0.9f, 0.2f, 0.6f);

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor InvalidColor = FLinearColor(0.9f, 0.1f, 0.1f, 0.6f);

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor ResourceColor = FLinearColor(1.0f, 0.85f, 0.0f, 0.7f);

	/**
	 * World-space size of a single grid cell.
	 * Must match VoxelWorldManager's grid cell size.
	 * TODO: Pull this from VoxelWorldManager or WorldForgeTerrainSettings at runtime.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Grid")
	float GridCellSize = 100.0f;

	/** Segments used for sphere and cylinder approximations. Higher = smoother. */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Grid")
	int32 CircleSegments = 24;

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseFrequency = 1.5f;

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseMinAlpha = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseMaxAlpha = 0.8f;

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Material")
	TObjectPtr<UMaterialInterface> BaseMaterial;

	// -------------------------------------------------------------------
	// Resource detection
	// -------------------------------------------------------------------

	/**
	 * Detection radius in world units. When the ghost is within this
	 * distance of a hidden resource node, the ghost transitions to
	 * ResourceDetected state (gold wireframe).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|ResourceDetection")
	float ResourceDetectionRadius = 500.0f;

	/**
	 * How often (in seconds) to query ResourceSpawnSubsystem for nearby nodes.
	 * Avoids running proximity queries every frame.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|ResourceDetection")
	float ResourceCheckInterval = 0.15f;

	/** Pulse speed multiplier when in ResourceDetected state (faster = more urgent). */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|ResourceDetection")
	float ResourcePulseFrequency = 3.0f;

	/** Accumulated time since last resource proximity check. */
	float ResourceCheckTimer = 0.0f;

	/** Cached ref to ResourceSpawnSubsystem (lazy-resolved). */
	UPROPERTY()
	TObjectPtr<UResourceSpawnSubsystem> CachedResourceSpawn;

	/** Resolve and cache ResourceSpawnSubsystem. Returns nullptr if unavailable. */
	UResourceSpawnSubsystem* GetResourceSpawnSubsystem();

	/** Check nearby resource nodes and auto-transition to ResourceDetected if found. */
	void CheckForNearbyResources(float DeltaTime);

	// -------------------------------------------------------------------
	// X-Ray resource outlines (Batch 3)
	//
	// When PlayerBuildContext::bXRayResourcesActive is true (Ctrl held
	// in Dig tool mode), renders wireframe sphere outlines at the
	// positions of hidden resource nodes within ResourceDetectionRadius.
	// Uses a second ProceduralMeshComponent so the outlines don't
	// interfere with the main dig ghost wireframe.
	// -------------------------------------------------------------------

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> XRayMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> XRayMaterial;

	/** Cached ref to PlayerBuildContext (lazy-resolved). */
	UPROPERTY()
	TObjectPtr<UPlayerBuildContext> CachedBuildContext;

	/** Whether X-ray outlines are currently being rendered. */
	bool bXRayOutlinesActive = false;

	/** Resolve and cache PlayerBuildContext. Returns nullptr if unavailable. */
	UPlayerBuildContext* GetPlayerBuildContext();

	/** Rebuild X-ray wireframe spheres at nearby hidden node positions. */
	void RebuildXRayOutlines();

	/** Clear X-ray wireframe geometry. */
	void ClearXRayOutlines();

	/** Generate a small wireframe sphere for a single node outline. */
	void GenerateXRaySphere(const FVector& LocalCenter, float Radius,
	                        TArray<FVector>& OutVerts,
	                        TArray<int32>& OutTris,
	                        TArray<FVector>& OutNormals,
	                        TArray<FVector2D>& OutUVs) const;

	// -------------------------------------------------------------------
	// Internal methods
	// -------------------------------------------------------------------

	/** Rebuild geometry to match current shape, radius, and depth. */
	void RebuildWireframeGeometry();

	/** Generate a box wireframe. */
	void GenerateBoxWireframe(int32 Width, int32 Height, int32 Depth,
	                          TArray<FVector>& OutVerts,
	                          TArray<int32>& OutTris,
	                          TArray<FVector>& OutNormals,
	                          TArray<FVector2D>& OutUVs) const;

	/** Generate a sphere wireframe (icosphere approximation). */
	void GenerateSphereWireframe(float Radius,
	                             TArray<FVector>& OutVerts,
	                             TArray<int32>& OutTris,
	                             TArray<FVector>& OutNormals,
	                             TArray<FVector2D>& OutUVs) const;

	/** Generate a cylinder wireframe. */
	void GenerateCylinderWireframe(float Radius, float HalfHeight,
	                               TArray<FVector>& OutVerts,
	                               TArray<int32>& OutTris,
	                               TArray<FVector>& OutNormals,
	                               TArray<FVector2D>& OutUVs) const;

	void UpdateMaterialState(float DeltaTime);
	FLinearColor GetColorForState() const;
};
