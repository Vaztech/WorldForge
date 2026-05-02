// ArtifactForgePass.cpp — Legendary artifact generation

#include "ArtifactForgePass.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldForgeDataTypes.h" // FCivilizationData, FHistoricalFigure, FWorldEpoch
#include "WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogArtifactForge, Log, All);

// ─────────────────────────────────────────────
//  Name fragment tables
// ─────────────────────────────────────────────

namespace ArtifactNames
{
	// Weapon prefixes
	static const TArray<FString> WeaponPrefixes = {
		TEXT("Dread"), TEXT("Storm"), TEXT("Blood"), TEXT("Shadow"),
		TEXT("Dawn"), TEXT("Iron"), TEXT("Frost"), TEXT("Ember"),
		TEXT("Void"), TEXT("Thunder"), TEXT("Bone"), TEXT("Rune")
	};

	// Weapon suffixes
	static const TArray<FString> WeaponSuffixes = {
		TEXT("bane"), TEXT("cleaver"), TEXT("fang"), TEXT("reaver"),
		TEXT("song"), TEXT("wail"), TEXT("strike"), TEXT("maw"),
		TEXT("edge"), TEXT("fury"), TEXT("thorn"), TEXT("rend")
	};

	// Armor names
	static const TArray<FString> ArmorPrefixes = {
		TEXT("Aegis"), TEXT("Bulwark"), TEXT("Mantle"), TEXT("Shroud"),
		TEXT("Ward"), TEXT("Shell"), TEXT("Bastion"), TEXT("Veil")
	};

	static const TArray<FString> ArmorSuffixes = {
		TEXT("of the Undying"), TEXT("of Echoes"), TEXT("of the Deep"),
		TEXT("of Ashes"), TEXT("of the Colossus"), TEXT("of Stars"),
		TEXT("of the Forgotten"), TEXT("of Flame")
	};

	// Relic names
	static const TArray<FString> RelicPrefixes = {
		TEXT("The Eye"), TEXT("The Heart"), TEXT("The Crown"),
		TEXT("The Shard"), TEXT("The Orb"), TEXT("The Seal"),
		TEXT("The Chalice"), TEXT("The Skull")
	};

	static const TArray<FString> RelicSuffixes = {
		TEXT("of Prophecy"), TEXT("of Dominion"), TEXT("of Twilight"),
		TEXT("of the Abyss"), TEXT("of Creation"), TEXT("of Ruin"),
		TEXT("of the First Age"), TEXT("of Silence")
	};

	// Tome names
	static const TArray<FString> TomePrefixes = {
		TEXT("Codex"), TEXT("Grimoire"), TEXT("Tome"), TEXT("Scroll"),
		TEXT("Chronicle"), TEXT("Libram"), TEXT("Folio"), TEXT("Lexicon")
	};

	static const TArray<FString> TomeSuffixes = {
		TEXT("of Forbidden Knowledge"), TEXT("of the Cosmic Veil"),
		TEXT("of Binding"), TEXT("of Lost Tongues"),
		TEXT("of the Architect"), TEXT("of Unmaking"),
		TEXT("of the Deep Current"), TEXT("of Final Truths")
	};
}

// ─────────────────────────────────────────────
//  Execute — main entry point
// ─────────────────────────────────────────────

void FArtifactForgePass::Execute(FLoreGenerationContext& Ctx)
{
	// Some current WorldForge history paths do not yet populate FWorldEpoch.
	// Do not let that block artifact generation. If no epochs exist, run a
	// deterministic fallback span so the lore system still produces artifacts.
	const int32 FallbackEpochCount = 3;
	const int32 EpochCount = Ctx.NumEpochs() > 0 ? Ctx.NumEpochs() : FallbackEpochCount;

	UE_LOG(LogArtifactForge, Log, TEXT("ArtifactForgePass — BEGIN (%d civs, %d epochs, effective epochs=%d)"),
		Ctx.Civilizations.Num(), Ctx.NumEpochs(), EpochCount);

	int32 ArtifactIndex = Ctx.LoreState.Artifacts.Num();

	for (int32 EpochIdx = 0; EpochIdx < EpochCount; ++EpochIdx)
	{
		for (const FCivilizationData& Civ : Ctx.Civilizations)
		{
			// Skip civs that collapsed before this epoch
			if (Civ.CollapseEpoch >= 0 && EpochIdx > Civ.CollapseEpoch)
			{
				continue;
			}

			if (!ShouldForgeArtifact(Civ, EpochIdx, Ctx.RNG))
			{
				continue;
			}

			FLoreArtifact Artifact;
			Artifact.ArtifactID = MakeArtifactID(ArtifactIndex++);
			Artifact.ForgedByCivID = Civ.CivID;
			Artifact.ForgedInEpoch = EpochIdx;

			// Pick artifact type weighted by civ traits
			Artifact.ArtifactType = ChooseArtifactType(Civ, Ctx.RNG);

			// Try to attribute to a figure from this civ
			TArray<const FHistoricalFigure*> CivFigures = Ctx.GetFiguresForCiv(Civ.CivID);
			const FHistoricalFigure* Smith = nullptr;
			if (CivFigures.Num() > 0)
			{
				const int32 FigIdx = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, CivFigures.Num() - 1);
				Smith = CivFigures[FigIdx];
				Artifact.ForgedByFigureID = Smith->FigureID;
			}

			// Name and describe
			Artifact.DisplayName = GenerateArtifactName(Artifact.ArtifactType, Ctx.RNG);
			Artifact.TraitTags = RollTraitTags(Ctx.RNG);
			Artifact.PowerRating = Ctx.RNG.NextFloatRange(UWorldForgeRNG::BRANCH_ANCHORS, 0.3f, 1.0f);
			Artifact.Description = GenerateArtifactDescription(Artifact, Civ, Smith);

			// Artifact starts held by forging civ
			Artifact.CurrentHolderCivID = Civ.CivID;

			Ctx.LoreState.Artifacts.Add(MoveTemp(Artifact));
		}
	}

	UE_LOG(LogArtifactForge, Log, TEXT("ArtifactForgePass — END | %d artifacts forged"),
		Ctx.LoreState.Artifacts.Num());
}

// ─────────────────────────────────────────────
//  Probability
// ─────────────────────────────────────────────

bool FArtifactForgePass::ShouldForgeArtifact(
	const FCivilizationData& Civ, int32 Epoch, UWorldForgeRNG& RNG)
{
	// Base 18% chance per civ per epoch.
	// Raised from 12% because fallback epoch mode only runs a few epochs.
	float Chance = 0.18f;

	// Warlike civs forge more
	if (Civ.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.CivTrait.Warlike"), false)))
	{
		Chance += 0.08f;
	}

	// Scholarly civs also forge (tomes/relics)
	if (Civ.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.CivTrait.Scholarly"), false)))
	{
		Chance += 0.06f;
	}

	// Later epochs slightly more likely (accumulated knowledge)
	Chance += Epoch * 0.03f;
	Chance = FMath::Clamp(Chance, 0.0f, 0.55f);

	return RNG.NextFloat(UWorldForgeRNG::BRANCH_ANCHORS) < Chance;
}

// ─────────────────────────────────────────────
//  Type selection
// ─────────────────────────────────────────────

FGameplayTag FArtifactForgePass::ChooseArtifactType(
	const FCivilizationData& Civ, UWorldForgeRNG& RNG)
{
	// Weight table: Weapon / Armor / Relic / Tome
	float Weights[4] = { 25.0f, 20.0f, 30.0f, 25.0f };

	if (Civ.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.CivTrait.Warlike"), false)))
	{
		Weights[0] += 20.0f; // More weapons
		Weights[1] += 10.0f; // More armor
	}
	if (Civ.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.CivTrait.Scholarly"), false)))
	{
		Weights[3] += 25.0f; // More tomes
		Weights[2] += 10.0f; // More relics
	}

	// Normalize and pick
	const float Total = Weights[0] + Weights[1] + Weights[2] + Weights[3];
	const float Roll = RNG.NextFloatRange(UWorldForgeRNG::BRANCH_ANCHORS, 0.0f, Total);

	static const FName TypeNames[] = {
		FName("WorldForge.Artifact.Type.Weapon"),
		FName("WorldForge.Artifact.Type.Armor"),
		FName("WorldForge.Artifact.Type.Relic"),
		FName("WorldForge.Artifact.Type.Tome")
	};

	float Cumulative = 0.0f;
	for (int32 i = 0; i < 4; ++i)
	{
		Cumulative += Weights[i];
		if (Roll <= Cumulative)
		{
			return FGameplayTag::RequestGameplayTag(TypeNames[i], false);
		}
	}

	return FGameplayTag::RequestGameplayTag(TypeNames[2], false); // Fallback: Relic
}

// ─────────────────────────────────────────────
//  Trait tags
// ─────────────────────────────────────────────

FGameplayTagContainer FArtifactForgePass::RollTraitTags(UWorldForgeRNG& RNG)
{
	FGameplayTagContainer Tags;

	struct FTraitRoll
	{
		FName TagName;
		float Chance;
	};

	static const TArray<FTraitRoll> PossibleTraits = {
		{ FName("WorldForge.Artifact.Trait.Cursed"),      0.15f },
		{ FName("WorldForge.Artifact.Trait.Holy"),        0.12f },
		{ FName("WorldForge.Artifact.Trait.Sentient"),    0.08f },
		{ FName("WorldForge.Artifact.Trait.Destructive"), 0.18f },
		{ FName("WorldForge.Artifact.Trait.Protective"),  0.15f },
		{ FName("WorldForge.Artifact.Trait.Prophetic"),   0.06f },
		{ FName("WorldForge.Artifact.Trait.Bound"),       0.10f },
	};

	for (const FTraitRoll& TR : PossibleTraits)
	{
		if (RNG.NextFloat(UWorldForgeRNG::BRANCH_ANCHORS) < TR.Chance)
		{
			const FGameplayTag TraitTag = FGameplayTag::RequestGameplayTag(TR.TagName, false);
			if (TraitTag.IsValid())
			{
				Tags.AddTag(TraitTag);
			}
		}
	}

	// Guarantee at least one valid trait if tags are registered.
	if (Tags.Num() == 0)
	{
		for (int32 Attempt = 0; Attempt < PossibleTraits.Num(); ++Attempt)
		{
			const int32 Pick = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, PossibleTraits.Num() - 1);
			const FGameplayTag TraitTag = FGameplayTag::RequestGameplayTag(PossibleTraits[Pick].TagName, false);

			if (TraitTag.IsValid())
			{
				Tags.AddTag(TraitTag);
				break;
			}
		}
	}

	return Tags;
}

// ─────────────────────────────────────────────
//  Name generation
// ─────────────────────────────────────────────

FText FArtifactForgePass::GenerateArtifactName(const FGameplayTag& Type, UWorldForgeRNG& RNG)
{
	FString Name;

	const FString TypeStr = Type.GetTagName().ToString();

	if (TypeStr.Contains(TEXT("Weapon")))
	{
		const auto& P = ArtifactNames::WeaponPrefixes;
		const auto& S = ArtifactNames::WeaponSuffixes;
		const int32 PIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, P.Num() - 1);
		const int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, S.Num() - 1);
		Name = P[PIdx] + S[SIdx];
	}
	else if (TypeStr.Contains(TEXT("Armor")))
	{
		const auto& P = ArtifactNames::ArmorPrefixes;
		const auto& S = ArtifactNames::ArmorSuffixes;
		const int32 PIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, P.Num() - 1);
		const int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, S.Num() - 1);
		Name = P[PIdx] + TEXT(" ") + S[SIdx];
	}
	else if (TypeStr.Contains(TEXT("Relic")))
	{
		const auto& P = ArtifactNames::RelicPrefixes;
		const auto& S = ArtifactNames::RelicSuffixes;
		const int32 PIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, P.Num() - 1);
		const int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, S.Num() - 1);
		Name = P[PIdx] + TEXT(" ") + S[SIdx];
	}
	else // Tome or invalid/unregistered type fallback
	{
		const auto& P = ArtifactNames::TomePrefixes;
		const auto& S = ArtifactNames::TomeSuffixes;
		const int32 PIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, P.Num() - 1);
		const int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_ANCHORS, 0, S.Num() - 1);
		Name = P[PIdx] + TEXT(" ") + S[SIdx];
	}

	return FText::FromString(Name);
}

// ─────────────────────────────────────────────
//  Description generation
// ─────────────────────────────────────────────

FText FArtifactForgePass::GenerateArtifactDescription(
	const FLoreArtifact& Artifact,
	const FCivilizationData& Civ,
	const FHistoricalFigure* Smith)
{
	FString Desc;

	if (Smith)
	{
		Desc = FString::Printf(TEXT("Forged by %s of the %s in epoch %d."),
			*Smith->Name.ToString(),
			*Civ.Name,
			Artifact.ForgedInEpoch);
	}
	else
	{
		Desc = FString::Printf(TEXT("Crafted by the %s in epoch %d."),
			*Civ.Name,
			Artifact.ForgedInEpoch);
	}

	// Append trait flavor
	if (Artifact.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.Artifact.Trait.Cursed"), false)))
	{
		Desc += TEXT(" It carries a dark curse that consumes its wielder.");
	}
	else if (Artifact.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.Artifact.Trait.Sentient"), false)))
	{
		Desc += TEXT(" It is said to whisper to those who hold it.");
	}
	else if (Artifact.TraitTags.HasTagExact(
		FGameplayTag::RequestGameplayTag(FName("WorldForge.Artifact.Trait.Holy"), false)))
	{
		Desc += TEXT(" It radiates an ancient, protective light.");
	}

	return FText::FromString(Desc);
}

// ─────────────────────────────────────────────
//  ID factory
// ─────────────────────────────────────────────

FName FArtifactForgePass::MakeArtifactID(int32 Index)
{
	return FName(*FString::Printf(TEXT("Artifact_%04d"), Index));
}