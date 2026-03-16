#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "SettlementManager.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API USettlementManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    int32 CreateSettlement(const FString& Name, int32 WorldTileID);

    UFUNCTION(BlueprintCallable)
    bool SettlementExists(int32 ID) const;

    UFUNCTION(BlueprintCallable)
    bool AddToStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable)
    bool ConsumeFromStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable)
    int32 GetStockpileAmount(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable)
    bool HasStockpile(int32 ID, const FGameplayTag& Tag, int32 Min) const;

    UFUNCTION(BlueprintCallable)
    void AddBuilding(int32 ID, const FGameplayTag& Tag);

    UFUNCTION(BlueprintCallable)
    bool HasBuilding(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable)
    TArray<FGameplayTag> GetBuildings(int32 ID) const;

    UFUNCTION(BlueprintCallable)
    FSettlementState GetSettlementState(int32 ID) const;

    UFUNCTION(BlueprintCallable)
    TArray<int32> GetAllSettlementIDs() const;

    FSettlementState* GetMutableSettlement(int32 ID);
    const FSettlementState* GetSettlement(int32 ID) const;

    void IncrementPopulation(int32 ID);
    void DecrementPopulation(int32 ID);
    int32 GetPopulation(int32 ID) const;

    const TArray<FSettlementState>& GetAllSettlements() const { return Settlements; }
    void RestoreFromSave(const TArray<FSettlementState>& Saved);

private:
    TArray<FSettlementState> Settlements;
    int32 NextSettlementID = 0;
};