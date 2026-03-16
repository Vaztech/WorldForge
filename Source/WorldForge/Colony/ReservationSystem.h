#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "ReservationSystem.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API UReservationSystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Returns ReservationID on success, -1 if target already reserved by another agent
    UFUNCTION(BlueprintCallable)
    int32 TryReserve(
        int32 OwnerID,
        int32 TargetID,
        const FGameplayTag& TargetTag,
        EReservationType Type,
        int32 SettlementID,
        int32 CurrentStep,
        int32 TimeoutSteps = 50
    );

    UFUNCTION(BlueprintCallable)
    void Release(int32 ReservationID);

    UFUNCTION(BlueprintCallable)
    void ReleaseAllForAgent(int32 AgentID);

    UFUNCTION(BlueprintCallable)
    void ReleaseAllForTarget(int32 TargetID, EReservationType Type);

    // ---- 5 cleanup triggers ----------------------------------------
    void CleanupExpiredReservations(int32 CurrentStep, int32 SettlementID); // T1: timeout
    void NotifyAgentDied(int32 AgentID);                                    // T2: death
    void NotifyJobCancelled(int32 AgentID);                                 // T3: cancel
    void NotifyTargetDestroyed(int32 TargetID, EReservationType Type);      // T4: destroyed
    void NotifySettlementUnloaded(int32 SettlementID);                      // T5: unload

    UFUNCTION(BlueprintCallable)
    bool IsReserved(int32 TargetID, EReservationType Type) const;

    UFUNCTION(BlueprintCallable)
    bool IsReservedByAgent(int32 TargetID, EReservationType Type, int32 AgentID) const;

    int32 GetActiveReservationCount() const { return ActiveReservations.Num(); }
    const TArray<FReservationRecord>& GetAllReservations() const { return ActiveReservations; }

    void RestoreFromSave(const TArray<FReservationRecord>& Saved);

private:
    TArray<FReservationRecord> ActiveReservations;
    int32 NextReservationID = 0;
};