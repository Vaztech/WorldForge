#include "WorldResourceActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

AWorldResourceActor::AWorldResourceActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
    MeshComponent->SetupAttachment(SceneRoot);
    MeshComponent->SetMobility(EComponentMobility::Movable);
    MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
    MeshComponent->SetGenerateOverlapEvents(false);

    SetActorEnableCollision(true);
    Tags.Add(FName(TEXT("WorldForgeDecoration")));
}

void AWorldResourceActor::ApplyDecorationMetadata(
    FGameplayTag InResourceTag,
    bool bInBlocksNavigation,
    bool bInChoppable,
    EWorldDecorationFootprint InFootprint
)
{
    ResourceTag = InResourceTag;
    bBlocksNavigation = bInBlocksNavigation;
    bChoppable = bInChoppable;
    Footprint = InFootprint;

    if (ResourceTag.IsValid())
    {
        Tags.AddUnique(FName(*ResourceTag.ToString()));
    }

    if (bChoppable)
    {
        Tags.AddUnique(FName(TEXT("WorldForgeChoppable")));
    }

    if (bBlocksNavigation)
    {
        Tags.AddUnique(FName(TEXT("WorldForgeBlocksNavigation")));
    }
}

void AWorldResourceActor::ApplyDecorationMesh(UStaticMesh* InStaticMesh)
{
    if (!MeshComponent || !InStaticMesh)
    {
        return;
    }

    if (MeshComponent->Mobility != EComponentMobility::Movable)
    {
        MeshComponent->SetMobility(EComponentMobility::Movable);
    }

    MeshComponent->SetStaticMesh(InStaticMesh);
}