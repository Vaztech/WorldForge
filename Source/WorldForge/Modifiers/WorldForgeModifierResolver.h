#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeModifierTypes.h"
#include "WorldForgeModifierResolver.generated.h"

class UDataTable;

UCLASS()
class WORLDFORGE_API UWorldForgeModifierResolver : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    // ─────────────────────────────────────────────────────────────
    // TRAITS
    // ─────────────────────────────────────────────────────────────

    // Loads Content/Data/Traits/DT_WorldForgeTraits.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Modifiers")
    static UDataTable* GetTraitTable();

    // ─────────────────────────────────────────────────────────────
    // EQUIPMENT (NEW)
    // ─────────────────────────────────────────────────────────────

    // Loads Content/Data/Equipment/DT_WorldForgeEquipment.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Modifiers")
    static UDataTable* GetEquipmentTable();

    // ─────────────────────────────────────────────────────────────
    // RESOLUTION
    // ─────────────────────────────────────────────────────────────

    // Resolves ALL modifier sources:
    // Traits + Equipment + (future) Buffs/Debuffs
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Modifiers")
    static FWorldForgeResolvedModifiers ResolveAgentModifiers(const FAgentData& Agent);

    // Returns base skill + all additive modifiers.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Modifiers")
    static float ResolveSkillValue(const FAgentData& Agent, FGameplayTag SkillTag);

    // Returns CurrentEfficiency adjusted by all multiplier sources.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Modifiers")
    static float ResolveWorkEfficiency(const FAgentData& Agent);
};