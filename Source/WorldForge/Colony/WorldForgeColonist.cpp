#include "WorldForgeColonist.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SpriteCharacterComponent.h"
#include "UObject/ConstructorHelpers.h"

AWorldForgeColonist::AWorldForgeColonist()
{
    // Colonist visuals update every frame so movement can be smoothed.
    PrimaryActorTick.bCanEverTick = true;

    // Create a temporary visible debug cube so the actor is visible in-world.
    // This remains as a fallback, but it is hidden in-game for V-F.
    DebugMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DebugMesh"));
    RootComponent = DebugMesh;

    // Use the engine's built-in cube mesh so no project asset is required.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));

    if (CubeMesh.Succeeded())
    {
        DebugMesh->SetStaticMesh(CubeMesh.Object);
    }

    // Scale the cube down so it reads more like a colonist marker than a block.
    DebugMesh->SetWorldScale3D(FVector(0.35f, 0.35f, 0.8f));

    // Enable shadow casting so it is easier to read in the world.
    DebugMesh->SetCastShadow(true);

    // This is a visual actor, so collision is disabled.
    DebugMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // Create the default sprite bridge component in C++.
    // This remains as the fallback/base component instance.
    SpriteVisual = CreateDefaultSubobject<USpriteCharacterComponent>(TEXT("SpriteVisual"));
    SpriteVisual->SetupAttachment(RootComponent);

    // Keep the sprite bridge at the actor origin by default.
    SpriteVisual->SetRelativeLocation(FVector::ZeroVector);
}

void AWorldForgeColonist::BeginPlay()
{
    Super::BeginPlay();

    // Hide the old debug cube in game so the sprite system becomes the visible path.
    // We keep the mesh component in the actor as a fallback/debug option.
    if (DebugMesh)
    {
        DebugMesh->SetHiddenInGame(true);
    }

    // Assign the current camera/view target so the active sprite visual component
    // can billboard toward the player's camera every frame.
    if (SpriteVisual)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            SpriteVisual->CameraTarget = PC->GetViewTarget();
        }
    }
}

// CHANGE MADE:
// Allows the Blueprint actor to replace the default C++ sprite component pointer
// with a Blueprint subclass instance that contains the actual visual logic.
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

    // If this actor has not received visual data yet, there is nothing to present.
    if (!bHasVisualData)
    {
        return;
    }

    // Cooldown timer for throttling critical-need events.
    if (NeedCriticalTimer > 0.f)
    {
        NeedCriticalTimer -= DeltaTime;
    }

    // Presentation-only movement update.
    // Important: no simulation polling happens here anymore.
    UpdateMovement(DeltaTime);
}

void AWorldForgeColonist::InitializeFromAgentData(const FAgentData& Data, float CellSize)
{
    // Cache identity and spatial scale from the initial simulation snapshot.
    AgentID = Data.AgentID;
    SettlementID = Data.SettlementID;
    VisualCellSize = CellSize;

    // Convert the simulation grid position into world space.
    // Raise the actor slightly so it sits visibly above the terrain.
    const FVector Position(
        Data.CurrentGridPos.X * CellSize,
        Data.CurrentGridPos.Y * CellSize,
        50.f
    );

    // Start exactly at the initial world position so there is no first-frame lerp.
    SetActorLocation(Position);
    TargetWorldPos = Position;
    LastKnownState = Data.State;
    bHasVisualData = true;

    // Push the initial state and race into the active sprite visual component immediately
    // so the visual layer starts with correct data on spawn.
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
    // Cache cell size in case the caller provides an updated value.
    VisualCellSize = CellSize;

    // Convert the latest simulation grid position into a world-space target.
    // Z is kept at a fixed visible height above the terrain.
    //
    // Important:
    // We only cache the target here.
    // Actual smoothing happens later in Tick() via UpdateMovement().
    TargetWorldPos = FVector(
        Data.CurrentGridPos.X * CellSize,
        Data.CurrentGridPos.Y * CellSize,
        50.f
    );

    bHasVisualData = true;

    // Push simulation-facing visual inputs into the active sprite visual component.
    // This keeps sprite animation and race visuals driven directly by FAgentData.
    if (SpriteVisual)
    {
        SpriteVisual->SetAnimStateFromAgentState(Data.State);
        SpriteVisual->SetRace(Data.RaceTag);
    }

    // Detect simulation state changes and fire Blueprint events.
    if (Data.State != LastKnownState)
    {
        OnStateChanged(Data.State, LastKnownState);
        LastKnownState = Data.State;

        // Fire death event once when entering the Dead state.
        if (Data.State == EAgentState::Dead)
        {
            OnColonistDied();
        }
    }

    // Temporary debug tinting by state.
    // This only shows if the debug mesh is made visible again and the active
    // material supports a "Color" vector parameter.
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

    // NeedCritical is still evaluated from incoming simulation data,
    // but it remains cooldown-throttled using a real-time timer.
    if (NeedCriticalTimer <= 0.f && (Data.FoodNeed < 20.f || Data.SleepNeed < 20.f))
    {
        OnNeedCritical(Data.FoodNeed, Data.SleepNeed);
        NeedCriticalTimer = NeedCriticalCooldown;
    }
}

void AWorldForgeColonist::UpdateMovement(float DeltaTime)
{
    const FVector CurrentLocation = GetActorLocation();

    // Smooth movement toward the most recently cached target world position.
    if (!CurrentLocation.Equals(TargetWorldPos, 1.f))
    {
        SetActorLocation(
            FMath::VInterpTo(
                CurrentLocation,
                TargetWorldPos,
                DeltaTime,
                MoveSpeed
            )
        );
    }
}