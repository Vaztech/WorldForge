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