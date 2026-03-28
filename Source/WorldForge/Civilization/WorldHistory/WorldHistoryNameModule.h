#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryContext.h"

class FWorldHistoryNameModule
{
public:
    virtual ~FWorldHistoryNameModule() {}

    virtual FString GenerateName(FWorldHistoryContext& Context) = 0;
};