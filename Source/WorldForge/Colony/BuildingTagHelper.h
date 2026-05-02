#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "BuildingTagHelper.generated.h"

UCLASS()
class WORLDFORGE_API UBuildingTagHelper : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // Section I / Milestone D:
    // Era 0 = early, 1 = mid, 2+ = late.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static FGameplayTag GetEraBuildingTag(EBuildingPurpose Purpose, int32 EraIndex);

    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static FGameplayTag GetDefaultMaterialForPurpose(EBuildingPurpose Purpose);

    // ================================
    // Shared Build Utilities
    // ================================
    // Centralised here so BuildGhostActor, VoxelConstructionSystem, and
    // BuildModeManager all share one implementation instead of duplicated
    // anonymous-namespace copies.

    // Parse "WxDxH" (e.g. "3x3x2") from a tag string.
    // Returns true and fills Out* if the pattern is found.
    static bool TryParsePrefabDimensions(
        const FString& Source,
        int32& OutWidth,
        int32& OutDepth,
        int32& OutHeight);

    // Resolve the voxel block count for a building tag.
    // Uses MatchesTag() — no .Contains() / .ToString() comparisons.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static int32 GetBlockCountForBuildingTag(FGameplayTag BuildingTag);

    // Map a voxel material tag to the stockpile item it consumes.
    // Uses MatchesTag() — no .Contains() / .ToString() comparisons.
    UFUNCTION(BlueprintPure, Category = "WorldForge|Building")
    static FGameplayTag ResolveStockpileItemForMaterial(FGameplayTag MaterialTag);
};