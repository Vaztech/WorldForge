#pragma once

#include "CoreMinimal.h"
#include "Civilization/WorldHistory/WorldHistoryPass.h"

class FFigureGenerationPass : public FWorldHistoryPass
{
public:
    virtual void Execute(FWorldHistoryContext& Context) override;
};