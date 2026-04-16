#include "WorldGenBootstrapActor.h"

#include "WorldGenPipeline.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldMapSubsystem.h"
#include "World/WorldDecorationSubsystem.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Voxel/VoxelWorldManager.h"

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

void AWorldGenBootstrapActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (bBoundToTerrainReady)
            {
                VM->OnTerrainReady.RemoveDynamic(this, &AWorldGenBootstrapActor::HandleTerrainReady);
                bBoundToTerrainReady = false;
            }
        }
    }

    Super::EndPlay(EndPlayReason);
}

void AWorldGenBootstrapActor::HandleTerrainReady()
{
    if (bDecorationPassDispatched)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World || !Pipeline || !Pipeline->IsComplete())
    {
        return;
    }

    if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
    {
        if (!VM->IsSurfaceReady())
        {
            return;
        }
    }

    UWorldDecorationSubsystem* DecorationSubsystem = World->GetSubsystem<UWorldDecorationSubsystem>();
    if (!DecorationSubsystem)
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] Decoration dispatch failed: WorldDecorationSubsystem missing."));
        return;
    }

    const bool bGenerated = DecorationSubsystem->GenerateDecorations(
        Pipeline->GetWorldTiles(),
        Pipeline->GetGridWidth(),
        Pipeline->GetGridHeight(),
        WorldSeed
    );

    bDecorationPassDispatched = true;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[WorldGenBootstrap] Decoration triggered via terrain-ready event. Spawned=%s"),
        bGenerated ? TEXT("true") : TEXT("false")
    );

    if (bDestroyAfterBootstrap)
    {
        Destroy();
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

    if (PipelineClass)
    {
        Pipeline = NewObject<UWorldGenPipeline>(this, PipelineClass);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldGenBootstrap] PipelineClass not set. Falling back to base class."));
        Pipeline = NewObject<UWorldGenPipeline>(this);
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

    if (UVoxelWorldManager* VM = World->GetSubsystem<UVoxelWorldManager>())
    {
        VM->OnTerrainReady.AddDynamic(this, &AWorldGenBootstrapActor::HandleTerrainReady);
        bBoundToTerrainReady = true;

        if (VM->IsSurfaceReady())
        {
            HandleTerrainReady();
        }
        else
        {
            UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] Waiting for terrain-ready event before decoration dispatch."));
        }
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[WorldGenBootstrap] VoxelWorldManager not found."));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldGenBootstrap] World generation and map initialization complete."));
}