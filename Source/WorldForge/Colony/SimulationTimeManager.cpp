#include "SimulationTimeManager.h"

void USimulationTimeManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Initialized. Interval=%.2fs MaxSteps/Frame=%d"),
        FixedStepInterval,
        MaxStepsPerFrame
    );
}

void USimulationTimeManager::Deinitialize()
{
    bRunning = false;
    Super::Deinitialize();
}

void USimulationTimeManager::Tick(float DeltaTime)
{
    if (!bRunning || bPaused)
    {
        return;
    }

    Accumulator += DeltaTime * SimSpeedMultiplier;
    int32 StepsThisFrame = 0;

    while (Accumulator >= FixedStepInterval)
    {
        if (StepsThisFrame >= MaxStepsPerFrame)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[SimTime] MaxStepsPerFrame cap hit at Step=%d. Discarding %.3fs."),
                StepNumber,
                Accumulator
            );

            Accumulator = 0.f;
            break;
        }

        Accumulator -= FixedStepInterval;
        StepNumber++;
        StepsThisFrame++;

        if (StepNumber % 25 == 0)
        {
            UE_LOG(LogTemp, Log, TEXT("[SimTime] Step %d"), StepNumber);
        }

        OnSimulationStep.Broadcast(StepNumber);

        if (StepNumber % StepsPerDay == 0)
        {
            CurrentDay++;

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[SimTime] Day advanced to %d at Step=%d"),
                CurrentDay,
                StepNumber
            );

            if (CurrentDay > DaysPerYear)
            {
                CurrentDay = 1;
                CurrentYear++;

                UE_LOG(LogTemp, Log, TEXT("[SimTime] Year %d begins"), CurrentYear);
            }
        }
    }
}

void USimulationTimeManager::StartSimulation()
{
    bRunning = true;
    bPaused = false;
    Accumulator = 0.f;

    UE_LOG(LogTemp, Log, TEXT("[SimTime] Started."));
}

void USimulationTimeManager::PauseSimulation()
{
    bPaused = true;
    UE_LOG(LogTemp, Log, TEXT("[SimTime] Paused at step %d"), StepNumber);
}

void USimulationTimeManager::ResumeSimulation()
{
    bPaused = false;
    Accumulator = 0.f;

    UE_LOG(LogTemp, Log, TEXT("[SimTime] Resumed at step %d"), StepNumber);
}

void USimulationTimeManager::SetSimSpeed(float M)
{
    SimSpeedMultiplier = FMath::Clamp(M, 0.1f, 20.f);
    UE_LOG(LogTemp, Log, TEXT("[SimTime] Speed %.1fx"), SimSpeedMultiplier);
}

void USimulationTimeManager::RestoreStepCount(int32 S, int32 D, int32 Y)
{
    StepNumber = S;
    CurrentDay = D;
    CurrentYear = Y;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Restored Step=%d Day=%d Year=%d"),
        S,
        D,
        Y
    );
}