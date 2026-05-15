// Source/WorldForge/Grid/LayerViewSubsystem.h
//
// Single source of truth for active Z-layer viewing/editing.
// This subsystem is intentionally small: it owns the active absolute voxel layer,
// mining height mode, and world-Z conversion used by grid, cutaway, and mining.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Player/BuildModeTypes.h"
#include "Grid/LayerViewTypes.h"
#include "LayerViewSubsystem.generated.h"

UCLASS(BlueprintType)
class WORLDFORGE_API ULayerViewSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override { return true; }
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void EnterLayerView(EBuildToolMode InMode);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void ExitLayerView();

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    bool IsLayerViewActive() const { return bLayerViewActive; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    bool HasActiveVoxelLayer() const { return bHasActiveVoxelLayer; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    EBuildToolMode GetLayerViewMode() const { return ActiveMode; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void SetSurfaceVoxelLayerZ(int32 InSurfaceVoxelZ);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void SetActiveVoxelLayerZ(int32 InVoxelZ);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void InitializeFromSurfaceVoxelZ(int32 InSurfaceVoxelZ, int32 InitialOffset = 0);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void MoveLayerBy(int32 Delta);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView")
    void ResetToSurfaceLayer();

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    int32 GetSurfaceVoxelLayerZ() const { return SurfaceVoxelLayerZ; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    int32 GetActiveVoxelLayerZ() const { return ActiveVoxelLayerZ; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    int32 GetRelativeLayerOffset() const { return bHasSurfaceVoxelLayer ? ActiveVoxelLayerZ - SurfaceVoxelLayerZ : 0; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    float GetActiveLayerWorldZ() const { return static_cast<float>(ActiveVoxelLayerZ) * VoxelLayerHeightUU; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView")
    float VoxelLayerToWorldZ(int32 VoxelZ) const { return static_cast<float>(VoxelZ) * VoxelLayerHeightUU; }

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView|Mining")
    void SetMiningHeightMode(EWFMiningHeightMode NewMode);

    UFUNCTION(BlueprintCallable, Category = "WorldForge|LayerView|Mining")
    EWFMiningHeightMode ToggleMiningHeightMode();

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView|Mining")
    EWFMiningHeightMode GetMiningHeightMode() const { return MiningHeightMode; }

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView|Mining")
    FText GetMiningHeightModeText() const;

    UFUNCTION(BlueprintPure, Category = "WorldForge|LayerView|Mining")
    int32 GetMiningHeightVoxelCount() const { return MiningHeightMode == EWFMiningHeightMode::TwoVoxelColumn ? 2 : 1; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|LayerView|Config", meta = (ClampMin = "1.0"))
    float VoxelLayerHeightUU = 100.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|LayerView|Config", meta = (ClampMin = "1"))
    int32 MinVoxelLayerZ = -512;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "WorldForge|LayerView|Config", meta = (ClampMin = "1"))
    int32 MaxVoxelLayerZ = 512;

private:
    bool bLayerViewActive = false;
    bool bHasSurfaceVoxelLayer = false;
    bool bHasActiveVoxelLayer = false;

    EBuildToolMode ActiveMode = EBuildToolMode::None;
    int32 SurfaceVoxelLayerZ = 0;
    int32 ActiveVoxelLayerZ = 0;

    EWFMiningHeightMode MiningHeightMode = EWFMiningHeightMode::TwoVoxelColumn;
};
