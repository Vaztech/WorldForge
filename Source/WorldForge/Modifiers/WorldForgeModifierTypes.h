#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldForgeModifierTypes.generated.h"

// Shared modifier source.
// Traits exist now. Equipment/Buffs are future hooks.
UENUM(BlueprintType)
enum class EWorldForgeModifierSource : uint8
{
    Trait,
    Equipment,
    Buff,
    Debuff,
    Other
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeResolvedModifiers
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WorkEfficiencyMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float HealingMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float InjuryChanceMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ImmunityMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MoodModifier = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float MoraleDeltaPerStep = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SocialModifier = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float LikeabilityModifier = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float AnnoyanceModifier = 0.0f;

    // Additive skill modifiers from traits/equipment/buffs.
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FGameplayTag, float> SkillModifiers;
};