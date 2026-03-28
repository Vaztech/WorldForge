#pragma once

#include "CoreMinimal.h"
#include "WorldHistoryPass.h"

class FWorldHistoryGenerator
{
public:
    void Run(FWorldHistoryContext& Context);

private:
    TArray<TUniquePtr<FWorldHistoryPass>> Passes;

    void BuildPipeline();
};