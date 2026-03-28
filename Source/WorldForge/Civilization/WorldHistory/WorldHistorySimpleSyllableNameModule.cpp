#include "WorldHistorySimpleSyllableNameModule.h"

FString FWorldHistorySimpleSyllableNameModule::GenerateName(FWorldHistoryContext& Context)
{
    if (!Context.RNG)
    {
        return TEXT("Unnamed");
    }

    constexpr int32 Branch = UWorldForgeRNG::BRANCH_PREHISTORY;

    static const TCHAR* Starts[] =
    {
        TEXT("ae"), TEXT("el"), TEXT("li"), TEXT("fae"),
        TEXT("ara"), TEXT("syl"), TEXT("vor"), TEXT("thal")
    };

    static const TCHAR* Middles[] =
    {
        TEXT("ri"), TEXT("la"), TEXT("nae"), TEXT("the"),
        TEXT("sha"), TEXT("vya"), TEXT("dor"), TEXT("ka")
    };

    static const TCHAR* Ends[] =
    {
        TEXT("l"), TEXT("th"), TEXT("ria"), TEXT("nor"),
        TEXT("wyn"), TEXT("iel"), TEXT("mar"), TEXT("ion")
    };

    const int32 StartIndex = Context.RNG->NextInt(Branch, 0, UE_ARRAY_COUNT(Starts) - 1);
    const int32 EndIndex = Context.RNG->NextInt(Branch, 0, UE_ARRAY_COUNT(Ends) - 1);

    // Safe first-step distribution: 2 to 4 parts total
    const int32 PartCount = Context.RNG->NextInt(Branch, 2, 4);

    FString Result = Starts[StartIndex];

    for (int32 PartIndex = 1; PartIndex < PartCount - 1; ++PartIndex)
    {
        const int32 MiddleIndex = Context.RNG->NextInt(Branch, 0, UE_ARRAY_COUNT(Middles) - 1);
        Result += Middles[MiddleIndex];
    }

    Result += Ends[EndIndex];

    return Result;
}