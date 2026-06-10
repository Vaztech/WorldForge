#include "UI/WorldForgeMainMenuWidget.h"

#include "UI/WorldForgeEmbarkSetupWidget.h"
#include "UI/WorldForgeSaveSelectWidget.h"
#include "UI/WorldForgeSettingsWidget.h"

#include "Blueprint/UserWidget.h"
#include "CommonActivatableWidget.h"
#include "Components/Button.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"

namespace
{
constexpr const TCHAR* EmbarkSetupWidgetPath =
	TEXT("/Game/WorldForge/WidgetsBlueprints/WBP_WF_WorldConfig.WBP_WF_WorldConfig_C");
constexpr const TCHAR* SaveSelectWidgetPath =
	TEXT("/Game/WorldForge/WidgetsBlueprints/WBP_WF_SaveSelect.WBP_WF_SaveSelect_C");
constexpr const TCHAR* SettingsWidgetPath =
	TEXT("/Game/WorldForge/WidgetsBlueprints/WBP_WF_Settings.WBP_WF_Settings_C");
}

void UWorldForgeMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BTN_NewGame)
	{
		BTN_NewGame->OnClicked.RemoveDynamic(this, &UWorldForgeMainMenuWidget::HandleNewGameClicked);
		BTN_NewGame->OnClicked.AddDynamic(this, &UWorldForgeMainMenuWidget::HandleNewGameClicked);
	}

	if (BTN_Continue)
	{
		BTN_Continue->OnClicked.RemoveDynamic(this, &UWorldForgeMainMenuWidget::HandleContinueClicked);
		BTN_Continue->OnClicked.AddDynamic(this, &UWorldForgeMainMenuWidget::HandleContinueClicked);
	}

	if (BTN_Settings)
	{
		BTN_Settings->OnClicked.RemoveDynamic(this, &UWorldForgeMainMenuWidget::HandleSettingsClicked);
		BTN_Settings->OnClicked.AddDynamic(this, &UWorldForgeMainMenuWidget::HandleSettingsClicked);
	}

	if (BTN_Quit)
	{
		BTN_Quit->OnClicked.RemoveDynamic(this, &UWorldForgeMainMenuWidget::HandleQuitClicked);
		BTN_Quit->OnClicked.AddDynamic(this, &UWorldForgeMainMenuWidget::HandleQuitClicked);
	}

	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Constructed. BTN_NewGame=%s BTN_Continue=%s BTN_Settings=%s BTN_Quit=%s"),
		BTN_NewGame ? TEXT("OK") : TEXT("NULL"),
		BTN_Continue ? TEXT("OK") : TEXT("NULL"),
		BTN_Settings ? TEXT("OK") : TEXT("NULL"),
		BTN_Quit ? TEXT("OK") : TEXT("NULL"));
}

void UWorldForgeMainMenuWidget::HandleNewGameClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[MainMenu] New Game clicked."));

	if (!OpenEmbarkSetup())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] New Game: OpenEmbarkSetup failed."));
	}
}

void UWorldForgeMainMenuWidget::HandleContinueClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Continue clicked."));

	if (!OpenSaveSelect())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Continue: OpenSaveSelect failed."));
	}
}

void UWorldForgeMainMenuWidget::HandleSettingsClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Settings clicked."));

	if (!OpenSettings())
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Settings: OpenSettings failed."));
	}
}

void UWorldForgeMainMenuWidget::HandleQuitClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Quit clicked."));

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (PlayerController)
	{
		UKismetSystemLibrary::QuitGame(this, PlayerController, EQuitPreference::Quit, false);
	}
}

bool UWorldForgeMainMenuWidget::OpenEmbarkSetup()
{
	TSubclassOf<UUserWidget> SetupClass = EmbarkSetupWidgetClass;
	if (!SetupClass)
	{
		SetupClass = LoadClass<UUserWidget>(nullptr, EmbarkSetupWidgetPath);
	}

	if (!SetupClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] EmbarkSetup widget class not found at %s"), EmbarkSetupWidgetPath);
		return false;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Cannot open Embark Setup: no PlayerController."));
		return false;
	}

	UUserWidget* SetupWidget = CreateWidget<UUserWidget>(PlayerController, SetupClass);
	if (!SetupWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Failed to create Embark Setup widget."));
		return false;
	}

	// Pass self so EmbarkSetup's Back button can restore the main menu.
	if (UWorldForgeEmbarkSetupWidget* SetupBridge = Cast<UWorldForgeEmbarkSetupWidget>(SetupWidget))
	{
		SetupBridge->SetPreviousWidget(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MainMenu] EmbarkSetup is not parented to WorldForgeEmbarkSetupWidget; Back restore will not work."));
	}

	SetupWidget->AddToViewport(1000);

	if (UCommonActivatableWidget* CommonSetupWidget = Cast<UCommonActivatableWidget>(SetupWidget))
	{
		CommonSetupWidget->ActivateWidget();
	}

	// Hide self while Embark Setup is active.
	SetVisibility(ESlateVisibility::Collapsed);
	SetIsEnabled(false);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(SetupWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Embark Setup opened: %s"), *SetupWidget->GetName());
	return true;
}

bool UWorldForgeMainMenuWidget::OpenSettings()
{
	TSubclassOf<UUserWidget> WidgetClass = SettingsWidgetClass;
	if (!WidgetClass)
	{
		WidgetClass = LoadClass<UUserWidget>(nullptr, SettingsWidgetPath);
	}

	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Settings widget class not found at %s"), SettingsWidgetPath);
		return false;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Cannot open Settings: no PlayerController."));
		return false;
	}

	UUserWidget* SettingsWidget = CreateWidget<UUserWidget>(PlayerController, WidgetClass);
	if (!SettingsWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Failed to create Settings widget."));
		return false;
	}

	if (UWorldForgeSettingsWidget* SettingsBridge = Cast<UWorldForgeSettingsWidget>(SettingsWidget))
	{
		SettingsBridge->SetPreviousWidget(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MainMenu] Settings is not parented to WorldForgeSettingsWidget; Back restore will not work."));
	}

	SettingsWidget->AddToViewport(1000);

	if (UCommonActivatableWidget* CommonSettingsWidget = Cast<UCommonActivatableWidget>(SettingsWidget))
	{
		CommonSettingsWidget->ActivateWidget();
	}

	SetVisibility(ESlateVisibility::Collapsed);
	SetIsEnabled(false);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(SettingsWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Settings opened: %s"), *SettingsWidget->GetName());
	return true;
}


bool UWorldForgeMainMenuWidget::OpenSaveSelect()
{
	TSubclassOf<UUserWidget> SelectClass = SaveSelectWidgetClass;
	if (!SelectClass)
	{
		SelectClass = LoadClass<UUserWidget>(nullptr, SaveSelectWidgetPath);
	}

	if (!SelectClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Save Select widget class not found at %s"), SaveSelectWidgetPath);
		return false;
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Cannot open Save Select: no PlayerController."));
		return false;
	}

	UUserWidget* SaveSelectWidget = CreateWidget<UUserWidget>(PlayerController, SelectClass);
	if (!SaveSelectWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MainMenu] Failed to create Save Select widget."));
		return false;
	}

	if (UWorldForgeSaveSelectWidget* SaveSelectBridge = Cast<UWorldForgeSaveSelectWidget>(SaveSelectWidget))
	{
		SaveSelectBridge->SetPreviousWidget(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MainMenu] Save Select is not parented to WorldForgeSaveSelectWidget; C++ bindings are not active."));
	}

	SaveSelectWidget->AddToViewport(1000);

	if (UCommonActivatableWidget* CommonSaveSelectWidget = Cast<UCommonActivatableWidget>(SaveSelectWidget))
	{
		CommonSaveSelectWidget->ActivateWidget();
	}

	SetVisibility(ESlateVisibility::Collapsed);
	SetIsEnabled(false);

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(SaveSelectWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[MainMenu] Save Select opened: %s"), *SaveSelectWidget->GetName());
	return true;
}
