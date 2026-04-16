#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "World/WorldDecorationTypes.h"
#include "WorldResourceActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;

UCLASS()
class WORLDFORGE_API AWorldResourceActor : public AActor
{
    GENERATED_BODY()

public:
    AWorldResourceActor();

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resource")
    void ApplyDecorationMetadata(
        FGameplayTag InResourceTag,
        bool bInBlocksNavigation,
        bool bInChoppable,
        EWorldDecorationFootprint InFootprint
    );

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Resource")
    void ApplyDecorationMesh(UStaticMesh* InStaticMesh);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Resource")
    FGameplayTag GetResourceTag() const { return ResourceTag; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Resource")
    bool BlocksNavigation() const { return bBlocksNavigation; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Resource")
    bool IsChoppable() const { return bChoppable; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|Resource")
    EWorldDecorationFootprint GetFootprint() const { return Footprint; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    TObjectPtr<USceneComponent> SceneRoot = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    TObjectPtr<UStaticMeshComponent> MeshComponent = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    FGameplayTag ResourceTag;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    bool bBlocksNavigation = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    bool bChoppable = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Resource")
    EWorldDecorationFootprint Footprint = EWorldDecorationFootprint::OneByOne;
};