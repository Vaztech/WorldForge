#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "WorldForgeBuildDataTypes.generated.h"

// ============================================================
// FWorldForgeBuildPieceRow
// CSV DataTable row for individual modular build pieces.
// All fields use CSV-safe types (no TArray, TMap, TSoftPtr).
// Resource costs and tags are encoded as delimited strings
// and parsed at runtime when needed.
// ============================================================
USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeBuildPieceRow : public FTableRowBase
{
    GENERATED_BODY()

    // --- Identity ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Identity")
    FName BuildID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Identity")
    FText Description;

    // --- Categorisation ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Category")
    FName Category;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Category")
    FName SubCategory;

    // --- Asset Paths ---
    // Use full Unreal content-browser paths, e.g. /Game/Meshes/Walls/SM_WallA
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Assets")
    FString MeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Assets")
    FString BlueprintClassPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Assets")
    FString PreviewMeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Assets")
    FString IconPath;

    // --- Footprint ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Footprint")
    int32 WidthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Footprint")
    int32 DepthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Footprint")
    int32 HeightLayers = 1;

    // --- Placement Flags ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bRequiresClaimedLand = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bBlocksMovement = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bSupportsRoof = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bRequiresFoundation = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bCanPlaceUnderground = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bCanPlaceOnSurface = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Placement")
    bool bCanRotate = true;

    // --- Cost & Tags (encoded strings) ---
    // ResourceCostText: pipe-delimited "ItemID:Amount" pairs.
    // Example: "Wood:10|Stone:5"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Data")
    FString ResourceCostText;

    // BuildTagsText: pipe-delimited tag names.
    // Example: "Structure.Wall|Structure.Interior"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPiece|Data")
    FString BuildTagsText;
};


// ============================================================
// FWorldForgeBuildingPrefabRow
// CSV DataTable row for multi-piece building prefabs.
// IncludedPieceIDsText holds pipe-delimited BuildIDs that
// make up this prefab, resolved at runtime against the
// build-piece DataTable.
// ============================================================
USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeBuildingPrefabRow : public FTableRowBase
{
    GENERATED_BODY()

    // --- Identity ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Identity")
    FName PrefabID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Identity")
    FText Description;

    // --- Categorisation ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Category")
    FName Category;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Category")
    FName SubCategory;

    // --- Asset Paths ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Assets")
    FString PrimaryMeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Assets")
    FString BlueprintClassPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Assets")
    FString PreviewMeshPath;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Assets")
    FString IconPath;

    // --- Footprint ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Footprint")
    int32 WidthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Footprint")
    int32 DepthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Footprint")
    int32 HeightLayers = 1;

    // --- Placement Flags ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Placement")
    bool bRequiresClaimedLand = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Placement")
    bool bCanPlaceUnderground = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Placement")
    bool bCanPlaceOnSurface = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Placement")
    bool bCanRotate = true;

    // --- Cost, Pieces & Tags (encoded strings) ---
    // ResourceCostText: pipe-delimited "ItemID:Amount" pairs.
    // Example: "Wood:20|Stone:10|Iron:2"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Data")
    FString ResourceCostText;

    // IncludedPieceIDsText: pipe-delimited BuildIDs from FWorldForgeBuildPieceRow.
    // Example: "wall_wood_a|wall_wood_b|floor_wood_a"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Data")
    FString IncludedPieceIDsText;

    // BuildTagsText: pipe-delimited tag names.
    // Example: "Structure.House|Structure.Residential"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildPrefab|Data")
    FString BuildTagsText;
};


// ============================================================
// FWorldForgeBuildCategoryRuleRow
// CSV DataTable row for build category defaults and rules.
// Acts as a fallback/template when individual piece rows
// do not override a value.
// ============================================================
USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeBuildCategoryRuleRow : public FTableRowBase
{
    GENERATED_BODY()

    // --- Identity ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Identity")
    FName CategoryID;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Identity")
    FText DisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Identity")
    FText Description;

    // --- Default Placement Flags ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bRequiresClaimedLand = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bBlocksMovementByDefault = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bRequiresFoundationByDefault = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bCanPlaceUndergroundByDefault = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bCanPlaceOnSurfaceByDefault = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    bool bCanRotateByDefault = true;

    // --- Default Footprint ---
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    int32 DefaultWidthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    int32 DefaultDepthTiles = 1;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Defaults")
    int32 DefaultHeightLayers = 1;

    // --- Default Cost & Tags (encoded strings) ---
    // DefaultResourceCostText: pipe-delimited "ItemID:Amount" pairs.
    // Example: "Wood:5"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Data")
    FString DefaultResourceCostText;

    // CategoryTagsText: pipe-delimited tag names applied to all pieces in this category.
    // Example: "Structure.Category.Walls"
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BuildCategory|Data")
    FString CategoryTagsText;
};
