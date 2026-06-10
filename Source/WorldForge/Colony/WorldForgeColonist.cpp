#include "WorldForgeColonist.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "SettlementManager.h"
#include "SpriteCharacterComponent.h"
#include "PaperFlipbook.h"
#include "PaperFlipbookComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelWorldManager.h"
#include "Utility/WorldForgeTerrainSnapUtility.h"

AWorldForgeColonist::AWorldForgeColonist()
{
    PrimaryActorTick.bCanEverTick = true;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    RootComponent = SceneRoot;

    DebugMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DebugMesh"));
    DebugMesh->SetupAttachment(SceneRoot);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));

    if (CubeMesh.Succeeded())
    {
        DebugMesh->SetStaticMesh(CubeMesh.Object);
    }

    DebugMesh->SetRelativeLocation(FVector::ZeroVector);
    DebugMesh->SetWorldScale3D(FVector(0.35f, 0.35f, 0.8f));
    DebugMesh->SetCastShadow(true);
    DebugMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    DebugMesh->SetCollisionResponseToAllChannels(ECR_Block);

    SpriteVisual = CreateDefaultSubobject<USpriteCharacterComponent>(TEXT("SpriteVisual"));
    SpriteVisual->SetupAttachment(SceneRoot);
    SpriteVisual->SetRelativeLocation(FVector::ZeroVector);
    SpriteVisual->SetRelativeScale3D(FVector(5.0f, 5.0f, 5.0f));

    VisualFlipbook = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("VisualFlipbook"));
    VisualFlipbook->SetupAttachment(SpriteVisual);
    VisualFlipbook->SetRelativeLocation(FVector::ZeroVector);
    VisualFlipbook->SetRelativeRotation(FRotator::ZeroRotator);
    VisualFlipbook->SetRelativeScale3D(FVector::OneVector);
    VisualFlipbook->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    VisualFlipbook->SetCastShadow(true);
    VisualFlipbook->SetLooping(true);
    VisualFlipbook->SetTranslucentSortPriority(100);
    VisualFlipbook->SetHiddenInGame(true);
}

FVector AWorldForgeColonist::GetCurrentCameraLocation() const
{
    const UWorld* World = GetWorld();
    if (!World)
    {
        return FVector::ZeroVector;
    }

    const APlayerController* PC = World->GetFirstPlayerController();
    if (!PC)
    {
        return FVector::ZeroVector;
    }

    FVector CameraLocation = FVector::ZeroVector;
    FRotator CameraRotation = FRotator::ZeroRotator;
    PC->GetPlayerViewPoint(CameraLocation, CameraRotation);
    return CameraLocation;
}

void AWorldForgeColonist::UpdateFlipbookBillboardRotation()
{
    if (!bBillboardFlipbookToCamera || !VisualFlipbook)
    {
        return;
    }

    const FVector CameraLocation = GetCurrentCameraLocation();
    if (CameraLocation.IsNearlyZero())
    {
        return;
    }

    const FVector ToCamera = CameraLocation - VisualFlipbook->GetComponentLocation();
    if (ToCamera.IsNearlyZero())
    {
        return;
    }

    // Yaw-only billboard keeps the Paper2D character upright while facing the camera.
    // PaperFlipbook planes commonly need a 90-degree yaw correction, exposed as
    // FlipbookBillboardYawOffset for art packs authored with different facing axes.
    const float TargetYaw = ToCamera.Rotation().Yaw + FlipbookBillboardYawOffset;
    VisualFlipbook->SetWorldRotation(FRotator(0.0f, TargetYaw, 0.0f));
}

void AWorldForgeColonist::BeginPlay()
{
    Super::BeginPlay();

    if (DebugMesh)
    {
        DebugMesh->SetHiddenInGame(!bForceDebugMeshVisible);
    }

    if (SpriteVisual)
    {
        if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
        {
            SpriteVisual->CameraTarget = PC->GetViewTarget();
        }
    }
}


void AWorldForgeColonist::ApplyVisualDefinition(const FWorldForgeColonistVisualRow& VisualDefinition)
{
    ActiveVisualDefinition = VisualDefinition;

    if (SpriteVisual)
    {
        SpriteVisual->SetRelativeLocation(VisualDefinition.RelativeLocation);
        SpriteVisual->SetRelativeRotation(VisualDefinition.RelativeRotation);
        SpriteVisual->SetRelativeScale3D(VisualDefinition.RelativeScale);
    }

    if (VisualFlipbook)
    {
        VisualFlipbook->SetRelativeLocation(FVector::ZeroVector);
        VisualFlipbook->SetRelativeRotation(FRotator::ZeroRotator);
        VisualFlipbook->SetRelativeScale3D(FVector::OneVector);
        VisualFlipbook->SetTranslucentSortPriority(100);
    }

    if (DebugMesh)
    {
        DebugMesh->SetWorldScale3D(VisualDefinition.DebugMeshScale);
        DebugMesh->SetVectorParameterValueOnMaterials(
            TEXT("Color"),
            FVector(VisualDefinition.DebugColor.R, VisualDefinition.DebugColor.G, VisualDefinition.DebugColor.B));
    }

    const bool bAppliedVisualAsset = ApplyVisualForCurrentAgentState();

    FIntPoint ExpectedWorldCell = CurrentGridCell;
    if (const UWorld* World = GetWorld())
    {
        if (const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>())
        {
            if (const FSettlementState* Settlement = SettlementManager->GetSettlement(SettlementID))
            {
                ExpectedWorldCell = Settlement->WorldOrigin + CurrentGridCell;
            }
        }
    }

    const UPaperFlipbook* CurrentFlipbook = VisualFlipbook ? VisualFlipbook->GetFlipbook() : nullptr;
    const FVector SpriteRelLoc = SpriteVisual ? SpriteVisual->GetRelativeLocation() : FVector::ZeroVector;
    const FVector SpriteRelScale = SpriteVisual ? SpriteVisual->GetRelativeScale3D() : FVector::ZeroVector;
    const FVector FlipbookWorldLoc = VisualFlipbook ? VisualFlipbook->GetComponentLocation() : FVector::ZeroVector;
    const FVector BoundsOrigin = VisualFlipbook ? VisualFlipbook->Bounds.Origin : FVector::ZeroVector;
    const FVector BoundsExtent = VisualFlipbook ? VisualFlipbook->Bounds.BoxExtent : FVector::ZeroVector;

    UE_LOG(
        LogTemp,
        Log,
        TEXT("[ColonistVisibilityAudit] AgentID=%d SettlementID=%d Actor=%s Hidden=%s ActorLoc=%s ActorScale=%s TerrainZOffset=%.1f FlipbookVisible=%s FlipbookHidden=%s Flipbook=%s DebugMeshVisible=%s DebugMeshHidden=%s SpriteRelLoc=%s SpriteRelScale=%s FlipbookWorldLoc=%s BoundsOrigin=%s BoundsExtent=%s VisualRow='%s' Race=%s CurrentGridPos=(%d,%d) ExpectedWorldCell=(%d,%d) ResolvedLocation=%s ApplyVisualAsset=%s"),
        AgentID,
        SettlementID,
        *GetNameSafe(this),
        IsHidden() ? TEXT("true") : TEXT("false"),
        *GetActorLocation().ToString(),
        *GetActorScale3D().ToString(),
        TerrainZOffset,
        (VisualFlipbook && VisualFlipbook->IsVisible()) ? TEXT("true") : TEXT("false"),
        (!VisualFlipbook || VisualFlipbook->bHiddenInGame) ? TEXT("true") : TEXT("false"),
        *GetNameSafe(CurrentFlipbook),
        (DebugMesh && DebugMesh->IsVisible()) ? TEXT("true") : TEXT("false"),
        (!DebugMesh || DebugMesh->bHiddenInGame) ? TEXT("true") : TEXT("false"),
        *SpriteRelLoc.ToString(),
        *SpriteRelScale.ToString(),
        *FlipbookWorldLoc.ToString(),
        *BoundsOrigin.ToString(),
        *BoundsExtent.ToString(),
        *ActiveVisualDefinition.DisplayName.ToString(),
        bHasLastAgentData ? *LastAgentData.RaceTag.ToString() : TEXT("<unknown>"),
        CurrentGridCell.X,
        CurrentGridCell.Y,
        ExpectedWorldCell.X,
        ExpectedWorldCell.Y,
        *TargetWorldPos.ToString(),
        bAppliedVisualAsset ? TEXT("true") : TEXT("false"));
}

void AWorldForgeColonist::SetSpriteVisualComponent(USpriteCharacterComponent* InSpriteVisual)
{
    if (InSpriteVisual)
    {
        SpriteVisual = InSpriteVisual;
    }
}

void AWorldForgeColonist::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (!bHasVisualData)
    {
        return;
    }

    if (NeedCriticalTimer > 0.f)
    {
        NeedCriticalTimer -= DeltaTime;
    }

    UpdateMovement(DeltaTime);
    UpdateFlipbookBillboardRotation();
}

FVector AWorldForgeColonist::ResolveWorldLocationFromAgentData(const FAgentData& Data, float CellSize) const
{
    FIntPoint RequestedWorldCell(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);

    if (const UWorld* World = GetWorld())
    {
        if (const USettlementManager* SettlementManager = World->GetSubsystem<USettlementManager>())
        {
            if (const FSettlementState* Settlement = SettlementManager->GetSettlement(Data.SettlementID))
            {
                RequestedWorldCell = Settlement->WorldOrigin + FIntPoint(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);
            }
        }

        if (const UVoxelWorldManager* VoxelWorldManager = World->GetSubsystem<UVoxelWorldManager>())
        {
            const FVector BaseWorldLocation = VoxelWorldManager->TerrainCellToWorld(RequestedWorldCell, 0.0f, true);

            if (!VoxelWorldManager->IsSurfaceReady())
            {
                if (bHasVisualData)
                {
                    return GetActorLocation();
                }

                if (!TargetWorldPos.IsNearlyZero())
                {
                    return TargetWorldPos;
                }

                return BaseWorldLocation + FVector(0.f, 0.f, TerrainZOffset);
            }

            FVector DirectCellSnapLocation = BaseWorldLocation;
            FIntPoint DirectResolvedCell = RequestedWorldCell;
            if (VoxelWorldManager->TryGetNearestSpawnLocationForTerrainCell(
                RequestedWorldCell,
                DirectCellSnapLocation,
                DirectResolvedCell,
                8,
                TerrainZOffset))
            {
                UE_LOG(
                    LogTemp,
                    Log,
                    TEXT("[CubeTerrainSnap] Colonist cell snap succeeded for AgentID=%d LocalCell=(%d,%d) WorldCell=(%d,%d) ResolvedCell=(%d,%d) Location=%s"),
                    Data.AgentID,
                    Data.CurrentGridPos.X,
                    Data.CurrentGridPos.Y,
                    RequestedWorldCell.X,
                    RequestedWorldCell.Y,
                    DirectResolvedCell.X,
                    DirectResolvedCell.Y,
                    *DirectCellSnapLocation.ToString()
                );

                return DirectCellSnapLocation;
            }

            FVector SnappedLocation = BaseWorldLocation;
            if (UWorldForgeTerrainSnapUtility::TryGetTerrainSnapLocation(
                const_cast<UWorld*>(World),
                BaseWorldLocation,
                SnappedLocation,
                TerrainZOffset))
            {
                return SnappedLocation;
            }

            UE_LOG(
                LogTemp,
                Warning,
                TEXT("[WorldForgeColonist] Terrain snap failed for AgentID=%d LocalCell=(%d,%d) WorldCell=(%d,%d). Holding current position until terrain resolves."),
                Data.AgentID,
                Data.CurrentGridPos.X,
                Data.CurrentGridPos.Y,
                RequestedWorldCell.X,
                RequestedWorldCell.Y
            );

            if (bHasVisualData)
            {
                return GetActorLocation();
            }

            if (!TargetWorldPos.IsNearlyZero())
            {
                return TargetWorldPos;
            }

            return BaseWorldLocation + FVector(0.f, 0.f, TerrainZOffset);
        }
    }

    return FVector(
        RequestedWorldCell.X * CellSize,
        RequestedWorldCell.Y * CellSize,
        TerrainZOffset
    );
}

void AWorldForgeColonist::InitializeFromAgentData(const FAgentData& Data, float CellSize)
{
    AgentID = Data.AgentID;
    SettlementID = Data.SettlementID;
    VisualCellSize = CellSize;
    CurrentGridCell = FIntPoint(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);

    const FVector Position = ResolveWorldLocationFromAgentData(Data, CellSize);

    SetActorLocation(Position);
    TargetWorldPos = Position;
    LastKnownState = Data.State;
    bHasVisualData = true;
    LastAgentData = Data;
    bHasLastAgentData = true;

    if (SpriteVisual)
    {
        SpriteVisual->SetAnimStateFromAgentState(Data.State);
        SpriteVisual->SetRace(Data.RaceTag);
    }

    // Do NOT apply flipbooks here. The manager applies the race visual
    // definition immediately after initialization; applying before that
    // causes false "No valid flipbook" warnings and can leave the
    // component in fallback state.
}

void AWorldForgeColonist::UpdateVisuals(
    const FAgentData& Data,
    float DeltaTime,
    float CellSize
)
{
    (void)DeltaTime;

    VisualCellSize = CellSize;
    CurrentGridCell = FIntPoint(Data.CurrentGridPos.X, Data.CurrentGridPos.Y);
    TargetWorldPos = ResolveWorldLocationFromAgentData(Data, CellSize);

    bHasVisualData = true;
    LastAgentData = Data;
    bHasLastAgentData = true;

    if (SpriteVisual)
    {
        SpriteVisual->SetAnimStateFromAgentState(Data.State);
        SpriteVisual->SetRace(Data.RaceTag);
    }

    ApplyVisualForCurrentAgentState();

    if (Data.State != LastKnownState)
    {
        OnStateChanged(Data.State, LastKnownState);
        LastKnownState = Data.State;

        if (Data.State == EAgentState::Dead)
        {
            OnColonistDied();
        }
    }

    if (DebugMesh)
    {
        FVector Tint = FVector(1.f, 1.f, 1.f);

        if (Data.State == EAgentState::Working)
        {
            Tint = FVector(0.f, 1.f, 0.f);
        }
        else if (Data.State == EAgentState::Sleeping)
        {
            Tint = FVector(0.f, 0.f, 1.f);
        }
        else if (Data.State == EAgentState::Eating)
        {
            Tint = FVector(1.f, 1.f, 0.f);
        }

        DebugMesh->SetVectorParameterValueOnMaterials(TEXT("Color"), Tint);
    }

    if (NeedCriticalTimer <= 0.f && (Data.FoodNeed < 20.f || Data.SleepNeed < 20.f))
    {
        OnNeedCritical(Data.FoodNeed, Data.SleepNeed);
        NeedCriticalTimer = NeedCriticalCooldown;
    }
}


UPaperFlipbook* AWorldForgeColonist::LoadFlipbookFromSoftObject(const TSoftObjectPtr<UObject>& VisualAsset) const
{
    if (VisualAsset.IsNull())
    {
        return nullptr;
    }

    UObject* LoadedObject = VisualAsset.Get();
    if (!LoadedObject)
    {
        LoadedObject = VisualAsset.LoadSynchronous();
    }

    return Cast<UPaperFlipbook>(LoadedObject);
}

const TSoftObjectPtr<UObject>& AWorldForgeColonist::SelectWorkVisualAssetByJob() const
{
    static const TSoftObjectPtr<UObject> EmptyAsset;

    if (!bHasLastAgentData || !LastAgentData.CurrentJobTag.IsValid())
    {
        return ActiveVisualDefinition.WorkVisualAsset;
    }

    const FString JobString = LastAgentData.CurrentJobTag.ToString();

    if (JobString.Contains(TEXT("Mine")) && !ActiveVisualDefinition.MineVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.MineVisualAsset;
    }
    if (JobString.Contains(TEXT("Farm")) && !ActiveVisualDefinition.FarmVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.FarmVisualAsset;
    }
    if (JobString.Contains(TEXT("Build")) && !ActiveVisualDefinition.BuildVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.BuildVisualAsset;
    }
    if (JobString.Contains(TEXT("Craft")) && !ActiveVisualDefinition.CraftVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.CraftVisualAsset;
    }
    if (JobString.Contains(TEXT("Research")) && !ActiveVisualDefinition.ResearchVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.ResearchVisualAsset;
    }
    if (JobString.Contains(TEXT("Haul")) && !ActiveVisualDefinition.HaulVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.HaulVisualAsset;
    }
    if (JobString.Contains(TEXT("Doctor")) && !ActiveVisualDefinition.DoctorVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.DoctorVisualAsset;
    }
    if (JobString.Contains(TEXT("Cook")) && !ActiveVisualDefinition.CookVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.CookVisualAsset;
    }
    if (JobString.Contains(TEXT("Guard")) && !ActiveVisualDefinition.GuardVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.GuardVisualAsset;
    }
    if (JobString.Contains(TEXT("Trade")) && !ActiveVisualDefinition.TradeVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.TradeVisualAsset;
    }
    if (JobString.Contains(TEXT("Pray")) && !ActiveVisualDefinition.PrayVisualAsset.IsNull())
    {
        return ActiveVisualDefinition.PrayVisualAsset;
    }

    return ActiveVisualDefinition.WorkVisualAsset.IsNull() ? EmptyAsset : ActiveVisualDefinition.WorkVisualAsset;
}

const TSoftObjectPtr<UObject>& AWorldForgeColonist::SelectVisualAssetForAgentState() const
{
    static const TSoftObjectPtr<UObject> EmptyAsset;

    const EAgentState State = bHasLastAgentData ? LastAgentData.State : LastKnownState;

    switch (State)
    {
    case EAgentState::Dead:
        if (!ActiveVisualDefinition.DeadVisualAsset.IsNull()) return ActiveVisualDefinition.DeadVisualAsset;
        if (!ActiveVisualDefinition.HurtVisualAsset.IsNull()) return ActiveVisualDefinition.HurtVisualAsset;
        break;

    case EAgentState::Incapacitated:
    case EAgentState::Recovering:
        if (!ActiveVisualDefinition.IncapacitatedVisualAsset.IsNull()) return ActiveVisualDefinition.IncapacitatedVisualAsset;
        if (!ActiveVisualDefinition.HurtVisualAsset.IsNull()) return ActiveVisualDefinition.HurtVisualAsset;
        if (!ActiveVisualDefinition.RestVisualAsset.IsNull()) return ActiveVisualDefinition.RestVisualAsset;
        break;

    case EAgentState::Sleeping:
        if (!ActiveVisualDefinition.SleepVisualAsset.IsNull()) return ActiveVisualDefinition.SleepVisualAsset;
        if (!ActiveVisualDefinition.RestVisualAsset.IsNull()) return ActiveVisualDefinition.RestVisualAsset;
        break;

    case EAgentState::Eating:
        if (!ActiveVisualDefinition.EatVisualAsset.IsNull()) return ActiveVisualDefinition.EatVisualAsset;
        break;

    case EAgentState::Socializing:
        if (!ActiveVisualDefinition.SocialVisualAsset.IsNull()) return ActiveVisualDefinition.SocialVisualAsset;
        break;

    case EAgentState::Fleeing:
        if (!ActiveVisualDefinition.FleeVisualAsset.IsNull()) return ActiveVisualDefinition.FleeVisualAsset;
        if (!ActiveVisualDefinition.WalkVisualAsset.IsNull()) return ActiveVisualDefinition.WalkVisualAsset;
        break;

    case EAgentState::Fighting:
        if (!ActiveVisualDefinition.FightVisualAsset.IsNull()) return ActiveVisualDefinition.FightVisualAsset;
        break;

    case EAgentState::Carrying:
        if (!ActiveVisualDefinition.HaulVisualAsset.IsNull()) return ActiveVisualDefinition.HaulVisualAsset;
        if (!ActiveVisualDefinition.CarryVisualAsset.IsNull()) return ActiveVisualDefinition.CarryVisualAsset;
        if (!ActiveVisualDefinition.WalkVisualAsset.IsNull()) return ActiveVisualDefinition.WalkVisualAsset;
        break;

    case EAgentState::Working:
    {
        const TSoftObjectPtr<UObject>& WorkAsset = SelectWorkVisualAssetByJob();
        if (!WorkAsset.IsNull()) return WorkAsset;
        break;
    }

    case EAgentState::MovingToTarget:
    case EAgentState::SearchingForJob:
    case EAgentState::ReservingTarget:
        if (!ActiveVisualDefinition.WalkVisualAsset.IsNull()) return ActiveVisualDefinition.WalkVisualAsset;
        break;

    case EAgentState::Idle:
    default:
        break;
    }

    if (!ActiveVisualDefinition.IdleVisualAsset.IsNull()) return ActiveVisualDefinition.IdleVisualAsset;
    if (!ActiveVisualDefinition.WorkVisualAsset.IsNull()) return ActiveVisualDefinition.WorkVisualAsset;
    if (!ActiveVisualDefinition.WalkVisualAsset.IsNull()) return ActiveVisualDefinition.WalkVisualAsset;
    return EmptyAsset;
}

bool AWorldForgeColonist::ApplyVisualAsset(const TSoftObjectPtr<UObject>& VisualAsset)
{
    UPaperFlipbook* Flipbook = LoadFlipbookFromSoftObject(VisualAsset);

    if (VisualFlipbook)
    {
        if (Flipbook)
        {
            VisualFlipbook->SetFlipbook(Flipbook);
            VisualFlipbook->SetLooping(true);
            VisualFlipbook->PlayFromStart();
            VisualFlipbook->SetHiddenInGame(false);
            VisualFlipbook->SetVisibility(true, true);
            VisualFlipbook->SetTranslucentSortPriority(100);
            UpdateFlipbookBillboardRotation();

            UE_LOG(
                LogTemp,
                Log,
                TEXT("[WorldForgeColonist] Applied flipbook for AgentID=%d Race=%s Row='%s' Flipbook=%s Loc=%s Rot=%s RelLoc=%s RelScale=%s Hidden=%d Visible=%d"),
                AgentID,
                bHasLastAgentData ? *LastAgentData.RaceTag.ToString() : TEXT("<unknown>"),
                *ActiveVisualDefinition.DisplayName.ToString(),
                *GetNameSafe(Flipbook),
                *VisualFlipbook->GetComponentLocation().ToString(),
                *VisualFlipbook->GetComponentRotation().ToString(),
                *VisualFlipbook->GetRelativeLocation().ToString(),
                *VisualFlipbook->GetRelativeScale3D().ToString(),
                VisualFlipbook->bHiddenInGame ? 1 : 0,
                VisualFlipbook->IsVisible() ? 1 : 0);
        }
        else
        {
            VisualFlipbook->SetFlipbook(nullptr);
            VisualFlipbook->SetHiddenInGame(true);
        }
    }

    if (SpriteVisual && Flipbook)
    {
        SpriteVisual->BroadcastFlipbookChanged(Flipbook);
    }

    UpdateDebugMeshVisibilityForCurrentVisual(Flipbook != nullptr);
    return Flipbook != nullptr;
}

void AWorldForgeColonist::UpdateDebugMeshVisibilityForCurrentVisual(bool bHasValidFlipbook)
{
    if (!DebugMesh)
    {
        return;
    }

    const bool bShowDebugMesh = bForceDebugMeshVisible || (ActiveVisualDefinition.bUseDebugMeshWhenNoAnimation && !bHasValidFlipbook);
    DebugMesh->SetHiddenInGame(!bShowDebugMesh);
    DebugMesh->SetVisibility(bShowDebugMesh, true);
}

bool AWorldForgeColonist::ApplyVisualForCurrentAgentState()
{
    const TSoftObjectPtr<UObject>& SelectedAsset = SelectVisualAssetForAgentState();
    const bool bApplied = ApplyVisualAsset(SelectedAsset);

    if (!bApplied)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("[WorldForgeColonist] No valid flipbook for AgentID=%d Race=%s Row='%s'. Using debug mesh fallback=%d"),
            AgentID,
            bHasLastAgentData ? *LastAgentData.RaceTag.ToString() : TEXT("<unknown>"),
            *ActiveVisualDefinition.DisplayName.ToString(),
            ActiveVisualDefinition.bUseDebugMeshWhenNoAnimation ? 1 : 0);
    }

    return bApplied;
}

void AWorldForgeColonist::UpdateMovement(float DeltaTime)
{
    const FVector CurrentLocation = GetActorLocation();

    if (CurrentLocation.Equals(TargetWorldPos, 1.0f))
    {
        return;
    }

    FVector DesiredLocation = FMath::VInterpConstantTo(
        CurrentLocation,
        TargetWorldPos,
        DeltaTime,
        VisualMoveSpeed
    );

    if (const UWorld* World = GetWorld())
    {
        FVector SnappedLocation = DesiredLocation;
        if (UWorldForgeTerrainSnapUtility::TryGetTerrainSnapLocation(
            const_cast<UWorld*>(World),
            DesiredLocation,
            SnappedLocation,
            TerrainZOffset))
        {
            DesiredLocation.Z = SnappedLocation.Z;
        }
    }

    SetActorLocation(DesiredLocation);
}