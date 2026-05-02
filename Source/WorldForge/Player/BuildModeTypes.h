// Source/WorldForge/Player/BuildModeTypes.h

#pragma once

#include "CoreMinimal.h"
#include "BuildModeTypes.generated.h"

// ===================================================================
// High-level Build Mode
// ===================================================================

UENUM(BlueprintType)
enum class EBuildMode : uint8
{
	None       UMETA(DisplayName = "None"),        // Not placing a build object
	Prefab     UMETA(DisplayName = "Prefab"),      // Placing a prefab building
	CustomDraw UMETA(DisplayName = "Custom Draw")  // Painting individual cells / voxels
};

// ===================================================================
// Build Ghost Visual State
// ===================================================================

UENUM(BlueprintType)
enum class EBuildGhostState : uint8
{
	Valid   UMETA(DisplayName = "Valid"),   // Green — can place here
	Invalid UMETA(DisplayName = "Invalid"), // Red — blocked / occupied / invalid terrain
	Hidden  UMETA(DisplayName = "Hidden")   // Not visible
};

// ===================================================================
// Phase 1: Build Tool Mode
// Separate from high-level build mode.
// Example:
//   EBuildMode::CustomDraw + EBuildToolMode::Place
//   EBuildMode::None       + EBuildToolMode::Dig
// ===================================================================

UENUM(BlueprintType)
enum class EBuildToolMode : uint8
{
	None   UMETA(DisplayName = "None"),
	Place  UMETA(DisplayName = "Place"),
	Remove UMETA(DisplayName = "Remove"),

	// Phase 1 additions
	Dig    UMETA(DisplayName = "Dig"),

	// Phase 4 stub for now
	Mine   UMETA(DisplayName = "Mine")
};

// ===================================================================
// Phase 1: Dig Settings
// ===================================================================

/**
 * Configuration for the dig tool. Serialized per-player.
 */
USTRUCT(BlueprintType)
struct FDigSettings
{
	GENERATED_BODY()

	/** Dig radius in grid cells. 1 = single, 2 = 3x3, 3 = 5x5. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig",
		meta = (ClampMin = "1", ClampMax = "5", UIMin = "1", UIMax = "5"))
	int32 Radius = 1;

	/** Vertical layers removed per action. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig",
		meta = (ClampMin = "1", ClampMax = "3", UIMin = "1", UIMax = "3"))
	int32 DepthPerAction = 1;

	/** Max allowed relative dig depth. 0 = unlimited. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig",
		meta = (ClampMin = "0"))
	int32 MaxDigDepth = 0;

	/** Cooldown between dig actions, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dig",
		meta = (ClampMin = "0.0"))
	float DigCooldown = 0.15f;
};