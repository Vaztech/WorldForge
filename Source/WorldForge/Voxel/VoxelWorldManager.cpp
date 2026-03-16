#include "VoxelWorldManager.h"

void UVoxelWorldManager::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UVoxelWorldManager::ApplyBiomeProfileToChunk(
    FGameplayTag BiomeTag, const FVoxelBiomeProfile& Profile, FVector ChunkOrigin)
{
    if (!ValidateSmoothSDF(Profile))
    {
        UE_LOG(LogTemp, Error, TEXT("[VoxelWorldManager] bSmoothSDF=false on %s! Overriding."), *BiomeTag.ToString());
    }

    OnBiomeChunkRequested(ChunkOrigin, Profile.NoiseAmplitude, Profile.NoiseFrequency, Profile.OctaveCount);
}

void UVoxelWorldManager::PlaceVoxelBlock(FVector WorldPosition, FGameplayTag MaterialTag)
{
    OnVoxelBlockPlaced(WorldPosition, MaterialTag);
}

void UVoxelWorldManager::NotifyVoxelDig(FVector WorldPosition, int32 AgentID)
{
    OnVoxelDigged(WorldPosition);
}

bool UVoxelWorldManager::ValidateSmoothSDF(const FVoxelBiomeProfile& Profile) const
{
    return Profile.bSmoothSDF;
}