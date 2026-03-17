#include "ReservationProcessor.h"
#include "ReservationSystem.h"

void UReservationProcessor::ProcessStep(
    int32 StepNumber,
    int32 SettlementID,
    TArray<FAgentData>& Agents,
    UWorld* World)
{
    // This processor only advances reservation cleanup state.
    // Agent array is unused here by design.
    (void)Agents;

    if (!World)
    {
        return;
    }

    // Remove expired reservations for this settlement at the current step.
    if (UReservationSystem* RS = World->GetSubsystem<UReservationSystem>())
    {
        RS->CleanupExpiredReservations(StepNumber, SettlementID);
    }
}