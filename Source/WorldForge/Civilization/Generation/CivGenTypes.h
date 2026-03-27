#pragma once

#include "CoreMinimal.h"
#include "CivGenTypes.generated.h"

// ADDED: Shared lightweight generation enums/types live here as modularization expands.
// ADDED: Keep this intentionally minimal for now to avoid noise and preserve behavior.

UENUM()
enum class ECivGenPassType : uint8
{
    Unknown,
    Placement,
    RaceSeed,
    CultureSeed,
    GovernmentSeed,
    EconomySeed,
    Naming,
    DiplomacySeed
};