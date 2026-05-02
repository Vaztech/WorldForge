#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "WorldForgeRNG.generated.h"

// ═══════════════════════════════════════════════════════════════════
// UWorldForgeRNG — THE ONLY SOURCE OF RANDOMNESS IN WORLDFORGE
// ═══════════════════════════════════════════════════════════════════
// RULES:
//  1. No system may EVER call FMath::Rand(), rand(), or time-based seeds.
//  2. Every random call passes a branch index constant from this file.
//  3. Same master seed ALWAYS produces the same world on any machine.
//  4. Each branch is an isolated LCG stream — one branch per system.
// ═══════════════════════════════════════════════════════════════════

UCLASS(BlueprintType)
class WORLDFORGE_API UWorldForgeRNG : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ── Initialization ─────────────────────────────────────────────
    // Call ONCE at world generation start with the player's chosen seed.
    UFUNCTION(BlueprintCallable, Category = "WorldForge|RNG")
    void InitializeWithSeed(int64 InMasterSeed);

    // ── Core Random Methods ────────────────────────────────────────
    // Returns float in [0.0, 1.0) — use branch index constants below
    float NextFloat(int32 BranchIndex);

    // Returns int in [Min, Max] inclusive
    int32 NextInt(int32 BranchIndex, int32 Min, int32 Max);

    // Returns raw int64 — useful for IDs and hash seeds
    int64 NextInt64(int32 BranchIndex);

    // Returns bool with given probability (0.0=never, 1.0=always)
    bool NextBool(int32 BranchIndex, float Probability = 0.5f);

    // Returns float in [Min, Max]
    float NextFloatRange(int32 BranchIndex, float Min, float Max);

    // ── State Queries ──────────────────────────────────────────────
    int64 GetMasterSeed() const { return MasterSeed; }
    bool IsInitialized() const { return bInitialized; }
    int32 GetNumBranches() const { return NUM_BRANCHES; }

    // Save/restore branch states for deterministic save-load
    TArray<int64> GetBranchStates() const { return BranchStates; }
    void RestoreBranchStates(const TArray<int64>& States);

    // ── Branch Index Constants ─────────────────────────────────────
    // ONE constant per system — each system uses ONLY its own branch.
    // Adding a new system: add a new constant and increment NUM_BRANCHES.
    static const int32 BRANCH_TERRAIN = 0;    // Heightmap generation
    static const int32 BRANCH_BIOME = 1;      // Rainfall + temperature
    static const int32 BRANCH_RESOURCES = 2;  // Ore vein placement
    static const int32 BRANCH_PREHISTORY = 3; // Pre-history events
    static const int32 BRANCH_CIV_START = 4;  // Civ starting positions
    static const int32 BRANCH_CULTURAL_DNA = 5; // Cultural DNA values
    static const int32 BRANCH_NPC_TRAITS = 6;   // Individual NPC traits
    static const int32 BRANCH_EVENTS = 7;       // Ongoing event scheduling
    static const int32 BRANCH_CARAVANS = 8;     // Caravan inventory
    static const int32 BRANCH_OFFSPRING = 9;    // NPC offspring traits
    static const int32 BRANCH_DIPLOMACY = 10;   // Diplomatic triggers
    static const int32 BRANCH_DISASTERS = 11;   // Disasters + magical surges
    static const int32 BRANCH_COSMIC = 12;      // Cosmic entity generation
    static const int32 BRANCH_ANCHORS = 13;     // Anchor + relic properties
    static const int32 BRANCH_ECONOMY = 14;     // Economy + currency seeding
    static const int32 BRANCH_VOLCANO = 15;     // Volcano placement + lava flow
    static const int32 BRANCH_CAVES = 16;       // v4.0: Cave network noise seeding
    static const int32 BRANCH_CANYONS = 17;     // v4.0: Canyon carving noise seeding
    static const int32 NUM_BRANCHES = 18;       // TOTAL — update when adding
    static const int32 BRANCH_INCIDENTS = 19;

private:
    int64 MasterSeed = 0;
    bool bInitialized = false;
    TArray<int64> BranchStates;

    // Linear Congruential Generator — fast, simple, deterministic
    FORCEINLINE static int64 AdvanceLCG(int64 State)
    {
        // Knuth multiplicative hash constants — well-tested for games
        return State * 6364136223846793005LL + 1442695040888963407LL;
    }
};
