#pragma once

#include "CoreMinimal.h"
#include "WorldForgeDataTypes.h"
#include "../Core/WorldForgeRNG.h"

struct FWorldHistoryContext
{
    UWorldForgeRNG* RNG = nullptr;
    int32 CurrentYear = 0;

    TArray<FString> GeneratedNames;
    TArray<FString> GeneratedLegends;
    TArray<FString> RaceOrigins;
    TArray<FString> GeneratedFigures;
};