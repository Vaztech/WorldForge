#pragma once

#include "CoreMinimal.h"
#include "WorldForgeDataTypes.h"

struct FCultureDriftContext;

class FCultureDriftLoggingPass
{
public:
    static void Run(
        FCultureDriftContext& Context,
        const FCivilizationData& Before,
        const FCivilizationData& After);
};