#pragma once

#include "CoreMinimal.h"
#include "Civilization/WorldHistory/WorldHistoryPass.h"

class FRaceOriginPass : public FWorldHistoryPass
{
public:
    virtual void Execute(FWorldHistoryContext& Context) override;
};