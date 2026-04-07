#include "SimulationTimeManager.h"

void USimulationTimeManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    RecalculateTimeSettings();

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Initialized. Target=%.2f min/day Interval=%.2fs StepsPerDay=%d MaxSteps/Frame=%d Speed=%.2fx"),
        TargetMinutesPerDay,
        FixedStepInterval,
        StepsPerDay,
        MaxStepsPerFrame,
        SimSpeedMultiplier
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

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Started. Target=%.2f min/day Interval=%.2fs StepsPerDay=%d Speed=%.2fx"),
        TargetMinutesPerDay,
        FixedStepInterval,
        StepsPerDay,
        SimSpeedMultiplier
    );
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

void USimulationTimeManager::SetSimSpeed(float Multiplier)
{
    SimSpeedMultiplier = FMath::Clamp(Multiplier, 0.01f, 10.0f);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Speed %.2fx"),
        SimSpeedMultiplier
    );
}

void USimulationTimeManager::RecalculateTimeSettings()
{
    const int32 SafeStepsPerDay = FMath::Max(1, StepsPerDay);
    const float SafeTargetMinutesPerDay = FMath::Clamp(TargetMinutesPerDay, 1.0f, 120.0f);
    const float SecondsPerDay = SafeTargetMinutesPerDay * 60.0f;

    StepsPerDay = SafeStepsPerDay;
    TargetMinutesPerDay = SafeTargetMinutesPerDay;
    FixedStepInterval = SecondsPerDay / static_cast<float>(SafeStepsPerDay);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Recalculated: %.2f min/day -> %.2fs/step (StepsPerDay=%d)"),
        TargetMinutesPerDay,
        FixedStepInterval,
        StepsPerDay
    );
}

void USimulationTimeManager::RestoreStepCount(int32 S, int32 D, int32 Y)
{
    StepNumber = S;
    CurrentDay = D;
    CurrentYear = Y;
    Accumulator = 0.f;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[SimTime] Restored Step=%d Day=%d Year=%d"),
        S,
        D,
        Y
    );
}