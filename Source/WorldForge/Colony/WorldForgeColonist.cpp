#include "WorldForgeColonist.h"
#include "ProcessorManager.h"

AWorldForgeColonist::AWorldForgeColonist()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AWorldForgeColonist::BeginPlay()
{
    Super::BeginPlay();
}

void AWorldForgeColonist::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (AgentID < 0)
    {
        return;
    }

    if (NeedCriticalTimer > 0.f)
    {
        NeedCriticalTimer -= DeltaTime;
    }

    // Pass DeltaTime explicitly -- visual code should not call GetDeltaSeconds()
    if (UWorld* World = GetWorld())
    {
        if (UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>())
        {
            if (const FAgentData* Data = ProcessorManager->GetAgent(AgentID))
            {
                UpdateVisuals(*Data, DeltaTime, VisualCellSize);
            }
        }
    }
}

void AWorldForgeColonist::InitializeFromAgentData(const FAgentData& Data, float CellSize)
{
    AgentID = Data.AgentID;
    SettlementID = Data.SettlementID;
    VisualCellSize = CellSize;

    const FVector Position(
        Data.CurrentGridPos.X * CellSize,
        Data.CurrentGridPos.Y * CellSize,
        0.f
    );

    SetActorLocation(Position);
    TargetWorldPos = Position;
    LastKnownState = Data.State;
}

void AWorldForgeColonist::UpdateVisuals(
    const FAgentData& Data,
    float DeltaTime,
    float CellSize
)
{
    // Smooth interpolation using passed DeltaTime
    TargetWorldPos = FVector(
        Data.CurrentGridPos.X * CellSize,
        Data.CurrentGridPos.Y * CellSize,
        GetActorLocation().Z
    );

    const FVector CurrentLocation = GetActorLocation();

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

    // State change events
    if (Data.State != LastKnownState)
    {
        OnStateChanged(Data.State, LastKnownState);
        LastKnownState = Data.State;

        if (Data.State == EAgentState::Dead)
        {
            OnColonistDied();
        }
    }

    // NeedCritical throttled -- fires at most every NeedCriticalCooldown seconds
    if (NeedCriticalTimer <= 0.f && (Data.FoodNeed < 20.f || Data.SleepNeed < 20.f))
    {
        OnNeedCritical(Data.FoodNeed, Data.SleepNeed);
        NeedCriticalTimer = NeedCriticalCooldown;
    }
}