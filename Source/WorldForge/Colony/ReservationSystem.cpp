#include "ReservationSystem.h"

void UReservationSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[ReservationSystem] Initialized."));
}

int32 UReservationSystem::TryReserve(
    int32 OwnerID,
    int32 TargetID,
    const FGameplayTag& TargetTag,
    EReservationType Type,
    int32 SettlementID,
    int32 CurrentStep,
    int32 TimeoutSteps
)
{
    for (const FReservationRecord& Reservation : ActiveReservations)
    {
        if (Reservation.TargetID == TargetID && Reservation.Type == Type)
        {
            if (Reservation.OwnerAgentID == OwnerID)
            {
                return Reservation.ReservationID;
            }

            return -1;
        }
    }

    FReservationRecord Record;
    Record.ReservationID = NextReservationID++;
    Record.OwnerAgentID = OwnerID;
    Record.TargetID = TargetID;
    Record.TargetTag = TargetTag;
    Record.Type = Type;
    Record.CreatedOnStep = CurrentStep;
    Record.TimeoutSteps = TimeoutSteps;
    Record.SettlementID = SettlementID;

    ActiveReservations.Add(Record);
    return Record.ReservationID;
}

void UReservationSystem::Release(int32 ReservationID)
{
    ActiveReservations.RemoveAll([ReservationID](const FReservationRecord& Reservation)
        {
            return Reservation.ReservationID == ReservationID;
        });
}

void UReservationSystem::ReleaseAllForAgent(int32 AgentID)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll([AgentID](const FReservationRecord& Reservation)
        {
            return Reservation.OwnerAgentID == AgentID;
        });

    if (RemovedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[ReservationSystem] Released %d for agent %d"),
            RemovedCount,
            AgentID
        );
    }
}

void UReservationSystem::ReleaseAllForTarget(int32 TargetID, EReservationType Type)
{
    ActiveReservations.RemoveAll([TargetID, Type](const FReservationRecord& Reservation)
        {
            return Reservation.TargetID == TargetID && Reservation.Type == Type;
        });
}

// Trigger 1: Timeout
void UReservationSystem::CleanupExpiredReservations(int32 CurrentStep, int32 SettlementID)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll(
        [CurrentStep, SettlementID](const FReservationRecord& Reservation)
        {
            if (Reservation.SettlementID != SettlementID)
            {
                return false;
            }

            return (CurrentStep - Reservation.CreatedOnStep) > Reservation.TimeoutSteps;
        }
    );

    if (RemovedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[ReservationSystem] Timeout cleanup: %d at step %d"),
            RemovedCount,
            CurrentStep
        );
    }
}

// Trigger 2: Agent Died
void UReservationSystem::NotifyAgentDied(int32 AgentID)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll([AgentID](const FReservationRecord& Reservation)
        {
            return Reservation.OwnerAgentID == AgentID;
        });

    if (RemovedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ReservationSystem] Agent %d died -- %d released."),
            AgentID,
            RemovedCount
        );
    }
}

// Trigger 3: Job Cancelled
// Keeps Bed reservations -- agent may still need sleep after job cancel
void UReservationSystem::NotifyJobCancelled(int32 AgentID)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll([AgentID](const FReservationRecord& Reservation)
        {
            if (Reservation.OwnerAgentID != AgentID)
            {
                return false;
            }

            return Reservation.Type == EReservationType::Item
                || Reservation.Type == EReservationType::Job
                || Reservation.Type == EReservationType::InteractionSlot;
        });

    if (RemovedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Verbose,
            TEXT("[ReservationSystem] Job cancel: %d released for agent %d."),
            RemovedCount,
            AgentID
        );
    }
}

// Trigger 4: Target Destroyed
void UReservationSystem::NotifyTargetDestroyed(int32 TargetID, EReservationType Type)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll([TargetID, Type](const FReservationRecord& Reservation)
        {
            return Reservation.TargetID == TargetID && Reservation.Type == Type;
        });

    if (RemovedCount > 0)
    {
        UE_LOG(
            LogTemp,
            Log,
            TEXT("[ReservationSystem] Target %d destroyed -- %d released."),
            TargetID,
            RemovedCount
        );
    }
}

// Trigger 5: Settlement Unloaded
void UReservationSystem::NotifySettlementUnloaded(int32 SettlementID)
{
    const int32 RemovedCount = ActiveReservations.RemoveAll([SettlementID](const FReservationRecord& Reservation)
        {
            return Reservation.SettlementID == SettlementID;
        });

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ReservationSystem] Settlement %d unloaded -- %d cleared."),
        SettlementID,
        RemovedCount
    );
}

bool UReservationSystem::IsReserved(int32 TargetID, EReservationType Type) const
{
    for (const FReservationRecord& Reservation : ActiveReservations)
    {
        if (Reservation.TargetID == TargetID && Reservation.Type == Type)
        {
            return true;
        }
    }

    return false;
}

bool UReservationSystem::IsReservedByAgent(int32 TargetID, EReservationType Type, int32 AgentID) const
{
    for (const FReservationRecord& Reservation : ActiveReservations)
    {
        if (
            Reservation.TargetID == TargetID
            && Reservation.Type == Type
            && Reservation.OwnerAgentID == AgentID
            )
        {
            return true;
        }
    }

    return false;
}

void UReservationSystem::RestoreFromSave(const TArray<FReservationRecord>& Saved)
{
    ActiveReservations = Saved;
    NextReservationID = 0;

    for (const FReservationRecord& Reservation : ActiveReservations)
    {
        NextReservationID = FMath::Max(NextReservationID, Reservation.ReservationID + 1);
    }
}