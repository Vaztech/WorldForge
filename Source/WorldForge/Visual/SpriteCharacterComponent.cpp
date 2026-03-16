#include "SpriteCharacterComponent.h"
#include "Math/RotationMatrix.h"

USpriteCharacterComponent::USpriteCharacterComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void USpriteCharacterComponent::BeginPlay()
{
    Super::BeginPlay();
}

void USpriteCharacterComponent::TickComponent(float DT, ELevelTick TT, FActorComponentTickFunction* TF)
{
    Super::TickComponent(DT, TT, TF);

    if (CameraTarget && GetOwner())
    {
        const FVector D = (CameraTarget->GetActorLocation() - GetComponentLocation()).GetSafeNormal();
        SetWorldRotation(FRotator(0.f, FRotationMatrix::MakeFromX(D).Rotator().Yaw, 0.f));
    }
}

void USpriteCharacterComponent::SetAnimStateFromAgentState(EAgentState S)
{
    CurrentAnimState = S;

    FName N = FName(TEXT("Idle"));

    switch (S)
    {
    case EAgentState::MovingToTarget: N = FName(TEXT("Walk")); break;
    case EAgentState::Working:        N = FName(TEXT("Work")); break;
    case EAgentState::Carrying:       N = FName(TEXT("Walk")); break;
    case EAgentState::Eating:         N = FName(TEXT("Eat")); break;
    case EAgentState::Sleeping:       N = FName(TEXT("Sleep")); break;
    case EAgentState::Fighting:       N = FName(TEXT("Fight")); break;
    case EAgentState::Incapacitated:  N = FName(TEXT("Hurt")); break;
    case EAgentState::Dead:           N = FName(TEXT("Dead")); break;
    default: break;
    }

    OnSetAnimState(N);
}

void USpriteCharacterComponent::SetRace(FGameplayTag T)
{
    CurrentRaceTag = T;
    OnRaceSet(T);
}