// Source/WorldForge/Grid/LayerViewSubsystem.cpp

#include "Grid/LayerViewSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogLayerView, Log, All);

void ULayerViewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    UE_LOG(LogLayerView, Log, TEXT("[LayerView] Initialized."));
}

void ULayerViewSubsystem::Deinitialize()
{
    ExitLayerView();
    Super::Deinitialize();
}

void ULayerViewSubsystem::EnterLayerView(EBuildToolMode InMode)
{
    bLayerViewActive = true;
    ActiveMode = InMode;

    UE_LOG(LogLayerView, Log,
        TEXT("[LayerView] Entered. Mode=%d ActiveZ=%d HasLayer=%s HeightMode=%d"),
        static_cast<int32>(ActiveMode),
        ActiveVoxelLayerZ,
        bHasActiveVoxelLayer ? TEXT("true") : TEXT("false"),
        static_cast<int32>(MiningHeightMode));
}

void ULayerViewSubsystem::ExitLayerView()
{
    bLayerViewActive = false;
    ActiveMode = EBuildToolMode::None;
    bHasSurfaceVoxelLayer = false;
    bHasActiveVoxelLayer = false;

    UE_LOG(LogLayerView, Log, TEXT("[LayerView] Exited."));
}

void ULayerViewSubsystem::SetSurfaceVoxelLayerZ(int32 InSurfaceVoxelZ)
{
    SurfaceVoxelLayerZ = FMath::Clamp(InSurfaceVoxelZ, MinVoxelLayerZ, MaxVoxelLayerZ);
    bHasSurfaceVoxelLayer = true;
}

void ULayerViewSubsystem::SetActiveVoxelLayerZ(int32 InVoxelZ)
{
    ActiveVoxelLayerZ = FMath::Clamp(InVoxelZ, MinVoxelLayerZ, MaxVoxelLayerZ);
    bHasActiveVoxelLayer = true;

    UE_LOG(LogLayerView, Log,
        TEXT("[LayerView] ActiveVoxelLayerZ=%d WorldZ=%.1f Relative=%+d"),
        ActiveVoxelLayerZ,
        GetActiveLayerWorldZ(),
        GetRelativeLayerOffset());
}

void ULayerViewSubsystem::InitializeFromSurfaceVoxelZ(int32 InSurfaceVoxelZ, int32 InitialOffset)
{
    SetSurfaceVoxelLayerZ(InSurfaceVoxelZ);
    SetActiveVoxelLayerZ(SurfaceVoxelLayerZ + InitialOffset);
}

void ULayerViewSubsystem::MoveLayerBy(int32 Delta)
{
    if (Delta == 0)
    {
        return;
    }

    if (!bHasActiveVoxelLayer)
    {
        SetActiveVoxelLayerZ(SurfaceVoxelLayerZ);
        return;
    }

    int32 DesiredVoxelZ = ActiveVoxelLayerZ + Delta;

    // Mining/dig layer view may descend into terrain, but it must never move
    // above the captured surface layer. Above-surface mine targets are empty air.
    if ((ActiveMode == EBuildToolMode::Mine || ActiveMode == EBuildToolMode::Dig) && bHasSurfaceVoxelLayer)
    {
        DesiredVoxelZ = FMath::Min(DesiredVoxelZ, SurfaceVoxelLayerZ);
    }

    SetActiveVoxelLayerZ(DesiredVoxelZ);
}

void ULayerViewSubsystem::ResetToSurfaceLayer()
{
    if (bHasSurfaceVoxelLayer)
    {
        SetActiveVoxelLayerZ(SurfaceVoxelLayerZ);
    }
    else
    {
        SetActiveVoxelLayerZ(0);
    }
}

void ULayerViewSubsystem::SetMiningHeightMode(EWFMiningHeightMode NewMode)
{
    MiningHeightMode = NewMode;

    UE_LOG(LogLayerView, Log,
        TEXT("[LayerView] MiningHeightMode=%d"),
        static_cast<int32>(MiningHeightMode));
}

EWFMiningHeightMode ULayerViewSubsystem::ToggleMiningHeightMode()
{
    MiningHeightMode = (MiningHeightMode == EWFMiningHeightMode::TwoVoxelColumn)
        ? EWFMiningHeightMode::SingleVoxel
        : EWFMiningHeightMode::TwoVoxelColumn;

    UE_LOG(LogLayerView, Log,
        TEXT("[LayerView] MiningHeightMode toggled to %d"),
        static_cast<int32>(MiningHeightMode));

    return MiningHeightMode;
}

FText ULayerViewSubsystem::GetMiningHeightModeText() const
{
    switch (MiningHeightMode)
    {
    case EWFMiningHeightMode::SingleVoxel:
        return FText::FromString(TEXT("Mine Height: 1 Voxel"));

    case EWFMiningHeightMode::TwoVoxelColumn:
    default:
        return FText::FromString(TEXT("Mine Height: 2 Block Column"));
    }
}
