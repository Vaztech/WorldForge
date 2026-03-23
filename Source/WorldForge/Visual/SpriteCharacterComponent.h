#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "PaperFlipbook.h" // CHANGE MADE:
// Required because UPaperFlipbook is used in a reflected
// multicast delegate signature. This requires the Paper2D
// module to be added in WorldForge.Build.cs.
#include "SpriteCharacterComponent.generated.h"

class AActor;

// CHANGE MADE:
// Added a multicast dispatcher signature that broadcasts the flipbook selected by the component.
// The actor Blueprint will bind to this and decide how to display the chosen flipbook.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnFlipbookChangedSignature,
    UPaperFlipbook*,
    NewFlipbook
);

UCLASS(Blueprintable, ClassGroup = (WorldForge), meta = (BlueprintSpawnableComponent))
class WORLDFORGE_API USpriteCharacterComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    USpriteCharacterComponent();

    virtual void BeginPlay() override;
    virtual void TickComponent(float DT, ELevelTick TT, FActorComponentTickFunction* TF) override;

    // Pushes a simulation state into the component and resolves it to a Blueprint-facing anim name.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SetAnimStateFromAgentState(EAgentState State);

    // Pushes a race tag into the component so Blueprint can resolve the visual race key.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void SetRace(FGameplayTag RaceTag);

    // Blueprint event:
    // Component decides the anim state name, Blueprint decides what visual asset that means.
    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Visual")
    void OnSetAnimState(const FName& AnimStateName);

    // Blueprint event:
    // Component receives the race tag, Blueprint resolves it into the race asset key.
    UFUNCTION(BlueprintImplementableEvent, Category = "WorldForge|Visual")
    void OnRaceSet(FGameplayTag RaceTag);

    // CHANGE MADE:
    // Added a C++ dispatcher so actor Blueprints can bind to flipbook changes through the actual
    // USpriteCharacterComponent reference owned by AWorldForgeColonist.
    //
    // BP_SpriteCharacterComponent resolves SelectedFlipbook, then calls BroadcastFlipbookChanged(...)
    // below. BP_WorldForgeColonist binds to this dispatcher and applies the flipbook to the renderer.
    UPROPERTY(BlueprintAssignable, Category = "WorldForge|Visual")
    FOnFlipbookChangedSignature OnFlipbookChanged;

    // CHANGE MADE:
    // Blueprint-safe wrapper used to broadcast the selected flipbook.
    // Blueprint cannot directly call/broadcast the C++ multicast delegate property,
    // so it calls this function instead.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|Visual")
    void BroadcastFlipbookChanged(UPaperFlipbook* NewFlipbook);

    // Actor to face toward every tick for billboard-style sprite presentation.
    UPROPERTY(BlueprintReadWrite, Category = "WorldForge|Visual")
    AActor* CameraTarget = nullptr;

    // CHANGE MADE:
    // Optional visual-only yaw offset for cases where the sprite plane is authored
    // sideways or backwards relative to the camera-facing rotation.
    // Keep this at 0 by default. If needed in Blueprint, try 90, 180, or -90.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|Visual")
    float BillboardYawOffset = 0.0f;

    // Last simulation state pushed into the component.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Visual")
    EAgentState CurrentAnimState = EAgentState::Idle;

    // Last race tag pushed into the component.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "WorldForge|Visual")
    FGameplayTag CurrentRaceTag;
};