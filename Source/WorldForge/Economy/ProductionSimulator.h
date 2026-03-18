#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ProductionSimulator.generated.h"

USTRUCT(BlueprintType)
struct WORLDFORGE_API FProductionInputs
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	int32 WorkerCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float SkillMod = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float BuildingMod = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float BiomeMod = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float DisruptionFactor = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Economy")
	float CosmicMod = 1.0f;
};

UCLASS(BlueprintType)
class WORLDFORGE_API UProductionSimulator : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "WorldForge|Economy")
	float ComputeOutput(const FProductionInputs& In) const;
};