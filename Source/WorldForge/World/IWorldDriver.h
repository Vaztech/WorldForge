#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "IWorldDriver.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UWorldDriver : public UInterface
{
    GENERATED_BODY()
};

class WORLDFORGE_API IWorldDriver
{
    GENERATED_BODY()

public:
    virtual FName GetDriverName() const = 0;
    virtual FText GetDriverDescription() const = 0;
    virtual void ApplyThemeToTile(FWorldTileData& Tile) = 0;
    virtual TArray<FGameplayTag> GetSupportedBiomes() const = 0;
    virtual void PostProcessWorld(TArray<FWorldTileData>& WorldTiles) = 0;

    virtual TArray<FGameplayTag> GetEmphasisedCosmicDomains() const
    {
        return {};
    }
};