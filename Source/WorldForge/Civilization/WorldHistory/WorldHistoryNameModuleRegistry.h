#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class FWorldHistoryNameModule;

class FWorldHistoryNameModuleRegistry
{
public:
    static void BuildModules(TArray<TUniquePtr<FWorldHistoryNameModule>>& OutModules);
};