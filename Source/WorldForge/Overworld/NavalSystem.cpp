#include "NavalSystem.h"
#include "WorldEconomySimulation.h"
#include "SettlementManager.h"
#include "EventBus.h"
#include "WorldForgeTags.h"
#include "Engine/World.h"

void UNavalSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[NavalSystem] Initialized."));
}

void UNavalSystem::Deinitialize()
{
    ActiveShips.Empty();          // ADDED
    BlockadedPorts.Empty();       // ADDED
    NextShipID = 0;               // ADDED
    CurrentWindDirection = FVector2D(1.0f, 0.0f); // ADDED

    Super::Deinitialize();
}

void UNavalSystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID; // ADDED
    (void)Agents;       // ADDED

    if (!World) return;

    // Phase 3B: read wind direction from SkyScape each step.
    // CurrentWindDirection = SkyScapeSubsystem->GetWindDirection();

    StepShips(StepNumber, World);
}

// ── Ship dispatch ──────────────────────────────────────────────────────────
int32 UNavalSystem::DispatchShip(
    int32 OwnerSettlementID,
    int32 DestSettlementID,
    EShipType ShipType,
    const TArray<FCargoItem>& Cargo,
    const FVector2D& WindDirection)
{
    if (IsBlockaded(OwnerSettlementID))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[Naval] Cannot dispatch -- port %d is blockaded."),
            OwnerSettlementID);
        return -1;
    }

    const FVector2D RouteDir = GetRouteDirection(OwnerSettlementID, DestSettlementID);

    FShipData Ship;
    Ship.ShipID = NextShipID++;
    Ship.ShipType = ShipType;
    Ship.OwnerSettlementID = OwnerSettlementID;
    Ship.OriginSettlementID = OwnerSettlementID;
    Ship.DestSettlementID = DestSettlementID;
    Ship.BaseSpeed = GetBaseSpeed(ShipType);
    Ship.CargoCap = GetCargoCap(ShipType);
    Ship.bInTransit = true;

    // Clamp cargo to capacity
    int32 TotalLoaded = 0;
    for (const FCargoItem& Item : Cargo)
    {
        if (TotalLoaded >= Ship.CargoCap)
        {
            break;
        }

        FCargoItem Clamped = Item;
        Clamped.Quantity = FMath::Min(Item.Quantity, Ship.CargoCap - TotalLoaded);
        Ship.Cargo.Add(Clamped);
        TotalLoaded += Clamped.Quantity;
    }

    // Compute travel steps based on effective speed
    const float Speed = ComputeShipSpeed(Ship, WindDirection, RouteDir);

    // Base travel = 50 steps per "unit distance". Speed modifies this.
    // Phase 3B stub: use actual grid distance between settlements later.
    Ship.StepsUntilArrival = FMath::Max(
        1,
        FMath::RoundToInt(50.0f / FMath::Max(0.1f, Speed)));

    ActiveShips.Add(Ship);

    UE_LOG(LogTemp, Log,
        TEXT("[Naval] Ship %d (%s) dispatched: %d -> %d, arrives in %d steps"),
        Ship.ShipID,
        ShipType == EShipType::Raft ? TEXT("Raft") :
        ShipType == EShipType::RiverBarge ? TEXT("Barge") : TEXT("Sloop"),
        OwnerSettlementID,
        DestSettlementID,
        Ship.StepsUntilArrival);

    return Ship.ShipID;
}

float UNavalSystem::ComputeShipSpeed(
    const FShipData& Ship,
    const FVector2D& WindDir,
    const FVector2D& RouteDir) const
{
    const float Base = FMath::Max(0.1f, Ship.BaseSpeed);

    const FVector2D SafeWind = WindDir.GetSafeNormal();
    const FVector2D SafeRoute = RouteDir.GetSafeNormal();
    const float WindDot = FVector2D::DotProduct(SafeWind, SafeRoute);

    // +1.0 = full tailwind -> +20% speed
    // -1.0 = full headwind -> -30% speed
    // Lerp: WindDot=-1 -> 0.70, WindDot=+1 -> 1.20
    const float WindMod = FMath::Lerp(
        0.70f,
        1.20f,
        FMath::Clamp((WindDot + 1.0f) * 0.5f, 0.0f, 1.0f));

    return FMath::Max(0.1f, Base * WindMod);
}

/*static*/ int32 UNavalSystem::GetCargoCap(EShipType Type)
{
    switch (Type)
    {
    case EShipType::Raft:        return 20;
    case EShipType::RiverBarge:  return 100; // high cargo, sea routes 3x = 300 effective
    case EShipType::ScoutSloop:  return 40;
    }

    return 20;
}

/*static*/ float UNavalSystem::GetBaseSpeed(EShipType Type)
{
    switch (Type)
    {
    case EShipType::Raft:        return 0.6f; // slow
    case EShipType::RiverBarge:  return 0.8f; // medium
    case EShipType::ScoutSloop:  return 1.4f; // fast
    }

    return 1.0f;
}

FVector2D UNavalSystem::GetRouteDirection(int32 OriginID, int32 DestID) const
{
    // ── BF-09 IMPLEMENTATION ─────────────────────────────────────────────
    // Derive the 2D direction vector from origin to destination settlement
    // using their world grid positions (FSettlementState::WorldOrigin).
    // Falls back to (1,0) if settlement data is unavailable.
    // ─────────────────────────────────────────────────────────────────────
    UWorld* World = GetWorld();
    USettlementManager* SM = World ? World->GetSubsystem<USettlementManager>() : nullptr;

    if (!SM)
    {
        return FVector2D(1.0f, 0.0f);
    }

    const FSettlementState Origin = SM->GetSettlementState(OriginID);
    const FSettlementState Dest = SM->GetSettlementState(DestID);

    if (Origin.SettlementID < 0 || Dest.SettlementID < 0)
    {
        return FVector2D(1.0f, 0.0f);
    }

    const FVector2D OriginPos(
        static_cast<float>(Origin.WorldOrigin.X),
        static_cast<float>(Origin.WorldOrigin.Y));
    const FVector2D DestPos(
        static_cast<float>(Dest.WorldOrigin.X),
        static_cast<float>(Dest.WorldOrigin.Y));

    const FVector2D Direction = DestPos - OriginPos;

    // Guard against zero-length direction (same-tile settlements)
    if (Direction.SizeSquared() < KINDA_SMALL_NUMBER)
    {
        return FVector2D(1.0f, 0.0f);
    }

    return Direction.GetSafeNormal();
}

// ── Blockade ───────────────────────────────────────────────────────────────
void UNavalSystem::SetBlockade(int32 PortSettlementID, bool bBlockade, UWorld* World)
{
    if (bBlockade)
    {
        BlockadedPorts.Add(PortSettlementID);
    }
    else
    {
        BlockadedPorts.Remove(PortSettlementID);
    }

    // Notify economy: set DiplomaticMod = 0 on all routes to/from this port
    UWorldEconomySimulation* ES = World ? World->GetSubsystem<UWorldEconomySimulation>() : nullptr;
    if (ES)
    {
        ES->NotifyRouteClosed(
            PortSettlementID,
            bBlockade ? TEXT("Blockade") : TEXT("Blockade lifted"));
    }

    UE_LOG(LogTemp, Log,
        TEXT("[Naval] Port %d blockade %s"),
        PortSettlementID,
        bBlockade ? TEXT("SET") : TEXT("LIFTED"));
}

bool UNavalSystem::IsBlockaded(int32 PortSettlementID) const
{
    return BlockadedPorts.Contains(PortSettlementID);
}

// ── Ship stepping ──────────────────────────────────────────────────────────
void UNavalSystem::StepShips(int32 StepNumber, UWorld* World)
{
    (void)StepNumber; // ADDED

    TArray<FShipData> Arrived;

    for (FShipData& Ship : ActiveShips)
    {
        if (!Ship.bInTransit)
        {
            continue;
        }

        // Check if destination is blockaded
        if (IsBlockaded(Ship.DestSettlementID))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[Naval] Ship %d blocked -- destination %d is blockaded."),
                Ship.ShipID,
                Ship.DestSettlementID);
            continue;
        }

        Ship.StepsUntilArrival--;
        if (Ship.StepsUntilArrival <= 0)
        {
            Ship.bInTransit = false;
            Arrived.Add(Ship);
        }
    }

    for (const FShipData& S : Arrived)
    {
        ActiveShips.RemoveAll([&S](const FShipData& R)
            {
                return R.ShipID == S.ShipID;
            });

        ProcessShipArrival(S, World);
    }
}

void UNavalSystem::ProcessShipArrival(const FShipData& Ship, UWorld* World) const
{
    UE_LOG(LogTemp, Log,
        TEXT("[Naval] Ship %d arrived at settlement %d"),
        Ship.ShipID,
        Ship.DestSettlementID);

    USettlementManager* SM = World ? World->GetSubsystem<USettlementManager>() : nullptr;
    if (SM)
    {
        for (const FCargoItem& Item : Ship.Cargo)
        {
            SM->AddToStockpile(Ship.DestSettlementID, Item.ItemTag, Item.Quantity);
        }
    }

    // Fire arrival event
    UEventBus* Bus = World ? World->GetSubsystem<UEventBus>() : nullptr;
    if (Bus)
    {
        FSimulationEvent E;
        E.EventType = FGameplayTag::RequestGameplayTag(
            FName(TEXT("WorldForge.Sim.Event.ShipArrived")));
        E.SettlementID = Ship.DestSettlementID;
        E.AgentID = Ship.ShipID;     // MODIFIED
        E.StepNumber = 0;               // ADDED
        E.Payload = FGameplayTag();  // ADDED
        E.FloatPayload = 0.0f;            // ADDED

        Bus->Dispatch(E);
    }
}

// ── Save / Load ───────────────────────────────────────────────────────────
FNavalSaveState UNavalSystem::GetNavalSaveState() const
{
    FNavalSaveState S;
    S.ActiveShips = ActiveShips;
    S.BlockadedPortSettlementIDs = BlockadedPorts.Array(); // MODIFIED
    S.NextShipID = NextShipID;
    return S;
}

void UNavalSystem::RestoreNavalFromSave(const FNavalSaveState& S)
{
    ActiveShips = S.ActiveShips;
    BlockadedPorts.Empty();

    for (int32 Port : S.BlockadedPortSettlementIDs)
    {
        BlockadedPorts.Add(Port);
    }

    NextShipID = S.NextShipID;

    UE_LOG(LogTemp, Log,
        TEXT("[Naval] Restored %d ships, %d blockaded ports."),
        ActiveShips.Num(),
        BlockadedPorts.Num());
}