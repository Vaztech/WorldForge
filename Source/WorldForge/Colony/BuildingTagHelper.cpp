#include "BuildingTagHelper.h"
#include "WorldForgeTags.h"

FGameplayTag UBuildingTagHelper::GetEraBuildingTag(EBuildingPurpose Purpose, int32 EraIndex)
{
    switch (Purpose)
    {
    case EBuildingPurpose::Housing:
        return TAG_Building_Housing_Hut;

    case EBuildingPurpose::Storage:
        return TAG_Building_Storage_Stockpile;

    case EBuildingPurpose::Production:
        return (EraIndex <= 0)
            ? TAG_Building_Production_Kitchen
            : TAG_Building_Production_Carpenter;

    case EBuildingPurpose::Defense:
        return TAG_Building_Defense_Barracks;

    case EBuildingPurpose::Infrastructure:
        return TAG_Building_Storage_Stockpile;

    case EBuildingPurpose::Spiritual:
        return TAG_Building_Spiritual_Shrine;

    case EBuildingPurpose::Economic:
        return TAG_Building_Economic_MarketStall;

    case EBuildingPurpose::Decorative:
        return TAG_Building_Spiritual_Shrine;

    default:
        return TAG_Building_Storage_Stockpile;
    }
}

FGameplayTag UBuildingTagHelper::GetDefaultMaterialForPurpose(EBuildingPurpose Purpose)
{
    switch (Purpose)
    {
    case EBuildingPurpose::Housing:
    case EBuildingPurpose::Production:
    case EBuildingPurpose::Economic:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Wood")));

    case EBuildingPurpose::Defense:
    case EBuildingPurpose::Infrastructure:
    case EBuildingPurpose::Spiritual:
    case EBuildingPurpose::Storage:
    case EBuildingPurpose::Decorative:
    default:
        return FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));
    }
}

// ================================
// Shared Build Utilities
// ================================

bool UBuildingTagHelper::TryParsePrefabDimensions(
    const FString& Source,
    int32& OutWidth,
    int32& OutDepth,
    int32& OutHeight)
{
    OutWidth = 0;
    OutDepth = 0;
    OutHeight = 0;

    const int32 Length = Source.Len();
    for (int32 Start = 0; Start < Length; ++Start)
    {
        if (!FChar::IsDigit(Source[Start]))
        {
            continue;
        }

        int32 Cursor = Start;

        FString A;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            A.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (A.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
        {
            continue;
        }

        ++Cursor;

        FString B;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            B.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (B.IsEmpty() || Cursor >= Length || (Source[Cursor] != TCHAR('x') && Source[Cursor] != TCHAR('X')))
        {
            continue;
        }

        ++Cursor;

        FString C;
        while (Cursor < Length && FChar::IsDigit(Source[Cursor]))
        {
            C.AppendChar(Source[Cursor]);
            ++Cursor;
        }

        if (C.IsEmpty())
        {
            continue;
        }

        const int32 ParsedWidth = FCString::Atoi(*A);
        const int32 ParsedDepth = FCString::Atoi(*B);
        const int32 ParsedHeight = FCString::Atoi(*C);

        if (ParsedWidth > 0 && ParsedDepth > 0 && ParsedHeight > 0)
        {
            OutWidth = ParsedWidth;
            OutDepth = ParsedDepth;
            OutHeight = ParsedHeight;
            return true;
        }
    }

    return false;
}

int32 UBuildingTagHelper::GetBlockCountForBuildingTag(FGameplayTag BuildingTag)
{
    if (!BuildingTag.IsValid())
    {
        return 0;
    }

    // 1. Check for explicit WxDxH dimensions embedded in the tag string.
    {
        const FString S = BuildingTag.ToString();

        int32 W = 0, D = 0, H = 0;
        if (TryParsePrefabDimensions(S, W, D, H))
        {
            return W * D * H;
        }

        // 1b. Trailing digits (e.g. "...Hut8" → 8 blocks).
        int32 TrailingDigitStart = INDEX_NONE;
        for (int32 Index = S.Len() - 1; Index >= 0; --Index)
        {
            if (FChar::IsDigit(S[Index]))
            {
                TrailingDigitStart = Index;
            }
            else
            {
                break;
            }
        }

        if (TrailingDigitStart != INDEX_NONE)
        {
            const int32 ParsedCount = FCString::Atoi(*S.Mid(TrailingDigitStart));
            if (ParsedCount > 0)
            {
                return ParsedCount;
            }
        }
    }

    // 2. MatchesTag()-based fallback — zero .ToString().Contains() calls.
    if (BuildingTag.MatchesTag(TAG_Building_Storage_Stockpile))  return 8;
    if (BuildingTag.MatchesTag(TAG_Building_Production_Kitchen))  return 16;
    if (BuildingTag.MatchesTag(TAG_Building_Production_Carpenter)) return 20;
    if (BuildingTag.MatchesTag(TAG_Building_Defense_Barracks))    return 24;
    if (BuildingTag.MatchesTag(TAG_Building_Spiritual_Shrine))    return 12;

    // Conservative default.
    return 10;
}

FGameplayTag UBuildingTagHelper::ResolveStockpileItemForMaterial(FGameplayTag MaterialTag)
{
    if (!MaterialTag.IsValid())
    {
        return TAG_Item_Stone;
    }

    // MatchesTag() hierarchy check — no .ToString().Contains().
    static const FGameplayTag TAG_Mat_Wood =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Wood")));

    static const FGameplayTag TAG_Mat_Stone =
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Voxel.Material.Stone")));

    if (MaterialTag.MatchesTag(TAG_Mat_Wood))
    {
        return TAG_Item_Wood;
    }

    if (MaterialTag.MatchesTag(TAG_Mat_Stone))
    {
        return TAG_Item_Stone;
    }

    // Default to stone for unrecognised materials.
    return TAG_Item_Stone;
}