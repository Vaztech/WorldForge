#pragma once

#include "CoreMinimal.h"
#include "../Data/WorldForgeDataTypes.h"

enum class EWorldPlacementRejectReason : uint8
{
    None,
    InvalidBiome,
    Water,
    Lava,
    Volcanic,
    LowElevation
};

namespace WorldForgePlacement
{
    inline const TCHAR* ToReasonString(EWorldPlacementRejectReason Reason)
    {
        switch (Reason)
        {
        case EWorldPlacementRejectReason::None:
            return TEXT("None");
        case EWorldPlacementRejectReason::InvalidBiome:
            return TEXT("InvalidBiome");
        case EWorldPlacementRejectReason::Water:
            return TEXT("Water");
        case EWorldPlacementRejectReason::Lava:
            return TEXT("Lava");
        case EWorldPlacementRejectReason::Volcanic:
            return TEXT("Volcanic");
        case EWorldPlacementRejectReason::LowElevation:
            return TEXT("LowElevation");
        default:
            return TEXT("Unknown");
        }
    }

    inline bool IsForbiddenWaterBiome(const FGameplayTag& Biome)
    {
        if (!Biome.IsValid())
        {
            return true;
        }

        static const FGameplayTag DeepOceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.DeepOcean")), false);
        static const FGameplayTag OceanTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Ocean")), false);
        static const FGameplayTag SeaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Sea")), false);
        static const FGameplayTag WaterTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Water")), false);

        return (DeepOceanTag.IsValid() && Biome.MatchesTag(DeepOceanTag))
            || (OceanTag.IsValid() && Biome.MatchesTag(OceanTag))
            || (SeaTag.IsValid() && Biome.MatchesTag(SeaTag))
            || (WaterTag.IsValid() && Biome == WaterTag);
    }

    inline EWorldPlacementRejectReason ClassifyMajorSiteTile(
        const FWorldTileData& Tile,
        bool bAllowCoastal = true,
        bool bAllowLake = false,
        bool bAllowRiver = false,
        bool bAllowWetland = false,
        bool bAllowVolcanic = false,
        bool bAllowMountain = true,
        float MinElevation = 0.1f)
    {
        static const FGameplayTag CoastalTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Coastal")), false);
        static const FGameplayTag LakeTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lake")), false);
        static const FGameplayTag RiverTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.River")), false);
        static const FGameplayTag MarshTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Marsh")), false);
        static const FGameplayTag SwampTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Swamp")), false);
        static const FGameplayTag WetlandTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Wetland")), false);
        static const FGameplayTag LavaTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Lava")), false);
        static const FGameplayTag VolcanicTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Volcanic")), false);
        static const FGameplayTag MountainTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.Mountain")), false);
        static const FGameplayTag SnowPeakTag = FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Biome.SnowPeak")), false);

        if (!Tile.BiomeType.IsValid())
        {
            return EWorldPlacementRejectReason::InvalidBiome;
        }

        if (Tile.Elevation < MinElevation)
        {
            return EWorldPlacementRejectReason::LowElevation;
        }

        if (SnowPeakTag.IsValid() && Tile.BiomeType.MatchesTag(SnowPeakTag))
        {
            return EWorldPlacementRejectReason::InvalidBiome;
        }

        if (LavaTag.IsValid() && Tile.BiomeType.MatchesTag(LavaTag))
        {
            return EWorldPlacementRejectReason::Lava;
        }

        if (!bAllowVolcanic && VolcanicTag.IsValid() && Tile.BiomeType.MatchesTag(VolcanicTag))
        {
            return EWorldPlacementRejectReason::Volcanic;
        }

        if (bAllowCoastal && CoastalTag.IsValid() && Tile.BiomeType.MatchesTag(CoastalTag))
        {
            return EWorldPlacementRejectReason::None;
        }

        if (IsForbiddenWaterBiome(Tile.BiomeType))
        {
            return EWorldPlacementRejectReason::Water;
        }

        if (!bAllowLake && LakeTag.IsValid() && Tile.BiomeType.MatchesTag(LakeTag))
        {
            return EWorldPlacementRejectReason::Water;
        }

        if (!bAllowRiver && RiverTag.IsValid() && Tile.BiomeType.MatchesTag(RiverTag))
        {
            return EWorldPlacementRejectReason::Water;
        }

        if (!bAllowWetland
            && ((MarshTag.IsValid() && Tile.BiomeType.MatchesTag(MarshTag))
                || (SwampTag.IsValid() && Tile.BiomeType.MatchesTag(SwampTag))
                || (WetlandTag.IsValid() && Tile.BiomeType.MatchesTag(WetlandTag))))
        {
            return EWorldPlacementRejectReason::InvalidBiome;
        }

        if (!bAllowMountain && MountainTag.IsValid() && Tile.BiomeType.MatchesTag(MountainTag))
        {
            return EWorldPlacementRejectReason::InvalidBiome;
        }

        return EWorldPlacementRejectReason::None;
    }

    inline EWorldPlacementRejectReason ClassifyMajorSettlementTile(const FWorldTileData& Tile)
    {
        return ClassifyMajorSiteTile(Tile, true, false, false, false, false, true, 0.1f);
    }

    inline bool IsValidMajorSettlementTile(const FWorldTileData& Tile)
    {
        return ClassifyMajorSettlementTile(Tile) == EWorldPlacementRejectReason::None;
    }

    inline EWorldPlacementRejectReason ClassifyLoreSiteTile(const FWorldTileData& Tile)
    {
        return ClassifyMajorSiteTile(Tile, true, false, false, false, false, true, 0.1f);
    }

    inline bool IsValidLoreSiteTile(const FWorldTileData& Tile)
    {
        return ClassifyLoreSiteTile(Tile) == EWorldPlacementRejectReason::None;
    }
}
