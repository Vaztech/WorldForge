#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "WorldForgeModifierTypes.h"
#include "WorldForgeEquipmentTypes.generated.h"

// ====================================================================
// EQUIPMENT SLOT ENUM
// Expand this freely as your game grows.
// ====================================================================
UENUM(BlueprintType)
enum class EWorldForgeEquipmentSlot : uint8
{
    None        UMETA(DisplayName = "None"),
    Head        UMETA(DisplayName = "Head"),
    Body        UMETA(DisplayName = "Body"),
    Hands       UMETA(DisplayName = "Hands"),
    Tool        UMETA(DisplayName = "Tool"),
    Accessory   UMETA(DisplayName = "Accessory")
};

// ====================================================================
// EQUIPMENT DEFINITION (DataTable row)
// This is what designers will edit in DT_WorldForgeEquipment.
// ====================================================================
USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeEquipmentDefinition : public FTableRowBase
{
    GENERATED_BODY()

    // Unique identifier (optional but useful)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FGameplayTag EquipmentTag;

    // Which slot this item occupies
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EWorldForgeEquipmentSlot Slot = EWorldForgeEquipmentSlot::None;

    // All gameplay modifiers this item provides
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FWorldForgeResolvedModifiers Modifiers;
};