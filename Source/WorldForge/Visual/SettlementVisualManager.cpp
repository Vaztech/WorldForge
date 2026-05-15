#include "Visual/SettlementVisualManager.h"

#include "Visual/DA_VoxelVisualRuleSet.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Voxel/VoxelWorldManager.h"

void USettlementVisualManager::Initialize(UWorld* InWorld, int32 InSettlementID, UDA_VoxelVisualRuleSet* InRuleSet)
{
    CachedWorld = InWorld;
    SettlementID = InSettlementID;
    RuleSet = InRuleSet;
}

void USettlementVisualManager::SetRuleSet(UDA_VoxelVisualRuleSet* InRuleSet)
{
    RuleSet = InRuleSet;
}

void USettlementVisualManager::RebuildBuildingVisuals(
    const FSettlementState& SettlementState,
    TArray<FVoxelSpawnRecord>& InOutSpawnRecords)
{
    ClearAllVisuals();

    InOutSpawnRecords.RemoveAll(
        [this](const FVoxelSpawnRecord& Record)
        {
            return Record.SettlementID == SettlementID;
        });

    if (!CachedWorld)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VSS] SettlementVisualManager %d has no cached world."),
            SettlementID);
        return;
    }

    if (!RuleSet)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[VSS] SettlementVisualManager %d has no visual rule set."),
            SettlementID);
        return;
    }

    for (int32 Index = 0; Index < SettlementState.CompletedBuildings.Num(); ++Index)
    {
        const FGameplayTag& BuildingTag = SettlementState.CompletedBuildings[Index];

        FBuildingVisualRule Rule;
        if (!RuleSet->GetBuildingRule(BuildingTag, Rule))
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("[VSS] No building visual rule for %s in settlement %d."),
                *BuildingTag.ToString(),
                SettlementID);
            continue;
        }

        if (!Rule.Mesh)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[VSS] Building rule for %s has no mesh in settlement %d."),
                *BuildingTag.ToString(),
                SettlementID);
            continue;
        }

        UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Rule.Mesh);
        if (!HISM)
        {
            continue;
        }

        const FTransform WorldTransform = MakeBuildingTransform(SettlementState, Index, Rule);
        const int32 InstanceIdx = HISM->AddInstance(WorldTransform, true);

        FVoxelSpawnRecord Record;
        Record.SimEntityTag = BuildingTag;
        Record.GridCell = FIntPoint(Index, 0);
        Record.WorldPos = WorldTransform.GetLocation();
        Record.Rotation = WorldTransform.Rotator();
        Record.Scale = WorldTransform.GetScale3D();
        Record.InstanceIdx = InstanceIdx;
        Record.SettlementID = SettlementID;
        Record.bIsProp = false;
        Record.bIsBlackMarket = false;

        InOutSpawnRecords.Add(Record);
    }
}

void USettlementVisualManager::ClearAllVisuals()
{
    for (UHierarchicalInstancedStaticMeshComponent* HISM : OwnedHISMs)
    {
        if (IsValid(HISM))
        {
            HISM->ClearInstances();
            HISM->DestroyComponent();
        }
    }

    OwnedHISMs.Reset();
    HISMByMesh.Reset();
}

UHierarchicalInstancedStaticMeshComponent* USettlementVisualManager::GetOrCreateHISM(UStaticMesh* Mesh)
{
    if (!CachedWorld || !Mesh)
    {
        return nullptr;
    }

    if (TObjectPtr<UHierarchicalInstancedStaticMeshComponent>* Found = HISMByMesh.Find(Mesh))
    {
        return Found->Get();
    }

    AWorldSettings* OuterActor = CachedWorld->GetWorldSettings();
    if (!OuterActor)
    {
        return nullptr;
    }

    UHierarchicalInstancedStaticMeshComponent* HISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(OuterActor);

    if (!HISM)
    {
        return nullptr;
    }

    HISM->SetStaticMesh(Mesh);
    HISM->SetMobility(EComponentMobility::Static);
    HISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HISM->RegisterComponentWithWorld(CachedWorld);

    OuterActor->AddInstanceComponent(HISM);

    OwnedHISMs.Add(HISM);
    HISMByMesh.Add(Mesh, HISM);

    return HISM;
}

FTransform USettlementVisualManager::MakeBuildingTransform(
    const FSettlementState& SettlementState,
    int32 BuildingIndex,
    const FBuildingVisualRule& Rule) const
{
    // Deterministic placeholder layout anchored to the settlement's real
    // WorldOrigin so generated NPC civ settlements appear at their civ capital
    // instead of all spawning near world zero.
    const int32 LocalX = BuildingIndex % 4;
    const int32 LocalY = BuildingIndex / 4;
    const FIntPoint BuildingWorldCell = SettlementState.WorldOrigin + FIntPoint(4 + LocalX * 2, 4 + LocalY * 2);

    FVector BaseLocation(
        static_cast<float>(BuildingWorldCell.X) * 3200.0f,
        static_cast<float>(BuildingWorldCell.Y) * 3200.0f,
        0.0f);

    if (CachedWorld)
    {
        if (const UVoxelWorldManager* VoxelWorldManager = CachedWorld->GetSubsystem<UVoxelWorldManager>())
        {
            BaseLocation = VoxelWorldManager->TerrainCellToWorld(BuildingWorldCell, 0.0f, true);
        }
    }

    const FVector WorldLocation = BaseLocation + Rule.RelativeOffset + FVector(0.0f, 0.0f, 120.0f);

    return FTransform(
        Rule.RelativeRotation.Quaternion(),
        WorldLocation,
        Rule.RelativeScale);
}
