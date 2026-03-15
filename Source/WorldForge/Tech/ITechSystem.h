#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "ITechSystem.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UTechSystem : public UInterface
{
    GENERATED_BODY()
};

class WORLDFORGE_API ITechSystem
{
    GENERATED_BODY()

public:

    virtual FName GetSystemName() const = 0;
    virtual FText GetSystemDescription() const = 0;

    // All research nodes in this tech tree
    virtual TArray<FGameplayTag> GetResearchableNodes() const = 0;

    // DAG prerequisite structure
    virtual TArray<FGameplayTag> GetPrerequisites(FGameplayTag NodeTag) const = 0;

    // Research point cost for a node
    virtual float GetResearchCost(FGameplayTag NodeTag) const = 0;

    // What does researching this node unlock?
    virtual TArray<FGameplayTag> GetUnlocks(FGameplayTag NodeTag) const = 0;

    // Race affinity modifier — some races research some trees faster
    virtual float GetRaceResearchModifier(FGameplayTag RaceTag) const { return 1.f; }
};