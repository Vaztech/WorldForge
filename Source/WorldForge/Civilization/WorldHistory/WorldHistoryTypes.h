// WorldHistoryTypes.h

#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryTypes.generated.h"

UENUM(BlueprintType)
enum class EHistoricalRole : uint8
{
    King,
    General,
    Scholar,
    Prophet,
    Artisan,
    Explorer,
    Traitor,
    Hero,
    Merchant,
    Rebel
};

// Safe to keep this now because WorldLoreTypes uses EWorldForgeLoreLegendType
UENUM(BlueprintType)
enum class ELegendType : uint8
{
    War,
    Love,
    Betrayal,
    Discovery,
    Prophecy,
    Creation,
    Tragedy,
    Triumph
};

UENUM(BlueprintType)
enum class ERelationType : uint8
{
    Rival,
    Mentor,
    Spouse,
    Parent,
    Child,
    Ally,
    Betrayer,
    Apprentice
};

UENUM(BlueprintType)
enum class EArrivalReason : uint8
{
    Refugee,
    Trader,
    Wanderer,
    Exile,
    Missionary,
    Deserter,
    Adventurer
};

UENUM(BlueprintType)
enum class ELoreDetailLevel : uint8
{
    Low,
    Medium,
    High
};