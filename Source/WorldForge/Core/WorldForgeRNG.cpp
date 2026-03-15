#include "WorldForgeRNG.h"
#include "Logging/LogMacros.h"

void UWorldForgeRNG::InitializeWithSeed(int64 InMasterSeed)
{
    MasterSeed = InMasterSeed;
    bInitialized = true;

    BranchStates.SetNum(NUM_BRANCHES);
    for (int32 i = 0; i < NUM_BRANCHES; i++)
    {
        // Each branch starts from a unique derivation of the master seed.
        // Multiplying by (i+1) ensures no two branches ever start at the same state,
        // even when MasterSeed = 0.
        BranchStates[i] = AdvanceLCG(MasterSeed + static_cast<int64>(i + 1) * 6364136223846793005LL);
    }

    UE_LOG(LogTemp, Log, TEXT("[WorldForgeRNG] Initialized - Seed: %lld Branches: %d"),
        MasterSeed, NUM_BRANCHES);
}

float UWorldForgeRNG::NextFloat(int32 BranchIndex)
{
    checkf(bInitialized,
        TEXT("[WorldForgeRNG] NextFloat called before InitializeWithSeed! Branch: %d"),
        BranchIndex);
    checkf(BranchStates.IsValidIndex(BranchIndex),
        TEXT("[WorldForgeRNG] Invalid branch index: %d Max: %d"),
        BranchIndex, NUM_BRANCHES - 1);

    BranchStates[BranchIndex] = AdvanceLCG(BranchStates[BranchIndex]);

    // Map to [0.0, 1.0) using upper 53 bits for good floating-point distribution.
    // Shifting by 11 gives us 53 bits, which is the mantissa width of double.
    const uint64 UnsignedVal = static_cast<uint64>(BranchStates[BranchIndex]);
    return static_cast<float>((double)(UnsignedVal >> 11) / (double)(1ULL << 53));
}

int32 UWorldForgeRNG::NextInt(int32 BranchIndex, int32 Min, int32 Max)
{
    checkf(Min <= Max,
        TEXT("[WorldForgeRNG] NextInt: Min (%d) must be <= Max (%d)"),
        Min, Max);

    const float F = NextFloat(BranchIndex);
    return Min + FMath::FloorToInt(F * (Max - Min + 1));
}

int64 UWorldForgeRNG::NextInt64(int32 BranchIndex)
{
    checkf(bInitialized,
        TEXT("[WorldForgeRNG] NextInt64 called before initialization!"));
    checkf(BranchStates.IsValidIndex(BranchIndex),
        TEXT("[WorldForgeRNG] Invalid branch index: %d"),
        BranchIndex);

    BranchStates[BranchIndex] = AdvanceLCG(BranchStates[BranchIndex]);
    return BranchStates[BranchIndex];
}

bool UWorldForgeRNG::NextBool(int32 BranchIndex, float Probability)
{
    return NextFloat(BranchIndex) < FMath::Clamp(Probability, 0.f, 1.f);
}

float UWorldForgeRNG::NextFloatRange(int32 BranchIndex, float Min, float Max)
{
    return FMath::Lerp(Min, Max, NextFloat(BranchIndex));
}

void UWorldForgeRNG::RestoreBranchStates(const TArray<int64>& States)
{
    checkf(States.Num() == NUM_BRANCHES,
        TEXT("[WorldForgeRNG] RestoreBranchStates: expected %d states, got %d"),
        NUM_BRANCHES, States.Num());

    BranchStates = States;
    bInitialized = true;

    UE_LOG(LogTemp, Log, TEXT("[WorldForgeRNG] Branch states restored from save."));
}