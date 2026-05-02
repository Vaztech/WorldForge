// ColonistBackstoryPass.cpp — Colonist backstory generation

#include "ColonistBackstoryPass.h"
#include "../Core/WorldForgeRNG.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeTags.h"

DEFINE_LOG_CATEGORY_STATIC(LogColonistBackstory, Log, All);

// ─────────────────────────────────────────────
//  Safe GameplayTag helpers
//  Optional lore tags should NEVER use strict RequestGameplayTag()
//  because missing tags cause handled ensures in PIE.
// ─────────────────────────────────────────────

namespace
{
	static FGameplayTag RequestOptionalTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	static void AddOptionalTag(FGameplayTagContainer& Container, const TCHAR* TagName)
	{
		const FGameplayTag Tag = RequestOptionalTag(TagName);
		if (Tag.IsValid())
		{
			Container.AddTag(Tag);
		}
	}

	static bool HasOptionalTagExact(const FGameplayTagContainer& Container, const TCHAR* TagName)
	{
		const FGameplayTag Tag = RequestOptionalTag(TagName);
		return Tag.IsValid() && Container.HasTagExact(Tag);
	}
}

// ─────────────────────────────────────────────
//  Name fragments (culturally neutral pool)
// ─────────────────────────────────────────────

namespace ColonistNames
{
	static const TArray<FString> FirstNames = {
		TEXT("Kael"), TEXT("Mira"), TEXT("Torvald"), TEXT("Senna"),
		TEXT("Bren"), TEXT("Yara"), TEXT("Dunn"), TEXT("Elke"),
		TEXT("Rook"), TEXT("Asha"), TEXT("Corwin"), TEXT("Lira"),
		TEXT("Hask"), TEXT("Venna"), TEXT("Orik"), TEXT("Thessa"),
		TEXT("Finn"), TEXT("Nara"), TEXT("Gareth"), TEXT("Isolde"),
		TEXT("Dax"), TEXT("Petra"), TEXT("Sigurd"), TEXT("Wren"),
	};

	static const TArray<FString> Surnames = {
		TEXT("Ashford"), TEXT("Blackwood"), TEXT("Cinder"), TEXT("Duskfall"),
		TEXT("Ember"), TEXT("Frostborne"), TEXT("Grimstone"), TEXT("Holloway"),
		TEXT("Ironheart"), TEXT("Kestrel"), TEXT("Lorn"), TEXT("Mossgrove"),
		TEXT("Nightvale"), TEXT("Oakshield"), TEXT("Pyre"), TEXT("Ravenholm"),
		TEXT("Stonehew"), TEXT("Thornwall"), TEXT("Underhill"), TEXT("Vane"),
	};
}

// ─────────────────────────────────────────────
//  Backstory templates by archetype
// ─────────────────────────────────────────────

namespace BackstoryTemplates
{
	static const TMap<EBackstoryArchetype, TArray<FString>> Templates = {
		{ EBackstoryArchetype::Exile, {
			TEXT("{NAME} was cast out from the {CIV} for reasons they refuse to discuss. They carry nothing but skills and silence."),
			TEXT("Once a respected member of the {CIV}, {NAME} ran afoul of the ruling council. The frontier is better than a cell."),
		}},
		{ EBackstoryArchetype::Veteran, {
			TEXT("{NAME} served the {CIV} through campaigns that left scars no healer could mend. They came here to forget."),
			TEXT("The wars of the {CIV} took everything from {NAME} except the ability to fight. Now they fight for a new home."),
		}},
		{ EBackstoryArchetype::Scholar, {
			TEXT("{NAME} left the archives of the {CIV} in search of truths that books could not contain."),
			TEXT("The {CIV} considered {NAME} their finest mind. {NAME} considered them too small for the questions that mattered."),
		}},
		{ EBackstoryArchetype::Orphan, {
			TEXT("{NAME} never knew their parents. Raised by the {CIV} state, they learned early that family is what you build."),
			TEXT("The streets of the {CIV} capital shaped {NAME} into a survivor. This colony is just another street to learn."),
		}},
		{ EBackstoryArchetype::Merchant, {
			TEXT("{NAME} traded across the {CIV} territories until a deal went wrong. New lands mean new markets."),
			TEXT("Every road the {CIV} built, {NAME} walked with a cart. Now they walk a road no one has mapped."),
		}},
		{ EBackstoryArchetype::Criminal, {
			TEXT("{NAME}'s crimes against the {CIV} are sealed. This colony was offered as an alternative to something worse."),
			TEXT("The {CIV} prisons could not hold {NAME}. The frontier might. Or it might find a better use for their talents."),
		}},
		{ EBackstoryArchetype::Pilgrim, {
			TEXT("{NAME} heard a calling that led them away from the {CIV} and toward the unknown. They do not explain what they seek."),
			TEXT("Faith drove {NAME} from the temples of the {CIV}. Not loss of faith — too much of it."),
		}},
		{ EBackstoryArchetype::Artisan, {
			TEXT("{NAME}'s craftsmanship was prized among the {CIV}. They came here to build something that would outlast them."),
			TEXT("The workshops of the {CIV} were too crowded for {NAME}'s ambition. Raw materials and freedom — that is all they need."),
		}},
	};
}

// ─────────────────────────────────────────────
//  Execute
// ─────────────────────────────────────────────

void FColonistBackstoryPass::Execute(FLoreGenerationContext& Ctx, int32 NumColonists)
{
	UE_LOG(LogColonistBackstory, Log,
		TEXT("ColonistBackstoryPass — BEGIN (generating %d colonists from %d civs)"),
		NumColonists, Ctx.Civilizations.Num());

	if (Ctx.Civilizations.Num() == 0)
	{
		UE_LOG(LogColonistBackstory, Warning,
			TEXT("ColonistBackstoryPass — No civilizations available. Skipping."));
		return;
	}

	for (int32 i = 0; i < NumColonists; ++i)
	{
		// Pick a home civ (round-robin with some randomness)
		int32 CivIdx = (i + Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, FMath::Max(0, Ctx.Civilizations.Num() - 1)))
			% Ctx.Civilizations.Num();
		const FCivilizationData& HomeCiv = Ctx.Civilizations[CivIdx];

		FColonistBackstory Colonist;
		Colonist.ColonistID = MakeColonistID(i);
		Colonist.HomeCivID = HomeCiv.CivID;
		Colonist.DisplayName = GenerateColonistName(HomeCiv, Ctx.RNG);
		Colonist.Archetype = ChooseArchetype(HomeCiv, Ctx.RNG);
		Colonist.PersonalityTraits = RollPersonalityTraits(Colonist.Archetype, Ctx.RNG);

		AssignAptitudes(Colonist, Colonist.Archetype, Ctx.RNG);
		AssignKnowledge(Colonist, HomeCiv, Ctx);

		Colonist.BackstoryText = GenerateBackstoryText(Colonist, HomeCiv, Ctx);

		Ctx.LoreState.ColonistBackstories.Add(MoveTemp(Colonist));
	}

	UE_LOG(LogColonistBackstory, Log,
		TEXT("ColonistBackstoryPass — END | %d colonists generated"),
		Ctx.LoreState.ColonistBackstories.Num());
}

// ─────────────────────────────────────────────
//  Archetype selection
// ─────────────────────────────────────────────

EBackstoryArchetype FColonistBackstoryPass::ChooseArchetype(
	const FCivilizationData& HomeCiv, UWorldForgeRNG& RNG)
{
	// Base weights for each archetype
	float Weights[] = {
		15.0f, // Exile
		15.0f, // Veteran
		12.0f, // Scholar
		10.0f, // Orphan
		12.0f, // Merchant
		 8.0f, // Criminal
		10.0f, // Pilgrim
		12.0f, // Artisan
	};

	// Civ trait biases.
	// These are optional because not every project build has all trait tags registered yet.
	if (HasOptionalTagExact(HomeCiv.TraitTags, TEXT("WorldForge.CivTrait.Warlike")))
	{
		Weights[1] += 15.0f; // More veterans
		Weights[4] -= 5.0f;  // Fewer merchants
	}

	if (HasOptionalTagExact(HomeCiv.TraitTags, TEXT("WorldForge.CivTrait.Scholarly")))
	{
		Weights[2] += 15.0f; // More scholars
		Weights[6] += 8.0f;  // More pilgrims
	}

	if (HomeCiv.CollapseEpoch >= 0)
	{
		Weights[0] += 10.0f; // Collapsed civs produce exiles
		Weights[3] += 8.0f;  // And orphans
	}

	// Ensure no negative weights
	float Total = 0.0f;
	for (float& W : Weights)
	{
		W = FMath::Max(W, 1.0f);
		Total += W;
	}

	float Roll = RNG.NextFloatRange(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0.0f, Total);
	float Cumulative = 0.0f;

	for (int32 i = 0; i < 8; ++i)
	{
		Cumulative += Weights[i];
		if (Roll <= Cumulative)
		{
			return static_cast<EBackstoryArchetype>(i);
		}
	}

	return EBackstoryArchetype::Exile;
}

// ─────────────────────────────────────────────
//  Personality traits
// ─────────────────────────────────────────────

FGameplayTagContainer FColonistBackstoryPass::RollPersonalityTraits(
	EBackstoryArchetype Archetype, UWorldForgeRNG& RNG)
{
	FGameplayTagContainer Tags;

	// Archetype guarantees.
	// All personality tags are optional-safe so missing config tags do not throw ensures.
	switch (Archetype)
	{
	case EBackstoryArchetype::Exile:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Guarded"));
		break;
	case EBackstoryArchetype::Veteran:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Brave"));
		break;
	case EBackstoryArchetype::Scholar:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Curious"));
		break;
	case EBackstoryArchetype::Orphan:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Resilient"));
		break;
	case EBackstoryArchetype::Merchant:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Shrewd"));
		break;
	case EBackstoryArchetype::Criminal:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Cunning"));
		break;
	case EBackstoryArchetype::Pilgrim:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Devout"));
		break;
	case EBackstoryArchetype::Artisan:
		AddOptionalTag(Tags, TEXT("WorldForge.Personality.Meticulous"));
		break;
	default:
		break;
	}

	// Roll 1-2 additional random traits
	static const TArray<FName> RandomTraits = {
		FName("WorldForge.Personality.Optimistic"),
		FName("WorldForge.Personality.Pessimistic"),
		FName("WorldForge.Personality.Aggressive"),
		FName("WorldForge.Personality.Gentle"),
		FName("WorldForge.Personality.Paranoid"),
		FName("WorldForge.Personality.Trusting"),
		FName("WorldForge.Personality.Greedy"),
		FName("WorldForge.Personality.Generous"),
		FName("WorldForge.Personality.Stoic"),
		FName("WorldForge.Personality.Passionate"),
	};

	int32 ExtraCount = RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 1, 2);
	TArray<int32> UsedIndices;

	for (int32 i = 0; i < ExtraCount; ++i)
	{
		int32 Idx;
		do
		{
			Idx = RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, RandomTraits.Num() - 1);
		} while (UsedIndices.Contains(Idx));

		UsedIndices.Add(Idx);

		const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(RandomTraits[Idx], false);
		if (Tag.IsValid())
		{
			Tags.AddTag(Tag);
		}
	}

	return Tags;
}

// ─────────────────────────────────────────────
//  Skill aptitudes
// ─────────────────────────────────────────────

void FColonistBackstoryPass::AssignAptitudes(
	FColonistBackstory& C, EBackstoryArchetype Archetype, UWorldForgeRNG& RNG)
{
	// Base aptitudes by archetype (primary/secondary emphasis)
	switch (Archetype)
	{
	case EBackstoryArchetype::Exile:
		C.CombatAptitude = 0.4f; C.CraftingAptitude = 0.3f;
		C.SocialAptitude = 0.2f; C.ScholarAptitude = 0.3f;
		break;
	case EBackstoryArchetype::Veteran:
		C.CombatAptitude = 0.8f; C.CraftingAptitude = 0.2f;
		C.SocialAptitude = 0.3f; C.ScholarAptitude = 0.1f;
		break;
	case EBackstoryArchetype::Scholar:
		C.CombatAptitude = 0.1f; C.CraftingAptitude = 0.3f;
		C.SocialAptitude = 0.4f; C.ScholarAptitude = 0.8f;
		break;
	case EBackstoryArchetype::Orphan:
		C.CombatAptitude = 0.3f; C.CraftingAptitude = 0.3f;
		C.SocialAptitude = 0.5f; C.ScholarAptitude = 0.2f;
		break;
	case EBackstoryArchetype::Merchant:
		C.CombatAptitude = 0.2f; C.CraftingAptitude = 0.4f;
		C.SocialAptitude = 0.7f; C.ScholarAptitude = 0.3f;
		break;
	case EBackstoryArchetype::Criminal:
		C.CombatAptitude = 0.5f; C.CraftingAptitude = 0.4f;
		C.SocialAptitude = 0.5f; C.ScholarAptitude = 0.1f;
		break;
	case EBackstoryArchetype::Pilgrim:
		C.CombatAptitude = 0.2f; C.CraftingAptitude = 0.2f;
		C.SocialAptitude = 0.6f; C.ScholarAptitude = 0.5f;
		break;
	case EBackstoryArchetype::Artisan:
		C.CombatAptitude = 0.1f; C.CraftingAptitude = 0.8f;
		C.SocialAptitude = 0.3f; C.ScholarAptitude = 0.4f;
		break;
	default:
		break;
	}

	// Apply ±0.1 variance to each
	C.CombatAptitude = FMath::Clamp(C.CombatAptitude + RNG.NextFloatRange(UWorldForgeRNG::BRANCH_NPC_TRAITS, -0.1f, 0.1f), 0.0f, 1.0f);
	C.CraftingAptitude = FMath::Clamp(C.CraftingAptitude + RNG.NextFloatRange(UWorldForgeRNG::BRANCH_NPC_TRAITS, -0.1f, 0.1f), 0.0f, 1.0f);
	C.SocialAptitude = FMath::Clamp(C.SocialAptitude + RNG.NextFloatRange(UWorldForgeRNG::BRANCH_NPC_TRAITS, -0.1f, 0.1f), 0.0f, 1.0f);
	C.ScholarAptitude = FMath::Clamp(C.ScholarAptitude + RNG.NextFloatRange(UWorldForgeRNG::BRANCH_NPC_TRAITS, -0.1f, 0.1f), 0.0f, 1.0f);
}

// ─────────────────────────────────────────────
//  Knowledge assignment (quest hooks)
// ─────────────────────────────────────────────

void FColonistBackstoryPass::AssignKnowledge(
	FColonistBackstory& Colonist,
	const FCivilizationData& HomeCiv,
	FLoreGenerationContext& Ctx)
{
	const FWorldLoreState& Lore = Ctx.LoreState;

	// Scholars know more artifacts/legends; veterans know about fallen cities
	int32 MaxArtifacts = (Colonist.Archetype == EBackstoryArchetype::Scholar) ? 3 : 1;
	int32 MaxLegends = (Colonist.Archetype == EBackstoryArchetype::Scholar) ? 3 : 2;

	// Know artifacts from home civ first
	TArray<const FLoreArtifact*> CivArtifacts = Lore.GetArtifactsByCiv(HomeCiv.CivID);
	for (int32 i = 0; i < FMath::Min(MaxArtifacts, CivArtifacts.Num()); ++i)
	{
		Colonist.KnownArtifactIDs.Add(CivArtifacts[i]->ArtifactID);
	}

	// Also know a random non-civ artifact occasionally (trade knowledge)
	if (Colonist.Archetype == EBackstoryArchetype::Merchant && Lore.Artifacts.Num() > 0)
	{
		int32 Pick = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, Lore.Artifacts.Num() - 1);
		Colonist.KnownArtifactIDs.AddUnique(Lore.Artifacts[Pick].ArtifactID);
	}

	// Know legends — prefer high-renown legends from home civ
	for (const FLoreLegend& Legend : Lore.Legends)
	{
		if (Colonist.KnownLegendIDs.Num() >= MaxLegends)
		{
			break;
		}

		bool bHomeCivLegend = Legend.ReferencedCivIDs.Contains(HomeCiv.CivID);
		float KnowChance = bHomeCivLegend ? 0.60f : (Legend.Renown * 0.30f);

		if (Ctx.RNG.NextFloat(UWorldForgeRNG::BRANCH_NPC_TRAITS) < KnowChance)
		{
			Colonist.KnownLegendIDs.Add(Legend.LegendID);
		}
	}
}

// ─────────────────────────────────────────────
//  Backstory text generation
// ─────────────────────────────────────────────

FText FColonistBackstoryPass::GenerateBackstoryText(
	const FColonistBackstory& Colonist,
	const FCivilizationData& HomeCiv,
	const FLoreGenerationContext& Ctx)
{
	const auto* ArchetypeTemplates = BackstoryTemplates::Templates.Find(Colonist.Archetype);
	if (!ArchetypeTemplates || ArchetypeTemplates->Num() == 0)
	{
		return FText::FromString(
			FString::Printf(TEXT("%s came from the %s with a past they prefer not to share."),
				*Colonist.DisplayName.ToString(), *HomeCiv.Name));
	}

	int32 Pick = Ctx.RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, ArchetypeTemplates->Num() - 1);
	FString Text = (*ArchetypeTemplates)[Pick];

	Text = Text.Replace(TEXT("{NAME}"), *Colonist.DisplayName.ToString());
	Text = Text.Replace(TEXT("{CIV}"), *HomeCiv.Name);

	// Append knowledge hooks
	if (Colonist.KnownArtifactIDs.Num() > 0)
	{
		const FLoreArtifact* KnownArtifact = Ctx.LoreState.FindArtifact(Colonist.KnownArtifactIDs[0]);
		if (KnownArtifact)
		{
			Text += FString::Printf(TEXT(" They speak often of %s, and seem to know more than they let on."),
				*KnownArtifact->DisplayName.ToString());
		}
	}

	if (Colonist.KnownLegendIDs.Num() > 0)
	{
		const FLoreLegend* KnownLegend = Ctx.LoreState.FindLegend(Colonist.KnownLegendIDs[0]);
		if (KnownLegend)
		{
			Text += FString::Printf(TEXT(" Around the fire, they retell %s with the weight of someone who was almost there."),
				*KnownLegend->Title.ToString());
		}
	}

	return FText::FromString(Text);
}

// ─────────────────────────────────────────────
//  Name generation
// ─────────────────────────────────────────────

FText FColonistBackstoryPass::GenerateColonistName(
	const FCivilizationData& HomeCiv, UWorldForgeRNG& RNG)
{
	const auto& F = ColonistNames::FirstNames;
	const auto& S = ColonistNames::Surnames;

	int32 FIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, F.Num() - 1);
	int32 SIdx = RNG.NextInt(UWorldForgeRNG::BRANCH_NPC_TRAITS, 0, S.Num() - 1);

	FString Name = FString::Printf(TEXT("%s %s"), *F[FIdx], *S[SIdx]);
	return FText::FromString(Name);
}

FName FColonistBackstoryPass::MakeColonistID(int32 Index)
{
	return FName(*FString::Printf(TEXT("Colonist_%04d"), Index));
}