// Source/WorldForge/Player/BuildGhostActor.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
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

    // Prefab mode: preview explicit block volume instead of a single cube
    void UpdatePrefabGhost(
        FVector WorldPosition,
        int32 RotationIndex,
        FGameplayTag BuildingTag,
        UVoxelWorldManager* VoxelManager,
        float SurfaceZ = 0.0f
    );

    // Custom draw mode: highlight selected cells + cursor cell as full cubes
    void UpdateCustomDrawCells(
        const TArray<FIntPoint>& SelectedCells,
        FIntPoint CursorCell,
        UVoxelWorldManager* VoxelManager,
        float SurfaceZ = 0.0f);

    // Canonical 3D custom-draw ghost. Each cell renders at its actual voxel
    // Z (so stacked cubes show real height) and cubes are slightly inset so
    // adjacent cubes have a visible seam between them instead of merging
    // into a single solid block.
    void UpdateCustomDrawCells3D(
        const TArray<FIntVector>& SelectedCells3D,
        FIntVector CursorVoxel,
        UVoxelWorldManager* VoxelManager);

    // Remove all cell highlights
    void ClearAllHighlights();

protected:
    virtual void BeginPlay() override;

private:

    UPROPERTY(VisibleAnywhere)
    UStaticMeshComponent* GhostMesh;

    UPROPERTY()
    UMaterialInstanceDynamic* GhostMaterial;

    UPROPERTY()
    TArray<UStaticMeshComponent*> CellHighlightPool;

    UPROPERTY()
    UMaterialInstanceDynamic* CellValidMaterial;

    UPROPERTY()
    UMaterialInstanceDynamic* CellCursorMaterial;

    UPROPERTY()
    UMaterialInstanceDynamic* CellInvalidMaterial;

    int32 ActiveHighlightCount = 0;

    static constexpr float GhostOpacity = 0.4f;

    // Slight inset (~8%) applied to every ghost cube so adjacent cubes show
    // a visible seam between them instead of merging into one solid mass.
    // This is what makes stacked cubes read as distinct cubes at a glance.
    static constexpr float GhostCubeInsetScale = 0.92f;

    FLinearColor GetColorForState(EBuildGhostState State) const;
    UStaticMeshComponent* GetOrCreateHighlight(int32 Index);
    void EnsurePoolSize(int32 RequiredSize);

    FIntPoint RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const;

    EBuildGhostState CurrentState = EBuildGhostState::Hidden;
};