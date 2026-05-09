// Source/WorldForge/Grid/GridDesignationTypes.h
//
// Generic grid designation types and visual style data used by WorldForge's
// tile overlay. This is intentionally tool-agnostic so the same system can be
// expanded later for stockpiles, rooms, roads, restricted areas, etc.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "GridDesignationTypes.generated.h"

UENUM(BlueprintType)
enum class EGridDesignationType : uint8
{
    None         UMETA(DisplayName = "None"),
    Build        UMETA(DisplayName = "Build"),
    Mine         UMETA(DisplayName = "Mine"),
    Farm         UMETA(DisplayName = "Farm"),
    Erase        UMETA(DisplayName = "Erase"),
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

USTRUCT(BlueprintType)
struct WORLDFORGE_API FGridDesignationCellVisual
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridDesignation")
    FIntVector VoxelCoord = FIntVector::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridDesignation")
    float TerrainZ = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridDesignation")
    EGridDesignationType DesignationType = EGridDesignationType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|GridDesignation")
    EGridDesignationVisualState VisualState = EGridDesignationVisualState::Neutral;
};

USTRUCT(BlueprintType)
struct WORLDFORGE_API FGridDesignationStyleRow : public FTableRowBase
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Designation")
    EGridDesignationType DesignationType = EGridDesignationType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor NeutralGridColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.16f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor TileColor = FLinearColor(0.2f, 0.6f, 1.0f, 0.45f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor BorderColor = FLinearColor(0.3f, 0.7f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor HoverColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.75f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
    FLinearColor DragPreviewColor = FLinearColor(0.8f, 0.8f, 0.2f, 0.55f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
    TObjectPtr<UTexture2D> IconTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon")
    FLinearColor IconTint = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Icon", meta = (ClampMin = "0.05", ClampMax = "1.0"))
    float IconScale = 0.4f;
};
