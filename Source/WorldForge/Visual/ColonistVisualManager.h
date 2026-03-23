#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ColonistVisualManager.generated.h"

class AWorldForgeColonist;
class UProcessorManager;
class USimulationTimeManager;

/**
 * UColonistVisualManager
 *
 * Thin bridge between simulation data and colonist visual actors.
 *
 * Responsibilities:
 * - Spawn one visual actor per simulation agent
 * - Keep a map of AgentID -> actor
 * - Sync visual actors from simulation snapshots
 * - Remove actors for dead / missing agents
 *
 * Non-responsibilities:
 * - Does NOT own simulation logic
 * - Does NOT modify FAgentData
 * - Does NOT decide gameplay outcomes
 *
 * This is the missing middle layer between:
 *   ProcessorManager  ->  ColonistVisualManager  ->  AWorldForgeColonist
 */
UCLASS(BlueprintType)
class WORLDFORGE_API UColonistVisualManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Deinitialize() override;

    /**
     * Spawns colonist visual actors for all existing agents in a settlement.
     *
     * Call this once AFTER simulation startup, once agents already exist.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SpawnColonistActors(int32 InSettlementID);

    /**
     * Sync all visual actors for a settlement from current simulation data.
     *
     * Safe to call every simulation step.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SyncActorsToAgents(int32 InSettlementID);

    /**
     * Lookup helper used by debug systems or Blueprint.
     */
    UFUNCTION(BlueprintPure, Category = "WorldForge|Visual")
    AWorldForgeColonist* GetActorForAgent(int32 AgentID) const;

    /**
     * Optional cleanup entry point if you want to manually remove visuals.
     */
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void DestroyAllColonistActors();

    // CHANGE MADE:
    // Exposed so Blueprint/debug tools can inspect which class the manager will try to spawn.
    // This is also editable for workflows where subsystem defaults are configured in code or data.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Visual")
    TSubclassOf<AWorldForgeColonist> ColonistActorClass;

private:
    /**
     * Bound to SimulationTimeManager.OnSimulationStep.
     * Keeps visuals synced on fixed-step boundaries instead of per-frame polling.
     */
    UFUNCTION()
    void HandleSimulationStep(int32 StepNumber);

    /**
     * Internal helper to spawn exactly one colonist actor from one snapshot.
     */
    AWorldForgeColonist* SpawnActorFromData(const struct FAgentData& AgentData, float CellSize);

    /**
     * Internal helper to remove one actor safely.
     */
    void DestroyActorForAgent(int32 AgentID);

    /**
     * Reads the world-space cell size used by your visual actor.
     * Falls back to 200 if the voxel subsystem is not available yet.
     */
    float ResolveCellSize() const;

private:
    /** All active visual actors keyed by simulation AgentID */
    UPROPERTY(Transient)
    TMap<int32, TObjectPtr<AWorldForgeColonist>> ColonistActors;

    /** Settlement this manager is currently tracking */
    UPROPERTY(Transient)
    int32 ActiveSettlementID = INDEX_NONE;

    /** Cached subsystem pointers */
    UPROPERTY(Transient)
    TObjectPtr<UProcessorManager> CachedProcessorManager = nullptr;

    UPROPERTY(Transient)
    TObjectPtr<USimulationTimeManager> CachedSimulationTimeManager = nullptr;

    /**
     * Re-entrancy guard in case sync is triggered from multiple places.
     * Not strictly required, but cheap safety.
     */
    bool bIsSyncInProgress = false;
};