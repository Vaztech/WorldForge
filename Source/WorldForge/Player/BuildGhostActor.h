// Source/WorldForge/Player/BuildGhostActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BuildModeTypes.h"
#include "BuildGhostActor.generated.h"

class UStaticMeshComponent;
class UDecalComponent;
class UMaterialInstanceDynamic;
class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API ABuildGhostActor : public AActor
{
	GENERATED_BODY()

public:
	ABuildGhostActor();

	// Sets the visual state of the ghost (green = valid, red = invalid, hidden)
	void SetGhostState(EBuildGhostState State);

	// Prefab mode: move ghost mesh to world position with rotation
	void UpdatePrefabGhost(FVector WorldPosition, int32 RotationIndex);

	// Custom draw mode: highlight selected cells + cursor cell
	void UpdateCustomDrawCells(
		const TArray<FIntPoint>& SelectedCells,
		FIntPoint CursorCell,
		UVoxelWorldManager* VoxelManager);

	// Remove all cell highlights
	void ClearAllHighlights();

protected:
	virtual void BeginPlay() override;

private:

	// ================================
	// Prefab ghost mesh
	// ================================

	UPROPERTY(VisibleAnywhere)
	UStaticMeshComponent* GhostMesh;

	UPROPERTY()
	UMaterialInstanceDynamic* GhostMaterial;

	// ================================
	// Custom draw cell highlights
	// ================================

	// Pool of mesh components used for cell highlighting
	UPROPERTY()
	TArray<UStaticMeshComponent*> CellHighlightPool;

	UPROPERTY()
	UMaterialInstanceDynamic* CellValidMaterial;

	UPROPERTY()
	UMaterialInstanceDynamic* CellCursorMaterial;

	// How many pool entries are currently in use
	int32 ActiveHighlightCount = 0;

	// ================================
	// Colors
	// ================================

	static constexpr float GhostOpacity = 0.4f;

	FLinearColor GetColorForState(EBuildGhostState State) const;

	// Get or create a cell highlight component from the pool
	UStaticMeshComponent* GetOrCreateHighlight(int32 Index);

	// Ensure enough pool capacity
	void EnsurePoolSize(int32 RequiredSize);

	// Current state
	EBuildGhostState CurrentState = EBuildGhostState::Hidden;
};
