#pragma once

#include "CoreMinimal.h"
#include "CommonActivatableWidget.h"
#include "WorldForgeMainMenuWidget.generated.h"

class UButton;
class UUserWidget;

/**
 * C++ bridge for WBP_WF_MainMenu.
 *
 * The Blueprint owns the layout while this class binds the nav buttons so the
 * Blueprint graph stays empty and fragile Blueprint wiring is avoided.
 */
UCLASS(BlueprintType, Blueprintable)
class WORLDFORGE_API UWorldForgeMainMenuWidget : public UCommonActivatableWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	/** Set in WBP_WF_MainMenu defaults. Falls back to the hardcoded path if unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|MainMenu|Flow")
	TSubclassOf<UUserWidget> EmbarkSetupWidgetClass;

	/** Set in WBP_WF_MainMenu defaults. Falls back to the hardcoded path if unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|MainMenu|Flow")
	TSubclassOf<UUserWidget> SaveSelectWidgetClass;

	/** Set in WBP_WF_MainMenu defaults. Falls back to the hardcoded path if unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "WorldForge|MainMenu|Flow")
	TSubclassOf<UUserWidget> SettingsWidgetClass;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_NewGame = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Continue = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Settings = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	UButton* BTN_Quit = nullptr;

private:
	UFUNCTION()
	void HandleNewGameClicked();

	UFUNCTION()
	void HandleContinueClicked();

	UFUNCTION()
	void HandleSettingsClicked();

	UFUNCTION()
	void HandleQuitClicked();

	bool OpenEmbarkSetup();
	bool OpenSaveSelect();
	bool OpenSettings();
};
