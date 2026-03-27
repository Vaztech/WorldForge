#pragma once

#include "CoreMinimal.h"
#include "WorldForgeDataTypes.h"

struct FCultureDriftContext;

class FCultureEventDriftPass
{
public:
    static void Run(FCultureDriftContext& Context, FCivilizationData& Civ);
};