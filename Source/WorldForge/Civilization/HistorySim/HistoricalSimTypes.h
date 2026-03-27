#pragma once

#include "CoreMinimal.h"
#include "HistoricalSimTypes.generated.h"

USTRUCT(BlueprintType)
struct FHistoricalSimConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 TotalYears = 200;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float WarFrequency = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DisasterFrequency = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float EconomicComplexity = 0.5f;
};