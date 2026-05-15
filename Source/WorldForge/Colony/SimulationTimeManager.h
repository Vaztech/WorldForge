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

    /**
     * Set the simulation speed multiplier.
     * 1.0 = normal, 2.0 = 2x faster, etc.
     * Clamped to [0.01, 10.0].
     *
     * Called by WorldForgePlayerController::ApplyCurrentSimSpeed()
     * via the + / - / * key bindings (or their UI button equivalents).
     */
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

    // ─────────────────────────────────────────────────────────────────────────
    // Simulation timing
    //
    // Production target time scale:
    //   1 in-game hour  = 1 real-world minute  (at 1x speed)
    //   1 in-game day   = 24 real-world minutes (at 1x speed)
    //   Full day cycle  = 120,000 engine ticks at 60fps ≈ 33 min
    //                     (SkyScape day/night is independent of this value)
    //
    // Speed multiplier effect at each level:
    //   1x → 24 real minutes per game day    (1 hr/min)
    //   2x → 12 real minutes per game day    (1 hr/30s)
    //   3x →  8 real minutes per game day    (1 hr/20s)
    //   4x →  6 real minutes per game day    (1 hr/15s)
    //
    // TargetMinutesPerDay was previously set to 1.0 (1 min/day) during
    // locomotion testing so colonist movement was visible in fast PIE sessions.
    // It is temporarily set to 4.0 for faster PIE testing so jobs and
    // colonist visual movement update often enough to see during debugging.
    // Return this to 24.0 later for production pacing.
    // ─────────────────────────────────────────────────────────────────────────

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning",
        meta = (ClampMin = "1.0", UIMin = "1.0", ClampMax = "120.0", UIMax = "120.0"))
    float TargetMinutesPerDay = 4.0f;

    // Number of simulation steps that make up a single in-game day.
    // At 4 min/day with 120 steps: FixedStepInterval = 2.0 seconds/step.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning",
        meta = (ClampMin = "1", UIMin = "1"))
    int32 StepsPerDay = 120;

    // Length of a year in simulation days.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning",
        meta = (ClampMin = "1", UIMin = "1"))
    int32 DaysPerYear = 100;

    // Runtime speed multiplier applied to DeltaTime accumulation.
    // Changed at runtime by WorldForgePlayerController via SetSimSpeed().
    // 1x, 2x, 3x, 4x are the player-facing levels (clamped to [0.01, 10.0] internally).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Tuning",
        meta = (ClampMin = "0.01", UIMin = "0.01", ClampMax = "10.0", UIMax = "10.0"))
    float SimSpeedMultiplier = 1.0f;

    // Calculated fixed-step interval in real seconds per simulation step.
    // Derived automatically from TargetMinutesPerDay / StepsPerDay via RecalculateTimeSettings().
    // Do not set this manually — call RecalculateTimeSettings() instead.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Advanced",
        meta = (ClampMin = "0.001", UIMin = "0.001"))
    float FixedStepInterval = 1.2f;

    // Safety cap to prevent huge catch-up bursts in a single frame.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation|Advanced",
        meta = (ClampMin = "1", UIMin = "1", ClampMax = "100", UIMax = "100"))
    int32 MaxStepsPerFrame = 5;

    int32 GetTotalStepCount() const { return StepNumber; }
    void RestoreStepCount(int32 Step, int32 Day, int32 Year);

private:
    float Accumulator = 0.f;
    int32 StepNumber  = 0;
    int32 CurrentDay  = 1;
    int32 CurrentYear = 1;
    bool  bRunning    = false;
    bool  bPaused     = false;
};
