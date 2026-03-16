#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "../Data/WorldForgeDataTypes.h"
#include "VoxelWorldManager.generated.h"

UCLASS()
class WORLDFORGE_API UVoxelWorldManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    void ApplyBiomeProfileToChunk(FGameplayTag BiomeTag, const FVoxelBiomeProfile& Profile, FVector ChunkOrigin);

    UFUNCTION(BlueprintCallable)
    void PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintCallable)
    void NotifyVoxelDig(FVector WorldPosition, int32 AgentID);

    UFUNCTION(BlueprintImplementableEvent)
    void OnBiomeChunkRequested(FVector Origin, float Amplitude, float Frequency, int32 Octaves);

    UFUNCTION(BlueprintImplementableEvent)
    void OnVoxelBlockPlaced(FVector WorldPosition, FGameplayTag MaterialTag);

    UFUNCTION(BlueprintImplementableEvent)
    void OnVoxelDigged(FVector WorldPosition);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ChunkWorldSize = 3200.0f;

private:
    bool ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const;
};