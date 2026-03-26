#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "BuildingTagHelper.generated.h"

UCLASS()
class WORLDFORGE_API UBuildingTagHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Section I / Milestone D:
    // Era 0 = early, 1 = mid, 2+ = late.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static FGameplayTag GetEraBuildingTag(EBuildingPurpose Purpose, int32 EraIndex);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static FGameplayTag GetDefaultMaterialForPurpose(EBuildingPurpose Purpose);
};