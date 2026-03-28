#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryNameModule.h"

class FWorldHistorySimpleSyllableNameModule : public FWorldHistoryNameModule
{
public:
    virtual FString GenerateName(FWorldHistoryContext& Context) override;
};