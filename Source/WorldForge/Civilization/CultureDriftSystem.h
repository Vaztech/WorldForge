#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "CultureDriftSystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UCultureDriftSystem -- Phase 3B
// Runs every LivingWorldTickInterval sim steps (default 100).
// Reads last-flushed EventBus events to drift FCivilizationData.CulturalDNA.
// Never modifies sim state mid-step. All EventBus reads are from the
// last-flushed snapshot (GetRecentEvents()).
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UCultureDriftSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 LivingWorldTickInterval = 100;
};