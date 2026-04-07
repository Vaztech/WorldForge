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

    int32 ActiveHighlightCount = 0;

    static constexpr float GhostOpacity = 0.4f;

    FLinearColor GetColorForState(EBuildGhostState State) const;
    UStaticMeshComponent* GetOrCreateHighlight(int32 Index);
    void EnsurePoolSize(int32 RequiredSize);

    bool TryParseExplicitPrefabDimensions(
        const FString& Source,
        int32& OutWidth,
        int32& OutDepth,
        int32& OutHeight
    ) const;

    FIntPoint RotateLocalCell(FIntPoint LocalXY, int32 RotationIndex, int32 Width, int32 Depth) const;
    int32 GetLegacyPrefabBlockCount(FGameplayTag BuildingTag) const;

    EBuildGhostState CurrentState = EBuildGhostState::Hidden;
};