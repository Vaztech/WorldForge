#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeDataTypes.h"
#include "CosmicSimulationSubsystem.generated.h"

// ─────────────────────────────────────────────────────────────────────────
// UCosmicSimulationSubsystem -- Phase 3B
// Manages cosmic entity states, anchor records, and narrative pressure.
// Entities gain power from shrines/anchors and from EventBus events.
// When TotalPower > EmergenceThreshold, entity becomes Active and begins
// queuing economic interventions via EventBus::Dispatch().
// ALL interventions are queued -- flushed after all 9 processors.
// ─────────────────────────────────────────────────────────────────────────
UCLASS()
class WORLDFORGE_API UCosmicSimulationSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // Called by ProcessorManager each sim step.
    void OnSimStep(int32 StepNumber, int32 SettlementID,
        TArray<FAgentData>& Agents, UWorld* World);

    // ── Entity registration ────────────────────────────────────────────
    // Called during world generation to seed the pantheon.
    int32 RegisterEntity(FGameplayTag EntityTag,
        const TArray<FGameplayTag>& DomainTags,
        float EmergenceThreshold = 10.0f);

    // ── Anchor management ──────────────────────────────────────────────
    // Called by UVoxelConstructionSystem::OnBuildComplete when a shrine is placed.
    int32 RegisterAnchor(int32 SettlementID,
        FIntPoint GridPosition,
        FGameplayTag BoundEntityTag,
        float PowerPerSimDay = 0.1f);

    void DestroyAnchor(int32 AnchorID);

    // ── Power manipulation ─────────────────────────────────────────────
    // External systems call these to add power to entities.
    void AddFearPower(FGameplayTag EntityTag, float Amount);
    void AddAffinityPower(FGameplayTag EntityTag, float Amount);
    void AddCatastrophePower(FGameplayTag EntityTag, float Amount);
    void AddBeliefPower(FGameplayTag EntityTag, float Amount);
    void AddIrritationPower(FGameplayTag EntityTag, float Amount);

    // ── Queries ────────────────────────────────────────────────────────
    bool IsEntityActive(FGameplayTag EntityTag) const;
    float GetEntityTotalPower(FGameplayTag EntityTag) const;
    const FCosmicEntityState* GetEntityState(FGameplayTag EntityTag) const;

    // ── Narrative pressure (Section H) ─────────────────────────────────
    const FNarrativePressureState& GetNarrativePressure() const { return NarrativePressure; }

    // ── Save / Load ────────────────────────────────────────────────────
    FCosmicSaveState GetCosmicSaveState() const;
    void RestoreCosmicFromSave(const FCosmicSaveState& S);

private:
    TArray<FCosmicEntityState> EntityStates;
    TArray<FAnchorRecord>      AnchorRecords;
    FNarrativePressureState    NarrativePressure;
    int32                      NextEntityID = 0;
    int32                      NextAnchorID = 0;

    // BF-10: Per-settlement average PurposeNeed / 100.0, refreshed each OnSimStep.
    TMap<int32, float>         CachedFaithFactors;

    // How many sim steps = 1 in-game day (from SimulationTimeManager.StepsPerDay)
    static constexpr int32 StepsPerDay = 50;

    // Process decay and status transitions for all entities.
    void DecayAllEntities();

    // Add anchor power contributions once per in-game day.
    void ProcessAnchorPower(int32 StepNumber,
        class USettlementManager* SM);

    // Check emergence transitions.
    void CheckEmergenceTransitions(int32 StepNumber, UWorld* World);

    // Dispatch economic intervention event for an active entity.
    void DispatchEconomicIntervention(const FCosmicEntityState& Entity,
        int32 StepNumber, UWorld* World);

    // Update narrative pressure accumulators.
    void UpdateNarrativePressure(int32 StepNumber, UWorld* World,
        const TArray<FSimulationEvent>& RecentEvents);

    // Try to fire a narrative beat when accumulator crosses threshold.
    void TryFireNarrativeBeat(const FString& BeatDescription,
        float& Accumulator, UWorld* World) const;

    // Decay one entity's power sources.
    static void DecayEntityPower(FCosmicEntityState& Entity);

    // Add power to a specific source on the entity matching EntityTag.
    void AddPowerToSource(FGameplayTag EntityTag, float Amount,
        float FCosmicEntityState::* PowerField);
};