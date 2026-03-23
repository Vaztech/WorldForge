#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "WorldForgeDataTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════════

// Fog of war knowledge levels per world tile.
UENUM(BlueprintType)
enum class EKnowledgeLevel : uint8
{
    Unexplored    UMETA(DisplayName = "Unexplored"),
    Outdated      UMETA(DisplayName = "Outdated"),
    Rumored       UMETA(DisplayName = "Rumored"),
    Surveyed      UMETA(DisplayName = "Surveyed"),
    Live          UMETA(DisplayName = "Live")
};

// Colony world size options used during world generation.
UENUM(BlueprintType)
enum class EWorldSize : uint8
{
    Small   UMETA(DisplayName = "Small (128x128)"),
    Medium  UMETA(DisplayName = "Medium (256x256)"),
    Large   UMETA(DisplayName = "Large (512x512)"),
    OMG     UMETA(DisplayName = "OMG (1024x1024)")
};

// Pre-history epoch identifiers used by the historical simulation.
UENUM(BlueprintType)
enum class EPreHistoryEpoch : uint8
{
    Emergence      UMETA(DisplayName = "Emergence 0-500yr"),
    Expansion      UMETA(DisplayName = "Expansion 500-1000yr"),
    Conflict       UMETA(DisplayName = "Conflict 1000-1500yr"),
    Stabilization  UMETA(DisplayName = "Stabilization 1500-2000yr")
};

// Colonist simulation state machine — all 14 states required by Phase 2.
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

// Reservation types used by the reservation system.
UENUM(BlueprintType)
enum class EReservationType : uint8
{
    Item,
    Job,
    Building,
    Bed,
    InteractionSlot
};

// Price-fix policy types used by economy/world systems.
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

// Settlement market tier / sophistication level.
UENUM(BlueprintType)
enum class EMarketTier : uint8
{
    None,
    BasicStall,
    TradeHall,
    GrandExchange,
    BlackMarket
};

// Phase 3A: trust component used for queued trust deltas.
UENUM(BlueprintType)
enum class ETrustComponent : uint8
{
    MilitaryStability,
    BackingStrength,
    TradeReputation,
    CosmicFavor,
    PoliticalCohesion,
    GovernmentCredibility
};

// Job priority ordering used by the job queue manager.
// Higher numeric value = higher priority.
UENUM(BlueprintType)
enum class EJobPriority : uint8
{
    Emergency = 4 UMETA(DisplayName = "Emergency"),
    High = 3 UMETA(DisplayName = "High"),
    Normal = 2 UMETA(DisplayName = "Normal"),
    Low = 1 UMETA(DisplayName = "Low"),
    Idle = 0 UMETA(DisplayName = "Idle")
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

    // Phase rule: WorldForge uses smooth SDF terrain only.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bSmoothSDF = true;

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

    // World/cosmic influence fields used by higher-level simulation layers.
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

// Minimal compile-safe trade flow state needed by FEconomySaveState.
// If this struct already exists elsewhere in your project, remove this copy
// and include the canonical header instead.
USTRUCT(BlueprintType)
struct FTradeFlowState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 RouteID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OriginID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 DestID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float OfficialCapacity = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float UnofficialCapacity = 0.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SafetyFactor = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float TaxFactor = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DiplomaticMod = 1.0f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> OfficialFlows;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> BlackMarketFlows;
};

USTRUCT(BlueprintType)
struct FMarketPriceArray
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FMarketPrice> Items;
};

USTRUCT(BlueprintType)
struct FFloatHistoryArray
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<float> Values;
};

USTRUCT(BlueprintType)
struct FSettlementPriceHistory
{
    GENERATED_BODY()

    // ItemTag -> last N prices
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, FFloatHistoryArray> ItemHistory;
};

USTRUCT(BlueprintType)
struct FCurrencyTrustDelta
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrencyTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) ETrustComponent Component = ETrustComponent::MilitaryStability;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Delta = 0.0f;
};

USTRUCT(BlueprintType)
struct FEconomySaveState
{
    GENERATED_BODY()

    // SettlementID -> price array
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FMarketPriceArray> SettlementPrices;

    // SettlementID -> (ItemTag -> history array)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FSettlementPriceHistory> PriceHistory;

    // Live currency trust states (keyed by CurrencyTag)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, FCurrencyTrustState> CurrencyTrust;

    // Currency definitions (keyed by CurrencyTag)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, FCurrencyDefinition> AllCurrencies;

    // Government profiles (keyed by CivID)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FGovernmentEconomicProfile> CivGovernments;

    // Trade routes (keyed by RouteID)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FTradeFlowState> TradeRoutes;

    // Pending trust deltas that were queued but not yet applied
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FCurrencyTrustDelta> PendingTrustDeltas;
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

    // Phase 3A: player currency stub until diplomacy/economy expands.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag PlayerCurrencyTag;

    // -1 = player-controlled settlement; AI settlements use owning civ id.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OwnerCivID = -1;

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

/** 2D navigation cell. Phase 2 uses a single elevation layer only. */
USTRUCT(BlueprintType)
struct FNavCell
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FIntPoint GridPos = FIntPoint::ZeroValue;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bWalkable = true;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float PathCost = 1.0f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bDangerZone = false;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 OccupantID = -1;
};

// Active construction work tracked separately from jobs.
// ConstructionProcessor mutates these while the job layer handles assignment/movement.
USTRUCT(BlueprintType)
struct FActiveBuildTask
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TaskID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag BuildingTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FIntPoint OriginGridCell = FIntPoint::ZeroValue;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TotalBlocks = 0;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 PlacedBlocks = 0;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 AssignedAgentID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bComplete = false;
};

// Job queue entry posted by systems and consumed by UJobProcessor.
// This is the assign -> move -> arrive part of the loop.
USTRUCT(BlueprintType)
struct FJobRequest
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 JobID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag JobTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag RequiredSkill;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag TargetItemTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag TargetBuildingTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FIntPoint TargetGridPos = FIntPoint::ZeroValue;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TargetID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) EJobPriority Priority = EJobPriority::Normal;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 AssignedAgentID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bAssigned = false;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 PostedOnStep = 0;
};

// Active work task executed after arrival.
// ProductionProcessor owns progression/completion for these tasks.
USTRUCT(BlueprintType)
struct FActiveProductionTask
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TaskID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag JobTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag BuildingTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 AssignedAgentID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float Progress = 0.f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float RequiredProgress = 10.f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TMap<FGameplayTag, int32> InputsConsumed;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TMap<FGameplayTag, int32> OutputItems;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bInputsReserved = false;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bComplete = false;
};

// Runtime colonist/agent state used by processors every fixed simulation step.
USTRUCT(BlueprintType)
struct FAgentData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 AgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EAgentState State = EAgentState::Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag RaceTag;

    // Core need values.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float FoodNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SleepNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SocialNeed = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SafetyNeed = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float PurposeNeed = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MoodScore = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FString> RecentThoughts;

    // Skill tags drive job weighting and production speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> Skills;

    // Ethics values affect compliance/refusal systems.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsPacifism = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsCompassion = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsHonour = 50.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Health = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInjured = false;

    // Reservation link used by the reservation system.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ActiveReservationID = -1;

    // Current job tag assigned by JobProcessor.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrentJobTag;

    // Current building context for production/build jobs.
    // Phase 2 patch: required so processors know which building the current job belongs to.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrentJobBuildingTag;

    // Deterministic grid-based movement state.
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

// ADDED
// WorldForgeDataTypes.h -- Phase 3B additions
// Append to bottom of file. Do NOT duplicate Phase 3A structs already present.

// ADDED
// ── Cultural DNA axis indices ─────────────────────────────────────────────
enum ECulturalAxis : int32
{
    CA_Hierarchy = 0,
    CA_OutgroupHostility = 1,
    CA_LaborEthics = 2,
    CA_Militarism = 3,
    CA_MagicReverence = 4,
    CA_Individualism = 5,
    CA_TheologicalRigid = 6,
    CA_EconomicOpenness = 7,
    CA_ExpansionDrive = 8,
    CA_CosmicDevotion = 9,
    CA_MAX = 10
};

// ADDED
// ── Phase 3B: Civilization resilience ────────────────────────────────────
UENUM()
enum class ECivilizationCollapseType : uint8
{
    None,
    Fragmentation,
    Diaspora,
    RefugeState,
    SplinterKingdom,
    UndergroundRetreat,
    ReligiousContinuation,
    EconomicCollapse
};

// ADDED
USTRUCT()
struct FResilienceRecord
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 CivID = -1;
    UPROPERTY(SaveGame) int32 ConsecutiveLowHealthTicks = 0;
    UPROPERTY(SaveGame) bool bHasCollapsed = false;
    UPROPERTY(SaveGame) ECivilizationCollapseType CollapseType = ECivilizationCollapseType::None;
    UPROPERTY(SaveGame) int32 CollapseStepNumber = -1;
};

// ADDED
USTRUCT()
struct FResilienceSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FResilienceRecord> Records;
};

// ADDED
// ── Phase 3B: Overworld ───────────────────────────────────────────────────
USTRUCT()
struct FCargoItem
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FGameplayTag ItemTag;
    UPROPERTY(SaveGame) int32 Quantity = 0;
    UPROPERTY(SaveGame) float PricePerUnit = 1.0f;
};

// ADDED
USTRUCT()
struct FCaravanData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 CaravanID = -1;
    UPROPERTY(SaveGame) int32 OriginSettlementID = -1;
    UPROPERTY(SaveGame) int32 DestSettlementID = -1;
    UPROPERTY(SaveGame) int32 StepsUntilArrival = 0;
    UPROPERTY(SaveGame) TArray<FCargoItem> Cargo;
    UPROPERTY(SaveGame) float PaymentAmount = 0.0f;
    UPROPERTY(SaveGame) FGameplayTag PaymentCurrencyTag;
    UPROPERTY(SaveGame) bool bOutbound = true;
};

// ADDED
UENUM()
enum class EOutpostType : uint8
{
    Resource,
    MilitaryFort,
    TradePost,
    NavalPort,
    SkyAnchor,
    MarketOutpost,
    DiplomaticMission,
    ShrineOutpost,
    ForwardBase
};

// ADDED
USTRUCT()
struct FOutpostData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 OutpostID = -1;
    UPROPERTY(SaveGame) EOutpostType OutpostType = EOutpostType::Resource;
    UPROPERTY(SaveGame) int32 OwnerSettlementID = -1;
    UPROPERTY(SaveGame) FIntPoint WorldGridCell;
    UPROPERTY(SaveGame) int32 StaffCount = 0;
    UPROPERTY(SaveGame) int32 StepsWithoutSupply = 0;
    UPROPERTY(SaveGame) bool bActive = true;
    UPROPERTY(SaveGame) float FlowRateMod = 1.0f;
};

// ADDED
USTRUCT()
struct FOverworldSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FCaravanData> ActiveCaravans;
    UPROPERTY(SaveGame) TArray<FOutpostData> ActiveOutposts;
    UPROPERTY(SaveGame) int32 NextCaravanID = 0;
    UPROPERTY(SaveGame) int32 NextOutpostID = 0;
};

// ADDED
// ── Phase 3B: Diplomacy ───────────────────────────────────────────────────
UENUM()
enum class EDiplomaticActionType : uint8
{
    DeclareWar,
    ProposePeace,
    TradeAgreement,
    CurrencyPegging,
    EconomicAid,
    CurrencyWar,
    TradeEmbargo,
    SpyMission,
    CulturalExchange,
    MilitaryAlliance,
    Vassalize,
    OpenBorders,
    RequestTribute,
    HonorDebt,
    MarriageAlliance,
    JointExpedition
};

// ADDED
USTRUCT()
struct FDiplomaticTreaty
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 CivA = -1;
    UPROPERTY(SaveGame) int32 CivB = -1;
    UPROPERTY(SaveGame) EDiplomaticActionType TreatyType = EDiplomaticActionType::TradeAgreement;
    UPROPERTY(SaveGame) int32 ExpiryStep = -1;
    UPROPERTY(SaveGame) float FlowRateMod = 1.0f;
    UPROPERTY(SaveGame) bool bActive = true;
};

// ADDED
USTRUCT()
struct FDiplomacySaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TMap<int64, float> RelationshipScores;
    UPROPERTY(SaveGame) TArray<FDiplomaticTreaty> ActiveTreaties;
    UPROPERTY(SaveGame) TArray<int32> EmbargoedCivIDs;
    UPROPERTY(SaveGame) TMap<int64, FGameplayTag> PeggedCurrencyPairs;
};

// ADDED
// ── Phase 3B: Naval ───────────────────────────────────────────────────────
UENUM()
enum class EShipType : uint8
{
    Raft,
    RiverBarge,
    ScoutSloop
};

// ADDED
USTRUCT()
struct FShipData
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 ShipID = -1;
    UPROPERTY(SaveGame) EShipType ShipType = EShipType::Raft;
    UPROPERTY(SaveGame) int32 OwnerSettlementID = -1;
    UPROPERTY(SaveGame) float BaseSpeed = 1.0f;
    UPROPERTY(SaveGame) int32 CargoCap = 50;
    UPROPERTY(SaveGame) int32 OriginSettlementID = -1;
    UPROPERTY(SaveGame) int32 DestSettlementID = -1;
    UPROPERTY(SaveGame) int32 StepsUntilArrival = 0;
    UPROPERTY(SaveGame) TArray<FCargoItem> Cargo;
    UPROPERTY(SaveGame) bool bInTransit = false;
};

// ADDED
USTRUCT()
struct FNavalSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FShipData> ActiveShips;
    UPROPERTY(SaveGame) TArray<int32> BlockadedPortSettlementIDs;
    UPROPERTY(SaveGame) int32 NextShipID = 0;
};

// ADDED
// ── Phase 3B: Cosmic ──────────────────────────────────────────────────────
UENUM()
enum class ECosmicEntityStatus : uint8
{
    Dormant,
    Emerging,
    Active,
    Fading,
    Gone
};

// ADDED
USTRUCT()
struct FCosmicEntityState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 EntityID = -1;
    UPROPERTY(SaveGame) FGameplayTag EntityTag;
    UPROPERTY(SaveGame) ECosmicEntityStatus Status = ECosmicEntityStatus::Dormant;
    UPROPERTY(SaveGame) float TotalPower = 0.0f;

    UPROPERTY(SaveGame) float FearPower = 0.0f;
    UPROPERTY(SaveGame) float AffinityPower = 0.0f;
    UPROPERTY(SaveGame) float CatastrophePower = 0.0f;
    UPROPERTY(SaveGame) float AnchorPower = 0.0f;
    UPROPERTY(SaveGame) float BeliefPower = 0.0f;
    UPROPERTY(SaveGame) float IrritationPower = 0.0f;

    UPROPERTY(SaveGame) float FearDecayRate = 0.001f;
    UPROPERTY(SaveGame) float AffinityDecayRate = 0.001f;
    UPROPERTY(SaveGame) float CatastropheDecayRate = 0.002f;
    UPROPERTY(SaveGame) float AnchorDecayRate = 0.0005f;
    UPROPERTY(SaveGame) float BeliefDecayRate = 0.001f;
    UPROPERTY(SaveGame) float IrritationDecayRate = 0.003f;

    UPROPERTY(SaveGame) float EmergenceThreshold = 10.0f;
    UPROPERTY(SaveGame) float FadeThreshold = 1.0f;
    UPROPERTY(SaveGame) TArray<FGameplayTag> DomainTags;
    UPROPERTY(SaveGame) int32 FlowBoostStepsRemaining = 0;
    UPROPERTY(SaveGame) int32 FlowDrainStepsRemaining = 0;
    UPROPERTY(SaveGame) float FlowBoostMultiplier = 1.0f;
};

// ADDED
USTRUCT()
struct FAnchorRecord
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) int32 AnchorID = -1;
    UPROPERTY(SaveGame) int32 SettlementID = -1;
    UPROPERTY(SaveGame) FIntPoint GridPosition;
    UPROPERTY(SaveGame) FGameplayTag BoundEntityTag;
    UPROPERTY(SaveGame) float PowerPerSimDay = 0.1f;
};

// ADDED
USTRUCT()
struct FNarrativePressureState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) float War = 0.0f;
    UPROPERTY(SaveGame) float Exploration = 0.0f;
    UPROPERTY(SaveGame) float Mysticism = 0.0f;
    UPROPERTY(SaveGame) float Fear = 0.0f;
    UPROPERTY(SaveGame) float Commerce = 0.0f;
    UPROPERTY(SaveGame) float Art = 0.0f;
    UPROPERTY(SaveGame) float CosmicUnstable = 0.0f;

    static constexpr float Threshold = 100.0f;
};

// ADDED
USTRUCT()
struct FCosmicSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FCosmicEntityState> EntityStates;
    UPROPERTY(SaveGame) TArray<FAnchorRecord> AnchorRecords;
    UPROPERTY(SaveGame) FNarrativePressureState NarrativePressure;
    UPROPERTY(SaveGame) int32 NextEntityID = 0;
    UPROPERTY(SaveGame) int32 NextAnchorID = 0;
};