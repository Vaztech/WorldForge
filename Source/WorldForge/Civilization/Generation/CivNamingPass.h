#pragma once

#include "CoreMinimal.h"
#include "../../Data/WorldForgeDataTypes.h"
#include "CivGenTypes.h" // ADDED

// ADDED: Dedicated naming pass extracted from placement flow
class WORLDFORGE_API FCivNamingPass
{
public:
    static void AssignDefaultName(FCivilizationData& Civ);
};