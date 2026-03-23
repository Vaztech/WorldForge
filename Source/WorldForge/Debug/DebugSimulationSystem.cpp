#include "DebugSimulationSystem.h"
#include "Engine/World.h"
#include "ProcessorManager.h"
#include "JobProcessor.h" // Required for GetTypedProcessor<UJobProcessor>()
#include "SettlementManager.h"
#include "ReservationSystem.h"
#include "EventBus.h"
#include "VoxelConstructionSystem.h"

void UDebugSimulationSystem::Initialize(FSubsystemCollectionBase& C)
{
    Super::Initialize(C);

    // CHANGE:
    // No architecture change here.
    // This subsystem still reads from existing world subsystems only.
    UE_LOG(LogTemp, Log, TEXT("[DebugSim] Initialized. 11 overlays ready."));
}

void UDebugSimulationSystem::SetActiveOverlay(EDebugOverlay O)
{
    ActiveOverlay = O;
}

void UDebugSimulationSystem::ToggleOverlay(EDebugOverlay O)
{
    ActiveOverlay = (ActiveOverlay == O) ? EDebugOverlay::None : O;
}

void UDebugSimulationSystem::CycleToNextOverlay()
{
    ActiveOverlay = static_cast<EDebugOverlay>((static_cast<int32>(ActiveOverlay) + 1) % 12);
}

void UDebugSimulationSystem::OnSimulationStep(int32 S, int32)
{
    LastStepNumber = S;
}

FString UDebugSimulationSystem::GetLowestNeedLabel(int32 ID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("?");
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        if (const FAgentData* A = PM->GetAgent(ID))
        {
            float M = A->FoodNeed;
            FString L = TEXT("Food");

            if (A->SleepNeed < M)
            {
                M = A->SleepNeed;
                L = TEXT("Sleep");
            }

            if (A->SafetyNeed < M)
            {
                M = A->SafetyNeed;
                L = TEXT("Safety");
            }

            if (A->SocialNeed < M)
            {
                M = A->SocialNeed;
                L = TEXT("Social");
            }

            if (A->PurposeNeed < M)
            {
                // CHANGE:
                // Fixed the displayed numeric value when Purpose is the lowest need.
                M = A->PurposeNeed;
                L = TEXT("Purpose");
            }

            return FString::Printf(TEXT("%s:%.0f"), *L, M);
        }
    }

    return TEXT("?");
}

FLinearColor UDebugSimulationSystem::GetNeedColor(int32 ID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return FLinearColor::White;
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        if (const FAgentData* A = PM->GetAgent(ID))
        {
            const float M = FMath::Min(
                FMath::Min(A->FoodNeed, A->SleepNeed),
                FMath::Min(FMath::Min(A->SafetyNeed, A->SocialNeed), A->PurposeNeed));

            if (M < 20.f)
            {
                return FLinearColor::Red;
            }

            if (M < 40.f)
            {
                return FLinearColor(1.f, 0.5f, 0.f);
            }

            return FLinearColor::Green;
        }
    }

    return FLinearColor::White;
}

FString UDebugSimulationSystem::GetAgentTaskLabel(int32 ID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("?");
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        if (const FAgentData* A = PM->GetAgent(ID))
        {
            const FString St = UEnum::GetValueAsString(A->State);
            const FString J = A->CurrentJobTag.IsValid() ? A->CurrentJobTag.ToString() : TEXT("None");
            return FString::Printf(TEXT("%s|%s"), *St, *J);
        }
    }

    return TEXT("?");
}

TArray<FString> UDebugSimulationSystem::GetReservationSummary(int32 SID) const
{
    TArray<FString> Lines;

    UWorld* World = GetWorld();
    if (!World)
    {
        Lines.Add(TEXT("World unavailable"));
        return Lines;
    }

    if (UReservationSystem* RS = World->GetSubsystem<UReservationSystem>())
    {
        for (const FReservationRecord& R : RS->GetAllReservations())
        {
            if (R.SettlementID != SID)
            {
                continue;
            }

            Lines.Add(FString::Printf(
                TEXT("Agent%d->Target%d(%s)"),
                R.OwnerAgentID,
                R.TargetID,
                *R.TargetTag.ToString()));
        }
    }

    if (Lines.Num() == 0)
    {
        Lines.Add(TEXT("No active reservations"));
    }

    return Lines;
}

TArray<FString> UDebugSimulationSystem::GetStockpileSummary(int32 SID) const
{
    TArray<FString> Lines;

    UWorld* World = GetWorld();
    if (!World)
    {
        Lines.Add(TEXT("World unavailable"));
        return Lines;
    }

    if (USettlementManager* SM = World->GetSubsystem<USettlementManager>())
    {
        const FSettlementState S = SM->GetSettlementState(SID);

        if (S.Stockpiles.Num() == 0)
        {
            Lines.Add(TEXT("Empty"));
            return Lines;
        }

        for (const auto& P : S.Stockpiles)
        {
            Lines.Add(FString::Printf(TEXT("%s:%d"), *P.Key.ToString(), P.Value));
        }
    }

    return Lines;
}

FString UDebugSimulationSystem::GetBuildingStateLabel(FGameplayTag Tag, int32 SID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("Not Built");
    }

    if (USettlementManager* SM = World->GetSubsystem<USettlementManager>())
    {
        if (SM->HasBuilding(SID, Tag))
        {
            return TEXT("Complete");
        }
    }

    if (UVoxelConstructionSystem* V = World->GetSubsystem<UVoxelConstructionSystem>())
    {
        if (V->HasActiveTask(SID, Tag))
        {
            return TEXT("Building");
        }
    }

    return TEXT("Not Built");
}

void UDebugSimulationSystem::SelectAgentForInspection(int32 ID)
{
    SelectedAgentID = ID;
}

FString UDebugSimulationSystem::GetInspectionReport() const
{
    if (SelectedAgentID < 0)
    {
        return TEXT("No colonist selected.");
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("Not found");
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        if (const FAgentData* A = PM->GetAgent(SelectedAgentID))
        {
            return FString::Printf(
                TEXT("Agent%d State:%s Food:%.0f Sleep:%.0f Mood:%.0f Health:%.0f"),
                A->AgentID,
                *UEnum::GetValueAsString(A->State),
                A->FoodNeed,
                A->SleepNeed,
                A->MoodScore,
                A->Health);
        }
    }

    return TEXT("Not found");
}

FString UDebugSimulationSystem::GetJobQueueSummary(int32 SettlementID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("ProcessorManager not found.");
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        int32 Idle = 0;
        int32 Working = 0;
        int32 Active = 0;
        int32 Dead = 0;

        for (const FAgentData& A : PM->GetAllAgents())
        {
            if (A.SettlementID != SettlementID)
            {
                continue;
            }

            if (A.State == EAgentState::Idle)
            {
                Idle++;
            }
            else if (A.State == EAgentState::Working)
            {
                Working++;
            }
            else if (A.State == EAgentState::Dead)
            {
                Dead++;
            }
            else
            {
                Active++;
            }
        }

        if (UJobProcessor* JP = PM->GetTypedProcessor<UJobProcessor>())
        {
            return FString::Printf(
                TEXT("Jobs Pending=%d Assigned=%d | Agents Idle=%d Work=%d Active=%d Dead=%d"),
                JP->GetPendingJobCount(SettlementID),
                JP->GetAssignedJobCount(SettlementID),
                Idle,
                Working,
                Active,
                Dead);
        }

        return FString::Printf(
            TEXT("Agents Idle=%d Work=%d Active=%d Dead=%d"),
            Idle,
            Working,
            Active,
            Dead);
    }

    return TEXT("ProcessorManager not found.");
}

FString UDebugSimulationSystem::GetMoraleReport(int32 ID) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return TEXT("?");
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        if (const FAgentData* A = PM->GetAgent(ID))
        {
            return FString::Printf(TEXT("Mood:%.0f Thoughts:%d"), A->MoodScore, A->RecentThoughts.Num());
        }
    }

    return TEXT("?");
}

TArray<FString> UDebugSimulationSystem::GetRecentEventLog() const
{
    TArray<FString> Lines;

    UWorld* World = GetWorld();
    if (!World)
    {
        Lines.Add(TEXT("World unavailable"));
        return Lines;
    }

    if (UEventBus* Bus = World->GetSubsystem<UEventBus>())
    {
        for (const FSimulationEvent& E : Bus->GetRecentEvents())
        {
            Lines.Add(FString::Printf(
                TEXT("[Step%d] %s Agent:%d"),
                E.StepNumber,
                *E.EventType.ToString(),
                E.AgentID));
        }
    }

    return Lines;
}

TArray<FString> UDebugSimulationSystem::GetProcessorTimingReport() const
{
    TArray<FString> Lines;
    Lines.Add(FString::Printf(TEXT("Step %d"), LastStepNumber));

    UWorld* World = GetWorld();
    if (!World)
    {
        Lines.Add(TEXT("World unavailable"));
        return Lines;
    }

    if (UProcessorManager* PM = World->GetSubsystem<UProcessorManager>())
    {
        const TArray<float>& T = PM->GetProcessorTimings();
        const TArray<FName>& N = PM->GetProcessorNames();

        // CHANGE:
        // Added a bounds-safe loop in case names/timings ever get out of sync.
        const int32 Count = FMath::Min(T.Num(), N.Num());
        for (int32 i = 0; i < Count; i++)
        {
            Lines.Add(FString::Printf(TEXT("%s: %.3fms"), *N[i].ToString(), T[i]));
        }
    }

    return Lines;
}

UDebugSimulationSystem* UDebugSimulationBlueprintLibrary::GetDebugSimulationSystem(const UObject* WorldContextObject)
{
    // CHANGE:
    // Explicit Blueprint helper to fetch the subsystem instance using any valid world context.
    // This is the minimal fallback when the engine does not surface the auto-generated getter node.
    if (!WorldContextObject)
    {
        return nullptr;
    }

    UWorld* World = WorldContextObject->GetWorld();
    if (!World)
    {
        return nullptr;
    }

    return World->GetSubsystem<UDebugSimulationSystem>();
}