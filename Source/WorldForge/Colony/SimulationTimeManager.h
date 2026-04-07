#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Tickable.h"
#include "SimulationTimeManager.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimulationStep, int32, StepNumber);

UCLASS(BlueprintType)
class WORLDFORGE_API USimulationTimeManager
    : public UWorldSubsystem
    , public FTickableGameObject
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual bool IsTickable() const override { return bRunning; }
    virtual bool IsTickableInEditor() const override { return false; }
    virtual TStatId GetStatId() const override
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(USimulationTimeManager, STATGROUP_Tickables);
    }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void StartSimulation();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void PauseSimulation();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void ResumeSimulation();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void SetSimSpeed(float Multiplier);

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    void RecalculateTimeSettings();

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    bool IsRunning() const { return bRunning; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    bool IsPaused() const { return bPaused; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    int32 GetStepNumber() const { return StepNumber; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    int32 GetCurrentDay() const { return CurrentDay; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    int32 GetCurrentYear() const { return CurrentYear; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    float GetSimSpeed() const { return SimSpeedMultiplier; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    float GetStepInterval() const { return FixedStepInterval; }

    UFUNCTION(BlueprintCallable, Category = "Simulation")
    float GetTargetMinutesPerDay() const { return TargetMinutesPerDay; }

    UPROPERTY(BlueprintAssignable, Category = "Simulation")
    FOnSimulationStep OnSimulationStep;

    // Automatically recalculates FixedStepInterval from TargetMinutesPerDay and StepsPerDay.
    // Example: 1 minute/day with 50 steps/day => 1.2 seconds/step.
    // Lowered from 20 to 1 during testing so colonist movement is visible in PIE.
    // Restore to 20 (or whatever the production target is) once locomotion tuning is done.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning", meta = (ClampMin = "1.0", UIMin = "1.0", ClampMax = "120.0", UIMax = "120.0"))
    float TargetMinutesPerDay = 1.0f;

    // Number of simulation steps that make up a single day.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning", meta = (ClampMin = "1", UIMin = "1"))
    int32 StepsPerDay = 50;

    // Length of a year in simulation days.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning", meta = (ClampMin = "1", UIMin = "1"))
    int32 DaysPerYear = 100;

    // Runtime speed multiplier applied to DeltaTime accumulation.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning", meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "10.0", UIMax = "10.0"))
    float SimSpeedMultiplier = 1.0f;

    // Calculated fixed-step interval in real seconds per simulation step.
    // Usually derived from TargetMinutesPerDay / StepsPerDay via RecalculateTimeSettings().
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Advanced", meta = (ClampMin = "0.001", UIMin = "0.001"))
    float FixedStepInterval = 1.2f;

    // Safety cap to prevent huge catch-up bursts in a single frame.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Advanced", meta = (ClampMin = "1", UIMin = "1", ClampMax = "100", UIMax = "100"))
    int32 MaxStepsPerFrame = 5;

    int32 GetTotalStepCount() const { return StepNumber; }
    void RestoreStepCount(int32 Step, int32 Day, int32 Year);

private:
    float Accumulator = 0.f;
    int32 StepNumber = 0;
    int32 CurrentDay = 1;
    int32 CurrentYear = 1;
    bool bRunning = false;
    bool bPaused = false;
};