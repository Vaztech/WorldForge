// DigGhostActor.h
// WorldForge — Phase 1: Digging Foundation
// Visual preview actor showing where a dig operation will occur.
// Mirrors the BuildGhostActor pattern — spawned and driven by BuildModeManager.
// Renders a wireframe volume at the dig target with depth/validity color coding.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DigGhostActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UDigSubsystem;

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
 * Lifecycle:
 * - Spawned by BuildModeManager when entering Dig tool mode.
 * - Updated every frame with the current dig target from PlayerBuildContext.
 * - Destroyed when leaving Dig tool mode.
 *
 * Visual:
 * - Procedural mesh generating a wireframe box matching the dig volume.
 * - Color-coded by validity state.
 * - Snaps to grid positions (same grid as BuildGhostActor).
 * - Pulses subtly to distinguish from static terrain.
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
	 */
	void UpdateGhost(const FVector& WorldPosition, const FIntVector& GridPosition,
	                 int32 Radius, int32 DepthPerAction);

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

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	// -------------------------------------------------------------------
	// Components
	// -------------------------------------------------------------------

	/** Root scene component. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> RootScene;

	/**
	 * Procedural mesh used to render the wireframe dig volume.
	 * We use a ProceduralMeshComponent so we can rebuild geometry
	 * when the dig radius or depth changes without swapping meshes.
	 */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UProceduralMeshComponent> WireframeMesh;

	// -------------------------------------------------------------------
	// Visual state
	// -------------------------------------------------------------------

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> GhostMaterial;

	EDigGhostState CurrentState = EDigGhostState::Hidden;

	FIntVector CurrentGridPos = FIntVector::ZeroValue;
	int32 CurrentRadius = 1;
	int32 CurrentDepth = 1;

	/** Elapsed time for pulse animation. */
	float PulseTimer = 0.0f;

	// -------------------------------------------------------------------
	// Configuration
	// -------------------------------------------------------------------

	/** Wireframe color when dig target is valid. */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor ValidColor = FLinearColor(0.1f, 0.9f, 0.2f, 0.6f);

	/** Wireframe color when dig target is invalid. */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor InvalidColor = FLinearColor(0.9f, 0.1f, 0.1f, 0.6f);

	/** Wireframe color when a resource node is detected at this depth. */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Colors")
	FLinearColor ResourceColor = FLinearColor(1.0f, 0.85f, 0.0f, 0.7f);

	/**
	 * World-space size of a single grid cell.
	 * Must match VoxelWorldManager's grid cell size.
	 * TODO: Pull this from VoxelWorldManager or WorldForgeTerrainSettings at runtime.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Grid")
	float GridCellSize = 100.0f;

	/** Pulse speed (cycles per second). */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseFrequency = 1.5f;

	/** Pulse opacity range (min/max multiplier on base alpha). */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseMinAlpha = 0.4f;

	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Animation")
	float PulseMaxAlpha = 0.8f;

	/** Base material to create the dynamic instance from. Should be translucent + wireframe. */
	UPROPERTY(EditDefaultsOnly, Category = "DigGhost|Material")
	TObjectPtr<UMaterialInterface> BaseMaterial;

	// -------------------------------------------------------------------
	// Internal methods
	// -------------------------------------------------------------------

	/**
	 * Rebuild the procedural mesh geometry to match current radius and depth.
	 * Called when radius or depth changes (not every frame).
	 */
	void RebuildWireframeGeometry();

	/**
	 * Generate wireframe box vertices and triangles for a volume
	 * of the given dimensions (in grid cells).
	 *
	 * @param Width      Number of cells in X.
	 * @param Height     Number of cells in Y.
	 * @param Depth      Number of cells in Z (downward).
	 * @param OutVerts   Output vertex positions (local space, centered).
	 * @param OutTris    Output triangle indices.
	 * @param OutNormals Output normals.
	 * @param OutUVs     Output UVs.
	 */
	void GenerateBoxWireframe(int32 Width, int32 Height, int32 Depth,
	                          TArray<FVector>& OutVerts,
	                          TArray<int32>& OutTris,
	                          TArray<FVector>& OutNormals,
	                          TArray<FVector2D>& OutUVs) const;

	/** Update the material color based on CurrentState and pulse timer. */
	void UpdateMaterialState(float DeltaTime);

	/** Get the base color for the current state. */
	FLinearColor GetColorForState() const;
};
