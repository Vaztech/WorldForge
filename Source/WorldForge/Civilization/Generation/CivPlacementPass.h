#pragma once

#include "CoreMinimal.h"
#include "CivGenTypes.h" // ADDED

class UCivilizationGenerator;
struct FCivGenContext;

// ADDED: Coherent placement/create pass extracted from CivilizationGenerator
class WORLDFORGE_API FCivPlacementPass
{
public:
    static void Execute(const UCivilizationGenerator& Generator, FCivGenContext& Context);
};