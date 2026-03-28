#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryPass.h"

class FNameGenerationPass : public FWorldHistoryPass
{
public:
    virtual void Execute(FWorldHistoryContext& Context) override;
};