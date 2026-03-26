#include "WorldEconomySimulation.h"

#include "../Core/WorldForgeRNG.h"
#include "SettlementManager.h"
#include "ProcessorManager.h"
#include "JobProcessor.h"
#include "SimulationTimeManager.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

namespace
{
	static FGameplayTag GetHarvestJobTagForItem(const FGameplayTag& ItemTag)
	{
		if (ItemTag == TAG_Item_Food)
		{
			return TAG_Job_Harvest_Food;
		}
		if (ItemTag == TAG_Item_Wood)
		{
			return TAG_Job_Harvest_Wood;
		}
		if (ItemTag == TAG_Item_Stone)
		{
			return TAG_Job_Harvest_Stone;
		}

		return FGameplayTag();
	}
}

void UWorldEconomySimulation::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegisterDefaultCommodities();
	RebuildLookupMapsFromArrays();

	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Initialized."));
}

void UWorldEconomySimulation::Deinitialize()
{
	TrackedItems.Empty();
	PendingNarrativeReports.Empty();
	LastNarrativeReportStep.Empty();
	PendingTrustDeltas.Empty();
	PriceHistory.Empty();
	SettlementPrices.Empty();
	TradeRoutes.Empty();
	CurrencyTrust.Empty();
	CivGovernments.Empty();
	AllCurrencies.Empty();
	AllGovernmentsArray.Empty();
	AllCurrenciesArray.Empty();

	Super::Deinitialize();
}

void UWorldEconomySimulation::RegisterDefaultCommodities()
{
	TrackedItems.Empty();
	TrackedItems.AddUnique(TAG_Item_Food);
	TrackedItems.AddUnique(TAG_Item_Wood);
	TrackedItems.AddUnique(TAG_Item_Stone);
}

// ── Legacy array/map sync helpers ───────────────────────────────────

void UWorldEconomySimulation::RebuildLookupMapsFromArrays()
{
	AllCurrencies.Empty();
	for (const FCurrencyDefinition& Currency : AllCurrenciesArray)
	{
		if (Currency.CurrencyTag.IsValid())
		{
			AllCurrencies.Add(Currency.CurrencyTag, Currency);
		}
	}

	CivGovernments.Empty();
	for (const FCurrencyDefinition& Currency : AllCurrenciesArray)
	{
		if (Currency.IssuingCivID >= 0)
		{
			if (const FGovernmentEconomicProfile* FoundGov = AllGovernmentsArray.FindByPredicate(
				[&Currency](const FGovernmentEconomicProfile& Gov)
				{
					return Gov.GovernmentType == Currency.GovernmentType;
				}))
			{
				CivGovernments.Add(Currency.IssuingCivID, *FoundGov);
			}
		}
	}
}

void UWorldEconomySimulation::RebuildArraysFromLookupMaps()
{
	AllCurrenciesArray.Empty();
	AllCurrencies.GenerateValueArray(AllCurrenciesArray);

	AllGovernmentsArray.Empty();
	CivGovernments.GenerateValueArray(AllGovernmentsArray);
}

// ── Phase 1 (preserved public API) ──────────────────────────────────

void UWorldEconomySimulation::GenerateWorldEconomy(
	TArray<FCivilizationData>& Civs,
	const TArray<FWorldTileData>& Tiles,
	TArray<FCurrencyDefinition>& OutCurrencies,
	UWorldForgeRNG* RNG)
{
	AllCurrenciesArray.Empty();
	AllGovernmentsArray.Empty();
	AllCurrencies.Empty();
	CivGovernments.Empty();
	CurrencyTrust.Empty();
	OutCurrencies.Empty();

	if (!RNG)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EconomySim] GenerateWorldEconomy called with null RNG."));
		return;
	}

	for (FCivilizationData& Civ : Civs)
	{
		if (Civ.bCollapsed)
		{
			continue;
		}

		const FGameplayTag GovType = DeriveGovernmentType(Civ.CulturalDNA, Civ.RaceType, RNG);
		Civ.GovernmentType = GovType;

		const FGovernmentEconomicProfile GovProfile = CreateGovernmentProfile(GovType, RNG);
		AllGovernmentsArray.Add(GovProfile);
		CivGovernments.Add(Civ.CivID, GovProfile);

		FCurrencyDefinition Currency = GenerateCurrencyForCiv(Civ, GovType, RNG);
		Currency.BaseAnchorValue = GetBaseAnchorValue(Civ, Tiles);
		Currency.CurrentAnchorValue = Currency.BaseAnchorValue;

		Civ.CurrencyTag = Currency.CurrencyTag;

		AllCurrenciesArray.Add(Currency);
		OutCurrencies.Add(Currency);
		AllCurrencies.Add(Currency.CurrencyTag, Currency);
		CurrencyTrust.Add(Currency.CurrencyTag, Currency.TrustState);

		UE_LOG(LogTemp, Log,
			TEXT("[EconomySim] Civ %d Gov=%s Cur=%s Anchor=%.2f"),
			Civ.CivID,
			*GovType.ToString(),
			*Currency.DisplayName.ToString(),
			Currency.CurrentAnchorValue);
	}

	UE_LOG(LogTemp, Log, TEXT("[EconomySim] %d currencies generated"), AllCurrenciesArray.Num());
}

void UWorldEconomySimulation::SimulateEconomicEpoch(
	EPreHistoryEpoch Epoch,
	int32 EpochYears,
	TArray<FCivilizationData>& Civs,
	TArray<FHistoryEventData>& OutHistory)
{
	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Epoch simulation stub. Epoch=%d Years=%d Civs=%d History=%d"),
		static_cast<int32>(Epoch),
		EpochYears,
		Civs.Num(),
		OutHistory.Num());
}

// ── Preserved helper logic, updated to merged header signatures ─────

FGameplayTag UWorldEconomySimulation::DeriveGovernmentType(
	const FCultureData& DNA,
	FGameplayTag RaceTag,
	UWorldForgeRNG* RNG) const
{
	(void)RNG;

	if (DNA.Militarism >= 70.f && DNA.EconomicOpenness <= 40.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.MilitaryDictatorship")));
	}
	if (DNA.TheologicalRigidity >= 70.f && DNA.HierarchyTolerance >= 60.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.Theocracy")));
	}
	if (DNA.EconomicOpenness >= 70.f && DNA.Militarism <= 40.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.FreeMarketRepublic")));
	}
	if (DNA.HierarchyTolerance >= 70.f && DNA.Militarism >= 50.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.FeudalKingdom")));
	}
	if (DNA.EconomicOpenness >= 60.f && DNA.Militarism >= 55.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.TradeConfederation")));
	}
	if (DNA.MagicReverence >= 70.f && DNA.HierarchyTolerance >= 55.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.Magocracy")));
	}
	if (DNA.HierarchyTolerance <= 30.f)
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.TribalConfederation")));
	}
	if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.AquaticCouncil")));
	}

	return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Gov.MerchantOligarchy")));
}

FCurrencyDefinition UWorldEconomySimulation::GenerateCurrencyForCiv(
	const FCivilizationData& Civ,
	const FGameplayTag& GovernmentType,
	UWorldForgeRNG* RNG) const
{
	FCurrencyDefinition Currency;

	// Use a valid registered currency tag instead of requesting dynamic tags
	// like WorldForge.Economy.Currency.CivX, which are not registered.
	Currency.CurrencyTag = FGameplayTag::RequestGameplayTag(
		FName(TEXT("WorldForge.Currency.Barter")));

	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))))
	{
		Currency.DisplayName = FText::FromString(TEXT("Dwarven Gold Mark"));
	}
	else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))))
	{
		Currency.DisplayName = FText::FromString(TEXT("Elven Memory Crystal"));
	}
	else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))))
	{
		Currency.DisplayName = FText::FromString(TEXT("Oni Iron Honor-Mark"));
	}
	else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
	{
		Currency.DisplayName = FText::FromString(TEXT("Tidecaller Tide-Pearl"));
	}
	else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin"))))
	{
		Currency.DisplayName = FText::FromString(TEXT("Fae Dream-Mote"));
	}
	else
	{
		Currency.DisplayName = FText::FromString(FString::Printf(TEXT("Currency_%d"), Civ.CivID));
	}

	Currency.IssuingCivID = Civ.CivID;
	Currency.GovernmentType = GovernmentType;
	Currency.BaseAnchorValue = GetBaseAnchorValue(Civ, TArray<FWorldTileData>());
	Currency.CurrentAnchorValue = Currency.BaseAnchorValue;

	if (RNG)
	{
		Currency.InflationRate = 0.0f;
	}

	Currency.TrustState.MilitaryStability = 1.0f;
	Currency.TrustState.BackingStrength = 1.0f;
	Currency.TrustState.TradeReputation = 1.0f;
	Currency.TrustState.CosmicFavor = 1.0f;
	Currency.TrustState.PoliticalCohesion = 1.0f;
	Currency.TrustState.GovernmentCredibility = 1.0f;

	return Currency;
}

FGovernmentEconomicProfile UWorldEconomySimulation::CreateGovernmentProfile(
	const FGameplayTag& GovernmentType,
	UWorldForgeRNG* RNG) const
{
	(void)RNG;

	FGovernmentEconomicProfile Profile;
	Profile.GovernmentType = GovernmentType;

	const FString GovString = GovernmentType.ToString();
	if (GovString.Contains(TEXT("FreeMarket")))
	{
		Profile.BaseTaxRate = 0.10f;
		Profile.TariffRate = 0.05f;
		Profile.EnforcementBudget = 0.3f;
		Profile.InnovationRate = 1.5f;
	}
	else if (GovString.Contains(TEXT("MilitaryDictatorship")))
	{
		Profile.BaseTaxRate = 0.30f;
		Profile.TariffRate = 0.25f;
		Profile.EnforcementBudget = 0.9f;
		Profile.bAllowPrivateTrade = false;
		Profile.bStateMonopolyOnTrade = true;
		Profile.InnovationRate = 0.5f;

		// Add real price-fix policies so authoritarian governments can
		// create shortages/surpluses and black markets during simulation.
		{
			FPriceFixPolicy FoodPolicy;
			FoodPolicy.ItemTag = TAG_Item_Food;
			FoodPolicy.FixType = EPriceFixType::Ceiling;
			FoodPolicy.FixedPrice = 2.0f;

			FPriceFixPolicy WoodPolicy;
			WoodPolicy.ItemTag = TAG_Item_Wood;
			WoodPolicy.FixType = EPriceFixType::Ceiling;
			WoodPolicy.FixedPrice = 2.5f;

			FPriceFixPolicy StonePolicy;
			StonePolicy.ItemTag = TAG_Item_Stone;
			StonePolicy.FixType = EPriceFixType::Ceiling;
			StonePolicy.FixedPrice = 2.5f;

			Profile.PriceFixPolicies.Add(FoodPolicy);
			Profile.PriceFixPolicies.Add(WoodPolicy);
			Profile.PriceFixPolicies.Add(StonePolicy);
		}
	}
	else if (GovString.Contains(TEXT("Theocracy")))
	{
		Profile.BaseTaxRate = 0.20f;
		Profile.TariffRate = 0.15f;
		Profile.EnforcementBudget = 0.7f;
		Profile.bAllowForeignMerchants = false;
		Profile.InnovationRate = 0.7f;
	}
	else if (GovString.Contains(TEXT("TradeConfederation")))
	{
		Profile.BaseTaxRate = 0.08f;
		Profile.TariffRate = 0.20f;
		Profile.EnforcementBudget = 0.6f;
		Profile.InnovationRate = 1.2f;
	}
	else
	{
		Profile.BaseTaxRate = 0.15f;
		Profile.TariffRate = 0.10f;
		Profile.EnforcementBudget = 0.5f;
		Profile.InnovationRate = 1.0f;
	}

	return Profile;
}

float UWorldEconomySimulation::GetBaseAnchorValue(
	const FCivilizationData& Civ,
	const TArray<FWorldTileData>& Tiles) const
{
	(void)Tiles;

	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf"))))
	{
		return 5.0f;
	}
	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf"))))
	{
		return 8.0f;
	}
	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller"))))
	{
		return 4.0f;
	}
	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin"))))
	{
		return 6.0f;
	}
	if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Oni"))))
	{
		return 2.0f;
	}

	return 2.5f;
}

// ── Phase 3A: main runtime tick ─────────────────────────────────────

void UWorldEconomySimulation::TickWorldEconomy(float FixedDeltaTime, int32 PlayerSettlementID)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
	UProcessorManager* ProcessorManager = World->GetSubsystem<UProcessorManager>();
	if (!SettlementManager || !ProcessorManager)
	{
		return;
	}

	const FSettlementState* Settlement = SettlementManager->GetSettlement(PlayerSettlementID);
	if (!Settlement)
	{
		return;
	}

	UpdateProduction(FixedDeltaTime, PlayerSettlementID, SettlementManager, ProcessorManager);
	UpdateConsumption(FixedDeltaTime, PlayerSettlementID, SettlementManager);
	UpdateFlow(FixedDeltaTime, PlayerSettlementID);
	UpdatePriceDiscovery(FixedDeltaTime, PlayerSettlementID);
	UpdateCurrencyTrust(FixedDeltaTime);

	EvaluatePriceFixPolicies(PlayerSettlementID, Settlement->OwnerCivID);
	UpdateBlackMarkets(PlayerSettlementID);
	CheckCurrencyCollapse();

	USimulationTimeManager* SimulationTimeManager = World->GetSubsystem<USimulationTimeManager>();
	const int32 CurrentStep = SimulationTimeManager ? SimulationTimeManager->GetTotalStepCount() : 0;
	ScanNarrativePatterns(PlayerSettlementID, CurrentStep);

	// Trust must update strictly after all force reads for determinism.
	ApplyPendingTrustDeltas();

	// Keep legacy accessors current.
	RebuildArraysFromLookupMaps();
}

// ── Force 1: Production ─────────────────────────────────────────────

void UWorldEconomySimulation::UpdateProduction(
	float DeltaTime,
	int32 SettlementID,
	USettlementManager* SettlementManager,
	UProcessorManager* ProcessorManager)
{
	if (!SettlementManager || !ProcessorManager)
	{
		return;
	}

	const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID);
	if (!Settlement)
	{
		return;
	}

	for (const FGameplayTag& ItemTag : TrackedItems)
	{
		FMarketPrice& Price = GetOrCreatePrice(SettlementID, ItemTag);

		int32 WorkerCount = 0;
		float TotalSkill = 0.0f;

		const FGameplayTag ExpectedJobTag = GetHarvestJobTagForItem(ItemTag);

		if (UJobProcessor* JobProcessor = ProcessorManager->GetTypedProcessor<UJobProcessor>())
		{
			const TArray<FAgentData>& Agents = ProcessorManager->GetAgentsForSettlement(SettlementID);

			for (const FAgentData& Agent : Agents)
			{
				if (Agent.State != EAgentState::Working)
				{
					continue;
				}

				if (ExpectedJobTag.IsValid() && Agent.CurrentJobTag != ExpectedJobTag)
				{
					continue;
				}

				++WorkerCount;

				const FGameplayTag SkillTag = JobProcessor->GetSkillTagForJob(Agent.CurrentJobTag);
				if (const float* FoundSkill = Agent.Skills.Find(SkillTag))
				{
					TotalSkill += *FoundSkill;
				}
				else
				{
					TotalSkill += 50.0f;
				}
			}
		}

		if (WorkerCount <= 0)
		{
			Price.SupplyPressure = FMath::Max(0.0f, Price.SupplyPressure - (0.1f * DeltaTime));
			continue;
		}

		const float AverageSkill = TotalSkill / static_cast<float>(WorkerCount);
		const float SkillMod = FMath::Clamp(AverageSkill / 50.0f, 0.0f, 2.0f);
		const float Disruption = 1.0f;

		const float RawOutput = FMath::FloorToFloat(
			static_cast<float>(WorkerCount) * SkillMod * Disruption);

		const float CurrentStock = FMath::Max(
			0.0f,
			static_cast<float>(SettlementManager->GetStockpileAmount(SettlementID, ItemTag)));

		const float ExpectedDemand = FMath::Max(
			1.0f,
			static_cast<float>(Settlement->Population) * Price.PriceElasticity * 0.5f);

		Price.SupplyPressure = FMath::Clamp(
			(CurrentStock + RawOutput) / ExpectedDemand,
			0.0f,
			3.0f);
	}
}

// ── Force 2: Consumption ────────────────────────────────────────────

void UWorldEconomySimulation::UpdateConsumption(
	float DeltaTime,
	int32 SettlementID,
	USettlementManager* SettlementManager)
{
	(void)DeltaTime;

	if (!SettlementManager)
	{
		return;
	}

	const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID);
	if (!Settlement || Settlement->Population <= 0)
	{
		return;
	}

	for (const FGameplayTag& ItemTag : TrackedItems)
	{
		FMarketPrice& Price = GetOrCreatePrice(SettlementID, ItemTag);

		float NeedWeight = 0.5f;
		if (ItemTag == TAG_Item_Food)
		{
			NeedWeight = 0.8f;
		}
		else if (ItemTag == TAG_Item_Wood)
		{
			NeedWeight = 0.4f;
		}
		else if (ItemTag == TAG_Item_Stone)
		{
			NeedWeight = 0.3f;
		}

		const float DemandRate =
			static_cast<float>(Settlement->Population) *
			NeedWeight *
			Price.PriceElasticity;

		const float CurrentStock = FMath::Max(
			0.0f,
			static_cast<float>(SettlementManager->GetStockpileAmount(SettlementID, ItemTag)));

		Price.DemandPressure = (CurrentStock > 0.0f)
			? FMath::Clamp(DemandRate / CurrentStock, 0.0f, 3.0f)
			: 2.0f;
	}
}

// ── Force 3: Flow ───────────────────────────────────────────────────

void UWorldEconomySimulation::UpdateFlow(float DeltaTime, int32 SettlementID)
{
	(void)DeltaTime;
	(void)SettlementID;

	// Phase 3A: no overworld trade routing yet.
	// TradeRoutes are persisted but not actively simulated until Phase 3B.
}

// ── Force 4: Price discovery ────────────────────────────────────────

void UWorldEconomySimulation::UpdatePriceDiscovery(float DeltaTime, int32 SettlementID)
{
	(void)DeltaTime;

	TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return;
	}

	for (FMarketPrice& Price : *Prices)
	{
		if (Price.SupplyPressure <= 0.0f)
		{
			Price.EquilibriumPrice =
				FMath::Min(Price.OfficialPrice * 1.5f, Price.BaseAnchorValue * 10.0f);
		}
		else
		{
			const float Elasticity = FMath::Clamp(Price.PriceElasticity, 0.1f, 3.0f);
			const float Demand = FMath::Max(0.0f, Price.DemandPressure);
			const float Supply = FMath::Max(0.01f, Price.SupplyPressure);

			Price.EquilibriumPrice = FMath::Clamp(
				Price.BaseAnchorValue * Demand / FMath::Pow(Supply, Elasticity),
				Price.BaseAnchorValue * 0.1f,
				Price.BaseAnchorValue * 10.0f);
		}

		const float ConvergenceRate = 0.05f;
		const float Delta = (Price.EquilibriumPrice - Price.OfficialPrice) * ConvergenceRate;

		Price.OfficialPrice =
			FMath::RoundToFloat((Price.OfficialPrice + Delta) * 100.0f) / 100.0f;

		TMap<FGameplayTag, TArray<float>>& SettlementHistory = PriceHistory.FindOrAdd(SettlementID);
		TArray<float>& ItemHistory = SettlementHistory.FindOrAdd(Price.ItemTag);
		ItemHistory.Add(Price.OfficialPrice);
		while (ItemHistory.Num() > 10)
		{
			ItemHistory.RemoveAt(0);
		}
	}
}

// ── Force 5: Currency trust ─────────────────────────────────────────

void UWorldEconomySimulation::UpdateCurrencyTrust(float DeltaTime)
{
	for (TPair<FGameplayTag, FCurrencyTrustState>& Pair : CurrencyTrust)
	{
		FCurrencyTrustState& Trust = Pair.Value;

		Trust.TrustVelocity = FMath::Lerp(Trust.TrustVelocity, 0.0f, 0.1f * DeltaTime);
		Trust.MilitaryStability = FMath::Clamp(
			Trust.MilitaryStability + (Trust.TrustVelocity * DeltaTime),
			0.0f,
			1.0f);

		const float AverageTrust =
			(Trust.MilitaryStability +
				Trust.BackingStrength +
				Trust.TradeReputation +
				Trust.CosmicFavor +
				Trust.PoliticalCohesion +
				Trust.GovernmentCredibility) / 6.0f;

		Trust.bInFreefall = AverageTrust < 0.3f;
	}

	for (TPair<FGameplayTag, FCurrencyDefinition>& CurrencyPair : AllCurrencies)
	{
		const FCurrencyTrustState* Trust = CurrencyTrust.Find(CurrencyPair.Key);
		if (!Trust)
		{
			continue;
		}

		const float Score =
			(Trust->MilitaryStability * 0.20f) +
			(Trust->BackingStrength * 0.20f) +
			(Trust->TradeReputation * 0.20f) +
			(Trust->CosmicFavor * 0.10f) +
			(Trust->PoliticalCohesion * 0.15f) +
			(Trust->GovernmentCredibility * 0.15f);

		CurrencyPair.Value.CurrentAnchorValue =
			FMath::Max(0.0f, CurrencyPair.Value.BaseAnchorValue * Score);

		CurrencyPair.Value.TrustState = *Trust;
	}
}

// ── Price fixing / black market ─────────────────────────────────────

void UWorldEconomySimulation::EvaluatePriceFixPolicies(int32 SettlementID, int32 OwnerCivID)
{
	if (OwnerCivID < 0)
	{
		return;
	}

	const FGovernmentEconomicProfile* Government = CivGovernments.Find(OwnerCivID);
	if (!Government)
	{
		return;
	}

	TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return;
	}

	for (FMarketPrice& Price : *Prices)
	{
		const FPriceFixPolicy* Policy = Government->PriceFixPolicies.FindByPredicate(
			[&Price](const FPriceFixPolicy& Candidate)
			{
				return Candidate.ItemTag == Price.ItemTag;
			});

		if (!Policy || Policy->FixType == EPriceFixType::None)
		{
			Price.bPriceFixed = false;
			Price.ActiveFixType = EPriceFixType::None;
			continue;
		}

		Price.bPriceFixed = true;
		Price.ActiveFixType = Policy->FixType;

		switch (Policy->FixType)
		{
		case EPriceFixType::Ceiling:
			Price.OfficialPrice = FMath::Min(Price.OfficialPrice, Policy->FixedPrice);
			break;

		case EPriceFixType::Floor:
			Price.OfficialPrice = FMath::Max(Price.OfficialPrice, Policy->FixedPrice);
			break;

		case EPriceFixType::StandardBoard:
		case EPriceFixType::Ration:
		case EPriceFixType::GuildMonopoly:
		case EPriceFixType::DivineMandated:
		case EPriceFixType::Requisition:
			Price.OfficialPrice = Policy->FixedPrice;
			break;

		default:
			break;
		}

		Price.OfficialPrice =
			FMath::RoundToFloat(Price.OfficialPrice * 100.0f) / 100.0f;
	}
}

void UWorldEconomySimulation::UpdateBlackMarkets(int32 SettlementID)
{
	TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return;
	}

	for (FMarketPrice& Price : *Prices)
	{
		if (!Price.bPriceFixed)
		{
			Price.BlackMarketPrice = 0.0f;
			continue;
		}

		const float Gap = Price.EquilibriumPrice - Price.OfficialPrice;
		const float GapRatio = (Price.OfficialPrice > 0.0f)
			? (Gap / Price.OfficialPrice)
			: 0.0f;

		if (GapRatio > 0.20f)
		{
			Price.BlackMarketPrice =
				FMath::RoundToFloat(Price.EquilibriumPrice * 1.15f * 100.0f) / 100.0f;
		}
		else if (GapRatio < 0.10f)
		{
			Price.BlackMarketPrice = 0.0f;
		}
	}
}

float UWorldEconomySimulation::ComputeBlackMarketGap(
	const FMarketPrice& Price,
	const FPriceFixPolicy& Policy) const
{
	(void)Policy;
	return FMath::Max(0.0f, Price.EquilibriumPrice - Price.OfficialPrice);
}

// ── Collapse detection ──────────────────────────────────────────────

void UWorldEconomySimulation::CheckCurrencyCollapse()
{
	for (TPair<FGameplayTag, FCurrencyDefinition>& Pair : AllCurrencies)
	{
		FCurrencyDefinition& Currency = Pair.Value;

		// MODIFIED:
		// Raise collapse sensitivity from 5% to 25% of base anchor value
		// so real long-run trust failures can actually surface in H2.
		if (Currency.CurrentAnchorValue < Currency.BaseAnchorValue * 0.25f)
		{
			if (!Currency.bInCrisis)
			{
				Currency.bInCrisis = true;
				UE_LOG(LogTemp, Warning,
					TEXT("[EconomySim] CURRENCY CRISIS: %s collapsed!"),
					*Currency.CurrencyTag.ToString());
			}
		}
		else
		{
			Currency.bInCrisis = false;
		}
	}
}

// ── Narrative pattern detection ─────────────────────────────────────

void UWorldEconomySimulation::ScanNarrativePatterns(int32 SettlementID, int32 CurrentStep)
{
	const TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return;
	}

	const TMap<FGameplayTag, TArray<float>>* SettlementHistory = PriceHistory.Find(SettlementID);

	for (const FMarketPrice& Price : *Prices)
	{
		if (Price.ItemTag != TAG_Item_Food)
		{
			continue;
		}

		if (SettlementHistory)
		{
			const TArray<float>* FoodHistory = SettlementHistory->Find(TAG_Item_Food);
			if (FoodHistory && FoodHistory->Num() >= 3)
			{
				int32 HighCount = 0;
				for (const float HistoricalPrice : *FoodHistory)
				{
					// MODIFIED:
					// Lower famine trigger from 300% of base to 180% of base
					// so real market stress can surface as narrative.
					if (HistoricalPrice > Price.BaseAnchorValue * 1.8f)
					{
						++HighCount;
					}
				}

				// MODIFIED:
				// Require 2 qualifying samples instead of 3.
				if (HighCount >= 2)
				{
					TryAddNarrativeReport(
						TAG_Economy_Pattern_Famine,
						FString::Printf(
							TEXT("FAMINE: Food at %.0f%% of base."),
							(Price.OfficialPrice / Price.BaseAnchorValue) * 100.0f),
						CurrentStep);
				}
			}
		}

		break;
	}

	for (const TPair<FGameplayTag, FCurrencyTrustState>& TrustPair : CurrencyTrust)
	{
		if (TrustPair.Value.bInFreefall)
		{
			const FCurrencyDefinition* Currency = AllCurrencies.Find(TrustPair.Key);
			const FString CurrencyName = Currency ? Currency->DisplayName.ToString() : TrustPair.Key.ToString();

			TryAddNarrativeReport(
				TAG_Economy_Pattern_CurrencyFreefall,
				FString::Printf(TEXT("CURRENCY CRISIS: %s in freefall."), *CurrencyName),
				CurrentStep);
		}
	}

	int32 BoomCount = 0;
	for (const FMarketPrice& Price : *Prices)
	{
		if (Price.BaseAnchorValue > 0.0f &&
			Price.OfficialPrice > Price.BaseAnchorValue * 1.5f)
		{
			++BoomCount;
		}
	}

	// MODIFIED:
	// You currently track only 3 commodities (Food/Wood/Stone),
	// so a boom threshold of 5 is unreachable. Set to 2 so the
	// existing sim can legitimately surface boom narratives.
	if (BoomCount >= 2)
	{
		TryAddNarrativeReport(
			TAG_Economy_Pattern_Boom,
			FString::Printf(TEXT("BOOM: %d commodities above 150%% base."), BoomCount),
			CurrentStep);
	}
}

bool UWorldEconomySimulation::IsNarrativeCooledDown(FGameplayTag PatternTag, int32 CurrentStep) const
{
	if (const int32* LastStep = LastNarrativeReportStep.Find(PatternTag))
	{
		return (CurrentStep - *LastStep) >= NarrativeCooldownSteps;
	}

	return true;
}

void UWorldEconomySimulation::TryAddNarrativeReport(
	FGameplayTag PatternTag,
	const FString& Report,
	int32 CurrentStep)
{
	if (!IsNarrativeCooledDown(PatternTag, CurrentStep))
	{
		return;
	}

	if (PendingNarrativeReports.Num() >= MaxNarrativeReports)
	{
		return;
	}

	PendingNarrativeReports.Add(Report);
	LastNarrativeReportStep.Add(PatternTag, CurrentStep);
}

TArray<FString> UWorldEconomySimulation::DrainNarrativeReports()
{
	TArray<FString> OutReports = PendingNarrativeReports;
	PendingNarrativeReports.Empty();
	return OutReports;
}

// ── Trust delta queue ───────────────────────────────────────────────

void UWorldEconomySimulation::QueueTrustDelta(
	FGameplayTag CurrencyTag,
	ETrustComponent Component,
	float Delta)
{
	FCurrencyTrustDelta TrustDelta;
	TrustDelta.CurrencyTag = CurrencyTag;
	TrustDelta.Component = Component;
	TrustDelta.Delta = Delta;
	PendingTrustDeltas.Add(TrustDelta);
}

void UWorldEconomySimulation::ApplyPendingTrustDeltas()
{
	for (const FCurrencyTrustDelta& Delta : PendingTrustDeltas)
	{
		FCurrencyTrustState* Trust = CurrencyTrust.Find(Delta.CurrencyTag);
		if (!Trust)
		{
			continue;
		}

		switch (Delta.Component)
		{
		case ETrustComponent::MilitaryStability:
			Trust->MilitaryStability =
				FMath::Clamp(Trust->MilitaryStability + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::BackingStrength:
			Trust->BackingStrength =
				FMath::Clamp(Trust->BackingStrength + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::TradeReputation:
			Trust->TradeReputation =
				FMath::Clamp(Trust->TradeReputation + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::CosmicFavor:
			Trust->CosmicFavor =
				FMath::Clamp(Trust->CosmicFavor + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::PoliticalCohesion:
			Trust->PoliticalCohesion =
				FMath::Clamp(Trust->PoliticalCohesion + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::GovernmentCredibility:
			Trust->GovernmentCredibility =
				FMath::Clamp(Trust->GovernmentCredibility + Delta.Delta, 0.0f, 1.0f);
			break;
		}
	}

	PendingTrustDeltas.Empty();
}

// ── Internal helpers ────────────────────────────────────────────────

FMarketPrice& UWorldEconomySimulation::GetOrCreatePrice(int32 SettlementID, FGameplayTag ItemTag)
{
	TArray<FMarketPrice>& Prices = SettlementPrices.FindOrAdd(SettlementID);

	if (FMarketPrice* FoundPrice = Prices.FindByPredicate(
		[&ItemTag](const FMarketPrice& Existing)
		{
			return Existing.ItemTag == ItemTag;
		}))
	{
		return *FoundPrice;
	}

	FMarketPrice NewPrice;
	NewPrice.ItemTag = ItemTag;
	NewPrice.SettlementID = SettlementID;
	NewPrice.BaseAnchorValue = 1.0f;
	NewPrice.OfficialPrice = 1.0f;
	NewPrice.EquilibriumPrice = 1.0f;
	NewPrice.BlackMarketPrice = 0.0f;
	NewPrice.SupplyPressure = 0.5f;
	NewPrice.DemandPressure = 0.5f;
	NewPrice.PriceElasticity = 0.7f;
	NewPrice.bPriceFixed = false;
	NewPrice.ActiveFixType = EPriceFixType::None;

	Prices.Add(NewPrice);
	return Prices.Last();
}

// ── Runtime queries ─────────────────────────────────────────────────

float UWorldEconomySimulation::GetItemPrice(
	FGameplayTag ItemTag,
	int32 SettlementID,
	bool bIncludeBlackMarket) const
{
	const TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return 1.0f;
	}

	const FMarketPrice* Price = Prices->FindByPredicate(
		[&ItemTag](const FMarketPrice& Entry)
		{
			return Entry.ItemTag == ItemTag;
		});

	if (!Price)
	{
		return 1.0f;
	}

	if (bIncludeBlackMarket && Price->BlackMarketPrice > 0.0f)
	{
		return Price->BlackMarketPrice;
	}

	return Price->OfficialPrice;
}

float UWorldEconomySimulation::GetExchangeRate(
	FGameplayTag FromCurrencyTag,
	FGameplayTag ToCurrencyTag) const
{
	const FCurrencyDefinition* FromCurrency = AllCurrencies.Find(FromCurrencyTag);
	const FCurrencyDefinition* ToCurrency = AllCurrencies.Find(ToCurrencyTag);

	if (!FromCurrency || !ToCurrency || ToCurrency->CurrentAnchorValue <= 0.0f)
	{
		return 1.0f;
	}

	return FromCurrency->CurrentAnchorValue / ToCurrency->CurrentAnchorValue;
}

const FGovernmentEconomicProfile* UWorldEconomySimulation::GetGovernment(int32 CivID) const
{
	if (const FGovernmentEconomicProfile* FoundGovernment = CivGovernments.Find(CivID))
	{
		return FoundGovernment;
	}

	if (AllGovernmentsArray.IsValidIndex(CivID))
	{
		return &AllGovernmentsArray[CivID];
	}

	return nullptr;
}

TArray<FMarketPrice> UWorldEconomySimulation::GetMarketReport(int32 SettlementID) const
{
	if (const TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID))
	{
		return *Prices;
	}

	return TArray<FMarketPrice>();
}

bool UWorldEconomySimulation::HasBlackMarket(FGameplayTag ItemTag, int32 SettlementID) const
{
	const TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices)
	{
		return false;
	}

	const FMarketPrice* Price = Prices->FindByPredicate(
		[&ItemTag](const FMarketPrice& Entry)
		{
			return Entry.ItemTag == ItemTag;
		});

	return Price && Price->BlackMarketPrice > 0.0f;
}

// ADDED
void UWorldEconomySimulation::ProcessCaravanTransaction(
	int32 BuyerSettlementID,
	int32 SellerSettlementID,
	int32 CaravanID)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>();
	if (!SettlementManager)
	{
		return;
	}

	const FSettlementState* BuyerSettlement = SettlementManager->GetSettlement(BuyerSettlementID);
	const FSettlementState* SellerSettlement = SettlementManager->GetSettlement(SellerSettlementID);

	if (!BuyerSettlement || !SellerSettlement)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EconomySim] Caravan transaction skipped. Invalid settlement(s). Caravan=%d Buyer=%d Seller=%d"),
			CaravanID,
			BuyerSettlementID,
			SellerSettlementID);
		return;
	}

	// ADDED
	// Recompute destination supply pressure from the stockpile state AFTER cargo delivery.
	// This intentionally does not touch prices directly. UpdatePriceDiscovery() will react
	// on the next economy tick, preserving determinism required by the guide.
	for (const FGameplayTag& ItemTag : TrackedItems)
	{
		FMarketPrice& Price = GetOrCreatePrice(BuyerSettlementID, ItemTag);

		const float CurrentStock = FMath::Max(
			0.0f,
			static_cast<float>(SettlementManager->GetStockpileAmount(BuyerSettlementID, ItemTag)));

		const float ExpectedDemand = FMath::Max(
			1.0f,
			static_cast<float>(BuyerSettlement->Population) * Price.PriceElasticity * 0.5f);

		Price.SupplyPressure = FMath::Clamp(
			CurrentStock / ExpectedDemand,
			0.0f,
			3.0f);
	}

	// ADDED
	// Apply/log currency exchange using existing settlement currency state.
	// PaymentAmount is not yet available inside EconomySimulation because caravan ownership
	// remains in the future Overworld subsystem. Keep this deterministic and log the rate now.
	const FGameplayTag BuyerCurrency = BuyerSettlement->PlayerCurrencyTag;
	const FGameplayTag SellerCurrency = SellerSettlement->PlayerCurrencyTag;
	const float ExchangeRate = GetExchangeRate(BuyerCurrency, SellerCurrency);

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Caravan transaction processed. Caravan=%d Buyer=%d Seller=%d BuyerCur=%s SellerCur=%s Rate=%.4f"),
		CaravanID,
		BuyerSettlementID,
		SellerSettlementID,
		*BuyerCurrency.ToString(),
		*SellerCurrency.ToString(),
		ExchangeRate);

	// ADDED
	// Keep legacy arrays in sync in case callers inspect economy state before next full tick.
	RebuildArraysFromLookupMaps();
}

// ── Phase 3B notification stubs ────────────────────────────────────

void UWorldEconomySimulation::NotifyWarStarted(int32 AttackerCivID, int32 DefenderCivID)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] War stub (Phase 3B). Attacker=%d Defender=%d"),
		AttackerCivID, DefenderCivID);
}

void UWorldEconomySimulation::NotifyWarEnded(int32 WinnerCivID, int32 LoserCivID)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Peace stub (Phase 3B). Winner=%d Loser=%d"),
		WinnerCivID, LoserCivID);
}

void UWorldEconomySimulation::NotifyPlagueEvent(int32 AffectedSettlementID, float Severity)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Plague stub (Phase 3B). Settlement=%d Severity=%.2f"),
		AffectedSettlementID, Severity);
}

void UWorldEconomySimulation::NotifyDisasterEvent(int32 TileID, FGameplayTag DisasterType)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Disaster stub (Phase 3B). Tile=%d Type=%s"),
		TileID, *DisasterType.ToString());
}

void UWorldEconomySimulation::NotifyGovernmentChange(int32 CivID, FGameplayTag NewGovernmentType)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] GovChange stub (Phase 3B). Civ=%d Gov=%s"),
		CivID, *NewGovernmentType.ToString());
}

void UWorldEconomySimulation::NotifyRouteOpened(int32 RouteID)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] RouteOpen stub (Phase 3B). Route=%d"), RouteID);
}

void UWorldEconomySimulation::NotifyRouteClosed(int32 RouteID, const FString& Reason)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] RouteClose stub (Phase 3B). Route=%d Reason=%s"),
		RouteID, *Reason);
}

void UWorldEconomySimulation::NotifyCivCollapse(int32 CollapsingCivID)
{
	UE_LOG(LogTemp, Log, TEXT("[EconomySim] CivCollapse stub (Phase 3B). Civ=%d"),
		CollapsingCivID);
}

// ── Save / Load ─────────────────────────────────────────────────────

FEconomySaveState UWorldEconomySimulation::GetEconomySaveState() const
{
	FEconomySaveState SaveState;

	SaveState.SettlementPrices.Empty();
	for (const TPair<int32, TArray<FMarketPrice>>& Pair : SettlementPrices)
	{
		FMarketPriceArray WrappedPrices;
		WrappedPrices.Items = Pair.Value;
		SaveState.SettlementPrices.Add(Pair.Key, WrappedPrices);
	}

	SaveState.PriceHistory.Empty();
	for (const TPair<int32, TMap<FGameplayTag, TArray<float>>>& SettlementPair : PriceHistory)
	{
		FSettlementPriceHistory WrappedHistory;

		for (const TPair<FGameplayTag, TArray<float>>& ItemPair : SettlementPair.Value)
		{
			FFloatHistoryArray FloatArray;
			FloatArray.Values = ItemPair.Value;
			WrappedHistory.ItemHistory.Add(ItemPair.Key, FloatArray);
		}

		SaveState.PriceHistory.Add(SettlementPair.Key, WrappedHistory);
	}

	SaveState.CurrencyTrust = CurrencyTrust;
	SaveState.AllCurrencies = AllCurrencies;
	SaveState.CivGovernments = CivGovernments;
	SaveState.TradeRoutes = TradeRoutes;
	SaveState.PendingTrustDeltas = PendingTrustDeltas;
	return SaveState;
}

void UWorldEconomySimulation::RestoreEconomyFromSave(const FEconomySaveState& SaveState)
{
	SettlementPrices.Empty();
	for (const TPair<int32, FMarketPriceArray>& Pair : SaveState.SettlementPrices)
	{
		SettlementPrices.Add(Pair.Key, Pair.Value.Items);
	}

	PriceHistory.Empty();
	for (const TPair<int32, FSettlementPriceHistory>& SettlementPair : SaveState.PriceHistory)
	{
		TMap<FGameplayTag, TArray<float>> RuntimeHistory;

		for (const TPair<FGameplayTag, FFloatHistoryArray>& ItemPair : SettlementPair.Value.ItemHistory)
		{
			RuntimeHistory.Add(ItemPair.Key, ItemPair.Value.Values);
		}

		PriceHistory.Add(SettlementPair.Key, RuntimeHistory);
	}

	CurrencyTrust = SaveState.CurrencyTrust;
	AllCurrencies = SaveState.AllCurrencies;
	CivGovernments = SaveState.CivGovernments;
	TradeRoutes = SaveState.TradeRoutes;
	PendingTrustDeltas = SaveState.PendingTrustDeltas;

	RebuildArraysFromLookupMaps();

	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Economy state restored."));
}