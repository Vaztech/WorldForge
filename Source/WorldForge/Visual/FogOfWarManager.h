#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "../Data/WorldForgeDataTypes.h"
#include "FogOfWarManager.generated.h"

UCLASS()
class WORLDFORGE_API UFogOfWarManager : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    void InitializeFogForWorld(int32 InGridWidth, int32 InGridHeight);

    UFUNCTION(BlueprintCallable)
    void RevealTile(int32 TileID, EKnowledgeLevel NewLevel, float Accuracy = 1.0f);

    UFUNCTION(BlueprintCallable)
    FVector GetTileWorldPosition(int32 TileID) const;

    bool IsInitialized() const { return bFogInitialized; }

    UFUNCTION(BlueprintImplementableEvent)
    void OnTileKnowledgeChanged(int32 TileID, EKnowledgeLevel NewLevel, FVector TileWorldPosition);

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TileWorldSize = 3200.0f;

private:
    int32 GridWidth = 256;
    int32 GridHeight = 256;
    bool bFogInitialized = false;
};