#include "Traits/WorldForgeTraitLibrary.h"

// ============================================================================
// WorldForgeTraitLibrary.cpp
//
// Trait math implementation.
// Future us: keep this file pure/stateless where possible.
// Do not store colonist state here. This should only read trait definitions
// and return useful modifiers to other systems.
// ============================================================================

bool UWorldForgeTraitLibrary::FindTraitDefinition(
	UDataTable* TraitTable,
	FGameplayTag TraitTag,
	FWorldForgeTraitDefinition& OutDefinition)
{
	if (!TraitTable || !TraitTag.IsValid())
	{
		return false;
	}

	const TMap<FName, uint8*>& RowMap = TraitTable->GetRowMap();

	for (const TPair<FName, uint8*>& RowPair : RowMap)
	{
		const FWorldForgeTraitDefinition* Row =
			reinterpret_cast<const FWorldForgeTraitDefinition*>(RowPair.Value);

		if (Row && Row->TraitTag == TraitTag)
		{
			OutDefinition = *Row;
			return true;
		}
	}

	return false;
}

float UWorldForgeTraitLibrary::CalculateWorkEfficiencyMultiplier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 1.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result *= Definition.WorkEfficiencyMultiplier;
		}
	}

	return FMath::Max(0.0f, Result);
}

float UWorldForgeTraitLibrary::CalculateHealingMultiplier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 1.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result *= Definition.HealingMultiplier;
		}
	}

	return FMath::Max(0.0f, Result);
}

float UWorldForgeTraitLibrary::CalculateInjuryChanceMultiplier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 1.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result *= Definition.InjuryChanceMultiplier;
		}
	}

	return FMath::Max(0.0f, Result);
}

float UWorldForgeTraitLibrary::CalculateImmunityMultiplier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 1.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result *= Definition.ImmunityMultiplier;
		}
	}

	return FMath::Max(0.0f, Result);
}

float UWorldForgeTraitLibrary::CalculateMoodModifier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 0.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result += Definition.MoodModifier;
		}
	}

	return Result;
}

float UWorldForgeTraitLibrary::CalculateSocialModifier(
	const FGameplayTagContainer& TraitTags,
	UDataTable* TraitTable)
{
	float Result = 0.0f;

	if (!TraitTable)
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			Result += Definition.SocialModifier;
		}
	}

	return Result;
}

float UWorldForgeTraitLibrary::CalculateSkillModifier(
	const FGameplayTagContainer& TraitTags,
	FGameplayTag SkillTag,
	UDataTable* TraitTable)
{
	float Result = 0.0f;

	if (!TraitTable || !SkillTag.IsValid())
	{
		return Result;
	}

	for (const FGameplayTag& TraitTag : TraitTags)
	{
		FWorldForgeTraitDefinition Definition;
		if (FindTraitDefinition(TraitTable, TraitTag, Definition))
		{
			if (const float* FoundModifier = Definition.SkillModifiers.Find(SkillTag))
			{
				Result += *FoundModifier;
			}
		}
	}

	return Result;
}