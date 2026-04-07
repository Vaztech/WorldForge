#include "WorldGenBootstrapActor.h"

#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldMapSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

AWorldGenBootstrapActor::AWorldGenBootstrapActor()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AWorldGenBootstrapActor::BeginPlay()
{
    Super::BeginPlay();

    if (bGenerateOnBeginPlay)
    {
        RunBootstrap();
    }
}

void AWorldGenBootstrapActor::RunBootstrap()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] BeginPlay failed: GetWorld() returned null."));
        return;
    }

    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Failed: GameInstance is null."));
        return;
    }

    RNG = GameInstance->GetSubsystem<UWorldForgeRNG>();
    if (!RNG)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Failed to get UWorldForgeRNG subsystem from GameInstance."));
        return;
    }

    Pipeline = NewObject<UWorldGenPipeline>(this);
    if (!Pipeline)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Failed to create UWorldGenPipeline."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] Starting world generation. Seed=%lld"), WorldSeed);

    RNG->InitializeWithSeed(WorldSeed);
    Pipeline->GenerateWorld(WorldSeed, WorldGenConfig, RNG);

    if (!Pipeline->IsComplete())
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Pipeline did not complete."));
        return;
    }

    if (UWorldMapSubsystem* WorldMap = World->GetSubsystem<UWorldMapSubsystem>())
    {
        WorldMap->InitializeFromPipeline(Pipeline);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] UWorldMapSubsystem not found."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] World generation and map initialization complete."));

    if (bDestroyAfterBootstrap)
    {
        Destroy();
    }
}