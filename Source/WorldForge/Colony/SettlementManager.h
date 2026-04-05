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

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 CreateSettlement(const FString& Name, int32 WorldTileID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool SettlementExists(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool AddToStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool ConsumeFromStockpile(int32 ID, const FGameplayTag& Tag, int32 Amount);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 GetStockpileAmount(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool HasStockpile(int32 ID, const FGameplayTag& Tag, int32 Min) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void AddBuilding(int32 ID, const FGameplayTag& Tag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    bool HasBuilding(int32 ID, const FGameplayTag& Tag) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    TArray<FGameplayTag> GetBuildings(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    FSettlementState GetSettlementState(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    TArray<int32> GetAllSettlementIDs() const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void SetPlayerCurrency(int32 SettlementID, FGameplayTag CurrencyTag);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    void SetVisualDirty(int32 SettlementID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    bool IsVisualDirty(int32 SettlementID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement|Visuals")
    void ClearVisualDirty(int32 SettlementID);

    FSettlementState* GetMutableSettlement(int32 ID);
    const FSettlementState* GetSettlement(int32 ID) const;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void IncrementPopulation(int32 ID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    void DecrementPopulation(int32 ID);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|Settlement")
    int32 GetPopulation(int32 ID) const;

    const TArray<FSettlementState>& GetAllSettlements() const { return Settlements; }
    void RestoreFromSave(const TArray<FSettlementState>& Saved);

private:
    TArray<FSettlementState> Settlements;
    int32 NextSettlementID = 0;

    UPROPERTY(Transient)
    TSet<int32> VisualDirtySettlements;
};