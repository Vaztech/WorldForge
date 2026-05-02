#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "Player/DigResult.h"
#include "EventBus.generated.h"

struct FMinedResourceResult;
struct FResourcePickupInfo;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSimEvent, const FSimulationEvent&, Event);

UCLASS(BlueprintType)
class WORLDFORGE_API UEventBus : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    UFUNCTION(BlueprintCallable)
    void Dispatch(const FSimulationEvent& Event);

    void FlushPendingEvents();

    UPROPERTY(BlueprintAssignable)
    FOnSimEvent OnEventFlushed;

    // ----- Phase 1: Digging Events -----

    /** Fired when terrain has been dug. */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnTerrainDug, const FDigResult&);
    FOnTerrainDug OnTerrainDug;

    /** Fired when a resource node is revealed by digging. */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnResourceNodeRevealed, const FRevealedNodeEntry&);
    FOnResourceNodeRevealed OnResourceNodeRevealed;

    // ----- Phase 4: Mining Events (declared now, wired later) -----

    /** Fired when a mining node is fully destroyed. */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnResourceNodeMined, const FMinedResourceResult&);
    FOnResourceNodeMined OnResourceNodeMined;

    /** Fired when a player picks up a resource from a mined node. */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnResourcePickedUp, const FResourcePickupInfo&);
    FOnResourcePickedUp OnResourcePickedUp;

    int32 GetPendingEventCount() const { return PendingEvents.Num(); }
    int32 GetTotalEventsDispatched() const { return TotalDispatched; }
    const TArray<FSimulationEvent>& GetRecentEvents() const { return RecentEvents; }

private:
    TArray<FSimulationEvent> PendingEvents;
    TArray<FSimulationEvent> RecentEvents;
    int32 TotalDispatched = 0;
    static const int32 MaxRecentEvents = 20;
};