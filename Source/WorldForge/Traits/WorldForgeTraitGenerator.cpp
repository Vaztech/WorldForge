#include "Traits/WorldForgeTraitGenerator.h"

// ============================================================================
// WorldForgeTraitGenerator.cpp
//
// Trait rolling rules live here.
// Future us:
// - Replace or supplement hardcoded candidates with DataTable rows later.
// - Add race-biased weights here.
// - Add animal/NPC-specific pools here.
// - Add stronger incompatibility rules here.
// ============================================================================

namespace
{
	struct FTraitRollCandidate
	{
		FGameplayTag TraitTag;
		float Weight = 1.0f;
	};

	FGameplayTag GetTraitTag(const TCHAR* TagName)
	{
		return FGameplayTag::RequestGameplayTag(FName(TagName), false);
	}

	void AddCandidate(TArray<FTraitRollCandidate>& OutCandidates, const TCHAR* TagName, float Weight)
	{
		const FGameplayTag Tag = GetTraitTag(TagName);

		if (Tag.IsValid() && Weight > 0.0f)
		{
			OutCandidates.Add({ Tag, Weight });
		}
	}

	bool HasExactTrait(const FGameplayTagContainer& Traits, const FGameplayTag& Trait)
	{
		return Trait.IsValid() && Traits.HasTagExact(Trait);
	}

	bool AreTraitsCompatible(const FGameplayTagContainer& CurrentTraits, const FGameplayTag& NewTrait)
	{
		if (!NewTrait.IsValid())
		{
			return false;
		}

		if (CurrentTraits.HasTagExact(NewTrait))
		{
			return false;
		}

		// Healing opposites.
		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.FastHealer")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.SlowHealer"))))
		{
			return false;
		}

		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.SlowHealer")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.FastHealer"))))
		{
			return false;
		}

		// Luck opposites.
		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.Lucky")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.Unlucky"))))
		{
			return false;
		}

		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.Unlucky")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.Lucky"))))
		{
			return false;
		}

		// Carefulness opposites.
		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.Careful")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.Clumsy"))))
		{
			return false;
		}

		if (NewTrait == GetTraitTag(TEXT("WorldForge.Trait.Clumsy")) &&
			HasExactTrait(CurrentTraits, GetTraitTag(TEXT("WorldForge.Trait.Careful"))))
		{
			return false;
		}

		return true;
	}

	TArray<FTraitRollCandidate> BuildDefaultTraitPool(FGameplayTag RaceTag)
	{
		TArray<FTraitRollCandidate> Pool;

		// Positive / useful traits.
		AddCandidate(Pool, TEXT("WorldForge.Trait.Lucky"), 0.80f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.FastHealer"), 0.70f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Careful"), 0.80f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.HardWorker"), 0.75f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Optimist"), 0.75f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Kind"), 0.70f);

		// Negative / difficult traits.
		AddCandidate(Pool, TEXT("WorldForge.Trait.Clumsy"), 0.85f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.SlowHealer"), 0.65f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Unlucky"), 0.55f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Lazy"), 0.70f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Annoying"), 0.70f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Pessimist"), 0.65f);

		// Strange/social traits.
		AddCandidate(Pool, TEXT("WorldForge.Trait.NEET"), 0.35f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Hikikomori"), 0.30f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Chuiboy"), 0.35f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.Oddball"), 0.50f);
		AddCandidate(Pool, TEXT("WorldForge.Trait.ConspiracyMinded"), 0.40f);

		// Race bias hook.
		// Future us: expand this once RaceDataAsset owns trait weights.
		if (RaceTag == FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Race.Dwarf")), false))
		{
			AddCandidate(Pool, TEXT("WorldForge.Trait.HardWorker"), 0.50f);
			AddCandidate(Pool, TEXT("WorldForge.Trait.Stubborn"), 0.50f);
		}

		return Pool;
	}

	bool TryWeightedPickTrait(
		const TArray<FTraitRollCandidate>& Pool,
		const FGameplayTagContainer& CurrentTraits,
		FRandomStream& Stream,
		FGameplayTag& OutTrait)
	{
		float TotalWeight = 0.0f;

		for (const FTraitRollCandidate& Candidate : Pool)
		{
			if (AreTraitsCompatible(CurrentTraits, Candidate.TraitTag))
			{
				TotalWeight += Candidate.Weight;
			}
		}

		if (TotalWeight <= 0.0f)
		{
			return false;
		}

		float Roll = Stream.FRandRange(0.0f, TotalWeight);

		for (const FTraitRollCandidate& Candidate : Pool)
		{
			if (!AreTraitsCompatible(CurrentTraits, Candidate.TraitTag))
			{
				continue;
			}

			Roll -= Candidate.Weight;

			if (Roll <= 0.0f)
			{
				OutTrait = Candidate.TraitTag;
				return true;
			}
		}

		return false;
	}
}

FGameplayTagContainer UWorldForgeTraitGenerator::RollRandomTraitsForNewEntity(
	FGameplayTag RaceTag,
	int32 Seed,
	int32 MinTraits,
	int32 MaxTraits)
{
	FGameplayTagContainer Result;

	MinTraits = FMath::Max(0, MinTraits);
	MaxTraits = FMath::Max(MinTraits, MaxTraits);

	FRandomStream Stream(Seed);
	const int32 TraitCount = Stream.RandRange(MinTraits, MaxTraits);

	const TArray<FTraitRollCandidate> Pool = BuildDefaultTraitPool(RaceTag);

	for (int32 i = 0; i < TraitCount; ++i)
	{
		FGameplayTag PickedTrait;

		if (TryWeightedPickTrait(Pool, Result, Stream, PickedTrait))
		{
			Result.AddTag(PickedTrait);
		}
	}

	return Result;
}

FGameplayTagContainer UWorldForgeTraitGenerator::RollInheritedTraitsFromParents(
	const FGameplayTagContainer& ParentATraits,
	const FGameplayTagContainer& ParentBTraits,
	int32 Seed,
	float InheritChancePerTrait,
	float MutationChance,
	int32 MaxTraits)
{
	FGameplayTagContainer Result;
	FRandomStream Stream(Seed);

	InheritChancePerTrait = FMath::Clamp(InheritChancePerTrait, 0.0f, 1.0f);
	MutationChance = FMath::Clamp(MutationChance, 0.0f, 1.0f);
	MaxTraits = FMath::Max(0, MaxTraits);

	TArray<FGameplayTag> ParentTraitList;
	ParentATraits.GetGameplayTagArray(ParentTraitList);

	TArray<FGameplayTag> ParentBTraitList;
	ParentBTraits.GetGameplayTagArray(ParentBTraitList);

	ParentTraitList.Append(ParentBTraitList);

	for (const FGameplayTag& ParentTrait : ParentTraitList)
	{
		if (Result.Num() >= MaxTraits)
		{
			break;
		}

		if (AreTraitsCompatible(Result, ParentTrait) &&
			Stream.FRand() <= InheritChancePerTrait)
		{
			Result.AddTag(ParentTrait);
		}
	}

	// Mutation: small chance to add one random non-parent trait.
	if (Result.Num() < MaxTraits && Stream.FRand() <= MutationChance)
	{
		const TArray<FTraitRollCandidate> Pool = BuildDefaultTraitPool(FGameplayTag());

		FGameplayTag MutatedTrait;
		if (TryWeightedPickTrait(Pool, Result, Stream, MutatedTrait))
		{
			Result.AddTag(MutatedTrait);
		}
	}

	return Result;
}