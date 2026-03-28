#include "FigureGenerationPass.h"
#include "../WorldHistoryContext.h"
#include "../../Core/WorldForgeRNG.h"

void FFigureGenerationPass::Execute(FWorldHistoryContext& Context)
{
    Context.GeneratedFigures.Empty();

    if (!Context.RNG)
    {
        UE_LOG(LogTemp, Warning, TEXT("[FigurePass] RNG missing"));
        return;
    }

    constexpr int32 Branch = UWorldForgeRNG::BRANCH_PREHISTORY;

    static const TCHAR* Titles[] =
    {
        TEXT("the Founder"),
        TEXT("the Conqueror"),
        TEXT("the Wise"),
        TEXT("the Eternal"),
        TEXT("the Broken"),
        TEXT("the Flamebearer")
    };

    for (int32 i = 0; i < Context.GeneratedNames.Num(); ++i)
    {
        FString BaseName = Context.GeneratedNames[i];

        if (BaseName.Len() > 0)
        {
            BaseName[0] = FChar::ToUpper(BaseName[0]);
        }

        const int32 TitleIndex = Context.RNG->NextInt(
            Branch,
            0,
            UE_ARRAY_COUNT(Titles) - 1);

        const FString Figure = FString::Printf(
            TEXT("%s %s"),
            *BaseName,
            Titles[TitleIndex]);

        Context.GeneratedFigures.Add(Figure);

        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[FigurePass] Generated Figure %d: %s"),
            i,
            *Figure);
    }
}