#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "IMagicSystem.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UMagicSystem : public UInterface
{
    GENERATED_BODY()
};

class WORLDFORGE_API IMagicSystem
{
    GENERATED_BODY()

public:
    virtual FName GetSystemName() const = 0;
    virtual FText GetSystemDescription() const = 0;

    // Tags of spells/abilities this system provides
    virtual TArray<FGameplayTag> GetLearnableSpells() const = 0;

    // How easy is it for this race to learn this system? 1.0 = baseline
    virtual float GetRaceAffinityModifier(FGameplayTag RaceTag) const = 0;

    // Does this system require a special biome or location to function?
    virtual bool RequiresBiomeTag(FGameplayTag BiomeTag) const { return false; }

    // Cosmic entity domains that amplify or oppose this magic system
    virtual TArray<FGameplayTag> GetAmplifyingDomains() const { return {}; }
    virtual TArray<FGameplayTag> GetOpposingDomains() const { return {}; }
};