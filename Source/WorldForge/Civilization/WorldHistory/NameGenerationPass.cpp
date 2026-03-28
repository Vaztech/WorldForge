#include "NameGenerationPass.h"
#include "WorldHistoryNameModule.h"
#include "WorldHistoryNameModuleRegistry.h"

void FNameGenerationPass::Execute(FWorldHistoryContext& Context)
{
    if (!Context.RNG)
    {
        UE_LOG(LogTemp, Warning, TEXT("[WorldHistory] NameGenerationPass: RNG is null"));
        return;
    }

    TArray<TUniquePtr<FWorldHistoryNameModule>> Modules;
    FWorldHistoryNameModuleRegistry::BuildModules(Modules);

    for (int32 ModuleIndex = 0; ModuleIndex < Modules.Num(); ++ModuleIndex)
    {
        if (!Modules[ModuleIndex])
        {
            continue;
        }

        for (int32 NameIndex = 0; NameIndex < 4; ++NameIndex)
        {
            const FString Name = Modules[ModuleIndex]->GenerateName(Context);

            Context.GeneratedNames.Add(Name);

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[WorldHistory] Generated Name %d: %s"),
                NameIndex,
                *Name);
        }
    }
}