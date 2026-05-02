#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldForgeEnvironmentalSubsystem.generated.h"

UENUM(BlueprintType)
enum class EForgeSeason : uint8 { Spring, Summer, Autumn, Winter, CosmicGale };

USTRUCT(BlueprintType)
struct FWeatherState
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Temperature;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Precipitation; // 0.0 to 1.0
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float CloudDensity;
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FVector WindDirection;
};

UCLASS()
class WORLDFORGE_API UWorldForgeEnvironmentalSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime);

    // Time Management
    UPROPERTY(BlueprintReadOnly) float TimeOfDay; // 0.0 to 24.0
    UPROPERTY(BlueprintReadOnly) int32 DaysPassed;

    // Logic Gate
    FWeatherState GetCurrentWeatherAtLocation(FVector Location);
    EForgeSeason GetCurrentSeason() const;

private:
    void UpdateDayNightCycle(float DeltaTime);
    void CalculateSeasonalTransition();
};