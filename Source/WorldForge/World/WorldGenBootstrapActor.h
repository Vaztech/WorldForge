#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldGenBootstrapActor.generated.h"

class UWorldForgeRNG;
class UWorldGenPipeline;

UCLASS()
class WORLDFORGE_API AWorldGenBootstrapActor : public AActor
{
    GENERATED_BODY()

public:
    AWorldGenBootstrapActor();

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    int64 WorldSeed = 12345;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    FWorldGenConfig WorldGenConfig;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    bool bGenerateOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "WorldForge|WorldGen")
    bool bDestroyAfterBootstrap = false;

    UPROPERTY(Transient)
    TObjectPtr<UWorldForgeRNG> RNG = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<UWorldGenPipeline> Pipeline = nullptr;

    void RunBootstrap();
};