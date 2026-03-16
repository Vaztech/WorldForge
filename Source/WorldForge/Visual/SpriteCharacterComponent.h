#pragma once
#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "SpriteCharacterComponent.generated.h"

class AActor;

UCLASS(ClassGroup = (WorldForge), meta = (BlueprintSpawnableComponent))
class WORLDFORGE_API USpriteCharacterComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    USpriteCharacterComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DT, ELevelTick TT, FActorComponentTickFunction* TF) override;

    UFUNCTION(BlueprintCallable) void SetAnimStateFromAgentState(EAgentState State);
    UFUNCTION(BlueprintCallable) void SetRace(FGameplayTag RaceTag);

    UFUNCTION(BlueprintImplementableEvent) void OnSetAnimState(const FName& AnimStateName);
    UFUNCTION(BlueprintImplementableEvent) void OnRaceSet(FGameplayTag RaceTag);

    UPROPERTY(BlueprintReadWrite) AActor* CameraTarget = nullptr;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EAgentState CurrentAnimState = EAgentState::Idle;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FGameplayTag CurrentRaceTag;
};