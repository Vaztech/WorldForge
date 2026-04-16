#include "WorldForgeCamera.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Math/RotationMatrix.h"

AWorldForgeCamera::AWorldForgeCamera()
{
	PrimaryActorTick.bCanEverTick = true;

	DefaultSceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DefaultSceneRoot"));
	SetRootComponent(DefaultSceneRoot);

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(DefaultSceneRoot);
	CameraArm->bDoCollisionTest = false;

	ColonyCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("ColonyCamera"));
	ColonyCamera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	ColonyCamera->bUsePawnControlRotation = false;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AWorldForgeCamera::BeginPlay()
{
	Super::BeginPlay();

	TargetLocation = GetActorLocation();
	TargetZoom = DefaultArmLength;
	TargetYaw = GetActorRotation().Yaw;
	TargetPitch = -65.0f;

	CameraArm->bUsePawnControlRotation = false;
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->SetAbsolute(false, true, false);
	CameraArm->bInheritPitch = false;
	CameraArm->bInheritYaw = false;
	CameraArm->bInheritRoll = false;
	CameraArm->TargetArmLength = TargetZoom;
	CameraArm->SetWorldRotation(FRotator(TargetPitch, TargetYaw, 0.0f));

	if (ColonyCamera)
	{
		ColonyCamera->bUsePawnControlRotation = false;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
		{
			if (IMC_WorldForgeCamera)
			{
				Subsystem->AddMappingContext(IMC_WorldForgeCamera, 10);
				UE_LOG(LogTemp, Log, TEXT("[WorldForgeCamera] IMC_WorldForgeCamera added at priority 10."));
			}
		}
	}
}

void AWorldForgeCamera::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	{
		const float RawRotateAxis = GetRotateKeyAxisFromKeyboard();
		if (!FMath::IsNearlyZero(RawRotateAxis))
		{
			TargetYaw = FMath::UnwindDegrees(TargetYaw + (RawRotateAxis * RotationSpeedKeys * DeltaTime));
		}
	}

	const FVector NewLocation =
		FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, LocationLagSpeed);
	SetActorLocation(NewLocation);

	const FRotator ArmRot = CameraArm->GetComponentRotation();

	const float NewYaw =
		FMath::FixedTurn(ArmRot.Yaw, TargetYaw, RotationLagSpeed * 100.0f * DeltaTime);

	const float NewPitch =
		FMath::FInterpTo(ArmRot.Pitch, TargetPitch, DeltaTime, RotationLagSpeed);

	CameraArm->SetWorldRotation(FRotator(NewPitch, NewYaw, 0.0f));

	CameraArm->TargetArmLength =
		FMath::FInterpTo(CameraArm->TargetArmLength, TargetZoom, DeltaTime, ZoomLagSpeed);
}

void AWorldForgeCamera::CalcCamera(float DeltaTime, FMinimalViewInfo& OutResult)
{
	if (ColonyCamera)
	{
		ColonyCamera->GetCameraView(DeltaTime, OutResult);
		return;
	}

	Super::CalcCamera(DeltaTime, OutResult);
}

void AWorldForgeCamera::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		if (IA_Move)
		{
			EIC->BindAction(IA_Move, ETriggerEvent::Triggered, this, &AWorldForgeCamera::HandleMove);
		}

		if (IA_MousePan)
		{
			EIC->BindAction(IA_MousePan, ETriggerEvent::Triggered, this, &AWorldForgeCamera::HandleMousePan);
		}

		if (IA_Zoom)
		{
			EIC->BindAction(IA_Zoom, ETriggerEvent::Triggered, this, &AWorldForgeCamera::HandleZoom);
		}

		if (IA_RotateKeys)
		{
			EIC->BindAction(IA_RotateKeys, ETriggerEvent::Triggered, this, &AWorldForgeCamera::HandleRotateKeys);
		}
	}
}

void AWorldForgeCamera::SnapToRTSView(const FVector& NewLocation, float NewYaw, float NewPitch, float NewZoom, bool bApplyImmediately)
{
	TargetLocation = NewLocation;
	TargetYaw = FMath::UnwindDegrees(NewYaw);
	TargetPitch = FMath::Clamp(NewPitch, MinPitch, MaxPitch);
	TargetZoom = FMath::Clamp(NewZoom, ZoomMin, ZoomMax);

	if (bApplyImmediately)
	{
		SetActorLocation(TargetLocation);

		if (CameraArm)
		{
			CameraArm->TargetArmLength = TargetZoom;
			CameraArm->SetWorldRotation(FRotator(TargetPitch, TargetYaw, 0.0f));
		}

		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PC->SetControlRotation(FRotator(TargetPitch, TargetYaw, 0.0f));
		}
	}

	if (CameraArm && ColonyCamera)
	{
		const FVector ArmLoc = CameraArm->GetComponentLocation();
		const FRotator ArmRot = CameraArm->GetComponentRotation();
		const FVector CamLoc = ColonyCamera->GetComponentLocation();
		const FRotator CamRot = ColonyCamera->GetComponentRotation();

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[WorldForgeCamera] PostSnap ArmLoc=(%.1f, %.1f, %.1f) ArmRot=(%.1f, %.1f, %.1f) CamLoc=(%.1f, %.1f, %.1f) CamRot=(%.1f, %.1f, %.1f)"),
			ArmLoc.X, ArmLoc.Y, ArmLoc.Z,
			ArmRot.Pitch, ArmRot.Yaw, ArmRot.Roll,
			CamLoc.X, CamLoc.Y, CamLoc.Z,
			CamRot.Pitch, CamRot.Yaw, CamRot.Roll
		);
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[WorldForgeCamera] SnapToRTSView Location=(%.1f, %.1f, %.1f) Yaw=%.1f Pitch=%.1f Zoom=%.1f ApplyNow=%d"),
		TargetLocation.X,
		TargetLocation.Y,
		TargetLocation.Z,
		TargetYaw,
		TargetPitch,
		TargetZoom,
		bApplyImmediately ? 1 : 0
	);
}

void AWorldForgeCamera::HandleMove(const FInputActionValue& Value)
{
	const FVector2D Input = Value.Get<FVector2D>();
	const float DT = GetWorld()->GetDeltaSeconds();

	const FRotator YawOnly(0.0f, TargetYaw, 0.0f);
	const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

	TargetLocation += (Forward * Input.Y + Right * Input.X) * MoveSpeed * DT;
}

void AWorldForgeCamera::HandleMousePan(const FInputActionValue& Value)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC) return;

	const bool bIsPanningKey =
		PC->IsInputKeyDown(EKeys::MiddleMouseButton) || PC->IsInputKeyDown(EKeys::RightMouseButton);

	const bool bIsOrbitKey =
		PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);

	if (!bIsPanningKey) return;

	const FVector2D MouseDelta = Value.Get<FVector2D>();

	if (bIsOrbitKey)
	{
		TargetYaw = FMath::UnwindDegrees(TargetYaw + (MouseDelta.X * RotationSpeedMouse));
		TargetPitch = FMath::Clamp(TargetPitch + (MouseDelta.Y * -RotationSpeedMouse), MinPitch, MaxPitch);
	}
	else
	{
		const FRotator YawOnly(0.0f, TargetYaw, 0.0f);
		const FVector Forward = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawOnly).GetUnitAxis(EAxis::Y);

		const float PanX = bInvertPanX ? -MouseDelta.X : MouseDelta.X;
		const float PanY = bInvertPanY ? -MouseDelta.Y : MouseDelta.Y;

		const float ZoomMultiplier = FMath::Max(0.35f, TargetZoom / 1000.0f);

		TargetLocation += (Forward * PanY + Right * PanX) * (MousePanSpeed * ZoomMultiplier);
	}
}

void AWorldForgeCamera::HandleZoom(const FInputActionValue& Value)
{
	const float ScrollVal = Value.Get<float>();

	float DynamicZoomStep = ZoomStep * (TargetZoom / 1200.0f);
	DynamicZoomStep = FMath::Clamp(DynamicZoomStep, ZoomStep * 0.30f, ZoomStep * 5.0f);

	TargetZoom = FMath::Clamp(TargetZoom + (ScrollVal * -DynamicZoomStep), ZoomMin, ZoomMax);
}

void AWorldForgeCamera::HandleRotateKeys(const FInputActionValue& Value)
{
	const float AxisVal = Value.Get<float>();
	const float DT = GetWorld()->GetDeltaSeconds();

	if (FMath::IsNearlyZero(AxisVal))
	{
		return;
	}

	TargetYaw = FMath::UnwindDegrees(TargetYaw + (AxisVal * RotationSpeedKeys * DT));
}

float AWorldForgeCamera::GetRotateKeyAxisFromKeyboard() const
{
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return 0.0f;
	}

	float Axis = 0.0f;

	if (PC->IsInputKeyDown(EKeys::Q))
	{
		Axis -= 1.0f;
	}

	if (PC->IsInputKeyDown(EKeys::E))
	{
		Axis += 1.0f;
	}

	return Axis;
}