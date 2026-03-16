#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeColonist.generated.h"

UCLASS()
class WORLDFORGE_API AWorldForgeColonist : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeColonist();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 AgentID = -1;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 SettlementID = -1;

    // DeltaTime is explicit -- no GetDeltaSeconds() inside visual code
    void UpdateVisuals(const FAgentData& Data, float DeltaTime, float CellSize = 200.f);

    UFUNCTION(BlueprintImplementableEvent)
    void OnStateChanged(EAgentState NewState, EAgentState OldState);

    UFUNCTION(BlueprintImplementableEvent)
    void OnNeedCritical(float FoodLevel, float SleepLevel);

    UFUNCTION(BlueprintImplementableEvent)
    void OnColonistDied();

    UFUNCTION(BlueprintCallable)
    void InitializeFromAgentData(const FAgentData& Data, float CellSize = 200.f);

    // How often OnNeedCritical can fire (seconds) -- prevents every-frame spam
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist")
    float NeedCriticalCooldown = 2.0f;

private:
    EAgentState LastKnownState = EAgentState::Idle;
    float VisualCellSize = 200.f;
    FVector TargetWorldPos = FVector::ZeroVector;
    float MoveSpeed = 150.f;
    float NeedCriticalTimer = 0.f;
};