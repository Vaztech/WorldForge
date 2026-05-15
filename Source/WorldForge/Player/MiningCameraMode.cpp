// Source/WorldForge/Player/MiningCameraMode.cpp

#include "Player/MiningCameraMode.h"
#include "Player/WorldForgeCamera.h"

// =============================================================================
// Enter
// =============================================================================

void UMiningCameraMode::Enter(AWorldForgeCamera* Camera, float InSurfaceWorldZ)
{
    if (!Camera) return;

    if (bActive)
    {
        SurfaceWorldZ = InSurfaceWorldZ;
        UE_LOG(LogTemp, Verbose,
            TEXT("[MiningCamera] Enter re-called while active. Updating SurfaceWorldZ=%.1f."),
            SurfaceWorldZ);
        return;
    }

    SavedMinPitch = Camera->MinPitch;
    SavedMaxPitch = Camera->MaxPitch;
    SavedZoomMax  = Camera->ZoomMax;

    Camera->MinPitch = MiningMinPitch;
    Camera->MaxPitch = MiningMaxPitch;
    Camera->ZoomMax  = MiningZoomMax;

    SurfaceWorldZ = InSurfaceWorldZ;
    bActive       = true;

    // Mine mode is a controlled diorama-style view. If the player enters from a
    // far zoomed-out surface camera, immediately clamp the target zoom/pitch so
    // the diorama walls and cap layer do not break the illusion.
    Camera->ApplyMiningCameraView(MiningDefaultPitch, MiningDefaultZoom, false);

    UE_LOG(LogTemp, Log,
        TEXT("[MiningCamera] Entered mining camera mode. "
             "SurfaceZ=%.1f | ZoomMax %.1f→%.1f | Pitch [%.1f, %.1f]→[%.1f, %.1f]"),
        SurfaceWorldZ,
        SavedZoomMax,  MiningZoomMax,
        SavedMinPitch, SavedMaxPitch,
        MiningMinPitch, MiningMaxPitch);
}

// =============================================================================
// Exit
// =============================================================================

void UMiningCameraMode::Exit(AWorldForgeCamera* Camera)
{
    if (!Camera || !bActive) return;

    Camera->MinPitch = SavedMinPitch;
    Camera->MaxPitch = SavedMaxPitch;
    Camera->ZoomMax  = SavedZoomMax;

    bActive = false;

    UE_LOG(LogTemp, Log,
        TEXT("[MiningCamera] Exited. Camera constraints restored."));
}

// =============================================================================
// ComputeTargetWorldZ
//
// The camera stays at the surface.
// Only the clip plane (TerrainCutawaySubsystem) descends when you press [.
// This matches Going Medieval's behaviour: you look DOWN from surface level
// through the clip plane opening to see the underground layers below.
//
// Moving the camera down alongside the clip plane causes two problems:
//   1. The camera enters the solid terrain (black void, nothing to see).
//   2. The clip plane and camera move together, so the "reveal" is invisible.
//
// If you want a subtle camera dip (to tilt the view slightly into the cut),
// multiply ViewLayerOffset by a small fraction instead of zero:
//     return SurfaceWorldZ + (float(ViewLayerOffset) * LayerZOffsetUU * 0.15f);
// =============================================================================

float UMiningCameraMode::ComputeTargetWorldZ(int32 ViewLayerOffset) const
{
    // Camera stays at the surface. The clip plane does all the revealing work.
    return SurfaceWorldZ;
}
