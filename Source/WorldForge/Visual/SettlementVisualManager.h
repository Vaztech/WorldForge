#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WorldForgeDataTypes.h"
#include "Visual/DA_VoxelVisualRuleSet.h" // ✅ REQUIRED FIX
#include "SettlementVisualManager.generated.h"

class UDA_VoxelVisualRuleSet;
class UHierarchicalInstancedStaticMeshComponent;
class UStaticMesh;
class UWorld;

UCLASS()
class WORLDFORGE_API USettlementVisualManager : public UObject
{
    GENERATED_BODY()

public:
    void Initialize(UWorld* InWorld, int32 InSettlementID, UDA_VoxelVisualRuleSet* InRuleSet);

    void SetRuleSet(UDA_VoxelVisualRuleSet* InRuleSet);

    void RebuildBuildingVisuals(
        const FSettlementState& SettlementState,
        TArray<FVoxelSpawnRecord>& InOutSpawnRecords);

    void ClearAllVisuals();

    int32 GetSettlementID() const { return SettlementID; }

private:
    UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(UStaticMesh* Mesh);

    // ✅ NOW VALID because struct is visible
    FTransform MakeBuildingTransform(int32 BuildingIndex, const FBuildingVisualRule& Rule) const;

private:
    UPROPERTY()
    TObjectPtr<UWorld> CachedWorld = nullptr;

    UPROPERTY()
    TObjectPtr<UDA_VoxelVisualRuleSet> RuleSet = nullptr;

    UPROPERTY(Transient)
    TMap<TObjectPtr<UStaticMesh>, TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> HISMByMesh;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> OwnedHISMs;

    int32 SettlementID = -1;
};