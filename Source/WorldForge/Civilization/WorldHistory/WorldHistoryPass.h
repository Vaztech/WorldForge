#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryContext.h"

class FWorldHistoryPass
{
public:
    virtual ~FWorldHistoryPass() {}

    virtual void Execute(FWorldHistoryContext& Context) PURE_VIRTUAL(FWorldHistoryPass::Execute, );
};