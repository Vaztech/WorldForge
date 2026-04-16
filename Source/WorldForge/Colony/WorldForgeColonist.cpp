#include "WorldForgeColonist.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SettlementManager.h"
#include "SpriteCharacterComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelWorldManager.h"

AWorldForgeColonist::AWorldForgeColonist()
{
    PrimaryActorTick.bCanEverTick = true;

    DebugMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DebugMesh"));
    RootComponent = DebugMesh;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));

    if (CubeMesh.Succeeded())
    {
        DebugMesh->SetStaticMesh(CubeMesh.Object);
    }

    DebugMesh->SetWorldScale3D(FVector(0.35f, 0.35f, 0.8f));
    DebugMesh->SetCastShadow(true);
    DebugMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    SpriteVisual = CreateDefaultSubobject<USpriteCharacterComponent>(TEXT("SpriteVisual"));
    SpriteVisual->SetupAttachment(RootComponent);
    SpriteVisual->SetRelativeLocation(FVector::ZeroVector);
}

void AWorldForgeColonist::BeginPlay()
{
    Super::BeginPlay();

    if (DebugMesh)
    {
        DebugMesh->SetHiddenInGame(true);
    }

    if (SpriteVisual)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            SpriteVisual->CameraTarget = PC->GetViewTarget();
        }
    }
}

void AWorldForgeColonist::SetSpriteVisualComponent(USpriteCharacterComponent* InSpriteVisual)
{
    if (InSpriteVisual)
    {
        SpriteVisual = InSpriteVisual;
    }
}

void AWorldForgeColonist::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bHasVisualData)
    {
        return;
    }

    if (NeedCriticalTimer > 0.f)
    {
        NeedCriticalTimer -= DeltaTime;
    }

    UpdateMovement(DeltaTime);
}

FVector AWorldForgeColonist::ResolveWorldLocationFromAgentData(const FAgentData& Data, float CellSize) const
{
    // 🔥 FIX: Do NOT offset by settlement origin (it is in mismatched space)
    FIntPoint RequestedWorldCell(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);

    if (const UWorld* World = GetWorld())
    {
        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (!VoxelWorldManager->IsSurfaceReady())
            {
                if (bHasVisualData)
                {
                    return GetActorLocation();
                }

                if (!TargetWorldPos.IsNearlyZero())
                {
                    return TargetWorldPos;
                }

                return VoxelWorldManager->GridToWorld(RequestedWorldCell, 0) + FVector(0.f, 0.f, TerrainZOffset);
            }

            const FIntPoint ChunkCell =
                VoxelWorldManager->ClampTerrainCellToBounds(RequestedWorldCell);

            FVector SurfaceLocation = FVector::ZeroVector;
            if (VoxelWorldManager->TryGetSurfaceWorldLocation(ChunkCell, SurfaceLocation, TerrainZOffset))
            {
                return SurfaceLocation;
            }

            FIntPoint FallbackChunkCell = ChunkCell;
            if (VoxelWorldManager->FindNearestSpawnableTerrainCell(ChunkCell, FallbackChunkCell))
            {
                if (VoxelWorldManager->TryGetSurfaceWorldLocation(FallbackChunkCell, SurfaceLocation, TerrainZOffset))
                {
                    return SurfaceLocation;
                }
            }

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[WorldForgeColonist] Surface query failed for AgentID=%d LocalCell=(%d,%d) VoxelCell=(%d,%d) ChunkCell=(%d,%d). Holding current position until terrain resolves."),
                Data.AgentID,
                Data.CurrentGridPos.X,
                Data.CurrentGridPos.Y,
                RequestedWorldCell.X,
                RequestedWorldCell.Y,
                ChunkCell.X,
                ChunkCell.Y
            );

            if (bHasVisualData)
            {
                return GetActorLocation();
            }

            if (!TargetWorldPos.IsNearlyZero())
            {
                return TargetWorldPos;
            }

            return VoxelWorldManager->GridToWorld(RequestedWorldCell, 0) + FVector(0.f, 0.f, TerrainZOffset);
        }
    }

    return FVector(
        RequestedWorldCell.X * CellSize,
        RequestedWorldCell.Y * CellSize,
        TerrainZOffset
    );
}

void AWorldForgeColonist::InitializeFromAgentData(const FAgentData& Data, float CellSize)
{
    AgentID = Data.AgentID;
    SettlementID = Data.SettlementID;
    VisualCellSize = CellSize;
    CurrentGridCell = FIntPoint(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);

    const FVector Position = ResolveWorldLocationFromAgentData(Data, CellSize);

    SetActorLocation(Position);
    TargetWorldPos = Position;
    LastKnownState = Data.State;
    bHasVisualData = true;

    if (SpriteVisual)
    {
        SpriteVisual->SetAnimStateFromAgentState(Data.State);
        SpriteVisual->SetRace(Data.RaceTag);
    }
}

void AWorldForgeColonist::UpdateVisuals(
    const FAgentData& Data,
    float DeltaTime,
    float CellSize
)
{
    (void)DeltaTime;

    VisualCellSize = CellSize;
    CurrentGridCell = FIntPoint(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);
    TargetWorldPos = ResolveWorldLocationFromAgentData(Data, CellSize);

    bHasVisualData = true;

    if (SpriteVisual)
    {
        SpriteVisual->SetAnimStateFromAgentState(Data.State);
        SpriteVisual->SetRace(Data.RaceTag);
    }

    if (Data.State != LastKnownState)
    {
        OnStateChanged(Data.State, LastKnownState);
        LastKnownState = Data.State;

        if (Data.State == EAgentState::Dead)
        {
            OnColonistDied();
        }
    }

    if (DebugMesh)
    {
        FVector Tint = FVector(1.f, 1.f, 1.f);

        if (Data.State == EAgentState::Working)
        {
            Tint = FVector(0.f, 1.f, 0.f);
        }
        else if (Data.State == EAgentState::Sleeping)
        {
            Tint = FVector(0.f, 0.f, 1.f);
        }
        else if (Data.State == EAgentState::Eating)
        {
            Tint = FVector(1.f, 1.f, 0.f);
        }

        DebugMesh->SetVectorParameterValueOnMaterials(TEXT("Color"), Tint);
    }

    if (NeedCriticalTimer <= 0.f && (Data.FoodNeed < 20.f || Data.SleepNeed < 20.f))
    {
        OnNeedCritical(Data.FoodNeed, Data.SleepNeed);
        NeedCriticalTimer = NeedCriticalCooldown;
    }
}

void AWorldForgeColonist::UpdateMovement(float DeltaTime)
{
    const FVector CurrentLocation = GetActorLocation();
    FVector DesiredLocation = TargetWorldPos;

    if (!CurrentLocation.Equals(TargetWorldPos, 1.f))
    {
        DesiredLocation = FMath::VInterpTo(
            CurrentLocation,
            TargetWorldPos,
            DeltaTime,
            MoveSpeed
        );
    }

    if (const UWorld* World = GetWorld())
    {
        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            if (VoxelWorldManager->IsSurfaceReady())
            {
                FIntPoint CurrentWorldCell = CurrentGridCell;

                if (const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>())
                {
                    if (const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID))
                    {
                        CurrentWorldCell = Settlement->WorldOrigin + CurrentGridCell;
                    }
                }

                const FIntPoint ChunkCell =
                    VoxelWorldManager->ClampTerrainCellToBounds(CurrentWorldCell);

                FVector SurfaceLocation = FVector::ZeroVector;
                if (VoxelWorldManager->TryGetSurfaceWorldLocation(ChunkCell, SurfaceLocation, TerrainZOffset))
                {
                    DesiredLocation.Z = SurfaceLocation.Z;
                }
            }
        }
    }

    SetActorLocation(DesiredLocation);
}