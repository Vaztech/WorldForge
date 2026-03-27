#include "CultureDriftSystem.h"
#include "Culture/CultureDriftContext.h"
#include "Culture/Passes/CultureDNACapPass.h"
#include "Culture/Passes/CultureDriftLoggingPass.h"
#include "Culture/Passes/CultureEventDriftPass.h"
#include "EventBus.h"
#include "WorldMapSubsystem.h"
#include "Engine/World.h"

void UCultureDriftSystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogTemp, Log, TEXT("[CultureDriftSystem] Initialized."));
}

void UCultureDriftSystem::Deinitialize()
{
    Super::Deinitialize();
}

void UCultureDriftSystem::OnSimStep(int32 StepNumber, int32 SettlementID,
    TArray<FAgentData>& Agents, UWorld* World)
{
    (void)SettlementID;
    (void)Agents;

    if (!World) return;
    if (StepNumber % LivingWorldTickInterval != 0) return;

    UWorldMapSubsystem* WMS = World->GetSubsystem<UWorldMapSubsystem>();
    UEventBus* Bus = World->GetSubsystem<UEventBus>();
    if (!WMS || !Bus) return;

    const TArray<FSimulationEvent>& RecentEvents = Bus->GetRecentEvents();

    FCultureDriftContext Context;
    Context.World = World;
    Context.WorldMapSubsystem = WMS;
    Context.EventBus = Bus;
    Context.RecentEvents = &RecentEvents;
    Context.StepNumber = StepNumber;

    TArray<FCivilizationData>& Civs = Context.WorldMapSubsystem->GetMutableCivilizations();
    for (FCivilizationData& Civ : Civs)
    {
        FCivilizationData Before = Civ;
        FCultureEventDriftPass::Run(Context, Civ);
        FCultureDNACapPass::Run(Context, Civ);
        FCultureDriftLoggingPass::Run(Context, Before, Civ);
    }
}