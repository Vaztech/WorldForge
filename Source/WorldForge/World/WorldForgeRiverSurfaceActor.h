#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldForgeRiverSurfaceActor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

// RIVER-SYSTEM-01: Lightweight river water surface actor.
// One actor per river descriptor; uses a simple scaled plane mesh.
// No SceneCapture, no render targets, no buoyancy, no post-process.
UCLASS()
class WORLDFORGE_API AWorldForgeRiverSurfaceActor : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeRiverSurfaceActor();

    void InitializeRiverSurface(
        const FWorldForgeRiverSurfaceDesc& Desc,
        UStaticMesh* Mesh,
        UMaterialInterface* Material,
        float TileSizeWorld,
        float ZOffset);

private:
    UPROPERTY(VisibleAnywhere, Category = "WorldForge|River")
    TObjectPtr<USceneComponent> SceneRoot = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "WorldForge|River")
    TObjectPtr<UStaticMeshComponent> RiverSurfaceMesh = nullptr;
};
