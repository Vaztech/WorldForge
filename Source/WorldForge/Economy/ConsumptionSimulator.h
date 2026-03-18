#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ConsumptionSimulator.generated.h"

USTRUCT(BlueprintType)
struct WORLDFORGE_API FConsumptionInputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	int32 Population = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float NeedWeight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float SeasonMod = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float CulturalMod = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float PriceElasticity = 1.0f;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UConsumptionSimulator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
	float ComputeDemand(const FConsumptionInputs& In) const;
};