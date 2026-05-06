#include "UI/WFFrontendGameMode.h"

#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

AWFFrontendGameMode::AWFFrontendGameMode()
{
	// Frontend maps should not spawn gameplay pawns.
	DefaultPawnClass = nullptr;

	// Keep ticking off unless you later need animated frontend logic here.
	PrimaryActorTick.bCanEverTick = false;
}

void AWFFrontendGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	SpawnFrontendWidget(NewPlayer);
}

void AWFFrontendGameMode::SpawnFrontendWidget(APlayerController* PlayerController)
{
	if (!PlayerController)
	{
		UE_LOG(LogTemp, Error, TEXT("[FrontendGameMode] Cannot spawn frontend widget: PlayerController is null."));
		return;
	}

	if (!MainConfigWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[FrontendGameMode] MainConfigWidgetClass is not set."));
		return;
	}

	ActiveWidget = CreateWidget<UUserWidget>(PlayerController, MainConfigWidgetClass);

	if (!ActiveWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[FrontendGameMode] Failed to create frontend widget."));
		return;
	}

	ActiveWidget->AddToViewport(999);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(ActiveWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);

	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[FrontendGameMode] Frontend widget spawned: %s"), *ActiveWidget->GetName());
}