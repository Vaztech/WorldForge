#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldForgeDataTypes.h"
#include "WorldForgeColonist.generated.h"

class UStaticMeshComponent;
class USpriteCharacterComponent; // Sprite-facing visual bridge component for V-F

/**
 * AWorldForgeColonist
 *
 * Visual actor representing a single colonist in the world.
 *
 * This actor does NOT poll simulation state on its own.
 * Instead, an external sync layer pushes FAgentData snapshots into it
 * through UpdateVisuals(...), and Tick() only handles interpolation.
 */
UCLASS()
class WORLDFORGE_API AWorldForgeColonist : public AActor
{
    GENERATED_BODY()

public:
    AWorldForgeColonist();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    /** Simulation agent ID this visual actor represents */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 AgentID = -1;

    /** Settlement this colonist belongs to */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist")
    int32 SettlementID = -1;

    /**
     * Pushes the latest simulation snapshot into this visual actor.
     *
     * Important:
     * - This no longer performs smoothing directly.
     * - It caches the latest target/state.
     * - Tick() handles interpolation toward TargetWorldPos.
     */
    void UpdateVisuals(const FAgentData& Data, float DeltaTime, float CellSize = 200.f);

    /** Fired when the simulation state changes */
    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnStateChanged(EAgentState NewState, EAgentState OldState);

    /** Fired when food or sleep need becomes critically low */
    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnNeedCritical(float FoodLevel, float SleepLevel);

    /** Fired once when the colonist enters the Dead state */
    UFUNCTION(BlueprintImplementableEvent, Category = "Colonist|Events")
    void OnColonistDied();

    /**
     * Initializes this visual actor from the initial simulation snapshot.
     * Also sets the starting world position immediately.
     */
    UFUNCTION(BlueprintCallable, Category = "Colonist")
    void InitializeFromAgentData(const FAgentData& Data, float CellSize = 200.f);

    // CHANGE MADE:
    // Allows the Blueprint actor to provide the actual sprite visual component instance
    // that should receive race/state updates.
    //
    // This is needed because the C++ actor creates a base USpriteCharacterComponent by default,
    // but the live visual logic currently exists in BP_SpriteCharacterComponent.
    UFUNCTION(BlueprintCallable, Category = "Colonist|Visual")
    void SetSpriteVisualComponent(USpriteCharacterComponent* InSpriteVisual);

    /** Minimum time between OnNeedCritical events to avoid spam */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Colonist")
    float NeedCriticalCooldown = 2.0f;

protected:
    /**
     * Old debug cube kept as a safe fallback during V-F.
     * It is hidden in game once sprite visuals are active.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    UStaticMeshComponent* DebugMesh = nullptr;

    /**
     * V-F sprite bridge component.
     * This component:
     * - faces the camera
     * - maps EAgentState -> animation names
     * - stores current race tag for visual selection
     *
     * Actual visible sprite implementation can be handled in Blueprint
     * via the component's BlueprintImplementableEvents.
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Colonist|Visual")
    USpriteCharacterComponent* SpriteVisual = nullptr;

private:
    /** Tick-only movement smoothing. No sim reads happen here. */
    void UpdateMovement(float DeltaTime);

    /** Last known simulation state so we can detect state transitions */
    EAgentState LastKnownState = EAgentState::Idle;

    /** Size of one simulation cell in world units */
    float VisualCellSize = 200.f;

    /** Current movement target in world space */
    FVector TargetWorldPos = FVector::ZeroVector;

    /** Interpolation speed for smoothing movement */
    float MoveSpeed = 150.f;

    /** Countdown timer used to throttle critical-need events */
    float NeedCriticalTimer = 0.f;

    /** True after this actor has received at least one valid visual snapshot */
    bool bHasVisualData = false;
};