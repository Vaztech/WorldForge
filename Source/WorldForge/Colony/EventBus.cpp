#include "EventBus.h"

void UEventBus::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("[EventBus] Initialized."));
}

void UEventBus::Dispatch(const FSimulationEvent& Event)
{
    PendingEvents.Add(Event);
}

void UEventBus::FlushPendingEvents()
{
    if (PendingEvents.Num() == 0)
    {
        return;
    }

    for (const FSimulationEvent& Event : PendingEvents)
    {
        RecentEvents.Add(Event);

        if (RecentEvents.Num() > MaxRecentEvents)
        {
            RecentEvents.RemoveAt(0);
        }

        OnEventFlushed.Broadcast(Event);
        TotalDispatched++;
    }

    PendingEvents.Empty();
}