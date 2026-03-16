#pragma once

#include "CoreMinimal.h"
#include "BaseProcessor.h"
#include "ReservationProcessor.generated.h"

UCLASS()
class WORLDFORGE_API UReservationProcessor : public UBaseProcessor
{
    GENERATED_BODY()

public:
    virtual void ProcessStep(
        int32 Step,
        int32 SettlementID,
        TArray<FAgentData>& Agents,
        UWorld* World
    ) override;

    virtual FName GetProcessorName() const override
    {
        return FName(TEXT("Reservation"));
    }
};