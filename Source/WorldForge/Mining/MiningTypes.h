#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Player/DigResult.h"
#include "MiningTypes.generated.h"

UENUM(BlueprintType)
enum class EMiningDesignationState : uint8
{
	Pending,
	Reserved,
	InProgress,
	Complete,
	Cancelled,
	Failed
};

USTRUCT(BlueprintType)
struct FMiningDesignation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 DesignationID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	int32 SettlementID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	FIntVector GridPosition = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly)
	FGameplayTag ExpectedBlockType;

	UPROPERTY(BlueprintReadOnly)
	EMiningDesignationState State = EMiningDesignationState::Pending;

	UPROPERTY(BlueprintReadOnly)
	int32 ReservedAgentID = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	int32 CreatedStep = 0;

	// -------------------------------------------------------------------
	// v5.0: SDF brush parameters
	// -------------------------------------------------------------------

	/**
	 * Brush shape to use when this designation is completed.
	 * Default: Sphere (standard single-block mine action).
	 * Cylinder: vertical shaft designations.
	 * Box: room excavation designations.
	 */
	UPROPERTY(BlueprintReadOnly)
	EDigBrushShape BrushShape = EDigBrushShape::Sphere;

	/**
	 * SDF brush ID assigned after the brush is applied.
	 * Used for undo/cancellation of completed mining ops.
	 * INDEX_NONE until the designation is completed.
	 */
	UPROPERTY(BlueprintReadOnly)
	int32 AppliedBrushID = INDEX_NONE;
};
