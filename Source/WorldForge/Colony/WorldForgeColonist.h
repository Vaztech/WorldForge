#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeColonist.generated.h"

class UStaticMeshComponent;
class USpriteCharacterComponent;
class UVoxelWorldManager;

UCLASS()
class WORLDFORGE_API AWorldForgeColonist : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeColonist();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 AgentID = -1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 SettlementID = -1;

    void UpdateVisuals(const FAgentData& Data, float DeltaTime, float CellSize = 200.f);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnStateChanged(EAgentState NewState, EAgentState OldState);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnNeedCritical(float FoodLevel, float SleepLevel);

    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnColonistDied();

    UFUNCTION(BlueprintCallable, Category = "Colonist")
    void InitializeFromAgentData(const FAgentData& Data, float CellSize = 200.f);

    UFUNCTION(BlueprintCallable, Category = "Colonist|Visual")
    void SetSpriteVisualComponent(USpriteCharacterComponent* InSpriteVisual);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist")
    float NeedCriticalCooldown = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist|Visual")
    float TerrainZOffset = 50.0f;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    UStaticMeshComponent* DebugMesh = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    USpriteCharacterComponent* SpriteVisual = nullptr;

private:
    void UpdateMovement(float DeltaTime);
    FVector ResolveWorldLocationFromAgentData(const FAgentData& Data, float CellSize) const;

    EAgentState LastKnownState = EAgentState::Idle;
    float VisualCellSize = 200.f;
    FVector TargetWorldPos = FVector::ZeroVector;
    float MoveSpeed = 150.f;
    float NeedCriticalTimer = 0.f;
    bool bHasVisualData = false;

    FIntPoint CurrentGridCell = FIntPoint::ZeroValue;
};