#include "Civilization/WorldHistory/Passes/RaceOriginPass.h"
#include "Civilization/WorldHistory/WorldHistoryContext.h"
#include "Core/WorldForgeRNG.h"

void FRaceOriginPass::Execute(FWorldHistoryContext& Context)
{
    const int32 Count = Context.GeneratedNames.Num();

    Context.RaceOrigins.SetNum(Count);

    for (int32 Index = 0; Index < Count; ++Index)
    {
        FString Origin = TEXT("Unknown Origin");

        if (Context.RNG)
        {
            const int32 BaseRoll = Context.RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, 2);
            const int32 Roll = (BaseRoll + Index) % 3;

            switch (Roll)
            {
            case 0:
                Origin = TEXT("Born from the First Forest");
                break;

            case 1:
                Origin = TEXT("Forged in Ancient Stone");
                break;

            case 2:
                Origin = TEXT("Descended from the Sky");
                break;

            default:
                break;
            }
        }

        Context.RaceOrigins[Index] = Origin;

        UE_LOG(
            LogTemp,
            Log,
            TEXT("[WorldHistory] Race Origin %d: %s"),
            Index,
            *Context.RaceOrigins[Index]);
    }
}