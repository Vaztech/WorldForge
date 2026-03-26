#include "OverworldSubsystem.h"
#include "WorldEconomySimulation.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

void UOverworldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[Overworld] Initialized."));
}

void UOverworldSubsystem::Deinitialize()
{
    ActiveCaravans.Empty();
    ActiveOutposts.Empty();
    NextCaravanID = 0;
    NextOutpostID = 0;

    Super::Deinitialize();
}

void UOverworldSubsystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)StepNumber;   // ADDED
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    if (!World) return;

    StepCaravans(StepNumber, World);
    StepOutpostSupplyLines(StepNumber, World);
}

// ── Caravan dispatch ───────────────────────────────────────────────────────
int32 UOverworldSubsystem::DispatchCaravan(
    int32 OriginSettlementID,
    int32 DestSettlementID,
    const TArray<FCargoItem>& Cargo,
    float PaymentAmount,
    FGameplayTag PaymentCurrencyTag,
    int32 TravelSteps)
{
    FCaravanData C;
    C.CaravanID = NextCaravanID++;
    C.OriginSettlementID = OriginSettlementID;
    C.DestSettlementID = DestSettlementID;
    C.Cargo = Cargo;
    C.PaymentAmount = PaymentAmount;
    C.PaymentCurrencyTag = PaymentCurrencyTag;
    C.StepsUntilArrival = FMath::Max(1, TravelSteps);
    C.bOutbound = true;

    ActiveCaravans.Add(C);

    UE_LOG(LogTemp, Log,
        TEXT("[Overworld] Caravan %d dispatched: %d -> %d, arrives in %d steps"),
        C.CaravanID,
        OriginSettlementID,
        DestSettlementID,
        C.StepsUntilArrival);

    return C.CaravanID;
}

// ── Caravan stepping ───────────────────────────────────────────────────────
void UOverworldSubsystem::StepCaravans(int32 StepNumber, UWorld* World)
{
    (void)StepNumber; // ADDED

    TArray<FCaravanData> Arrived;

    for (FCaravanData& C : ActiveCaravans)
    {
        C.StepsUntilArrival--;

        if (C.StepsUntilArrival <= 0)
        {
            Arrived.Add(C);
        }
    }

    for (const FCaravanData& C : Arrived)
    {
        ActiveCaravans.RemoveAll([&C](const FCaravanData& Existing)
            {
                return Existing.CaravanID == C.CaravanID;
            });

        ProcessCaravanArrival(C, World);
    }
}

void UOverworldSubsystem::ProcessCaravanArrival(
    const FCaravanData& Caravan,
    UWorld* World) const
{
    if (!World)
    {
        return;
    }

    UE_LOG(LogTemp, Log,
        TEXT("[Overworld] Caravan %d arrived at settlement %d"),
        Caravan.CaravanID,
        Caravan.DestSettlementID);

    USettlementManager* SM = World->GetSubsystem<USettlementManager>();
    if (SM)
    {
        // Deliver cargo to destination stockpile
        for (const FCargoItem& Item : Caravan.Cargo)
        {
            SM->AddToStockpile(
                Caravan.DestSettlementID,
                Item.ItemTag,
                Item.Quantity);
        }
    }

    UWorldEconomySimulation* EconSim = World->GetSubsystem<UWorldEconomySimulation>();
    if (EconSim)
    {
        // MODIFIED
        // Call using the actual current project signature:
        // ProcessCaravanTransaction(BuyerSettlementID, SellerSettlementID, CaravanID)
        EconSim->ProcessCaravanTransaction(
            Caravan.DestSettlementID,
            Caravan.OriginSettlementID,
            Caravan.CaravanID);
    }

    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    if (Bus)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.CaravanArrived")));
        E.SettlementID = Caravan.DestSettlementID;
        E.AgentID = Caravan.CaravanID;     // MODIFIED
        E.StepNumber = 0;                  // ADDED
        E.Payload = FGameplayTag();        // ADDED
        E.FloatPayload = 0.0f;             // ADDED

        Bus->Dispatch(E);
    }
}

// ── Outpost management ─────────────────────────────────────────────────────
int32 UOverworldSubsystem::BuildOutpost(
    int32 OwnerSettlementID,
    FIntPoint GridCell,
    EOutpostType OutpostType)
{
    FOutpostData O;
    O.OutpostID = NextOutpostID++;
    O.OwnerSettlementID = OwnerSettlementID;
    O.WorldGridCell = GridCell;
    O.OutpostType = OutpostType;
    O.bActive = true;
    O.StepsWithoutSupply = 0;
    O.FlowRateMod = 1.0f;

    ActiveOutposts.Add(O);

    UE_LOG(LogTemp, Log,
        TEXT("[Overworld] Outpost %d built at (%d,%d) type %d"),
        O.OutpostID,
        GridCell.X,
        GridCell.Y,
        static_cast<int32>(OutpostType));

    return O.OutpostID;
}

void UOverworldSubsystem::AbandonOutpost(int32 OutpostID)
{
    for (FOutpostData& O : ActiveOutposts)
    {
        if (O.OutpostID == OutpostID && O.bActive)
        {
            O.bActive = false; // MODIFIED
            UE_LOG(LogTemp, Warning,
                TEXT("[Overworld] Outpost %d abandoned."),
                OutpostID);
            return;
        }
    }
}

void UOverworldSubsystem::StepOutpostSupplyLines(int32 StepNumber, UWorld* World)
{
    (void)World; // ADDED

    for (FOutpostData& O : ActiveOutposts)
    {
        if (!O.bActive)
        {
            continue;
        }

        if (IsOutpostSupplied(O, StepNumber))
        {
            O.StepsWithoutSupply = 0;
            continue;
        }

        O.StepsWithoutSupply++;

        if (O.StepsWithoutSupply >= OutpostAbandonsAt)
        {
            O.bActive = false; // MODIFIED
            UE_LOG(LogTemp, Warning,
                TEXT("[Overworld] Outpost %d abandoned after %d unsupplied steps."),
                O.OutpostID,
                O.StepsWithoutSupply);
        }
        else if (O.StepsWithoutSupply >= OutpostDegradesAt)
        {
            O.FlowRateMod = 0.5f; // MODIFIED
            UE_LOG(LogTemp, Warning,
                TEXT("[Overworld] Outpost %d degraded after %d unsupplied steps."),
                O.OutpostID,
                O.StepsWithoutSupply);
        }
    }
}

bool UOverworldSubsystem::IsOutpostSupplied(
    const FOutpostData& Outpost,
    int32 StepNumber) const
{
    (void)Outpost;    // ADDED
    (void)StepNumber; // ADDED

    // ADDED
    // Phase 3B stub: outposts are always considered supplied.
    // Real supply-line checks are deferred to a later phase.
    return true;
}

float UOverworldSubsystem::GetInfrastructureMod(int32 OriginID, int32 DestID) const
{
    float BestMod = 1.0f;

    for (const FOutpostData& O : ActiveOutposts)
    {
        if (!O.bActive)
        {
            continue;
        }

        if (O.OwnerSettlementID == OriginID || O.OwnerSettlementID == DestID)
        {
            if (O.OutpostType == EOutpostType::TradePost ||
                O.OutpostType == EOutpostType::MarketOutpost)
            {
                BestMod = FMath::Max(BestMod, O.FlowRateMod * 1.3f);
            }
        }
    }

    return BestMod;
}

// ── Save / Load ───────────────────────────────────────────────────────────
FOverworldSaveState UOverworldSubsystem::GetOverworldSaveState() const
{
    FOverworldSaveState S;
    S.ActiveCaravans = ActiveCaravans;
    S.ActiveOutposts = ActiveOutposts;
    S.NextCaravanID = NextCaravanID;
    S.NextOutpostID = NextOutpostID;
    return S;
}

void UOverworldSubsystem::RestoreOverworldFromSave(const FOverworldSaveState& S)
{
    ActiveCaravans = S.ActiveCaravans;
    ActiveOutposts = S.ActiveOutposts;
    NextCaravanID = S.NextCaravanID;
    NextOutpostID = S.NextOutpostID;

    UE_LOG(LogTemp, Log,
        TEXT("[Overworld] Restored %d caravans, %d outposts."),
        ActiveCaravans.Num(),
        ActiveOutposts.Num());
}