#include "WorldHistoryNameModuleRegistry.h"
#include "WorldHistorySimpleSyllableNameModule.h"

void FWorldHistoryNameModuleRegistry::BuildModules(TArray<TUniquePtr<FWorldHistoryNameModule>>& OutModules)
{
    OutModules.Reset();

    // Default starter module.
    // Future naming expansions should register here without changing NameGenerationPass.cpp.
    OutModules.Add(MakeUnique<FWorldHistorySimpleSyllableNameModule>());
}