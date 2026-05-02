#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
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
};