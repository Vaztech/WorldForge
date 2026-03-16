#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════════

// Fog of war knowledge levels per world tile
UENUM(BlueprintType)
enum class EKnowledgeLevel : uint8
{
    Unexplored    UMETA(DisplayName = "Unexplored"),
    Outdated      UMETA(DisplayName = "Outdated"),
    Rumored       UMETA(DisplayName = "Rumored"),
    Surveyed      UMETA(DisplayName = "Surveyed"),
    Live          UMETA(DisplayName = "Live")
};

// Colony world size options
UENUM(BlueprintType)
enum class EWorldSize : uint8
{
    Small   UMETA(DisplayName = "Small (128x128)"),
    Medium  UMETA(DisplayName = "Medium (256x256)"),
    Large   UMETA(DisplayName = "Large (512x512)"),
    OMG     UMETA(DisplayName = "OMG (1024x1024)")
};

// Pre-history epoch identifiers
UENUM(BlueprintType)
enum class EPreHistoryEpoch : uint8
{
    Emergence      UMETA(DisplayName = "Emergence 0-500yr"),
    Expansion      UMETA(DisplayName = "Expansion 500-1000yr"),
    Conflict       UMETA(DisplayName = "Conflict 1000-1500yr"),
    Stabilization  UMETA(DisplayName = "Stabilization 1500-2000yr")
};

// Colonist simulation state machine — all 14 states
UENUM(BlueprintType)
enum class EAgentState : uint8
{
    Idle,
    SearchingForJob,
    ReservingTarget,
    MovingToTarget,
    Working,
    Carrying,
    Eating,
    Sleeping,
    Socializing,
    Fleeing,
    Fighting,
    Recovering,
    Incapacitated,
    Dead
};

// Reservation types — all 5 from Architecture v3.1
UENUM(BlueprintType)
enum class EReservationType : uint8
{
    Item,
    Job,
    Building,
    Bed,
    InteractionSlot
};

// Price fix policy types — all 8
UENUM(BlueprintType)
enum class EPriceFixType : uint8
{
    None,
    Ceiling,
    Floor,
    Ration,
    StandardBoard,
    GuildMonopoly,
    DivineMandated,
    Requisition
};

// Market building tier
UENUM(BlueprintType)
enum class EMarketTier : uint8
{
    None,
    BasicStall,
    TradeHall,
    GrandExchange,
    BlackMarket
};

// ═══════════════════════════════════════════════════════════════════
// FOG OF WAR
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FWorldKnowledgeState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TileID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EKnowledgeLevel KnowledgeLevel = EKnowledgeLevel::Unexplored;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 LastSurveyYear = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DataAccuracy = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString RumorSource;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> KnownCurrencies;
};

// ═══════════════════════════════════════════════════════════════════
// VOXEL
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FVoxelBiomeProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float NoiseFrequency = 0.02f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float NoiseAmplitude = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OctaveCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bEnableCaves = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveThreshold = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bSmoothSDF = true;  // ALWAYS true
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> SurfaceMaterials;
};

// ═══════════════════════════════════════════════════════════════════
// WORLD MAP
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FWorldTileData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TileID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag BiomeType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Elevation = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float WaterDepth = 0.f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float Temperature = 0.5f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float Rainfall = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> ResourceTags;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ControllingCivID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FWorldKnowledgeState KnowledgeState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FVoxelBiomeProfile VoxelProfile;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Sacredness = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Corruption = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float FearPressure = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DreamInstability = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CommercePressure = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float VoidProximity = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> HistoryEventIDs;
};

// ═══════════════════════════════════════════════════════════════════
// CIVILIZATION AND CULTURE
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FCultureData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 CivID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float HierarchyTolerance = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float OutgroupHostility = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float LaborEthics = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Militarism = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MagicReverence = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Individualism = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TheologicalRigidity = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EconomicOpenness = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float ExpansionDrive = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CosmicDevotion = 50.f;
};

USTRUCT(BlueprintType)
struct FHistoryEventData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 EventID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 Year = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag EpochTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> ParticipantIDs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag EventType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString Result;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString Cause;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> NarrativeTags;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 WorldTileID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EconomicImpact = 0.f;
};

USTRUCT(BlueprintType)
struct FCivilizationData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 CivID = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag RaceType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag GovernmentType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrencyTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FCultureData CulturalDNA;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<int32, float> RelationshipMap;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TechLevel = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bCollapsed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> ControlledTileIDs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> HistoryEventIDs;
};

// ═══════════════════════════════════════════════════════════════════
// ECONOMY
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FCurrencyTrustState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MilitaryStability = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BackingStrength = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TradeReputation = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CosmicFavor = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float PoliticalCohesion = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float GovernmentCredibility = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TrustVelocity = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInFreefall = false;
};

USTRUCT(BlueprintType)
struct FCurrencyDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrencyTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FText DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FText PhysicalForm;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 IssuingCivID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag GovernmentType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BaseAnchorValue = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CurrentAnchorValue = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float InflationRate = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag BackingType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> AcceptingCivIDs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bIsReserveCurrency = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInCrisis = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bCollapsed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FCurrencyTrustState TrustState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FHistoryEventData> EconomicHistory;
};

USTRUCT(BlueprintType)
struct FPriceFixPolicy
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag ItemTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 CivID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EPriceFixType FixType = EPriceFixType::None;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float FixedPrice = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EnforcementStrength = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bGeneratesBlackMarket = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BlackMarketGap = 0.f;
};

USTRUCT(BlueprintType)
struct FGovernmentEconomicProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag GovernmentType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BaseTaxRate = 0.15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TariffRate = 0.10f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EnforcementBudget = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FPriceFixPolicy> PriceFixPolicies;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bAllowPrivateTrade = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bAllowForeignMerchants = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bStateMonopolyOnTrade = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float InnovationRate = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float InequalityGrowthRate = 0.f;
};

USTRUCT(BlueprintType)
struct FMarketPrice
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag ItemTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BaseAnchorValue = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EquilibriumPrice = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float OfficialPrice = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BlackMarketPrice = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SupplyPressure = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DemandPressure = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float PriceElasticity = 0.7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bIsVeblenGood = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bPriceFixed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EPriceFixType ActiveFixType = EPriceFixType::None;
};

USTRUCT(BlueprintType)
struct FSettlementMarket
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag PrimaryCurrency;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> AcceptedCurrencies;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, FMarketPrice> Prices;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> CurrencyReserves;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EMarketTier MarketTier = EMarketTier::None;
};

// ═══════════════════════════════════════════════════════════════════
// COLONY SIMULATION
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FSettlementState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 WorldTileID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 Population = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MoraleSummary = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float ThreatLevel = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, int32> Stockpiles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> CompletedBuildings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag ReligionState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTagContainer ActivePolicies;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bActorsLoaded = false;
};

USTRUCT(BlueprintType)
struct FReservationRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ReservationID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OwnerAgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TargetID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag TargetTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EReservationType Type = EReservationType::Item;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 CreatedOnStep = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TimeoutSteps = 100;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
};

/** 2D navigation cell. Phase 2: single elevation layer only. */
USTRUCT(BlueprintType)
struct FNavCell
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FIntPoint GridPos;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bWalkable = true;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float PathCost = 1.0f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bDangerZone = false;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 OccupantID = -1;
};

USTRUCT(BlueprintType)
struct FAgentData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 AgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EAgentState State = EAgentState::Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag RaceTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float FoodNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SleepNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SocialNeed = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SafetyNeed = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float PurposeNeed = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MoodScore = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FString> RecentThoughts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> Skills;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsPacifism = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsCompassion = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsHonour = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Health = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInjured = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ActiveReservationID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrentJobTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FIntPoint CurrentGridPos = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FIntPoint CurrentNavTarget = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FName, float> EntityFavor;
};

// ═══════════════════════════════════════════════════════════════════
// SIMULATION EVENTS
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FSimulationEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag EventType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StepNumber = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag Payload;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FloatPayload = 0.f;
};

// ═══════════════════════════════════════════════════════════════════
// WORLD GENERATION CONFIG
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FWorldGenConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EWorldSize WorldSize = EWorldSize::Medium;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 PreHistoryYears = 200;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float WarFrequency = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DisasterFrequency = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MagicPrevalence = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CosmicDensity = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EconomicComplexity = 0.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 StartingCivCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int64 MasterSeed = 12345;
};