// Source/WorldForge/Grid/GridDesignationTypes.h
//
// Generic, data-driven visual designation types for the WorldForge grid overlay.
// This is intentionally tool-agnostic so the same overlay system can later
// support rooms, stockpiles, roads, restricted zones, work areas, etc.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GridDesignationTypes.generated.h"

class UTexture2D;

/**
 * What a grid cell MEANS to the player.
 * Tool systems decide gameplay behavior; the overlay only uses this for visuals.
 */
UENUM(BlueprintType)
enum class EGridDesignationType : uint8
{
    None         UMETA(DisplayName = "None"),

    // Current tool family
    Build        UMETA(DisplayName = "Build"),
    Mine         UMETA(DisplayName = "Mine"),
    Farm         UMETA(DisplayName = "Farm"),
    Erase        UMETA(DisplayName = "Erase"),

    // Future zoning / room designations
    Stockpile    UMETA(DisplayName = "Stockpile"),
    LivingSpace  UMETA(DisplayName = "Living Space"),
    Bedroom      UMETA(DisplayName = "Bedroom"),
    Workshop     UMETA(DisplayName = "Workshop"),
    DiningArea   UMETA(DisplayName = "Dining Area"),
    MedicalArea  UMETA(DisplayName = "Medical Area"),
    WorkZone     UMETA(DisplayName = "Work Zone"),
    Road         UMETA(DisplayName = "Road"),
    Restricted   UMETA(DisplayName = "Restricted"),
    Custom       UMETA(DisplayName = "Custom")
};

FORCEINLINE uint32 GetTypeHash(const EGridDesignationType Type)
{
    return ::GetTypeHash(static_cast<uint8>(Type));
}

/**
 * How a grid cell should be drawn right now.
 * This stays separate from designation type so any designation can be hovered,
 * previewed, confirmed, invalid, etc.
 */
UENUM(BlueprintType)
enum class EGridDesignationVisualState : uint8
{
    Neutral      UMETA(DisplayName = "Neutral"),
    Hover        UMETA(DisplayName = "Hover"),
    Preview      UMETA(DisplayName = "Preview"),
    Selected     UMETA(DisplayName = "Selected"),
    Confirmed    UMETA(DisplayName = "Confirmed"),
    Invalid      UMETA(DisplayName = "Invalid"),
    ErasePreview UMETA(DisplayName = "Erase Preview")
};

/**
 * DataTable row used to configure overlay visuals without hardcoding them.
 * Suggested DataTable path for fallback auto-load:
 *   /Game/WorldForge/Visual/UI/DT_GridDesignationStyles
 *
 * Suggested row names:
 *   Build, Mine, Farm, Erase, Stockpile, LivingSpace, Bedroom, Workshop, Road
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FGridDesignationStyleRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Which designation this row configures. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Designation")
    EGridDesignationType DesignationType = EGridDesignationType::None;

    /** Faint always-visible background grid tile while a tool is active. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor NeutralGridColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.08f);

    /** Selected/confirmed tile fill tint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor TileColor = FLinearColor(0.2f, 0.6f, 1.0f, 0.35f);

    /** Border/outline color. Current material uses alpha as BorderAlpha. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor BorderColor = FLinearColor(0.3f, 0.7f, 1.0f, 0.8f);

    /** Hover tint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor HoverColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.5f);

    /** Click-drag preview tint. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor DragPreviewColor = FLinearColor(0.8f, 0.8f, 0.2f, 0.25f);

    /** Optional centered icon texture. Use square texture with alpha. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
    TObjectPtr<UTexture2D> IconTexture = nullptr;

    /** Icon tint. Alpha controls icon opacity if the material reads custom data alpha. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
    FLinearColor IconTint = FLinearColor(1.0f, 1.0f, 1.0f, 0.9f);

    /** Icon size relative to tile size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float IconScale = 0.4f;
};

/** A generic visual cell record that future systems can persist or replay. */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FGridDesignationCellVisual
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FIntVector VoxelCoord = FIntVector::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    float TerrainZ = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    EGridDesignationType DesignationType = EGridDesignationType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    EGridDesignationVisualState VisualState = EGridDesignationVisualState::Neutral;
};
