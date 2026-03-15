#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "IBiome.generated.h"

// Economy resource profile for this biome type
// Defines base commodity prices — mountain iron is cheap, coastal iron expensive
USTRUCT(BlueprintType)
struct FBiomeEconomyProfile
{
    GENERATED_BODY()

    // Maps ItemTag to base anchor value multiplier (1.0 = standard price)
    // < 1.0 = this biome makes this good cheap (abundant locally)
    // > 1.0 = this biome makes this good expensive (scarce locally)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TMap<FGameplayTag, float> BaseAnchorMultipliers;
};

UINTERFACE(MinimalAPI, Blueprintable)
class UBiome : public UInterface
{
    GENERATED_BODY()
};

class WORLDFORGE_API IBiome
{
    GENERATED_BODY()

public:

    virtual FName GetBiomeName() const = 0;
    virtual FText GetBiomeDisplayName() const = 0;

    // Min/Max elevation (negative = below surface)
    virtual FVector2D GetElevationRange() const = 0;

    virtual FVoxelBiomeProfile GetVoxelGenerationProfile() const = 0;

    virtual TArray<FGameplayTag> GetNativeRaces() const = 0;

    // Nav.Land / Nav.Water / Nav.Sky / Nav.Void
    virtual FGameplayTag GetNavigationType() const = 0;

    virtual FBiomeEconomyProfile GetEconomicResourceProfile() const = 0;

    // Optional overrides
    virtual void TickBiomeSimulation(float DeltaTime) {}
    virtual void OnColonistEnterBiome(class ACharacter* Colonist) {}
    virtual float GetCosmicInfluenceMultiplier(FGameplayTag Domain) const { return 1.f; }
};