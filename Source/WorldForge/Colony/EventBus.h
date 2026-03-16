#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.h"
#include "EventBus.generated.h"

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

    int32 GetPendingEventCount() const { return PendingEvents.Num(); }
    int32 GetTotalEventsDispatched() const { return TotalDispatched; }
    const TArray<FSimulationEvent>& GetRecentEvents() const { return RecentEvents; }

private:
    TArray<FSimulationEvent> PendingEvents;
    TArray<FSimulationEvent> RecentEvents;
    int32 TotalDispatched = 0;
    static const int32 MaxRecentEvents = 20;
};