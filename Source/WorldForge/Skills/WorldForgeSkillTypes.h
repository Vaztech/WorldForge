#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "WorldForgeSkillTypes.generated.h"

// ============================================================================
// WorldForgeSkillTypes.h
//
// Colonist skill definitions for WorldForge.
//
// Notes:
// - Data-only. No behavior here.
// - Each row maps to one skill in DT_WorldForgeSkills.
// - UsefulForRoles is advisory; job/scoring logic lives in processors.
// ============================================================================

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeSkillDefinition : public FTableRowBase
{
	GENERATED_BODY()

	// Canonical gameplay tag for this skill.
	// Example: WorldForge.Skill.Mining
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FGameplayTag SkillTag;

	// Player-facing skill name.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString DisplayName;

	// Player-facing description of what this skill covers.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString Description;

	// Broad category grouping.
	// Example: Labor, Production, Combat, Support, Knowledge, Survival, Social, Construction
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString Category;

	// The primary colonist attribute this skill scales from.
	// Example: Strength, Dexterity, Intelligence, Endurance, Perception, Charisma
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FString PrimaryAttribute;

	// Work roles that primarily rely on this skill.
	// Advisory: job assignment logic may consult this list.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	TArray<FString> UsefulForRoles;
};
