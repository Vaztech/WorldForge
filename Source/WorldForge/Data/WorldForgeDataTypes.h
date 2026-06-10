#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataTable.h"
#include "UObject/SoftObjectPtr.h"
#include "Civilization/WorldHistory/WorldHistoryTypes.h"
#include "Modifiers/WorldForgeEquipmentTypes.h"
#include "WorldForgeDataTypes.generated.h"

// ═══════════════════════════════════════════════════════════════════
// ENUMS
// ═══════════════════════════════════════════════════════════════════

UENUM(BlueprintType)
enum class EKnowledgeLevel : uint8
{
    Unexplored    UMETA(DisplayName = "Unexplored"),
    Outdated      UMETA(DisplayName = "Outdated"),
    Rumored       UMETA(DisplayName = "Rumored"),
    Surveyed      UMETA(DisplayName = "Surveyed"),
    Live          UMETA(DisplayName = "Live")
};

UENUM(BlueprintType)
enum class EWorldSize : uint8
{
    Small   UMETA(DisplayName = "Small (128x128)"),
    Medium  UMETA(DisplayName = "Medium (256x256)"),
    Large   UMETA(DisplayName = "Large (512x512)"),
    OMG     UMETA(DisplayName = "OMG (1024x1024)")
};

UENUM(BlueprintType)
enum class EPreHistoryEpoch : uint8
{
    Emergence      UMETA(DisplayName = "Emergence 0-500yr"),
    Expansion      UMETA(DisplayName = "Expansion 500-1000yr"),
    Conflict       UMETA(DisplayName = "Conflict 1000-1500yr"),
    Stabilization  UMETA(DisplayName = "Stabilization 1500-2000yr")
};

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

UENUM(BlueprintType)
enum class EReservationType : uint8
{
    Item,
    Job,
    Building,
    Bed,
    InteractionSlot
};

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

UENUM(BlueprintType)
enum class EMarketTier : uint8
{
    None,
    BasicStall,
    TradeHall,
    GrandExchange,
    BlackMarket
};

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

UENUM(BlueprintType)
enum class EJobPriority : uint8
{
    Emergency = 4 UMETA(DisplayName = "Emergency"),
    High = 3 UMETA(DisplayName = "High"),
    Normal = 2 UMETA(DisplayName = "Normal"),
    Low = 1 UMETA(DisplayName = "Low"),
    Idle = 0 UMETA(DisplayName = "Idle")
};

UENUM(BlueprintType)
enum class EBuildingPurpose : uint8
{
    Housing        UMETA(DisplayName = "Housing"),
    Storage        UMETA(DisplayName = "Storage"),
    Production     UMETA(DisplayName = "Production"),
    Defense        UMETA(DisplayName = "Defense"),
    Infrastructure UMETA(DisplayName = "Infrastructure"),
    Spiritual      UMETA(DisplayName = "Spiritual"),
    Economic       UMETA(DisplayName = "Economic"),
    Decorative     UMETA(DisplayName = "Decorative")
};

UENUM(BlueprintType)
enum class ETerrainMode : uint8
{
    Smooth         UMETA(DisplayName = "Smooth"),
    Blocky         UMETA(DisplayName = "Blocky"),
    CubePrototype  UMETA(DisplayName = "Cube Prototype (Disabled)")
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
// RESOURCE DISCOVERY (v6.0)
// ═══════════════════════════════════════════════════════════════════

/**
 * Categories of mineable resources.  Each category gets its own
 * 3D noise field in the terrain generator so veins cluster
 * independently.  Expand by adding entries before COUNT and adding
 * a matching noise field in WorldForgeTerrainGenerator.
 */
UENUM(BlueprintType)
enum class EResourceCategory : uint8
{
    Metal    UMETA(DisplayName = "Metal Ores"),
    Crystal  UMETA(DisplayName = "Crystals & Gems"),
    Fossil   UMETA(DisplayName = "Fossils & Stone"),
    COUNT    UMETA(Hidden)
};

/**
 * Per-tile resource survey result.  Written during Step08,
 * consumed by WorldMapSubsystem for the heatmap overlay.
 */
USTRUCT(BlueprintType)
struct FResourceSurveyData
{
    GENERATED_BODY()

    /** Peak density per resource category (indexed by EResourceCategory) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<float> DensityByCategory;

    /** Depth (voxels below surface) where the strongest signal was found */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<float> PeakDepthByCategory;

    /** Overall richness score [0,1] for quick map coloring */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float OverallRichness = 0.0f;

    FResourceSurveyData()
    {
        const int32 N = static_cast<int32>(EResourceCategory::COUNT);
        DensityByCategory.SetNumZeroed(N);
        PeakDepthByCategory.SetNumZeroed(N);
    }
};

/**
 * Per-tile vein configuration.  Cached during Step08 so the
 * ResourceSpawnSubsystem doesn't need to re-evaluate noise at runtime.
 */
USTRUCT(BlueprintType)
struct FResourceVeinConfig
{
    GENERATED_BODY()

    /** Which resource categories are present in this tile */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<EResourceCategory> PresentCategories;

    /** Per-category spawn seed for deterministic node placement */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<int64> SpawnSeeds;

    /** Per-category vein richness [0,1] — controls spawn density */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<float> VeinRichness;

    /** Depth range (min, max in voxels below surface) per category */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FVector2D> DepthRanges;

    /** True if this tile has any resource veins at all (quick early-out). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bHasVeins = false;

    /** NEW: allows spawning visible surface resource nodes (Shatter Stone style) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bHasSurfaceExposure = false;
};

/**
 * Runtime record of a spawned Shatter Stone node.
 * Tracked by ResourceSpawnSubsystem for proximity queries and save/load.
 */
USTRUCT(BlueprintType)
struct FSpawnedResourceNode
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 NodeID = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    EResourceCategory Category = EResourceCategory::Metal;

    /** Gameplay tag for the specific ore type (e.g. WorldForge.Resource.Iron) */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag ResourceTag;

    /** Row name in the Shatter Stone Data Table */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FName DataTableRowName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FIntPoint ChunkCell = FIntPoint::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector WorldPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bExposed = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bDepleted = false;

    /** Weak reference to the spawned actor (not saved — re-spawned on chunk activation). */
    UPROPERTY(Transient, BlueprintReadOnly)
    TWeakObjectPtr<AActor> SpawnedActor;
};

/**
 * Resource noise tuning for one EResourceCategory.
 * Stored in FResourceNoiseRegistry so the pipeline, terrain generator,
 * and spawn subsystem all share the same magic numbers.
 *
 * To add a new resource category:
 *   1. Add entry to EResourceCategory (before COUNT)
 *   2. Add an FResourceNoiseProfile to ResourceNoiseRegistry::GetProfile()
 *   3. Add a noise field in WorldForgeTerrainGenerator (Seed + new offset)
 */
USTRUCT(BlueprintType)
struct FResourceNoiseProfile
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EResourceCategory Category = EResourceCategory::Metal;

    /** 3D noise frequency — higher = tighter clusters */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Frequency = 0.025f;

    /** Z-axis frequency scale.  < 1 = horizontal bias. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ZScale = 0.8f;

    /** Seed offset added to the world seed for this noise field */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float SeedOffset = 25000.0f;

    /** Minimum intersection mask to consider a vein "present" */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float PresenceThreshold = 0.55f;

    /** Minimum intersection mask for surface exposure */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float ExposedThreshold = 0.72f;
};

/**
 * Static registry of noise profiles.  One source of truth for the
 * terrain generator, Step08, and ResourceSpawnSubsystem.
 */
struct FResourceNoiseRegistry
{
    static const FResourceNoiseProfile& GetProfile(EResourceCategory Category)
    {
        static const FResourceNoiseProfile Profiles[] =
        {
            { EResourceCategory::Metal,   0.025f, 0.8f, 25000.0f, 0.55f, 0.72f },
            { EResourceCategory::Crystal, 0.018f, 1.2f, 29000.0f, 0.55f, 0.72f },
            { EResourceCategory::Fossil,  0.030f, 0.4f, 33000.0f, 0.55f, 0.72f },
        };

        const int32 Idx = FMath::Clamp(
            static_cast<int32>(Category), 0,
            static_cast<int32>(EResourceCategory::COUNT) - 1);
        return Profiles[Idx];
    }

    static int32 Num() { return static_cast<int32>(EResourceCategory::COUNT); }
};

// ═══════════════════════════════════════════════════════════════════
// VOXEL
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FVoxelBiomeProfile
{
    GENERATED_BODY()

    // ── Surface detail noise (existing) ──
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float NoiseFrequency = 0.02f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float NoiseAmplitude = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OctaveCount = 4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bEnableCaves = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveThreshold = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bSmoothSDF = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> SurfaceMaterials;

    // ── v4.0: 3D SDF Cave layer ──
    // Worm-cave noise frequency.  Higher = tighter, more frequent tunnels.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveWormFrequency = 0.012f;
    // Overall cave density multiplier.  0 = no caves, 1 = normal, 2+ = dense networks.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveIntensity = 0.0f;
    // Min depth below surface (voxels) before caves can start.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveMinDepth = 8.0f;
    // Max depth below surface (voxels) caves can reach.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveMaxDepth = 128.0f;
    // Z-axis frequency scale.  < 1 = horizontal bias (lava tubes).  1 = isotropic.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CaveZScale = 1.0f;

    // ── v4.0: 3D SDF Canyon layer ──
    // Overall canyon carving strength.  0 = no canyons.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CanyonIntensity = 0.0f;
    // Max canyon carve depth in voxels below surface.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CanyonMaxDepth = 40.0f;
    // Ridge noise sharpness.  Higher = narrower, sharper canyon cuts.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CanyonSharpness = 3.0f;

    // ── v4.0: Underground modifiers ──
    // Solid foundation depth below which terrain is always solid (voxels).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float UndergroundFloorDepth = 200.0f;
    // Global density offset.  Positive = more porous, negative = denser.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float DensityBias = 0.0f;
    // Multiplier on surface detail noise amplitude.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SurfaceRoughnessScale = 1.0f;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> ResourceNodes;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> ResourceTags;

    /** v6.0: Resource survey results for world map heatmap */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FResourceSurveyData ResourceSurvey;

    /** v6.0: Vein configuration for runtime Shatter Stone spawning */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FResourceVeinConfig VeinConfig;
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
struct FFigureRelationship
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 TargetFigureID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    ERelationType RelationType = ERelationType::Ally;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 FormedYear = 0;
};

USTRUCT(BlueprintType)
struct FHistoricalFigure
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 FigureID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Epithet;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag RaceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 CivID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 BirthYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 DeathYear = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    EHistoricalRole Role = EHistoricalRole::Hero;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<int32> KeyEventIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FGameplayTag> Skills;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FFigureRelationship> Relationships;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float LegacyScore = 0.f;
};

USTRUCT(BlueprintType)
struct FHistoricalEvent
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 EventID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Title;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 Year = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag EventType;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<int32> InvolvedFigureIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<int32> InvolvedCivIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float HistoricalImpact = 0.5f;
};

USTRUCT(BlueprintType)
struct FLegendaryArtifact
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ArtifactID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Name;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag ItemTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 CreatorFigureID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 CreationYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 CreationEventID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 CurrentOwnerCivID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, float> StatModifiers;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText LoreDescription;
};

USTRUCT(BlueprintType)
struct FArtifactOwnershipRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 ArtifactID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 OwnerCivID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 AcquiredEventID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 LostEventID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 StartYear = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 EndYear = 0;
};

USTRUCT(BlueprintType)
struct FMythRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MythID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Title;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FText Description;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 RelatedFigureID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 RelatedArtifactID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 OriginEventID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MythStrength = 0.5f;
};

// Lightweight settlement reference for lore cross-referencing
USTRUCT(BlueprintType)
struct FSettlementRef
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FIntPoint TileCoord = FIntPoint::ZeroValue;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString LegendaryFigure;

    // Phase 3.5 — RaceOrigin binding
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString RaceOrigin;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bCollapsed = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> ControlledTileIDs;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> HistoryEventIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TotalPopulation = 0;

    // ── Territory ownership — tile index where this civ was placed during Step09.
    // Set by CivPlacementPass, consumed by ClaimTerritories() to BFS-flood territory.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 StartTileIndex = -1;

    // ── Phase 3.5 — Lore integration fields ──
    // Epoch indices set by WorldGenPipeline; consumed by Lore passes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 FoundedEpoch = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 CollapseEpoch = -1;  // -1 = still alive

    // Gameplay tags that describe this civ's personality (e.g. Warlike, Scholarly).
    // Populated by CivilizationGenerator from CulturalDNA thresholds.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTagContainer TraitTags;

    // Lightweight settlement list for lore cross-referencing.
    // Populated by CivPlacementPass / SettlementSpawner.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FSettlementRef> Settlements;
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
struct FSettlementState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 WorldTileID = -1;

    // These are strategic world-map tiles claimed by this settlement. The player can eventually build/mine/farm only inside these claimed tiles.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<int32> ClaimedWorldTileIDs;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString Name;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 Population = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, int32> RacePopulation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MoraleSummary = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag PlayerCurrencyTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 OwnerCivID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float ThreatLevel = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, int32> Stockpiles;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FGameplayTag> CompletedBuildings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag ReligionState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTagContainer ActivePolicies;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bActorsLoaded = false;

    // NEW:
    // World-space terrain/grid origin for this settlement.
    // Local nav cells are converted to world terrain cells by:
    // WorldCell = WorldOrigin + LocalNavCell
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FIntPoint WorldOrigin = FIntPoint::ZeroValue;
};

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FMarketPriceArray> SettlementPrices;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FSettlementPriceHistory> PriceHistory;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, FCurrencyTrustState> CurrencyTrust;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<FGameplayTag, FCurrencyDefinition> AllCurrencies;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FGovernmentEconomicProfile> CivGovernments;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TMap<int32, FTradeFlowState> TradeRoutes;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FCurrencyTrustDelta> PendingTrustDeltas;
};

// ═══════════════════════════════════════════════════════════════════
// COLONY SIMULATION
// ═══════════════════════════════════════════════════════════════════

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

    // Legacy flat custom-build data kept for backward compatibility with existing saves.
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TArray<FIntPoint> BlockList;

    // New canonical 3D block list.
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TArray<FIntVector> BlockList3D;

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag MaterialTag;
};



// ═══════════════════════════════════════════════════════════════════
// COLONIST VISUAL DATA TABLES
// ═══════════════════════════════════════════════════════════════════

/**
 * Row type used by per-race colonist visual DataTables.
 *
 * Create one DataTable per race using this row structure, for example:
 * - DT_ColonistVisual_Dwarf
 * - DT_ColonistVisual_HighElf
 *
 * Keep this row generic: Paper2D flipbooks, PaperZD assets, PaperZD-in-3D
 * assets, or temporary placeholder art can all be assigned as soft UObject
 * references without forcing another compile dependency here.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FWorldForgeColonistVisualRow : public FTableRowBase
{
    GENERATED_BODY()

    /** Race this table/row belongs to, e.g. WorldForge.Race.Dwarf. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual")
    FGameplayTag RaceTag;

    /** Optional body/variant tag, e.g. WorldForge.BodyType.Default or WorldForge.BodyType.Stocky. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual")
    FGameplayTag BodyTypeTag;

    /** Human-readable label for UI menus. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual")
    FText DisplayName;

    /** Optional tags for filtering this visual row later: culture, profession, gender, class, faction, etc. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Filtering")
    FGameplayTagContainer VariantTags;

    /** Tags that must be present in the colonist visual context for this row to be valid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Filtering")
    FGameplayTagContainer RequiredTags;

    /** Tags that make this row invalid for the colonist visual context. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Filtering")
    FGameplayTagContainer BlockedTags;

    /** Paper2D idle flipbook or equivalent visual asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> IdleVisualAsset;

    /** Paper2D walk flipbook or equivalent visual asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> WalkVisualAsset;

    /** Generic work fallback. Used when a specific job state is missing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> WorkVisualAsset;

    /** Carrying/hauling item animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> CarryVisualAsset;

    /** Combat/attack animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> FightVisualAsset;

    /** Hurt, hit reaction, injured, or low-health animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> HurtVisualAsset;

    /** Resting/relaxing animation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> RestVisualAsset;

    /** Sleep flipbook or equivalent visual asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> SleepVisualAsset;

    /** Dead/downed flipbook or equivalent visual asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Core States")
    TSoftObjectPtr<UObject> DeadVisualAsset;

    /** Eating animation. Falls back to Idle if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Need States")
    TSoftObjectPtr<UObject> EatVisualAsset;

    /** Socializing animation. Falls back to Idle if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Need States")
    TSoftObjectPtr<UObject> SocialVisualAsset;

    /** Fleeing/panic animation. Falls back to Walk if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Need States")
    TSoftObjectPtr<UObject> FleeVisualAsset;

    /** Recovering/incapacitated animation. Falls back to Hurt, Rest, or Sleep if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Need States")
    TSoftObjectPtr<UObject> IncapacitatedVisualAsset;

    /** Mining-specific work animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> MineVisualAsset;

    /** Farming-specific work animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> FarmVisualAsset;

    /** Building-specific work animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> BuildVisualAsset;

    /** Crafting-specific work animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> CraftVisualAsset;

    /** Research-specific work animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> ResearchVisualAsset;

    /** Hauling-specific animation. Falls back to Carry, Walk, or Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> HaulVisualAsset;

    /** Doctor/healing animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> DoctorVisualAsset;

    /** Cooking animation. Falls back to Work if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> CookVisualAsset;

    /** Guarding/standing watch animation. Falls back to Fight or Idle if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> GuardVisualAsset;

    /** Trading/merchant animation. Falls back to Social or Idle if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> TradeVisualAsset;

    /** Prayer/ritual animation. Falls back to Idle if empty. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Job States")
    TSoftObjectPtr<UObject> PrayVisualAsset;

    /** Optional PaperZD animation source, PaperZD-in-3D asset, or animation data asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|PaperZD")
    TSoftObjectPtr<UObject> PaperZDAnimationSource;

    /** Optional PaperZD animation blueprint/instance asset. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|PaperZD")
    TSoftObjectPtr<UObject> PaperZDAnimBP;

    /** Relative placement for the visual component under the native actor. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Transform")
    FVector RelativeLocation = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Transform")
    FRotator RelativeRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Transform")
    FVector RelativeScale = FVector(5.0f, 5.0f, 5.0f);

    /** Native fallback cube size. Useful while art is missing or being debugged. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Fallback")
    FVector DebugMeshScale = FVector(0.35f, 0.35f, 0.8f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Fallback")
    FLinearColor DebugColor = FLinearColor::White;

    /** Shows the native fallback cube when no visual asset is assigned. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual|Fallback")
    bool bUseDebugMeshWhenNoAnimation = true;
};

/**
 * Race → DataTable registration entry.
 * The UI can query these entries from UColonistVisualManager to list which
 * race visual tables are available.
 */
USTRUCT(BlueprintType)
struct WORLDFORGE_API FColonistRaceVisualTable
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual")
    FGameplayTag RaceTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|ColonistVisual")
    TObjectPtr<UDataTable> VisualTable = nullptr;
};

USTRUCT(BlueprintType)
struct FColonistJobPriority
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag JobTag;

    // 0 = disabled, 1 = low, 2 = normal, 3 = high, 4 = emergency.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0", ClampMax = "4", UIMin = "0", UIMax = "4"))
    int32 Priority = 2;
};

USTRUCT(BlueprintType)
struct FColonistInspectionData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag RaceTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) EAgentState State = EAgentState::Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTag CurrentJobTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FIntPoint CurrentGridPos = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FIntPoint CurrentNavTarget = FIntPoint::ZeroValue;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float FoodNeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SleepNeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SocialNeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float SafetyNeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float PurposeNeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MoodScore = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float Health = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float MaxHealth = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) float CurrentEfficiency = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bInjured = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FGameplayTagContainer TraitTags;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FGameplayTag, float> Skills;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FColonistJobPriority> JobPriorities;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<EWorldForgeEquipmentSlot, FGameplayTag> EquippedItems;
};

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

USTRUCT(BlueprintType)
struct FActiveProductionTask
{
    GENERATED_BODY()

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TaskID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 SettlementID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag JobTag;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag BuildingTag;

    // ------------------------------------------------------------------------
    // Job target metadata copied from FJobRequest when the agent arrives.
    //
    // Why this exists:
    // FJobRequest already knows which farm zone, mining designation, tile, item,
    // or building target the job belongs to. Once JobProcessor starts a
    // ProductionProcessor task, that information must survive until CompleteTask().
    //
    // Examples:
    // - Farming:
    //     TargetID      = Farm ZoneID
    //     TargetGridPos = Farm tile in settlement-local coordinates
    //
    // - Mining:
    //     TargetID      = Mining DesignationID
    //     TargetGridPos = Mine target X/Y in settlement/nav coordinates
    //
    // - Generic work:
    //     TargetItemTag can still identify item/resource outputs when needed.
    // ------------------------------------------------------------------------
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 TargetID = INDEX_NONE;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FIntPoint TargetGridPos = FIntPoint::ZeroValue;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTag TargetItemTag;

    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 AssignedAgentID = -1;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float Progress = 0.f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) float RequiredProgress = 10.f;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TMap<FGameplayTag, int32> InputsConsumed;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TMap<FGameplayTag, int32> OutputItems;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bInputsReserved = false;
    UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) bool bComplete = false;
};

USTRUCT(BlueprintType)
struct FAgentData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 AgentID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 SettlementID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FString DisplayName;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) EAgentState State = EAgentState::Idle;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag RaceTag;

    // ============================================================================
    // TRAITS
    // Per-colonist traits such as Lucky, Clumsy, NEET, Hikikomori, Chuiboy,
    // FastHealer, SlowHealer, etc.
    //
    // Future us:
    // - Do not hardcode trait behavior directly inside processors.
    // - Resolve trait effects through WorldForgeTraitLibrary / trait DataTable.
    // - RaceTag stays for ancestry/species; TraitTags is for individual modifiers.
    // ============================================================================
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTagContainer TraitTags;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float FoodNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SleepNeed = 100.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SocialNeed = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float SafetyNeed = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float PurposeNeed = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MoodScore = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TArray<FString> RecentThoughts;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, float> Skills;

    // Per-colonist work matrix. Key = WorldForge.Job.* tag. Value: 0 disabled, 1 low, 2 normal, 3 high, 4 emergency.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<FGameplayTag, int32> JobPriorities;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsPacifism = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsCompassion = 50.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float EthicsHonour = 50.f;

    // Equipped items by slot (Tool, Head, etc)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) TMap<EWorldForgeEquipmentSlot, FGameplayTag> EquippedItems;


    // ── Health System ──
// Overall health pool. 100 = healthy, 0 = dead/incapacitated depending on processor rules.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Health = 100.f;

    // Race/trait-adjusted max health. Default humans/neutral colonists use 100.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MaxHealth = 100.f;

    // Work/move efficiency multiplier. 1.0 = normal, 0.6 = injured, 0 = unable to work.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float CurrentEfficiency = 1.0f;

    // Active injury state.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bInjured = false;

    // Remaining simulation steps before injury can naturally recover.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 InjuryStepsRemaining = 0;

    // Severity of the current injury. 0 = none, 1 = minor, 2 = moderate, 3 = severe.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 InjurySeverity = 0;

    // Passive healing per simulation step before modifiers.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float BaseHealingRate = 0.05f;

    // Medical care modifier. Better doctors/medicine/rest increase this.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float MedicalCareMultiplier = 1.0f;

    // Hygiene affects infection/disease risk. 1.0 = clean, 0 = filthy.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Hygiene = 1.0f;

    // Disease/infection resistance from race, traits, medicine, magic, etc.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) float Immunity = 1.0f;

    // True when healing magic, potion, medicine, or treatment was applied recently.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) bool bReceivingTreatment = false;

    // Remaining steps for temporary treatment boost.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 TreatmentStepsRemaining = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) int32 ActiveReservationID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrentJobTag;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) FGameplayTag CurrentJobBuildingTag;
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
struct FMountainRange
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D Start = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D Direction = FVector2D(1.f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float Length = 64.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float Width = 12.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float HeightMultiplier = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float BasinOffset = 18.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float BasinWidth = 10.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float BasinStrength = 0.03f;
};

USTRUCT(BlueprintType)
struct FMountainProvince
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D Center = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D Direction = FVector2D(1.f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float SpreadRadius = 64.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float Strength = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 RangeCount = 3;
};

USTRUCT(BlueprintType)
struct FMountainGenerationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MinProvinceCount = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MaxProvinceCount = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MinRangesPerProvince = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MaxRangesPerProvince = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MinRangeLength = 55.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MaxRangeLength = 130.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MinRangeWidth = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MaxRangeWidth = 18.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MinHeightMultiplier = 0.04f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float MaxHeightMultiplier = 0.16f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float ProvinceDirectionJitterDegrees = 14.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float StartOffsetJitterRadius = 22.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float BasinStrengthScale = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float FalloffSharpness = 2.4f;
};

// ═══════════════════════════════════════════════════════════════════
// VOLCANO (v3.0)
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FVolcanoData
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D Center = FVector2D::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float ConeRadius = 12.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float ConeHeight = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float CraterRadiusFraction = 0.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float CraterDepthFraction = 0.40f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float LavaFlowRadius = 18.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float ConeSteepness = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bActive = true;
};

// ═══════════════════════════════════════════════════════════════════
// WIND / CLIMATE CONFIG (v3.0)
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FWindConfig
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector2D PrevailingDirection = FVector2D(1.0f, 0.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float RainShadowStrength = 0.65f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float RainShadowReach = 30.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float LapseRate = 0.30f;
};

// ── LAKE-FIX-01A: Per-basin descriptor built by BuildLakeBasins() after Step04. ──
// Stores geometry metadata for each flood-filled lake group.
// TileIndices are intentionally omitted — terrain shaping uses bilinear
// WaterDepth sampling instead, keeping this struct lightweight.
USTRUCT(BlueprintType)
struct FLakeBasinData
{
    GENERATED_BODY()

    UPROPERTY() int32 BasinID = -1;
    UPROPERTY() int32 CenterTileX = 0;
    UPROPERTY() int32 CenterTileY = 0;
    UPROPERTY() int32 TileCount = 0;
    UPROPERTY() int32 MinTileX = 0;
    UPROPERTY() int32 MaxTileX = 0;
    UPROPERTY() int32 MinTileY = 0;
    UPROPERTY() int32 MaxTileY = 0;
    UPROPERTY() float WaterSurfaceElevation01 = 0.48f;
    UPROPERTY() float FloorElevation01 = 0.46f;
    UPROPERTY() float ShoreFalloffTiles = 2.0f;
    UPROPERTY() int32 SourceType = 0; // 0=RiverEndpoint, 1=BasinNoise
};

// ── WATERLINE-LAKE-01B: World-space lake surface descriptor for Waterline Pro 6 integration. ──
// Derived from FLakeBasinData after terrain generation. Used by WorldGenBootstrapActor to
// spawn/reposition BP_WaterSim_V2_Local actors over each generated lake basin.
USTRUCT(BlueprintType)
struct FWaterlineLakeSurfaceDesc
{
    GENERATED_BODY()

    // Basin ID matching FLakeBasinData.BasinID
    UPROPERTY(BlueprintReadOnly) int32 BasinID = -1;

    // World-space center of the lake (X, Y = tile center, Z = water surface elevation in UU)
    UPROPERTY(BlueprintReadOnly) FVector WorldCenter = FVector::ZeroVector;

    // Water surface world Z (same as WorldCenter.Z, provided separately for actor placement)
    UPROPERTY(BlueprintReadOnly) float WaterSurfaceZ = 0.0f;

    // Half-extent of the lake bounding box in world units (X and Y axes)
    UPROPERTY(BlueprintReadOnly) float HalfExtentX = 0.0f;
    UPROPERTY(BlueprintReadOnly) float HalfExtentY = 0.0f;

    // Number of lake tiles in this basin
    UPROPERTY(BlueprintReadOnly) int32 TileCount = 0;

    // Biome tag at the basin center tile (may be invalid if no biome data)
    UPROPERTY(BlueprintReadOnly) FGameplayTag BiomeTag;
};

// ── RIVER-SYSTEM-01: Lightweight river surface descriptor. ──
// Built from existing river biome / WaterDepth tiles after terrain generation.
// Descriptors are intentionally segmented so the existing WorldForgeRiverSurfaceActor
// can render short local stretches without a heavy water simulation stack.
USTRUCT(BlueprintType)
struct FWorldForgeRiverSurfaceDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 RiverID = -1;
    UPROPERTY(BlueprintReadOnly) FIntPoint StartTile = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntPoint EndTile = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntPoint MinTile = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntPoint MaxTile = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly) int32 TileCount = 0;
    UPROPERTY(BlueprintReadOnly) TArray<FIntPoint> TilePath;
    UPROPERTY(BlueprintReadOnly) FVector WorldStart = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float AverageWaterZ = 0.0f;
    UPROPERTY(BlueprintReadOnly) float AverageWidth = 1.0f;
    UPROPERTY(BlueprintReadOnly) float AverageWaterDepth = 0.0f;
    UPROPERTY(BlueprintReadOnly) float AverageSurfaceElevation01 = 0.48f;
    UPROPERTY(BlueprintReadOnly) float FlowSpeed = 0.25f;
    UPROPERTY(BlueprintReadOnly) float ChannelDepthVoxels = 1.0f;
    UPROPERTY(BlueprintReadOnly) float BankFalloffTiles = 1.25f;
};

// ── RIVER-WATER-02B: Full river network descriptor. ──
// One descriptor per connected river component. Persisted in the terrain
// snapshot so future patches can carve and render 3D rivers from full ordered
// paths instead of short reconstructed 8-tile chunks.
// Does not replace FWorldForgeRiverSurfaceDesc — the legacy short-segment
// path remains intact for the current river actor spawning system.
USTRUCT(BlueprintType)
struct FWorldForgeRiverNetworkDesc
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) int32 NetworkID = -1;
    // Ordered tile indices (Y*GridWidth+X) from source to outlet.
    UPROPERTY(BlueprintReadOnly) TArray<int32> TilePath;
    UPROPERTY(BlueprintReadOnly) int32 SourceTileIndex = -1;
    UPROPERTY(BlueprintReadOnly) int32 OutletTileIndex = -1;
    UPROPERTY(BlueprintReadOnly) int32 TileCount = 0;
    UPROPERTY(BlueprintReadOnly) float AverageWaterZ = 0.0f;
    UPROPERTY(BlueprintReadOnly) float AverageWidth = 1.0f;
    UPROPERTY(BlueprintReadOnly) float AverageWaterDepth = 0.0f;
    UPROPERTY(BlueprintReadOnly) float MaxWaterDepth = 0.0f;
    UPROPERTY(BlueprintReadOnly) float FlowStrength = 0.0f;
    UPROPERTY(BlueprintReadOnly) bool bHasOutlet = false;
    UPROPERTY(BlueprintReadOnly) bool bTouchesLake = false;
    UPROPERTY(BlueprintReadOnly) bool bTouchesSeaOrOcean = false;
    UPROPERTY(BlueprintReadOnly) FIntPoint BoundsMin = FIntPoint::ZeroValue;
    UPROPERTY(BlueprintReadOnly) FIntPoint BoundsMax = FIntPoint::ZeroValue;
};

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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame) ETerrainMode TerrainMode = ETerrainMode::Smooth;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "1", UIMin = "1"))
    int32 BlockHeightStep = 4;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FMountainGenerationSettings MountainSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FWindConfig WindSettings;
};

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

USTRUCT()
struct FResilienceSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FResilienceRecord> Records;
};

USTRUCT()
struct FCargoItem
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) FGameplayTag ItemTag;
    UPROPERTY(SaveGame) int32 Quantity = 0;
    UPROPERTY(SaveGame) float PricePerUnit = 1.0f;
};

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
    UPROPERTY(SaveGame) int32 LastSupplyArrivedStep = 0;
    UPROPERTY(SaveGame) bool bActive = true;
    UPROPERTY(SaveGame) float FlowRateMod = 1.0f;
};

USTRUCT()
struct FOverworldSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FCaravanData> ActiveCaravans;
    UPROPERTY(SaveGame) TArray<FOutpostData> ActiveOutposts;
    UPROPERTY(SaveGame) int32 NextCaravanID = 0;
    UPROPERTY(SaveGame) int32 NextOutpostID = 0;
};

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

USTRUCT()
struct FDiplomacySaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TMap<int64, float> RelationshipScores;
    UPROPERTY(SaveGame) TArray<FDiplomaticTreaty> ActiveTreaties;
    UPROPERTY(SaveGame) TArray<int32> EmbargoedCivIDs;
    UPROPERTY(SaveGame) TMap<int64, FGameplayTag> PeggedCurrencyPairs;
};

UENUM()
enum class EShipType : uint8
{
    Raft,
    RiverBarge,
    ScoutSloop
};

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

USTRUCT()
struct FNavalSaveState
{
    GENERATED_BODY()

    UPROPERTY(SaveGame) TArray<FShipData> ActiveShips;
    UPROPERTY(SaveGame) TArray<int32> BlockadedPortSettlementIDs;
    UPROPERTY(SaveGame) int32 NextShipID = 0;
};

UENUM()
enum class ECosmicEntityStatus : uint8
{
    Dormant,
    Emerging,
    Active,
    Fading,
    Gone
};

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

// ═══════════════════════════════════════════════════════════════════
// VSS SAVE DATA
// ═══════════════════════════════════════════════════════════════════

USTRUCT(BlueprintType)
struct FVoxelSpawnRecord
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FGameplayTag SimEntityTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FIntPoint GridCell = FIntPoint::ZeroValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector WorldPos = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FRotator Rotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector Scale = FVector(1.f, 1.f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 InstanceIdx = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 SettlementID = -1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bIsProp = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bIsBlackMarket = false;
};

USTRUCT(BlueprintType)
struct FVSSSaveState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FVoxelSpawnRecord> SpawnRecords;
};