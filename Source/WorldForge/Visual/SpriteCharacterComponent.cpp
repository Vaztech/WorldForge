#include "SpriteCharacterComponent.h"
#include "Math/RotationMatrix.h"
#include "Camera/CameraComponent.h" // CHANGE MADE:
// Used to read the actual world-space position of a CameraComponent
// on the assigned CameraTarget actor.

USpriteCharacterComponent::USpriteCharacterComponent()
{
    // Component ticks so it can billboard toward the active camera target.
    PrimaryComponentTick.bCanEverTick = true;
}

void USpriteCharacterComponent::BeginPlay()
{
    Super::BeginPlay();
}

void USpriteCharacterComponent::TickComponent(float DT, ELevelTick TT, FActorComponentTickFunction* TF)
{
    Super::TickComponent(DT, TT, TF);

    // Face the assigned camera target every tick.
    // This keeps the sprite presentation billboarded without polling simulation state.
    if (CameraTarget && GetOwner())
    {
        // Use the real camera viewpoint when possible instead of only the actor root location.
        FVector TargetLocation = CameraTarget->GetActorLocation();

        // Prefer the actual camera component location if one exists.
        if (const UCameraComponent* CameraComponent = CameraTarget->FindComponentByClass<UCameraComponent>())
        {
            TargetLocation = CameraComponent->GetComponentLocation();
        }
        else
        {
            // Fallback: use the actor's eye/viewpoint if supported.
            FVector ViewLocation = FVector::ZeroVector;
            FRotator ViewRotation = FRotator::ZeroRotator;
            CameraTarget->GetActorEyesViewPoint(ViewLocation, ViewRotation);

            // Only use the viewpoint if it returned a meaningful location.
            if (!ViewLocation.IsNearlyZero())
            {
                TargetLocation = ViewLocation;
            }
        }

        const FVector D = (TargetLocation - GetComponentLocation()).GetSafeNormal();

        // Guard against invalid zero-length direction vectors before calculating rotation.
        if (!D.IsNearlyZero())
        {
            // CHANGE MADE:
            // Use the configurable Blueprint-exposed yaw offset instead of a hardcoded value.
            // This keeps the camera-facing behavior flexible without changing architecture.
            const float Yaw = FRotationMatrix::MakeFromX(D).Rotator().Yaw + BillboardYawOffset;

            // Yaw-only billboard rotation keeps the sprite upright and readable.
            SetWorldRotation(FRotator(0.f, Yaw, 0.f));
        }
    }
}

void USpriteCharacterComponent::SetAnimStateFromAgentState(EAgentState S)
{
    // Cache the latest simulation state for debugging and inspection.
    CurrentAnimState = S;

    // Default animation state name.
    FName N = FName(TEXT("Idle"));

    // Map simulation state to the Blueprint-facing animation name key.
    switch (S)
    {
    case EAgentState::MovingToTarget:
        N = FName(TEXT("Walk"));
        break;

    case EAgentState::Working:
        N = FName(TEXT("Work"));
        break;

    case EAgentState::Carrying:
        // CHANGE MADE:
        // This previously mapped to "Walk", which prevented the Blueprint Carry branch
        // from ever executing. BP_SpriteCharacterComponent expects the exact anim name
        // "Carry", so we now map Carrying -> Carry.
        N = FName(TEXT("Carry"));
        break;

    case EAgentState::Eating:
        N = FName(TEXT("Eat"));
        break;

    case EAgentState::Sleeping:
        N = FName(TEXT("Sleep"));
        break;

    case EAgentState::Fighting:
        N = FName(TEXT("Fight"));
        break;

    case EAgentState::Incapacitated:
        N = FName(TEXT("Hurt"));
        break;

    case EAgentState::Dead:
        N = FName(TEXT("Dead"));
        break;

    default:
        break;
    }

    // Notify Blueprint so the component can resolve the final visual asset choice.
    OnSetAnimState(N);
}

void USpriteCharacterComponent::SetRace(FGameplayTag T)
{
    // Cache the latest race tag for debugging and inspection.
    CurrentRaceTag = T;

    // Notify Blueprint so the component can resolve the visual race key.
    OnRaceSet(T);
}

void USpriteCharacterComponent::BroadcastFlipbookChanged(UPaperFlipbook* NewFlipbook)
{
    // Blueprint-safe wrapper for broadcasting the selected flipbook.
    // This preserves the actor/component separation while avoiding direct
    // Blueprint calls to the multicast delegate property.
    OnFlipbookChanged.Broadcast(NewFlipbook);
}