#include "WorldHistoryGenerator.h"
#include "NameGenerationPass.h"
#include "Civilization/WorldHistory/Passes/RaceOriginPass.h"
#include "Civilization/WorldHistory/Passes/FigureGenerationPass.h"

void FWorldHistoryGenerator::Run(FWorldHistoryContext& Context)
{
    BuildPipeline();

    for (TUniquePtr<FWorldHistoryPass>& Pass : Passes)
    {
        if (Pass)
        {
            Pass->Execute(Context);
        }
    }
}

void FWorldHistoryGenerator::BuildPipeline()
{
    Passes.Empty();

    // Locked Phase 3.5 order (current implemented subset only)
    Passes.Add(MakeUnique<FNameGenerationPass>());
    Passes.Add(MakeUnique<FRaceOriginPass>());
    Passes.Add(MakeUnique<FFigureGenerationPass>());
}