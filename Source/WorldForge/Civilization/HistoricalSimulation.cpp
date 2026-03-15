#include "HistoricalSimulation.h"
#include "../Core/WorldForgeRNG.h"

void UHistoricalSimulation::RunSimulation(
    FHistoricalSimConfig Config, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& Currencies,
    TArray<FHistoryEventData>& OutHistory, UWorldForgeRNG* RNG)
{
    if (!RNG || Civs.Num() == 0) return;

    OutHistory.Reset();
    AllEvents.Reset();
    NextEventID = 0;

    const int32 Y = Config.TotalYears;
    SimulateEpoch_Emergence(0, Y / 4, Civs, Tiles, Currencies, OutHistory, RNG);
    SimulateEpoch_Expansion(Y / 4, Y / 2, Civs, Tiles, Currencies, OutHistory, RNG);
    SimulateEpoch_Conflict(Y / 2, 3 * Y / 4, Civs, Tiles, Currencies, OutHistory, RNG);
    SimulateEpoch_Stabilization(3 * Y / 4, Y, Civs, Tiles, Currencies, OutHistory, RNG);

    UE_LOG(LogTemp, Log, TEXT("[HistSim] Complete -- %d events"), OutHistory.Num());
}

void UHistoricalSimulation::SimulateEpoch_Emergence(
    int32 YS, int32 YE, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    const int32 N = FMath::Max(2, Civs.Num());
    for (int32 i = 0; i < N; i++)
    {
        const int32 Year = YS + RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, FMath::Max(0, YE - YS - 1));
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
        if (R < 0.50f)      GenerateTradeEvent(Year, Civs, C, H, RNG);
        else if (R < 0.75f) GenerateWarEvent(Year, Civs, C, H, RNG);
        else                GenerateDisasterEvent(Year, Civs, Tiles, H, RNG);
    }
}

void UHistoricalSimulation::SimulateEpoch_Expansion(
    int32 YS, int32 YE, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    const int32 N = FMath::Max(3, Civs.Num() * 2);
    for (int32 i = 0; i < N; i++)
    {
        const int32 Year = YS + RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, FMath::Max(0, YE - YS - 1));
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
        if (R < 0.40f)      GenerateTradeEvent(Year, Civs, C, H, RNG);
        else if (R < 0.70f) GenerateWarEvent(Year, Civs, C, H, RNG);
        else                GenerateDisasterEvent(Year, Civs, Tiles, H, RNG);
    }
}

void UHistoricalSimulation::SimulateEpoch_Conflict(
    int32 YS, int32 YE, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    const int32 N = FMath::Max(4, Civs.Num() * 2);
    for (int32 i = 0; i < N; i++)
    {
        const int32 Year = YS + RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, FMath::Max(0, YE - YS - 1));
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
        if (R < 0.55f)      GenerateWarEvent(Year, Civs, C, H, RNG);
        else if (R < 0.75f) GenerateDisasterEvent(Year, Civs, Tiles, H, RNG);
        else if (R < 0.90f) GenerateCollapseEvent(Year, Civs, C, H, RNG);
        else                GenerateTradeEvent(Year, Civs, C, H, RNG);
    }
}

void UHistoricalSimulation::SimulateEpoch_Stabilization(
    int32 YS, int32 YE, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles, TArray<FCurrencyDefinition>& C,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    const int32 N = FMath::Max(3, Civs.Num());
    for (int32 i = 0; i < N; i++)
    {
        const int32 Year = YS + RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, FMath::Max(0, YE - YS - 1));
        const float R = RNG->NextFloat(UWorldForgeRNG::BRANCH_PREHISTORY);
        if (R < 0.60f)      GenerateTradeEvent(Year, Civs, C, H, RNG);
        else if (R < 0.80f) GenerateWarEvent(Year, Civs, C, H, RNG);
        else                GenerateDisasterEvent(Year, Civs, Tiles, H, RNG);
    }
}

void UHistoricalSimulation::GenerateWarEvent(
    int32 Year, TArray<FCivilizationData>& Civs,
    TArray<FCurrencyDefinition>& Currencies,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    if (Civs.Num() < 2) return;

    const int32 AI = RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Civs.Num() - 1);
    int32 DI = RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Civs.Num() - 1);
    if (DI == AI) DI = (AI + 1) % Civs.Num();

    const bool bAttWins = Civs[AI].CulturalDNA.Militarism >= Civs[DI].CulturalDNA.Militarism;
    const int32 LoserID = bAttWins ? Civs[DI].CivID : Civs[AI].CivID;

    for (FCurrencyDefinition& Cur : Currencies)
    {
        if (Cur.IssuingCivID == LoserID)
        {
            Cur.CurrentAnchorValue *= 0.85f;
            Cur.TrustState.MilitaryStability *= 0.8f;
        }
    }

    RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Violence"))),
        { Civs[AI].CivID, Civs[DI].CivID },
        bAttWins ? TEXT("Attacker victory") : TEXT("Defender repelled"),
        TEXT("Border dispute"),
        -0.1f,
        H);
}

void UHistoricalSimulation::GenerateTradeEvent(
    int32 Year, TArray<FCivilizationData>& Civs,
    TArray<FCurrencyDefinition>& Currencies,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    if (Civs.Num() < 2) return;

    const int32 A = RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Civs.Num() - 1);
    int32 B = RNG->NextInt(UWorldForgeRNG::BRANCH_PREHISTORY, 0, Civs.Num() - 1);
    if (B == A) B = (A + 1) % Civs.Num();

    for (FCurrencyDefinition& Cur : Currencies)
    {
        if (Cur.IssuingCivID == Civs[A].CivID || Cur.IssuingCivID == Civs[B].CivID)
        {
            Cur.CurrentAnchorValue *= 1.02f;
            Cur.TrustState.TradeReputation = FMath::Min(1.0f, Cur.TrustState.TradeReputation + 0.02f);
        }
    }

    RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Expansion"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Economic"))),
        { Civs[A].CivID, Civs[B].CivID },
        TEXT("Trade agreement"),
        TEXT("Mutual interest"),
        0.05f,
        H);
}

void UHistoricalSimulation::GenerateDisasterEvent(
    int32 Year, TArray<FCivilizationData>& Civs,
    TArray<FWorldTileData>& Tiles,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    if (Civs.Num() == 0) return;

    const int32 VI = RNG->NextInt(UWorldForgeRNG::BRANCH_DISASTERS, 0, Civs.Num() - 1);
    if (Civs[VI].ControlledTileIDs.Num() > 0)
    {
        const int32 TI = RNG->NextInt(UWorldForgeRNG::BRANCH_DISASTERS, 0, Civs[VI].ControlledTileIDs.Num() - 1);
        const int32 TID = Civs[VI].ControlledTileIDs[TI];
        if (Tiles.IsValidIndex(TID))
        {
            Tiles[TID].Corruption += 0.1f;
            Tiles[TID].FearPressure += 0.1f;
        }
    }

    RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Violence"))),
        { Civs[VI].CivID },
        TEXT("Disaster struck"),
        TEXT("Natural catastrophe"),
        -0.08f,
        H);
}

void UHistoricalSimulation::GenerateCollapseEvent(
    int32 Year, TArray<FCivilizationData>& Civs,
    TArray<FCurrencyDefinition>& Currencies,
    TArray<FHistoryEventData>& H, UWorldForgeRNG* RNG)
{
    if (Civs.Num() < 2) return;

    int32 WI = INDEX_NONE;
    for (int32 i = 0; i < Civs.Num(); i++)
    {
        if (Civs[i].bCollapsed)
        {
            continue;
        }

        if (WI == INDEX_NONE || Civs[i].TechLevel < Civs[WI].TechLevel)
        {
            WI = i;
        }
    }

    if (WI == INDEX_NONE)
    {
        return;
    }

    Civs[WI].bCollapsed = true;

    for (FCurrencyDefinition& Cur : Currencies)
    {
        if (Cur.IssuingCivID == Civs[WI].CivID)
        {
            Cur.bCollapsed = true;
            Cur.CurrentAnchorValue = 0.01f;
            Cur.TrustState.MilitaryStability = 0.0f;
            Cur.TrustState.PoliticalCohesion = 0.0f;
        }
    }

    RecordEvent(
        Year,
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Epoch.Conflict"))),
        FGameplayTag::RequestGameplayTag(FName(TEXT("WorldForge.Event.Category.Economic"))),
        { Civs[WI].CivID },
        TEXT("Civilization collapsed"),
        TEXT("Military defeat and economic strain"),
        -0.5f,
        H);
}

void UHistoricalSimulation::RecordEvent(
    int32 Year, FGameplayTag EpochTag, FGameplayTag EventType,
    const TArray<int32>& PIDs, const FString& Result,
    const FString& Cause, float EconImpact,
    TArray<FHistoryEventData>& OutHistory)
{
    FHistoryEventData E;
    E.EventID = NextEventID++;
    E.Year = Year;
    E.EpochTag = EpochTag;
    E.EventType = EventType;
    E.ParticipantIDs = PIDs;
    E.Result = Result;
    E.Cause = Cause;
    E.EconomicImpact = EconImpact;

    OutHistory.Add(E);
    AllEvents.Add(E);
}