#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldForgeLakeSurfaceActor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class WORLDFORGE_API AWorldForgeLakeSurfaceActor : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeLakeSurfaceActor();

    void InitializeLakeSurface(
        const FWaterlineLakeSurfaceDesc& Desc,
        UStaticMesh* Mesh,
        UMaterialInterface* Material,
        float BaseHalfExtent,
        float CoverageScale,
        float MinHalfExtent,
        float MinScaleXY,
        float MaxScaleXY,
        float ZOffset);

private:
    UPROPERTY(VisibleAnywhere, Category = "WorldForge|Waterline")
    TObjectPtr<USceneComponent> SceneRoot = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "WorldForge|Waterline")
    TObjectPtr<UStaticMeshComponent> LakeSurfaceMesh = nullptr;
};
