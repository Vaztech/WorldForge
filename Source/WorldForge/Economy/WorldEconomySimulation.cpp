#include "WorldEconomySimulation.h"
#include "../Core/WorldForgeRNG.h"

void UWorldEconomySimulation::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[EconomySim] Initialized."));
}

void UWorldEconomySimulation::GenerateWorldEconomy(
    TArray<FCivilizationData>& Civs, const TArray<FWorldTileData>& Tiles,
    TArray<FCurrencyDefinition>& OutCurrencies, UWorldForgeRNG* RNG)
{
    AllCurrencies.Empty();
    AllGovernments.Empty();
    OutCurrencies.Empty();

    for (FCivilizationData& Civ : Civs)
    {
        if (Civ.bCollapsed) continue;

        const FGameplayTag GovType = DeriveGovernmentType(Civ.CulturalDNA, Civ.RaceType, RNG);
        Civ.GovernmentType = GovType;
        AllGovernments.Add(CreateGovernmentProfile(GovType));

        FCurrencyDefinition Cur = GenerateCurrencyForCiv(Civ, GovType, RNG);
        Civ.CurrencyTag = Cur.CurrencyTag;
        AllCurrencies.Add(Cur);
        OutCurrencies.Add(Cur);

        UE_LOG(LogTemp, Log, TEXT("[EconomySim] Civ %d Gov=%s Cur=%s Anchor=%.2f"),
            Civ.CivID, *GovType.ToString(), *Cur.DisplayName.ToString(), Cur.CurrentAnchorValue);
    }

    UE_LOG(LogTemp, Log, TEXT("[EconomySim] %d currencies generated"), AllCurrencies.Num());
}

FGameplayTag UWorldEconomySimulation::DeriveGovernmentType(
    const FCultureData& DNA, FGameplayTag RaceTag, UWorldForgeRNG* RNG) const
{
    if (DNA.Militarism >= 70.f && DNA.EconomicOpenness <= 40.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.MilitaryDictatorship")));
    if (DNA.TheologicalRigidity >= 70.f && DNA.HierarchyTolerance >= 60.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Theocracy")));
    if (DNA.EconomicOpenness >= 70.f && DNA.Militarism <= 40.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.FreeMarketRepublic")));
    if (DNA.HierarchyTolerance >= 70.f && DNA.Militarism >= 50.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.FeudalKingdom")));
    if (DNA.EconomicOpenness >= 60.f && DNA.Militarism >= 55.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.TradeConfederation")));
    if (DNA.MagicReverence >= 70.f && DNA.HierarchyTolerance >= 55.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.Magocracy")));
    if (DNA.HierarchyTolerance <= 30.f)
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.TribalConfederation")));
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.AquaticCouncil")));

    return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Government.MerchantOligarchy")));
}

FCurrencyDefinition UWorldEconomySimulation::GenerateCurrencyForCiv(
    const FCivilizationData& Civ, FGameplayTag GovType, UWorldForgeRNG* RNG) const
{
    FCurrencyDefinition C;
    const FString TagStr = FString::Printf(TEXT("WorldForge.Economy.Currency.Civ%d"), Civ.CivID);
    C.CurrencyTag = FGameplayTag::RequestGameplayTag(FName(*TagStr));

    if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))))
        C.DisplayName = FText::FromString(TEXT("Dwarven Gold Mark"));
    else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))))
        C.DisplayName = FText::FromString(TEXT("Elven Memory Crystal"));
    else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))))
        C.DisplayName = FText::FromString(TEXT("Oni Iron Honor-Mark"));
    else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
        C.DisplayName = FText::FromString(TEXT("Tidecaller Tide-Pearl"));
    else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin"))))
        C.DisplayName = FText::FromString(TEXT("Fae Dream-Mote"));
    else
        C.DisplayName = FText::FromString(FString::Printf(TEXT("Currency_%d"), Civ.CivID));

    C.IssuingCivID = Civ.CivID;
    C.GovernmentType = GovType;
    C.BaseAnchorValue = GetBaseAnchorValue(Civ.RaceType, RNG);
    C.CurrentAnchorValue = C.BaseAnchorValue;
    C.TrustState.MilitaryStability = C.TrustState.BackingStrength = 1.0f;
    C.TrustState.TradeReputation = C.TrustState.CosmicFavor = 1.0f;
    C.TrustState.PoliticalCohesion = C.TrustState.GovernmentCredibility = 1.0f;

    return C;
}

float UWorldEconomySimulation::GetBaseAnchorValue(
    FGameplayTag RaceTag, UWorldForgeRNG* RNG) const
{
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))))
        return 5.0f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 3.0f;
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))))
        return 8.0f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 7.0f;
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
        return 4.0f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 4.0f;
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin"))))
        return 6.0f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 6.0f;
    if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))))
        return 2.0f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 2.0f;

    return 2.5f + RNG->NextFloat(UWorldForgeRNG::BRANCH_ECONOMY) * 3.0f;
}

FGovernmentEconomicProfile UWorldEconomySimulation::CreateGovernmentProfile(
    FGameplayTag GovType) const
{
    FGovernmentEconomicProfile P;
    P.GovernmentType = GovType;

    const FString GS = GovType.ToString();
    if (GS.Contains(TEXT("FreeMarket")))
    {
        P.BaseTaxRate = 0.10f;
        P.TariffRate = 0.05f;
        P.EnforcementBudget = 0.3f;
        P.InnovationRate = 1.5f;
    }
    else if (GS.Contains(TEXT("MilitaryDictatorship")))
    {
        P.BaseTaxRate = 0.30f;
        P.TariffRate = 0.25f;
        P.EnforcementBudget = 0.9f;
        P.bAllowPrivateTrade = false;
        P.bStateMonopolyOnTrade = true;
        P.InnovationRate = 0.5f;
    }
    else if (GS.Contains(TEXT("Theocracy")))
    {
        P.BaseTaxRate = 0.20f;
        P.TariffRate = 0.15f;
        P.EnforcementBudget = 0.7f;
        P.bAllowForeignMerchants = false;
        P.InnovationRate = 0.7f;
    }
    else if (GS.Contains(TEXT("TradeConfederation")))
    {
        P.BaseTaxRate = 0.08f;
        P.TariffRate = 0.20f;
        P.EnforcementBudget = 0.6f;
        P.InnovationRate = 1.2f;
    }
    else
    {
        P.BaseTaxRate = 0.15f;
        P.TariffRate = 0.10f;
        P.EnforcementBudget = 0.5f;
        P.InnovationRate = 1.0f;
    }

    return P;
}

float UWorldEconomySimulation::GetExchangeRate(
    FGameplayTag From, FGameplayTag To) const
{
    const FCurrencyDefinition* F = FindCurrency(From);
    const FCurrencyDefinition* T = FindCurrency(To);

    if (!F || !T) return 1.0f;
    return F->CurrentAnchorValue / FMath::Max(0.001f, T->CurrentAnchorValue);
}

const FCurrencyDefinition* UWorldEconomySimulation::FindCurrency(FGameplayTag Tag) const
{
    for (const FCurrencyDefinition& C : AllCurrencies)
    {
        if (C.CurrencyTag == Tag)
            return &C;
    }

    return nullptr;
}

const FGovernmentEconomicProfile* UWorldEconomySimulation::GetGovernment(int32 CivID) const
{
    // Phase 1 stub: match by index -- civs and governments generated in same order
    // Phase 3 upgrade: use TMap<int32, FGovernmentEconomicProfile> keyed by CivID
    if (AllGovernments.IsValidIndex(CivID))
        return &AllGovernments[CivID];

    return nullptr;
}

void UWorldEconomySimulation::TickWorldEconomy(float, int32)
{
    /* Phase 3 TODO */
}