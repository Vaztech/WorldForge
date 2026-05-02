#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Mining/MiningTypes.h"
#include "MiningSubsystem.generated.h"

class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API UMiningSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable)
    bool CreateMiningDesignation(
        int32 SettlementID,
        FIntVector GridPosition,
        int32 CurrentStep,
        int32& OutDesignationID);

    UFUNCTION(BlueprintCallable)
    bool CancelMiningDesignation(int32 DesignationID);

    UFUNCTION(BlueprintCallable)
    bool TryReserveDesignation(
        int32 SettlementID,
        int32 AgentID,
        int32& OutDesignationID,
        FIntVector& OutGridPosition);

    UFUNCTION(BlueprintCallable)
    bool CompleteDesignation(int32 DesignationID, int32 AgentID);

    UFUNCTION(BlueprintCallable)
    bool FailDesignation(int32 DesignationID, int32 AgentID);

    UFUNCTION(BlueprintCallable)
    void GetPendingDesignations(
        int32 SettlementID,
        TArray<FMiningDesignation>& OutDesignations) const;

    UFUNCTION(BlueprintCallable)
    void GetAllDesignations(
        int32 SettlementID,
        TArray<FMiningDesignation>& OutDesignations) const;

private:
    UPROPERTY()
    TObjectPtr<UVoxelWorldManager> VoxelWorldManager = nullptr;

    UPROPERTY()
    TMap<int32, FMiningDesignation> Designations;

    int32 NextDesignationID = 1;

private:
    bool IsDesignationTerminal(const FMiningDesignation& Designation) const;

    UVoxelWorldManager* GetVoxelWorldManager();

    bool ResolveMineTarget(
        FIntVector GridPosition,
        FIntVector& OutResolvedGridPosition,
        FGameplayTag& OutBlockType);

    bool IsValidMineTarget(
        FIntVector GridPosition,
        FGameplayTag& OutBlockType);
};