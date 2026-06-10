#include "WorldEconomySimulation.h"

#include "Economy/ConsumptionSimulator.h"
#include "Economy/ProductionSimulator.h"
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

	static FGameplayTag TryRequestCurrencyTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	static FGameplayTag PickRegisteredCurrencyTagForCiv(const FCivilizationData& Civ, const FGameplayTag& GovernmentType)
	{
		TArray<FGameplayTag> Candidates;

		const FString GovString = GovernmentType.ToString();

		if (GovString.Contains(TEXT("Theocracy")))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.Tithe")));
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.Scrip")));
		}
		if (GovString.Contains(TEXT("Military")) || GovString.Contains(TEXT("Dictatorship")))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.StateIssue")));
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.IronMark")));
		}
		if (GovString.Contains(TEXT("Trade")) || GovString.Contains(TEXT("Market")) || Civ.CulturalDNA.EconomicOpenness >= 65.0f)
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.TradeCoin")));
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.MerchantNote")));
		}

		if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")), false))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.IronMark")));
		}
		else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.HighElf")), false))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.MemoryCrystal")));
		}
		else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Tidecaller")), false))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.TidePearl")));
		}
		else if (Civ.RaceType == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.FaeKin")), false))
		{
			Candidates.Add(TryRequestCurrencyTag(TEXT("WorldForge.Currency.DreamMote")));
		}

		for (const FGameplayTag& Candidate : Candidates)
		{
			if (Candidate.IsValid())
			{
				return Candidate;
			}
		}

		return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Currency.Barter")), false);
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
	CurrencyTrustByCivID.Empty();
	CivGovernments.Empty();
	AllCurrencies.Empty();
	AllCurrenciesByCivID.Empty();
	CurrencyTagToCivIDs.Empty();
	SettlementCurrencyBalances.Empty();
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
	AllCurrenciesByCivID.Empty();
	CurrencyTagToCivIDs.Empty();

	for (const FCurrencyDefinition& Currency : AllCurrenciesArray)
	{
		if (Currency.IssuingCivID >= 0)
		{
			AllCurrenciesByCivID.Add(Currency.IssuingCivID, Currency);
			CurrencyTagToCivIDs.FindOrAdd(Currency.CurrencyTag).AddUnique(Currency.IssuingCivID);
		}

		// Legacy compatibility map. If multiple civs share the same registered tag
		// (currently common when only WorldForge.Currency.Barter exists), do not
		// overwrite the first entry. Use AllCurrenciesByCivID for real identity.
		if (Currency.CurrencyTag.IsValid() && !AllCurrencies.Contains(Currency.CurrencyTag))
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

	if (AllCurrenciesByCivID.Num() > 0)
	{
		AllCurrenciesByCivID.GenerateValueArray(AllCurrenciesArray);
	}
	else
	{
		AllCurrencies.GenerateValueArray(AllCurrenciesArray);
	}

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
	AllCurrenciesByCivID.Empty();
	CurrencyTagToCivIDs.Empty();
	CivGovernments.Empty();
	CurrencyTrust.Empty();
	CurrencyTrustByCivID.Empty();
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

		AllCurrenciesByCivID.Add(Civ.CivID, Currency);
		CurrencyTagToCivIDs.FindOrAdd(Currency.CurrencyTag).AddUnique(Civ.CivID);

		// Legacy compatibility: this map cannot uniquely identify multiple civs
		// if they share a registered currency tag, so preserve the first entry.
		if (!AllCurrencies.Contains(Currency.CurrencyTag))
		{
			AllCurrencies.Add(Currency.CurrencyTag, Currency);
		}

		CurrencyTrustByCivID.Add(Civ.CivID, Currency.TrustState);
		if (!CurrencyTrust.Contains(Currency.CurrencyTag))
		{
			CurrencyTrust.Add(Currency.CurrencyTag, Currency.TrustState);
		}

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
	const float Years = FMath::Max(1.0f, static_cast<float>(EpochYears));

	for (FCivilizationData& Civ : Civs)
	{
		if (Civ.bCollapsed)
		{
			continue;
		}

		FCurrencyDefinition* Currency = AllCurrenciesByCivID.Find(Civ.CivID);
		if (!Currency)
		{
			continue;
		}

		const FGovernmentEconomicProfile* Gov = CivGovernments.Find(Civ.CivID);

		const float InnovationRate = Gov ? Gov->InnovationRate : 1.0f;
		const float TaxRate = Gov ? Gov->BaseTaxRate : 0.15f;
		const float TariffRate = Gov ? Gov->TariffRate : 0.10f;

		const float GrowthDelta = FMath::Clamp(
			((InnovationRate * 0.0125f) - ((TaxRate + TariffRate) * 0.015f)) * Years,
			-0.25f,
			0.25f);

		QueueTrustDeltaForCiv(Civ.CivID, ETrustComponent::BackingStrength, GrowthDelta);
		QueueTrustDeltaForCiv(Civ.CivID, ETrustComponent::TradeReputation, GrowthDelta * 0.5f);
		QueueTrustDeltaForCiv(Civ.CivID, ETrustComponent::GovernmentCredibility, GrowthDelta * 0.25f);
	}

	ApplyPendingTrustDeltas();
	UpdateCurrencyTrust(1.0f);
	CheckCurrencyCollapse();
	RebuildArraysFromLookupMaps();

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Epoch simulated. Epoch=%d Years=%d Civs=%d History=%d"),
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

	// Pick the best registered currency archetype tag available.
	// Unique civ identity is tracked by AllCurrenciesByCivID because gameplay tags
	// must be registered ahead of time and cannot safely be generated per civ.
	Currency.CurrencyTag = PickRegisteredCurrencyTagForCiv(Civ, GovernmentType);

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

		FProductionInputs ProductionInputs;
		ProductionInputs.WorkerCount = WorkerCount;
		ProductionInputs.SkillMod = FMath::Clamp(AverageSkill / 50.0f, 0.0f, 2.0f);
		ProductionInputs.BuildingMod = 1.0f;
		ProductionInputs.BiomeMod = 1.0f;
		ProductionInputs.DisruptionFactor = 1.0f;
		ProductionInputs.CosmicMod = 1.0f;

		UProductionSimulator* ProductionSimulator = NewObject<UProductionSimulator>(this);
		const int32 ProducedAmount = ProductionSimulator
			? FMath::Max(0, FMath::FloorToInt(ProductionSimulator->ComputeOutput(ProductionInputs)))
			: 0;

		if (ProducedAmount > 0)
		{
			SettlementManager->AddToStockpile(SettlementID, ItemTag, ProducedAmount);
			SettlementManager->SetVisualDirty(SettlementID);
		}

		const float CurrentStock = FMath::Max(
			0.0f,
			static_cast<float>(SettlementManager->GetStockpileAmount(SettlementID, ItemTag)));

		const float ExpectedDemand = FMath::Max(
			1.0f,
			static_cast<float>(Settlement->Population) * Price.PriceElasticity * 0.5f);

		Price.SupplyPressure = FMath::Clamp(
			CurrentStock / ExpectedDemand,
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

		FConsumptionInputs ConsumptionInputs;
		ConsumptionInputs.Population = Settlement->Population;
		ConsumptionInputs.NeedWeight = NeedWeight;
		ConsumptionInputs.SeasonMod = 1.0f;
		ConsumptionInputs.CulturalMod = 1.0f;
		ConsumptionInputs.PriceElasticity = Price.PriceElasticity;

		UConsumptionSimulator* ConsumptionSimulator = NewObject<UConsumptionSimulator>(this);
		const int32 DemandAmount = ConsumptionSimulator
			? FMath::Max(0, FMath::FloorToInt(ConsumptionSimulator->ComputeDemand(ConsumptionInputs)))
			: 0;

		const int32 StockBeforeConsumption = SettlementManager->GetStockpileAmount(SettlementID, ItemTag);
		const int32 ConsumedAmount = FMath::Min(StockBeforeConsumption, DemandAmount);

		if (ConsumedAmount > 0)
		{
			SettlementManager->ConsumeFromStockpile(SettlementID, ItemTag, ConsumedAmount);
			SettlementManager->SetVisualDirty(SettlementID);
		}

		const float CurrentStock = FMath::Max(
			0.0f,
			static_cast<float>(SettlementManager->GetStockpileAmount(SettlementID, ItemTag)));

		const float UnmetDemand = FMath::Max(0.0f, static_cast<float>(DemandAmount - ConsumedAmount));
		const float EffectiveDemand = static_cast<float>(DemandAmount) + UnmetDemand;

		Price.DemandPressure = (CurrentStock > 0.0f)
			? FMath::Clamp(EffectiveDemand / CurrentStock, 0.0f, 3.0f)
			: FMath::Clamp(1.0f + (UnmetDemand * 0.05f), 1.0f, 3.0f);
	}
}

// ── Force 3: Flow ───────────────────────────────────────────────────

void UWorldEconomySimulation::UpdateFlow(float DeltaTime, int32 SettlementID)
{
	TArray<FMarketPrice>* Prices = SettlementPrices.Find(SettlementID);
	if (!Prices || Prices->Num() <= 0 || TradeRoutes.Num() <= 0)
	{
		return;
	}

	float RouteStrength = 0.0f;
	int32 ConnectedRouteCount = 0;

	for (const TPair<int32, FTradeFlowState>& Pair : TradeRoutes)
	{
		const FTradeFlowState& Flow = Pair.Value;
		const bool bConnected =
			Flow.OriginID == SettlementID ||
			Flow.DestID == SettlementID;

		if (!bConnected)
		{
			continue;
		}

		const float CapacityFactor = FMath::Clamp(
			Flow.OfficialCapacity + Flow.UnofficialCapacity,
			0.0f,
			2.0f);

		RouteStrength +=
			FMath::Clamp(Flow.DiplomaticMod, 0.0f, 2.0f) *
			FMath::Clamp(Flow.SafetyFactor, 0.0f, 2.0f) *
			FMath::Clamp(Flow.TaxFactor, 0.0f, 2.0f) *
			FMath::Max(0.25f, CapacityFactor);

		++ConnectedRouteCount;
	}

	if (ConnectedRouteCount <= 0)
	{
		return;
	}

	RouteStrength /= static_cast<float>(ConnectedRouteCount);

	const float FlowAlpha = FMath::Clamp(
		DeltaTime * 0.02f * RouteStrength,
		0.0f,
		0.25f);

	for (FMarketPrice& Price : *Prices)
	{
		Price.SupplyPressure = FMath::Lerp(Price.SupplyPressure, 1.0f, FlowAlpha);
		Price.DemandPressure = FMath::Lerp(Price.DemandPressure, 1.0f, FlowAlpha * 0.5f);
	}
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
	for (TPair<int32, FCurrencyTrustState>& Pair : CurrencyTrustByCivID)
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

	for (TPair<int32, FCurrencyDefinition>& CurrencyPair : AllCurrenciesByCivID)
	{
		const FCurrencyTrustState* Trust = CurrencyTrustByCivID.Find(CurrencyPair.Key);
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

	CurrencyTrust.Empty();
	AllCurrencies.Empty();

	for (const TPair<int32, FCurrencyDefinition>& CurrencyPair : AllCurrenciesByCivID)
	{
		const FCurrencyDefinition& Currency = CurrencyPair.Value;

		if (!AllCurrencies.Contains(Currency.CurrencyTag))
		{
			AllCurrencies.Add(Currency.CurrencyTag, Currency);
		}

		if (!CurrencyTrust.Contains(Currency.CurrencyTag))
		{
			if (const FCurrencyTrustState* Trust = CurrencyTrustByCivID.Find(CurrencyPair.Key))
			{
				CurrencyTrust.Add(Currency.CurrencyTag, *Trust);
			}
		}
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
	for (TPair<int32, FCurrencyDefinition>& Pair : AllCurrenciesByCivID)
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

	for (const TPair<int32, FCurrencyTrustState>& TrustPair : CurrencyTrustByCivID)
	{
		if (TrustPair.Value.bInFreefall)
		{
			const FCurrencyDefinition* Currency = AllCurrenciesByCivID.Find(TrustPair.Key);
			const FString CurrencyName = Currency ? Currency->DisplayName.ToString() : FString::Printf(TEXT("Civ %d Currency"), TrustPair.Key);

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

void UWorldEconomySimulation::QueueTrustDeltaForCiv(
	int32 CivID,
	ETrustComponent Component,
	float Delta)
{
	if (CivID < 0)
	{
		return;
	}

	FCurrencyTrustDelta TrustDelta;
	TrustDelta.Component = Component;
	TrustDelta.Delta = Delta;

	if (const FCurrencyDefinition* Currency = AllCurrenciesByCivID.Find(CivID))
	{
		TrustDelta.CurrencyTag = Currency->CurrencyTag;
	}
	else
	{
		TrustDelta.CurrencyTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Currency.Barter")), false);
	}

	PendingTrustDeltasByCivID.FindOrAdd(CivID).Add(TrustDelta);
}

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

	if (const TArray<int32>* CivIDs = CurrencyTagToCivIDs.Find(CurrencyTag))
	{
		for (const int32 CivID : *CivIDs)
		{
			PendingTrustDeltasByCivID.FindOrAdd(CivID).Add(TrustDelta);
		}
	}
}

void UWorldEconomySimulation::ApplyPendingTrustDeltas()
{
	auto ApplyDeltaToTrust = [](FCurrencyTrustState& Trust, const FCurrencyTrustDelta& Delta)
	{
		switch (Delta.Component)
		{
		case ETrustComponent::MilitaryStability:
			Trust.MilitaryStability =
				FMath::Clamp(Trust.MilitaryStability + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::BackingStrength:
			Trust.BackingStrength =
				FMath::Clamp(Trust.BackingStrength + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::TradeReputation:
			Trust.TradeReputation =
				FMath::Clamp(Trust.TradeReputation + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::CosmicFavor:
			Trust.CosmicFavor =
				FMath::Clamp(Trust.CosmicFavor + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::PoliticalCohesion:
			Trust.PoliticalCohesion =
				FMath::Clamp(Trust.PoliticalCohesion + Delta.Delta, 0.0f, 1.0f);
			break;

		case ETrustComponent::GovernmentCredibility:
			Trust.GovernmentCredibility =
				FMath::Clamp(Trust.GovernmentCredibility + Delta.Delta, 0.0f, 1.0f);
			break;
		}
	};

	for (const TPair<int32, TArray<FCurrencyTrustDelta>>& CivDeltaPair : PendingTrustDeltasByCivID)
	{
		FCurrencyTrustState* Trust = CurrencyTrustByCivID.Find(CivDeltaPair.Key);
		if (!Trust)
		{
			continue;
		}

		for (const FCurrencyTrustDelta& Delta : CivDeltaPair.Value)
		{
			ApplyDeltaToTrust(*Trust, Delta);
		}
	}

	for (const FCurrencyTrustDelta& Delta : PendingTrustDeltas)
	{
		if (const TArray<int32>* CivIDs = CurrencyTagToCivIDs.Find(Delta.CurrencyTag))
		{
			for (const int32 CivID : *CivIDs)
			{
				if (FCurrencyTrustState* Trust = CurrencyTrustByCivID.Find(CivID))
				{
					ApplyDeltaToTrust(*Trust, Delta);
				}
			}
		}

		FCurrencyTrustState* LegacyTrust = CurrencyTrust.Find(Delta.CurrencyTag);
		if (LegacyTrust)
		{
			ApplyDeltaToTrust(*LegacyTrust, Delta);
		}
	}

	PendingTrustDeltas.Empty();
	PendingTrustDeltasByCivID.Empty();
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

float UWorldEconomySimulation::GetExchangeRateForCivs(
	int32 FromCivID,
	int32 ToCivID) const
{
	const FCurrencyDefinition* FromCurrency = AllCurrenciesByCivID.Find(FromCivID);
	const FCurrencyDefinition* ToCurrency = AllCurrenciesByCivID.Find(ToCivID);

	if (!FromCurrency || !ToCurrency || ToCurrency->CurrentAnchorValue <= 0.0f)
	{
		return 1.0f;
	}

	return FromCurrency->CurrentAnchorValue / ToCurrency->CurrentAnchorValue;
}

const FCurrencyDefinition* UWorldEconomySimulation::GetCurrencyForCiv(int32 CivID) const
{
	return AllCurrenciesByCivID.Find(CivID);
}

float UWorldEconomySimulation::GetSettlementCurrencyBalance(int32 SettlementID) const
{
	if (const float* Balance = SettlementCurrencyBalances.Find(SettlementID))
	{
		return *Balance;
	}

	return 0.0f;
}

void UWorldEconomySimulation::SetSettlementCurrencyBalance(int32 SettlementID, float NewBalance)
{
	if (SettlementID < 0)
	{
		return;
	}

	SettlementCurrencyBalances.Add(SettlementID, FMath::Max(0.0f, NewBalance));
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
	ProcessCaravanTransaction(
		BuyerSettlementID,
		SellerSettlementID,
		CaravanID,
		TMap<FGameplayTag, int32>(),
		0.0f,
		false);
}

void UWorldEconomySimulation::ProcessCaravanTransaction(
	int32 BuyerSettlementID,
	int32 SellerSettlementID,
	int32 CaravanID,
	const TMap<FGameplayTag, int32>& CargoByItem,
	float PaymentAmount,
	bool bTransferCargo)
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

	float CargoValueInBuyerCurrency = 0.0f;

	for (const TPair<FGameplayTag, int32>& CargoPair : CargoByItem)
	{
		const FGameplayTag& ItemTag = CargoPair.Key;
		const int32 Amount = FMath::Max(0, CargoPair.Value);

		if (!ItemTag.IsValid() || Amount <= 0)
		{
			continue;
		}

		const float UnitPrice = GetItemPrice(ItemTag, BuyerSettlementID, true);
		CargoValueInBuyerCurrency += UnitPrice * static_cast<float>(Amount);

		if (bTransferCargo)
		{
			const bool bSellerHadCargo = SettlementManager->ConsumeFromStockpile(
				SellerSettlementID,
				ItemTag,
				Amount);

			if (bSellerHadCargo)
			{
				SettlementManager->AddToStockpile(BuyerSettlementID, ItemTag, Amount);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[EconomySim] Caravan cargo missing at seller. Caravan=%d Seller=%d Item=%s Amount=%d"),
					CaravanID,
					SellerSettlementID,
					*ItemTag.ToString(),
					Amount);
			}
		}
	}

	const float EffectivePaymentInBuyerCurrency = (PaymentAmount > 0.0f)
		? PaymentAmount
		: CargoValueInBuyerCurrency;

	const int32 BuyerCivID = BuyerSettlement->OwnerCivID;
	const int32 SellerCivID = SellerSettlement->OwnerCivID;
	const float ExchangeRate = GetExchangeRateForCivs(BuyerCivID, SellerCivID);
	const float SellerReceives = EffectivePaymentInBuyerCurrency * ExchangeRate;

	if (EffectivePaymentInBuyerCurrency > 0.0f)
	{
		float& BuyerBalance = SettlementCurrencyBalances.FindOrAdd(BuyerSettlementID);
		float& SellerBalance = SettlementCurrencyBalances.FindOrAdd(SellerSettlementID);

		const float PaidByBuyer = FMath::Min(BuyerBalance, EffectivePaymentInBuyerCurrency);
		BuyerBalance = FMath::Max(0.0f, BuyerBalance - PaidByBuyer);
		SellerBalance += PaidByBuyer * ExchangeRate;

		if (PaidByBuyer + KINDA_SMALL_NUMBER < EffectivePaymentInBuyerCurrency)
		{
			QueueTrustDeltaForCiv(BuyerCivID, ETrustComponent::TradeReputation, -0.02f);
			UE_LOG(LogTemp, Warning,
				TEXT("[EconomySim] Caravan payment shortfall. Caravan=%d Buyer=%d Needed=%.2f Paid=%.2f"),
				CaravanID,
				BuyerSettlementID,
				EffectivePaymentInBuyerCurrency,
				PaidByBuyer);
		}
	}

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

	SettlementManager->SetVisualDirty(BuyerSettlementID);
	SettlementManager->SetVisualDirty(SellerSettlementID);

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Caravan transaction processed. Caravan=%d Buyer=%d Seller=%d CargoValueBuyerCur=%.2f PaymentBuyerCur=%.2f SellerReceives=%.2f Rate=%.4f TransferCargo=%s"),
		CaravanID,
		BuyerSettlementID,
		SellerSettlementID,
		CargoValueInBuyerCurrency,
		EffectivePaymentInBuyerCurrency,
		SellerReceives,
		ExchangeRate,
		bTransferCargo ? TEXT("true") : TEXT("false"));

	RebuildArraysFromLookupMaps();
}

// ── Phase 3B notification handlers ─────────────────────────────────

void UWorldEconomySimulation::NotifyWarStarted(int32 AttackerCivID, int32 DefenderCivID)
{
	auto ApplyWarShock = [this](int32 CivID)
		{
			QueueTrustDeltaForCiv(CivID, ETrustComponent::MilitaryStability, -0.20f);
			QueueTrustDeltaForCiv(CivID, ETrustComponent::TradeReputation, -0.15f);
			QueueTrustDeltaForCiv(CivID, ETrustComponent::PoliticalCohesion, -0.08f);
		};

	ApplyWarShock(AttackerCivID);
	ApplyWarShock(DefenderCivID);

	for (TPair<int32, FTradeFlowState>& Pair : TradeRoutes)
	{
		Pair.Value.DiplomaticMod = FMath::Clamp(
			Pair.Value.DiplomaticMod - 0.25f,
			0.0f,
			2.0f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] War started. Attacker=%d Defender=%d"),
		AttackerCivID,
		DefenderCivID);
}

void UWorldEconomySimulation::NotifyWarEnded(int32 WinnerCivID, int32 LoserCivID)
{
	QueueTrustDeltaForCiv(WinnerCivID, ETrustComponent::MilitaryStability, 0.12f);
	QueueTrustDeltaForCiv(WinnerCivID, ETrustComponent::GovernmentCredibility, 0.08f);
	QueueTrustDeltaForCiv(LoserCivID, ETrustComponent::MilitaryStability, -0.12f);
	QueueTrustDeltaForCiv(LoserCivID, ETrustComponent::PoliticalCohesion, -0.10f);

	for (TPair<int32, FTradeFlowState>& Pair : TradeRoutes)
	{
		Pair.Value.DiplomaticMod = FMath::Clamp(
			Pair.Value.DiplomaticMod + 0.10f,
			0.0f,
			2.0f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] War ended. Winner=%d Loser=%d"),
		WinnerCivID,
		LoserCivID);
}

void UWorldEconomySimulation::NotifyPlagueEvent(int32 AffectedSettlementID, float Severity)
{
	const float ClampedSeverity = FMath::Clamp(Severity, 0.0f, 1.0f);

	if (TArray<FMarketPrice>* Prices = SettlementPrices.Find(AffectedSettlementID))
	{
		for (FMarketPrice& Price : *Prices)
		{
			Price.SupplyPressure = FMath::Clamp(
				Price.SupplyPressure - (0.35f * ClampedSeverity),
				0.0f,
				3.0f);

			Price.DemandPressure = FMath::Clamp(
				Price.DemandPressure + (0.25f * ClampedSeverity),
				0.0f,
				3.0f);
		}
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Plague event applied. Settlement=%d Severity=%.2f"),
		AffectedSettlementID,
		ClampedSeverity);
}

void UWorldEconomySimulation::NotifyDisasterEvent(int32 TileID, FGameplayTag DisasterType)
{
	for (TPair<int32, TArray<FMarketPrice>>& SettlementPair : SettlementPrices)
	{
		for (FMarketPrice& Price : SettlementPair.Value)
		{
			if (Price.ItemTag == TAG_Item_Food || Price.ItemTag == TAG_Item_Wood)
			{
				Price.SupplyPressure = FMath::Clamp(
					Price.SupplyPressure - 0.10f,
					0.0f,
					3.0f);

				Price.DemandPressure = FMath::Clamp(
					Price.DemandPressure + 0.08f,
					0.0f,
					3.0f);
			}
		}
	}

	for (const TPair<int32, FCurrencyDefinition>& Pair : AllCurrenciesByCivID)
	{
		QueueTrustDeltaForCiv(Pair.Key, ETrustComponent::BackingStrength, -0.03f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Disaster event applied. Tile=%d Type=%s"),
		TileID,
		*DisasterType.ToString());
}

void UWorldEconomySimulation::NotifyGovernmentChange(int32 CivID, FGameplayTag NewGovernmentType)
{
	FGovernmentEconomicProfile NewProfile = CreateGovernmentProfile(NewGovernmentType, nullptr);
	CivGovernments.Add(CivID, NewProfile);

	if (FCurrencyDefinition* Currency = AllCurrenciesByCivID.Find(CivID))
	{
		Currency->GovernmentType = NewGovernmentType;

		QueueTrustDeltaForCiv(CivID, ETrustComponent::PoliticalCohesion, -0.08f);
		QueueTrustDeltaForCiv(CivID, ETrustComponent::GovernmentCredibility, 0.05f);
	}

	RebuildArraysFromLookupMaps();

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Government changed. Civ=%d Gov=%s"),
		CivID,
		*NewGovernmentType.ToString());
}

void UWorldEconomySimulation::NotifyRouteOpened(int32 RouteID)
{
	FTradeFlowState& Route = TradeRoutes.FindOrAdd(RouteID);
	Route.DiplomaticMod = FMath::Max(Route.DiplomaticMod, 1.0f);

	for (const TPair<int32, FCurrencyDefinition>& Pair : AllCurrenciesByCivID)
	{
		QueueTrustDeltaForCiv(Pair.Key, ETrustComponent::TradeReputation, 0.02f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Trade route opened. Route=%d"),
		RouteID);
}

void UWorldEconomySimulation::NotifyRouteClosed(int32 RouteID, const FString& Reason)
{
	if (FTradeFlowState* Route = TradeRoutes.Find(RouteID))
	{
		Route->DiplomaticMod = 0.0f;
	}

	for (const TPair<int32, FCurrencyDefinition>& Pair : AllCurrenciesByCivID)
	{
		QueueTrustDeltaForCiv(Pair.Key, ETrustComponent::TradeReputation, -0.04f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Trade route closed. Route=%d Reason=%s"),
		RouteID,
		*Reason);
}

void UWorldEconomySimulation::NotifyCivCollapse(int32 CollapsingCivID)
{
	if (FCurrencyDefinition* Currency = AllCurrenciesByCivID.Find(CollapsingCivID))
	{
		QueueTrustDeltaForCiv(CollapsingCivID, ETrustComponent::MilitaryStability, -1.0f);
		QueueTrustDeltaForCiv(CollapsingCivID, ETrustComponent::BackingStrength, -1.0f);
		QueueTrustDeltaForCiv(CollapsingCivID, ETrustComponent::TradeReputation, -1.0f);
		QueueTrustDeltaForCiv(CollapsingCivID, ETrustComponent::PoliticalCohesion, -1.0f);
		QueueTrustDeltaForCiv(CollapsingCivID, ETrustComponent::GovernmentCredibility, -1.0f);

		Currency->bInCrisis = true;
	}

	for (TPair<int32, FTradeFlowState>& Pair : TradeRoutes)
	{
		Pair.Value.DiplomaticMod = FMath::Clamp(
			Pair.Value.DiplomaticMod - 0.20f,
			0.0f,
			2.0f);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] Civ collapse applied. Civ=%d"),
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

	// Save legacy tag-keyed maps for compatibility. Runtime unique identity is rebuilt
	// from the civ-keyed maps while this subsystem remains alive.
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

	AllCurrenciesByCivID.Empty();
	CurrencyTrustByCivID.Empty();
	CurrencyTagToCivIDs.Empty();

	for (const TPair<FGameplayTag, FCurrencyDefinition>& Pair : AllCurrencies)
	{
		const FCurrencyDefinition& Currency = Pair.Value;
		if (Currency.IssuingCivID >= 0)
		{
			AllCurrenciesByCivID.Add(Currency.IssuingCivID, Currency);
			CurrencyTagToCivIDs.FindOrAdd(Currency.CurrencyTag).AddUnique(Currency.IssuingCivID);

			if (const FCurrencyTrustState* Trust = CurrencyTrust.Find(Currency.CurrencyTag))
			{
				CurrencyTrustByCivID.Add(Currency.IssuingCivID, *Trust);
			}
		}
	}

	RebuildArraysFromLookupMaps();

	UE_LOG(LogTemp, Log, TEXT("[EconomySim] Economy state restored."));
}

int32 UWorldEconomySimulation::SetDiplomaticModForSettlementPairs(
	const TSet<int32>& FromSettlementIDs,
	const TSet<int32>& ToSettlementIDs,
	float DiplomaticMod
)
{
	int32 ModifiedCount = 0;

	for (TPair<int32, FTradeFlowState>& Pair : TradeRoutes)
	{
		FTradeFlowState& Flow = Pair.Value;

		const bool bForwardMatch =
			FromSettlementIDs.Contains(Flow.OriginID) &&
			ToSettlementIDs.Contains(Flow.DestID);

		const bool bReverseMatch =
			FromSettlementIDs.Contains(Flow.DestID) &&
			ToSettlementIDs.Contains(Flow.OriginID);

		if (!bForwardMatch && !bReverseMatch)
		{
			continue;
		}

		Flow.DiplomaticMod = FMath::Clamp(DiplomaticMod, 0.0f, 2.0f);
		++ModifiedCount;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[EconomySim] SetDiplomaticModForSettlementPairs applied Mod=%.2f Count=%d From=%d To=%d"),
		DiplomaticMod,
		ModifiedCount,
		FromSettlementIDs.Num(),
		ToSettlementIDs.Num());

	return ModifiedCount;
}
