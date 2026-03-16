#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "WorldEconomySimulation.generated.h"

class UWorldForgeRNG;

UCLASS()
class WORLDFORGE_API UWorldEconomySimulation : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Step 11 -- called from UWorldGenPipeline::Step11_GenerateEconomyLayer()
    void GenerateWorldEconomy(
        TArray<FCivilizationData>& Civs,
        const TArray<FWorldTileData>& Tiles,
        TArray<FCurrencyDefinition>& OutCurrencies,
        UWorldForgeRNG* RNG
    );

    // Runtime queries
    UFUNCTION(BlueprintCallable) float GetExchangeRate(FGameplayTag FromCurrency, FGameplayTag ToCurrency) const;
    const FCurrencyDefinition* FindCurrency(FGameplayTag CurrencyTag) const;
    const FGovernmentEconomicProfile* GetGovernment(int32 CivID) const;

    // Phase 3 TODO
    void TickWorldEconomy(float FixedDeltaTime, int32 PlayerSettlementID);

    // Save/load support
    const TArray<FCurrencyDefinition>& GetAllCurrencies()  const { return AllCurrencies; }
    const TArray<FGovernmentEconomicProfile>& GetAllGovernments() const { return AllGovernments; }

private:
    FGameplayTag               DeriveGovernmentType(const FCultureData& DNA, FGameplayTag RaceTag, UWorldForgeRNG* RNG) const;
    FCurrencyDefinition        GenerateCurrencyForCiv(const FCivilizationData& Civ, FGameplayTag GovType, UWorldForgeRNG* RNG) const;
    FGovernmentEconomicProfile CreateGovernmentProfile(FGameplayTag GovType) const;
    float                      GetBaseAnchorValue(FGameplayTag RaceTag, UWorldForgeRNG* RNG) const;

    TArray<FCurrencyDefinition>        AllCurrencies;
    TArray<FGovernmentEconomicProfile> AllGovernments;
};