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

    UFUNCTION(BlueprintCallable)
    void StartSimulation();

    UFUNCTION(BlueprintCallable)
    void PauseSimulation();

    UFUNCTION(BlueprintCallable)
    void ResumeSimulation();

    UFUNCTION(BlueprintCallable)
    void SetSimSpeed(float Multiplier);

    UFUNCTION(BlueprintCallable)
    bool IsRunning() const { return bRunning; }

    UFUNCTION(BlueprintCallable)
    bool IsPaused() const { return bPaused; }

    UFUNCTION(BlueprintCallable)
    int32 GetStepNumber() const { return StepNumber; }

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentDay() const { return CurrentDay; }

    UFUNCTION(BlueprintCallable)
    int32 GetCurrentYear() const { return CurrentYear; }

    UFUNCTION(BlueprintCallable)
    float GetSimSpeed() const { return SimSpeedMultiplier; }

    UFUNCTION(BlueprintCallable)
    float GetStepInterval() const { return FixedStepInterval; }

    UPROPERTY(BlueprintAssignable)
    FOnSimulationStep OnSimulationStep;

    // MODIFIED:
    // Faster default fixed-step cadence for long-run validation.
    // Was 0.2f (5 steps/sec), now 0.1f (10 steps/sec).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    float FixedStepInterval = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    int32 StepsPerDay = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    int32 DaysPerYear = 100;

    // MODIFIED:
    // Raise the per-frame cap so time acceleration can actually advance the sim
    // without hitting the limiter too early during fast-forward tests.
    // Was 10.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    int32 MaxStepsPerFrame = 100;

    int32 GetTotalStepCount() const { return StepNumber; }
    void RestoreStepCount(int32 Step, int32 Day, int32 Year);

private:
    float Accumulator = 0.f;
    int32 StepNumber = 0;
    int32 CurrentDay = 1;
    int32 CurrentYear = 1;
    float SimSpeedMultiplier = 1.f;
    bool bRunning = false;
    bool bPaused = false;
};