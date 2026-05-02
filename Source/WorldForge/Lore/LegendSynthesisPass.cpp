// LegendSynthesisPass.cpp — Legend synthesis from lore cross-references

#include "LegendSynthesisPass.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogLegendSynthesis, Log, All);

// ─────────────────────────────────────────────
//  Narrative templates
// ─────────────────────────────────────────────

namespace LegendTemplates
{
	// Artifact-centered legends
	static const TArray<FString> ArtifactCreation = {
		TEXT("In the {EPOCH}th age, {FIGURE} of the {CIV} forged {ARTIFACT} from star-metal and spite. Those who held it felt the weight of its maker's will."),
		TEXT("The {CIV} say that {ARTIFACT} was not made but found — drawn from the earth by {FIGURE}, who alone could hear its call."),
		TEXT("{FIGURE} labored for a year and a day to craft {ARTIFACT}. When finished, even the {CIV} elders knelt before its power."),
	};

	static const TArray<FString> ArtifactLost = {
		TEXT("When {CITY} fell, {ARTIFACT} vanished into the rubble. Seekers have searched for generations, but the ruins keep their secrets."),
		TEXT("{ARTIFACT} was last seen in the burning towers of {CITY}. Some say it still glows beneath the ash, waiting."),
		TEXT("The fall of {CITY} claimed many treasures, but none so mourned as {ARTIFACT}. Its loss haunts the {CIV} to this day."),
	};

	// City-centered legends
	static const TArray<FString> CityTragicFall = {
		TEXT("They called {CITY} unconquerable. The {CIV} built it to last ten thousand years. It lasted {DURATION}."),
		TEXT("The {CAUSE} that took {CITY} came without warning. By the time the {CIV} rallied, there was nothing left to save."),
		TEXT("Travelers still claim to hear bells ringing from {CITY} on windless nights — echoes of the day it died."),
	};

	static const TArray<FString> CityWarFall = {
		TEXT("The siege of {CITY} lasted forty days. When the {CONQUEROR} breached the walls, the {CIV} fought in the streets until the stones ran red."),
		TEXT("{CITY} did not fall in battle — it fell to betrayal. A gate left open, a guard bought with {CONQUEROR} gold."),
		TEXT("The {CONQUEROR} razed {CITY} so thoroughly that grass grew where towers once stood within a single season."),
	};

	// Figure-centered legends
	static const TArray<FString> FigureHeroic = {
		TEXT("They sing of {FIGURE} in every hall of the {CIV}. In the {EPOCH}th age, when all seemed lost, it was {FIGURE} who turned the tide."),
		TEXT("{FIGURE} walked where others feared. The {CIV} owe their survival to one who asked for nothing in return."),
		TEXT("No chronicle of the {CIV} is complete without {FIGURE}, whose deeds in the {EPOCH}th age passed from history into myth."),
	};

	static const TArray<FString> FigureBetrayal = {
		TEXT("{FIGURE} was trusted above all in the {CIV}. That trust was the weapon that cut deepest when {FIGURE} turned."),
		TEXT("The {CIV} do not speak the name {FIGURE} aloud. To betray one's own in the {EPOCH}th age — some wounds do not heal."),
	};

	// Cause strings for city legends
	static const TMap<ECityFallCause, FString> CauseStrings = {
		{ ECityFallCause::Plague,          TEXT("plague") },
		{ ECityFallCause::Famine,          TEXT("famine") },
		{ ECityFallCause::NaturalDisaster, TEXT("cataclysm") },
		{ ECityFallCause::CosmicWrath,     TEXT("cosmic wrath") },
		{ ECityFallCause::CivilWar,        TEXT("civil war") },
		{ ECityFallCause::Abandonment,     TEXT("slow decay") },
	};
}

// ─────────────────────────────────────────────
//  Template expansion helper
// ─────────────────────────────────────────────

static FString ExpandTemplate(FString Template, const TMap<FString, FString>& Vars)
{
	for (const auto& KV : Vars)
	{
		Template = Template.Replace(*FString::Printf(TEXT("{%s}"), *KV.Key), *KV.Value);
	}
	return Template;
}

// ─────────────────────────────────────────────
//  Execute
// ─────────────────────────────────────────────

void FLegendSynthesisPass::Execute(FLoreGenerationContext& Ctx)
{
	UE_LOG(LogLegendSynthesis, Log,
		TEXT("LegendSynthesisPass — BEGIN (artifacts:%d, cities:%d, figures:%d)"),
		Ctx.LoreState.Artifacts.Num(),
		Ctx.LoreState.FallenCities.Num(),
		Ctx.Figures.Num());

	int32 LegendIndex = 0;

	// ── Phase 1: Artifact legends ──
	for (const FLoreArtifact& Artifact : Ctx.LoreState.Artifacts)
	{
		// High-power or trait-heavy artifacts get legends
		if (Artifact.PowerRating >= 0.6f || Artifact.TraitTags.Num() >= 2)
		{
			TrySynthesizeArtifactLegend(Artifact, Ctx, LegendIndex);
		}
		else if (Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_COSMIC) < 0.25f)
		{
			// Low-power artifacts have a small chance
			TrySynthesizeArtifactLegend(Artifact, Ctx, LegendIndex);
		}
	}

	// ── Phase 2: Fallen city legends ──
	for (const FLoreFallenCity& City : Ctx.LoreState.FallenCities)
	{
		// Dangerous ruins and artifact-holding ruins always get legends
		if (City.DangerRating >= 0.5f || City.ArtifactsLostHere.Num() > 0)
		{
			TrySynthesizeFallenCityLegend(City, Ctx, LegendIndex);
		}
		else if (Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_COSMIC) < 0.40f)
		{
			TrySynthesizeFallenCityLegend(City, Ctx, LegendIndex);
		}
	}

	// ── Phase 3: Figure legends ──
	for (const FHistoricalFigure& Figure : Ctx.Figures)
	{
		// Prominent figures (high legacy score) always get legends
		if (Figure.LegacyScore >= 0.7f)
		{
			TrySynthesizeFigureLegend(Figure, Ctx, LegendIndex);
		}
		else if (Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_COSMIC) < 0.20f)
		{
			TrySynthesizeFigureLegend(Figure, Ctx, LegendIndex);
		}
	}

	// Compute renown for all legends
	for (FLoreLegend& Legend : Ctx.LoreState.Legends)
	{
		Legend.Renown = ComputeRenown(Legend, Ctx);
	}

	UE_LOG(LogLegendSynthesis, Log, TEXT("LegendSynthesisPass — END | %d legends synthesized"),
		Ctx.LoreState.Legends.Num());
}

// ─────────────────────────────────────────────
//  Artifact legends
// ─────────────────────────────────────────────

bool FLegendSynthesisPass::TrySynthesizeArtifactLegend(
	const FLoreArtifact& Artifact,
	FLoreGenerationContext& Ctx,
	int32& LegendIndex)
{
	const FCivilizationData* Civ = Ctx.FindCiv(Artifact.ForgedByCivID);
	if (!Civ) return false;

	FLoreLegend Legend;
	Legend.LegendID = MakeLegendID(LegendIndex++);
	Legend.Epoch = Artifact.ForgedInEpoch;
	Legend.ReferencedArtifactIDs.Add(Artifact.ArtifactID);
	Legend.ReferencedCivIDs.Add(Civ->CivID);

	TMap<FString, FString> Vars;
	Vars.Add(TEXT("ARTIFACT"), Artifact.DisplayName.ToString());
	Vars.Add(TEXT("CIV"), Civ->Name);
	Vars.Add(TEXT("EPOCH"), FString::FromInt(Artifact.ForgedInEpoch));

	// If the artifact has a maker figure
	const FHistoricalFigure* Figure = Ctx.FindFigure(Artifact.ForgedByFigureID);
	if (Figure)
	{
		Vars.Add(TEXT("FIGURE"), Figure->Name.ToString());
		Legend.ReferencedFigureIDs.Add(Figure->FigureID);
	}
	else
	{
		Vars.Add(TEXT("FIGURE"), TEXT("an unnamed smith"));
	}

	// Choose template: creation legend or lost legend
	if (!Artifact.LostInCityID.IsNone())
	{
		// Lost artifact legend — reference the city
		const FLoreFallenCity* City = Ctx.LoreState.FindFallenCity(Artifact.LostInCityID);
		if (City)
		{
			Vars.Add(TEXT("CITY"), City->DisplayName.ToString());
			Legend.ReferencedCityIDs.Add(City->CityID);
		}
		else
		{
			Vars.Add(TEXT("CITY"), TEXT("a nameless ruin"));
		}

		Legend.LegendType = EWorldForgeLoreLegendType::TragicFall;
		const auto& Templates = LegendTemplates::ArtifactLost;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		FString Narrative = ExpandTemplate(Templates[TIdx], Vars);

		Legend.Title = FText::FromString(
			FString::Printf(TEXT("The Loss of %s"), *Artifact.DisplayName.ToString()));
		Legend.NarrativeText = FText::FromString(Narrative);
	}
	else
	{
		Legend.LegendType = EWorldForgeLoreLegendType::Creation;
		const auto& Templates = LegendTemplates::ArtifactCreation;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		FString Narrative = ExpandTemplate(Templates[TIdx], Vars);

		Legend.Title = FText::FromString(
			FString::Printf(TEXT("The Forging of %s"), *Artifact.DisplayName.ToString()));
		Legend.NarrativeText = FText::FromString(Narrative);
	}

	Legend.CosmicDomain = ChooseCosmicDomain(Legend.LegendType, Ctx.RNG);
	Ctx.LoreState.Legends.Add(MoveTemp(Legend));
	return true;
}

// ─────────────────────────────────────────────
//  Fallen city legends
// ─────────────────────────────────────────────

bool FLegendSynthesisPass::TrySynthesizeFallenCityLegend(
	const FLoreFallenCity& City,
	FLoreGenerationContext& Ctx,
	int32& LegendIndex)
{
	const FCivilizationData* OrigCiv = Ctx.FindCiv(City.OriginalCivID);
	if (!OrigCiv) return false;

	FLoreLegend Legend;
	Legend.LegendID = MakeLegendID(LegendIndex++);
	Legend.Epoch = City.FellInEpoch;
	Legend.ReferencedCityIDs.Add(City.CityID);
	Legend.ReferencedCivIDs.Add(OrigCiv->CivID);

	TMap<FString, FString> Vars;
	Vars.Add(TEXT("CITY"), City.DisplayName.ToString());
	Vars.Add(TEXT("CIV"), OrigCiv->Name);
	Vars.Add(TEXT("EPOCH"), FString::FromInt(City.FellInEpoch));

	int32 Duration = City.FellInEpoch - City.FoundedInEpoch;
	Vars.Add(TEXT("DURATION"),
		FString::Printf(TEXT("%d epoch%s"), Duration, Duration != 1 ? TEXT("s") : TEXT("")));

	FString Narrative;

	if (City.CauseOfFall == ECityFallCause::War && City.ConquerorCivID >= 0)
	{
		const FCivilizationData* Conqueror = Ctx.FindCiv(City.ConquerorCivID);
		Vars.Add(TEXT("CONQUEROR"),
			Conqueror ? Conqueror->Name : FString(TEXT("an unknown horde")));
		if (Conqueror)
		{
			Legend.ReferencedCivIDs.AddUnique(Conqueror->CivID);
		}

		Legend.LegendType = EWorldForgeLoreLegendType::HeroicBattle;
		const auto& Templates = LegendTemplates::CityWarFall;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		Narrative = ExpandTemplate(Templates[TIdx], Vars);
	}
	else
	{
		// Find cause string
		const FString* CauseStr = LegendTemplates::CauseStrings.Find(City.CauseOfFall);
		Vars.Add(TEXT("CAUSE"), CauseStr ? *CauseStr : TEXT("an unknown calamity"));

		Legend.LegendType = EWorldForgeLoreLegendType::TragicFall;
		const auto& Templates = LegendTemplates::CityTragicFall;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		Narrative = ExpandTemplate(Templates[TIdx], Vars);
	}

	Legend.Title = FText::FromString(
		FString::Printf(TEXT("The Fall of %s"), *City.DisplayName.ToString()));
	Legend.NarrativeText = FText::FromString(Narrative);
	Legend.CosmicDomain = ChooseCosmicDomain(Legend.LegendType, Ctx.RNG);

	Ctx.LoreState.Legends.Add(MoveTemp(Legend));
	return true;
}

// ─────────────────────────────────────────────
//  Figure legends
// ─────────────────────────────────────────────

bool FLegendSynthesisPass::TrySynthesizeFigureLegend(
	const FHistoricalFigure& Figure,
	FLoreGenerationContext& Ctx,
	int32& LegendIndex)
{
	const FCivilizationData* Civ = Ctx.FindCiv(Figure.CivID);
	if (!Civ) return false;

	FLoreLegend Legend;
	Legend.LegendID = MakeLegendID(LegendIndex++);
	Legend.Epoch = Figure.BirthYear;
	Legend.ReferencedFigureIDs.Add(Figure.FigureID);
	Legend.ReferencedCivIDs.Add(Civ->CivID);

	TMap<FString, FString> Vars;
	Vars.Add(TEXT("FIGURE"), Figure.Name.ToString());
	Vars.Add(TEXT("CIV"), Civ->Name);
	Vars.Add(TEXT("EPOCH"), FString::FromInt(Figure.BirthYear));

	// Cross-reference: does this figure appear in any artifact?
	for (const FLoreArtifact& A : Ctx.LoreState.Artifacts)
	{
		if (A.ForgedByFigureID == Figure.FigureID)
		{
			Legend.ReferencedArtifactIDs.Add(A.ArtifactID);
		}
	}

	// Choose heroic vs betrayal based on figure skills/tags
	const FGameplayTag TraitorTag = FGameplayTag::RequestGameplayTag(FName("WorldForge.FigureTrait.Traitor"));
	bool bBetrayer = Figure.Skills.Contains(TraitorTag);

	FString Narrative;

	if (bBetrayer && Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_COSMIC) < 0.70f)
	{
		Legend.LegendType = EWorldForgeLoreLegendType::Betrayal;
		const auto& Templates = LegendTemplates::FigureBetrayal;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		Narrative = ExpandTemplate(Templates[TIdx], Vars);
		Legend.Title = FText::FromString(
			FString::Printf(TEXT("The Betrayal of %s"), *Figure.Name.ToString()));
	}
	else
	{
		Legend.LegendType = EWorldForgeLoreLegendType::HeroicBattle;
		const auto& Templates = LegendTemplates::FigureHeroic;
		int32 TIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Templates.Num() - 1);
		Narrative = ExpandTemplate(Templates[TIdx], Vars);
		Legend.Title = FText::FromString(
			FString::Printf(TEXT("The Saga of %s"), *Figure.Name.ToString()));
	}

	Legend.NarrativeText = FText::FromString(Narrative);
	Legend.CosmicDomain = ChooseCosmicDomain(Legend.LegendType, Ctx.RNG);

	Ctx.LoreState.Legends.Add(MoveTemp(Legend));
	return true;
}

// ─────────────────────────────────────────────
//  Cosmic domain assignment
// ─────────────────────────────────────────────

FGameplayTag FLegendSynthesisPass::ChooseCosmicDomain(EWorldForgeLoreLegendType Type, UWorldForgeRNG& RNG)
{
	// Map legend type to likely cosmic domain, with randomness
	static const TMap<EWorldForgeLoreLegendType, TArray<FName>> DomainMap = {
		{ EWorldForgeLoreLegendType::HeroicBattle, { FName("WorldForge.Domain.War"), FName("WorldForge.Domain.Death") } },
		{ EWorldForgeLoreLegendType::TragicFall,   { FName("WorldForge.Domain.Death"), FName("WorldForge.Domain.Nature") } },
		{ EWorldForgeLoreLegendType::Creation,     { FName("WorldForge.Domain.Creation"), FName("WorldForge.Domain.Knowledge") } },
		{ EWorldForgeLoreLegendType::Discovery,    { FName("WorldForge.Domain.Knowledge"), FName("WorldForge.Domain.Commerce") } },
		{ EWorldForgeLoreLegendType::Betrayal,     { FName("WorldForge.Domain.War"), FName("WorldForge.Domain.Commerce") } },
		{ EWorldForgeLoreLegendType::Prophecy,     { FName("WorldForge.Domain.Knowledge"), FName("WorldForge.Domain.Creation") } },
		{ EWorldForgeLoreLegendType::Migration,    { FName("WorldForge.Domain.Nature"), FName("WorldForge.Domain.Commerce") } },
	};

	const TArray<FName>* Domains = DomainMap.Find(Type);
	if (Domains && Domains->Num() > 0)
	{
		int32 Pick = RNG.NextInt(UWorldForgeRNG::BRANCH_COSMIC, 0, Domains->Num() - 1);
		return FGameplayTag::RequestGameplayTag((*Domains)[Pick]);
	}

	return FGameplayTag::RequestGameplayTag(FName("WorldForge.Domain.Creation"));
}

// ─────────────────────────────────────────────
//  Renown computation
// ─────────────────────────────────────────────

float FLegendSynthesisPass::ComputeRenown(
	const FLoreLegend& Legend, const FLoreGenerationContext& Ctx)
{
	float Score = 0.0f;

	// More cross-references = more widely known
	Score += Legend.ReferencedCivIDs.Num() * 0.15f;
	Score += Legend.ReferencedArtifactIDs.Num() * 0.20f;
	Score += Legend.ReferencedFigureIDs.Num() * 0.10f;
	Score += Legend.ReferencedCityIDs.Num() * 0.10f;

	// Heroic and betrayal legends spread further
	if (Legend.LegendType == EWorldForgeLoreLegendType::HeroicBattle) Score += 0.15f;
	if (Legend.LegendType == EWorldForgeLoreLegendType::Betrayal) Score += 0.10f;

	// Older legends have more renown (more time to spread)
	if (Ctx.NumEpochs() > 0)
	{
		float AgeRatio = 1.0f - (static_cast<float>(Legend.Epoch) / Ctx.NumEpochs());
		Score += AgeRatio * 0.20f;
	}

	return FMath::Clamp(Score, 0.0f, 1.0f);
}

FName FLegendSynthesisPass::MakeLegendID(int32 Index)
{
	return FName(*FString::Printf(TEXT("Legend_%04d"), Index));
}