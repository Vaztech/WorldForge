#include "UI/WorldForgeSettingsWidget.h"

#include "WorldForgeUserSettingsSaveGame.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/PanelWidget.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/GenericWindow.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"

namespace
{
const TCHAR* const UserSettingsSaveSlotName = TEXT("WorldForge_UserSettings");
constexpr int32 UserSettingsSaveUserIndex = 0;

const TCHAR* const RuntimeSoundMixFallbackPaths[] =
{
	TEXT("/Game/Hyper/ResourcePack/Audio/Classes/SCM_Generic.SCM_Generic")
};

const TCHAR* const RuntimeMasterSoundClassFallbackPaths[] =
{
	TEXT("/Game/Hyper/ResourcePack/Audio/Classes/Overall.Overall"),
	TEXT("/Game/Necropolis/Audio/Sound_Classes/soundclass_master.soundclass_master")
};

const TCHAR* const RuntimeMusicSoundClassFallbackPaths[] =
{
	TEXT("/Game/Hyper/ResourcePack/Audio/Classes/Music.Music"),
	TEXT("/Game/Necropolis/Audio/Sound_Classes/soundclass_music.soundclass_music")
};

const TCHAR* const RuntimeSfxSoundClassFallbackPaths[] =
{
	TEXT("/Game/Hyper/ResourcePack/Audio/Classes/SFX.SFX")
};

const TCHAR* const RuntimeAmbienceSoundClassFallbackPaths[] =
{
	TEXT("/Game/Hyper/ResourcePack/Audio/Classes/Ambient.Ambient")
};

constexpr float MinTextScale = 0.75f;
constexpr float MaxTextScale = 1.50f;
constexpr float DefaultTextScale = 1.00f;
constexpr float DefaultAccessibilityTextScale = 1.00f;

const TCHAR* const DefaultResolutionLabel = TEXT("1920x1080");
const TCHAR* const CommonResolutionLabels[] =
{
	TEXT("1280x720"),
	TEXT("1600x900"),
	TEXT("1920x1080"),
	TEXT("2560x1440")
};

const TCHAR* const WindowModeWindowedLabel = TEXT("Windowed");
const TCHAR* const WindowModeBorderlessLabel = TEXT("Borderless");
const TCHAR* const WindowModeFullscreenLabel = TEXT("Fullscreen");

struct FSettingsControlDisplayRow
{
	const TCHAR* ActionLabel = TEXT("");
	const TCHAR* KeyLabel = TEXT("");
};

const FSettingsControlDisplayRow ReadOnlyControlDisplayRows[] =
{
	{ TEXT("Move Up"), TEXT("W") },
	{ TEXT("Move Down"), TEXT("S") },
	{ TEXT("Move Left"), TEXT("A") },
	{ TEXT("Move Right"), TEXT("D") },
	{ TEXT("Interact"), TEXT("Left Mouse Button") },
	{ TEXT("Open Build Menu"), TEXT("B") },
	{ TEXT("Pause/Menu"), TEXT("Space Bar") }
};

const TCHAR* BindingStatus(const UObject* Widget)
{
	return Widget ? TEXT("OK") : TEXT("NULL");
}

const TCHAR* BoolStatus(const bool bValue)
{
	return bValue ? TEXT("true") : TEXT("false");
}

float ClampTextScale(const float InValue)
{
	return FMath::Clamp(InValue, MinTextScale, MaxTextScale);
}

float ClampVolume(const float InValue)
{
	return FMath::Clamp(InValue, 0.0f, 1.0f);
}

FString MakeResolutionLabel(const FIntPoint& Resolution)
{
	return FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y);
}

bool TryParsePositiveInt32(const FString& InText, int32& OutValue)
{
	const FString TrimmedText = InText.TrimStartAndEnd();
	if (TrimmedText.IsEmpty())
	{
		return false;
	}

	for (const TCHAR Character : TrimmedText)
	{
		if (!FChar::IsDigit(Character))
		{
			return false;
		}
	}

	const int64 ParsedValue = FCString::Atoi64(*TrimmedText);
	if (ParsedValue <= 0 || ParsedValue > MAX_int32)
	{
		return false;
	}

	OutValue = static_cast<int32>(ParsedValue);
	return true;
}

bool TryParseResolutionLabel(const FString& InLabel, FIntPoint& OutResolution)
{
	FString WidthText;
	FString HeightText;
	if (!InLabel.TrimStartAndEnd().Split(TEXT("x"), &WidthText, &HeightText, ESearchCase::IgnoreCase))
	{
		return false;
	}

	int32 Width = 0;
	int32 Height = 0;
	if (!TryParsePositiveInt32(WidthText, Width) || !TryParsePositiveInt32(HeightText, Height))
	{
		return false;
	}

	OutResolution = FIntPoint(Width, Height);
	return true;
}

FString GetGameUserSettingsResolutionLabel()
{
	if (!GEngine)
	{
		return FString();
	}

	UGameUserSettings* const UserSettings = GEngine->GetGameUserSettings();
	if (!UserSettings)
	{
		return FString();
	}

	const FIntPoint Resolution = UserSettings->GetScreenResolution();
	return Resolution.X > 0 && Resolution.Y > 0 ? MakeResolutionLabel(Resolution) : FString();
}

FString GetDesktopResolutionLabel()
{
	if (!GEngine)
	{
		return FString();
	}

	UGameUserSettings* const UserSettings = GEngine->GetGameUserSettings();
	if (!UserSettings)
	{
		return FString();
	}

	const FIntPoint DesktopResolution = UserSettings->GetDesktopResolution();
	return DesktopResolution.X > 0 && DesktopResolution.Y > 0 ? MakeResolutionLabel(DesktopResolution) : FString();
}

FString GetDefaultResolutionLabel()
{
	const FString GameUserSettingsResolution = GetGameUserSettingsResolutionLabel();
	return GameUserSettingsResolution.IsEmpty() ? FString(DefaultResolutionLabel) : GameUserSettingsResolution;
}

void AddUniqueResolutionOption(UComboBoxString* const Dropdown, const FString& InResolutionLabel)
{
	if (!Dropdown)
	{
		return;
	}

	FIntPoint ParsedResolution;
	if (!TryParseResolutionLabel(InResolutionLabel, ParsedResolution))
	{
		return;
	}

	const FString NormalizedResolutionLabel = MakeResolutionLabel(ParsedResolution);
	if (Dropdown->FindOptionIndex(NormalizedResolutionLabel) == INDEX_NONE)
	{
		Dropdown->AddOption(NormalizedResolutionLabel);
	}
}

void ConfigureResolutionDropdown(UComboBoxString* const Dropdown, const FString& SelectedResolutionLabel)
{
	if (!Dropdown)
	{
		return;
	}

	Dropdown->ClearOptions();
	for (const TCHAR* const ResolutionLabel : CommonResolutionLabels)
	{
		AddUniqueResolutionOption(Dropdown, ResolutionLabel);
	}

	AddUniqueResolutionOption(Dropdown, GetGameUserSettingsResolutionLabel());
	AddUniqueResolutionOption(Dropdown, GetDesktopResolutionLabel());
	AddUniqueResolutionOption(Dropdown, SelectedResolutionLabel);
	Dropdown->RefreshOptions();

	FIntPoint ParsedResolution;
	const FString NormalizedSelection = TryParseResolutionLabel(SelectedResolutionLabel, ParsedResolution)
		? MakeResolutionLabel(ParsedResolution)
		: GetDefaultResolutionLabel();
	Dropdown->SetSelectedOption(NormalizedSelection);
}

bool TryNormalizePersistedWindowModeLabel(const FString& InLabel, FString& OutLabel)
{
	const FString TrimmedLabel = InLabel.TrimStartAndEnd();
	if (TrimmedLabel.Equals(WindowModeWindowedLabel, ESearchCase::IgnoreCase))
	{
		OutLabel = WindowModeWindowedLabel;
		return true;
	}

	if (TrimmedLabel.Equals(WindowModeBorderlessLabel, ESearchCase::IgnoreCase))
	{
		OutLabel = WindowModeBorderlessLabel;
		return true;
	}

	if (TrimmedLabel.Equals(WindowModeFullscreenLabel, ESearchCase::IgnoreCase))
	{
		OutLabel = WindowModeFullscreenLabel;
		return true;
	}

	return false;
}

FString NormalizeWindowModeLabel(const FString& InLabel)
{
	const FString TrimmedLabel = InLabel.TrimStartAndEnd();
	if (TrimmedLabel.Equals(WindowModeWindowedLabel, ESearchCase::IgnoreCase))
	{
		return WindowModeWindowedLabel;
	}

	if (TrimmedLabel.Equals(WindowModeFullscreenLabel, ESearchCase::IgnoreCase))
	{
		return WindowModeFullscreenLabel;
	}

	if (TrimmedLabel.Equals(WindowModeBorderlessLabel, ESearchCase::IgnoreCase) ||
		TrimmedLabel.Equals(TEXT("Windowed Fullscreen"), ESearchCase::IgnoreCase) ||
		TrimmedLabel.Equals(TEXT("WindowedFullscreen"), ESearchCase::IgnoreCase) ||
		TrimmedLabel.Equals(TEXT("Borderless Fullscreen"), ESearchCase::IgnoreCase))
	{
		return WindowModeBorderlessLabel;
	}

	return WindowModeBorderlessLabel;
}

EWindowMode::Type ToFullscreenMode(const FString& InWindowModeLabel)
{
	const FString NormalizedWindowModeLabel = NormalizeWindowModeLabel(InWindowModeLabel);
	if (NormalizedWindowModeLabel.Equals(WindowModeWindowedLabel, ESearchCase::CaseSensitive))
	{
		return EWindowMode::Windowed;
	}

	if (NormalizedWindowModeLabel.Equals(WindowModeFullscreenLabel, ESearchCase::CaseSensitive))
	{
		return EWindowMode::Fullscreen;
	}

	return EWindowMode::WindowedFullscreen;
}

void ConfigureWindowModeDropdown(UComboBoxString* const Dropdown)
{
	if (!Dropdown)
	{
		return;
	}

	Dropdown->ClearOptions();
	Dropdown->AddOption(WindowModeWindowedLabel);
	Dropdown->AddOption(WindowModeBorderlessLabel);
	Dropdown->AddOption(WindowModeFullscreenLabel);
	Dropdown->RefreshOptions();
}

float SanitizeFloatValue(
	const float InValue,
	const float DefaultValue,
	const float MinValue,
	const float MaxValue,
	const TCHAR* SettingName,
	const bool bLogPersistedWarnings)
{
	if (!FMath::IsFinite(InValue))
	{
		if (bLogPersistedWarnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Settings] Invalid persisted %s. Falling back to default."), SettingName);
		}

		return DefaultValue;
	}

	return FMath::Clamp(InValue, MinValue, MaxValue);
}

void SanitizeSettingsState(
	FWorldForgeSettingsState& SettingsState,
	const FWorldForgeSettingsState& DefaultSettings,
	const bool bLogPersistedWarnings)
{
	SettingsState.MasterVolume = SanitizeFloatValue(
		SettingsState.MasterVolume,
		DefaultSettings.MasterVolume,
		0.0f,
		1.0f,
		TEXT("MasterVolume"),
		bLogPersistedWarnings);
	SettingsState.MusicVolume = SanitizeFloatValue(
		SettingsState.MusicVolume,
		DefaultSettings.MusicVolume,
		0.0f,
		1.0f,
		TEXT("MusicVolume"),
		bLogPersistedWarnings);
	SettingsState.SfxVolume = SanitizeFloatValue(
		SettingsState.SfxVolume,
		DefaultSettings.SfxVolume,
		0.0f,
		1.0f,
		TEXT("SfxVolume"),
		bLogPersistedWarnings);
	SettingsState.AmbienceVolume = SanitizeFloatValue(
		SettingsState.AmbienceVolume,
		DefaultSettings.AmbienceVolume,
		0.0f,
		1.0f,
		TEXT("AmbienceVolume"),
		bLogPersistedWarnings);
	SettingsState.TextScale = SanitizeFloatValue(
		SettingsState.TextScale,
		DefaultSettings.TextScale,
		MinTextScale,
		MaxTextScale,
		TEXT("TextScale"),
		bLogPersistedWarnings);
	SettingsState.AccessibilityTextScale = SanitizeFloatValue(
		SettingsState.AccessibilityTextScale,
		DefaultSettings.AccessibilityTextScale,
		MinTextScale,
		MaxTextScale,
		TEXT("AccessibilityTextScale"),
		bLogPersistedWarnings);

	FIntPoint ParsedResolution;
	if (TryParseResolutionLabel(SettingsState.ResolutionLabel, ParsedResolution))
	{
		SettingsState.ResolutionLabel = MakeResolutionLabel(ParsedResolution);
	}
	else
	{
		if (bLogPersistedWarnings)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Settings] Invalid persisted resolution label. Falling back to default."));
		}

		FIntPoint DefaultResolution;
		SettingsState.ResolutionLabel = TryParseResolutionLabel(DefaultSettings.ResolutionLabel, DefaultResolution)
			? MakeResolutionLabel(DefaultResolution)
			: GetDefaultResolutionLabel();
	}

	FString NormalizedWindowModeLabel;
	if (bLogPersistedWarnings)
	{
		if (TryNormalizePersistedWindowModeLabel(SettingsState.WindowModeLabel, NormalizedWindowModeLabel))
		{
			SettingsState.WindowModeLabel = NormalizedWindowModeLabel;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[Settings] Invalid persisted window mode label. Falling back to default."));
			SettingsState.WindowModeLabel = NormalizeWindowModeLabel(DefaultSettings.WindowModeLabel);
		}
	}
	else
	{
		SettingsState.WindowModeLabel = NormalizeWindowModeLabel(SettingsState.WindowModeLabel);
	}
}

FWorldForgeSettingsState MakeSettingsStateFromSaveGame(
	const UWorldForgeUserSettingsSaveGame& SavedSettings,
	const FWorldForgeSettingsState& DefaultSettings)
{
	FWorldForgeSettingsState SettingsState = DefaultSettings;
	SettingsState.bAutosaveEnabled = SavedSettings.bAutosaveEnabled;
	SettingsState.bTutorialHintsEnabled = SavedSettings.bTutorialHintsEnabled;
	SettingsState.bPauseOnMenuEnabled = SavedSettings.bPauseOnMenuEnabled;
	SettingsState.MasterVolume = SavedSettings.MasterVolume;
	SettingsState.MusicVolume = SavedSettings.MusicVolume;
	SettingsState.SfxVolume = SavedSettings.SfxVolume;
	SettingsState.AmbienceVolume = SavedSettings.AmbienceVolume;
	SettingsState.bVSyncEnabled = SavedSettings.bVSyncEnabled;
	SettingsState.ResolutionLabel = SavedSettings.ResolutionLabel;
	SettingsState.WindowModeLabel = SavedSettings.WindowModeLabel;
	SettingsState.TextScale = SavedSettings.TextScale;
	SettingsState.bHighContrastMode = SavedSettings.bHighContrastMode;
	SettingsState.bColorblindAssist = SavedSettings.bColorblindAssist;
	SettingsState.bReduceMotion = SavedSettings.bReduceMotion;
	SettingsState.bLargeTextMode = SavedSettings.bLargeTextMode;
	SettingsState.AccessibilityTextScale = SavedSettings.AccessibilityTextScale;
	return SettingsState;
}

void CopySettingsStateToSaveGame(
	const FWorldForgeSettingsState& SettingsState,
	UWorldForgeUserSettingsSaveGame& SavedSettings)
{
	SavedSettings.SaveVersion = 2;
	SavedSettings.bAutosaveEnabled = SettingsState.bAutosaveEnabled;
	SavedSettings.bTutorialHintsEnabled = SettingsState.bTutorialHintsEnabled;
	SavedSettings.bPauseOnMenuEnabled = SettingsState.bPauseOnMenuEnabled;
	SavedSettings.MasterVolume = SettingsState.MasterVolume;
	SavedSettings.MusicVolume = SettingsState.MusicVolume;
	SavedSettings.SfxVolume = SettingsState.SfxVolume;
	SavedSettings.AmbienceVolume = SettingsState.AmbienceVolume;
	SavedSettings.bVSyncEnabled = SettingsState.bVSyncEnabled;
	SavedSettings.ResolutionLabel = SettingsState.ResolutionLabel;
	SavedSettings.WindowModeLabel = SettingsState.WindowModeLabel;
	SavedSettings.TextScale = SettingsState.TextScale;
	SavedSettings.bHighContrastMode = SettingsState.bHighContrastMode;
	SavedSettings.bColorblindAssist = SettingsState.bColorblindAssist;
	SavedSettings.bReduceMotion = SettingsState.bReduceMotion;
	SavedSettings.bLargeTextMode = SettingsState.bLargeTextMode;
	SavedSettings.AccessibilityTextScale = SettingsState.AccessibilityTextScale;
}

void ApplyVideoSettings(const FString& InResolutionLabel, const FString& InWindowModeLabel)
{
	const FString NormalizedWindowModeLabel = NormalizeWindowModeLabel(InWindowModeLabel);

	if (!GEngine)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply resolution: %s (GEngine unavailable)"), *InResolutionLabel);
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply window mode: %s (GEngine unavailable)"), *NormalizedWindowModeLabel);
		return;
	}

	UGameUserSettings* const UserSettings = GEngine->GetGameUserSettings();
	if (!UserSettings)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply resolution: %s (GameUserSettings unavailable)"), *InResolutionLabel);
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply window mode: %s (GameUserSettings unavailable)"), *NormalizedWindowModeLabel);
		return;
	}

	FIntPoint ParsedResolution;
	const bool bHasValidResolution = TryParseResolutionLabel(InResolutionLabel, ParsedResolution);
	if (bHasValidResolution)
	{
		UserSettings->SetScreenResolution(ParsedResolution);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply resolution: %s (parse failed)"), *InResolutionLabel);
	}

	UserSettings->SetFullscreenMode(ToFullscreenMode(NormalizedWindowModeLabel));
	UserSettings->ApplySettings(false);

	if (bHasValidResolution)
	{
		UE_LOG(LogTemp, Log, TEXT("[Settings] Applied resolution: %s"), *MakeResolutionLabel(ParsedResolution));
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Applied window mode: %s"), *NormalizedWindowModeLabel);
}

void ApplyVSyncSetting(UObject* WorldContextObject, const bool bEnableVSync)
{
	const int32 VSyncValue = bEnableVSync ? 1 : 0;

	if (IConsoleVariable* const VSyncConsoleVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		VSyncConsoleVariable->Set(VSyncValue);
		UE_LOG(LogTemp, Log, TEXT("[Settings] Applied VSync setting: %s"), BoolStatus(bEnableVSync));
		return;
	}

	if (WorldContextObject)
	{
		UKismetSystemLibrary::ExecuteConsoleCommand(
			WorldContextObject,
			FString::Printf(TEXT("r.VSync %d"), VSyncValue),
			nullptr);
		UE_LOG(LogTemp, Log, TEXT("[Settings] Applied VSync setting: %s"), BoolStatus(bEnableVSync));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to apply VSync setting: %s"), BoolStatus(bEnableVSync));
}

void ConfigureTextScaleSlider(USlider* const TextScaleSlider)
{
	if (!TextScaleSlider)
	{
		return;
	}

	TextScaleSlider->SetMinValue(MinTextScale);
	TextScaleSlider->SetMaxValue(MaxTextScale);
}

void LogMissingWidget(const TCHAR* WidgetName)
{
	UE_LOG(LogTemp, Warning, TEXT("[Settings] Missing optional widget: %s"), WidgetName);
}

FString GetTrimmedWidgetText(const UTextBlock* TextWidget)
{
	return TextWidget ? TextWidget->GetText().ToString().TrimStartAndEnd() : FString();
}

FString MakeSafeWidgetNameFragment(const TCHAR* InLabel)
{
	FString SafeName;
	if (InLabel)
	{
		for (const TCHAR Character : FString(InLabel))
		{
			if (FChar::IsAlpha(Character) || FChar::IsDigit(Character))
			{
				SafeName.AppendChar(Character);
			}
		}
	}

	return SafeName.IsEmpty() ? FString(TEXT("Setting")) : SafeName;
}

UTextBlock* CreateRuntimeTextBlock(UObject* const Outer, const FName WidgetName, const FText& Text)
{
	UTextBlock* const TextBlock = NewObject<UTextBlock>(Outer, UTextBlock::StaticClass(), WidgetName);
	if (TextBlock)
	{
		TextBlock->SetText(Text);
		TextBlock->SetAutoWrapText(false);
	}

	return TextBlock;
}

UButton* AddControlDisplayRow(UObject* const Outer, UPanelWidget* const ParentPanel, const FSettingsControlDisplayRow& DisplayRow)
{
	if (!Outer || !ParentPanel)
	{
		return nullptr;
	}

	const FString NameFragment = MakeSafeWidgetNameFragment(DisplayRow.ActionLabel);
	UHorizontalBox* const Row = NewObject<UHorizontalBox>(
		Outer,
		UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("HB_Control_%s_Row"), *NameFragment)));
	if (!Row || !ParentPanel->AddChild(Row))
	{
		return nullptr;
	}

	UTextBlock* const ActionText = CreateRuntimeTextBlock(
		Outer,
		FName(*FString::Printf(TEXT("TXT_Control_%s_Action"), *NameFragment)),
		FText::FromString(FString(DisplayRow.ActionLabel)));
	if (ActionText)
	{
		Row->AddChild(ActionText);
	}

	UTextBlock* const KeyText = CreateRuntimeTextBlock(
		Outer,
		FName(*FString::Printf(TEXT("TXT_Control_%s_Key"), *NameFragment)),
		FText::FromString(FString(DisplayRow.KeyLabel)));
	if (KeyText)
	{
		Row->AddChild(KeyText);
	}

	UButton* const Button = NewObject<UButton>(
		Outer,
		UButton::StaticClass(),
		FName(*FString::Printf(TEXT("BTN_Rebind_%s_Placeholder"), *NameFragment)));
	if (!Button)
	{
		return nullptr;
	}

	Button->SetToolTipText(FText::FromString(TEXT("Rebinding not implemented yet")));
	UTextBlock* const ButtonText = CreateRuntimeTextBlock(
		Button,
		FName(*FString::Printf(TEXT("TXT_Rebind_%s_Button"), *NameFragment)),
		FText::FromString(TEXT("Coming Soon")));
	if (ButtonText)
	{
		Button->SetContent(ButtonText);
	}

	Row->AddChild(Button);
	return Button;
}

UCheckBox* AddAccessibilityCheckboxRow(
	UObject* const Outer,
	UPanelWidget* const ParentPanel,
	const TCHAR* const SettingName,
	const FText& LabelText)
{
	if (!Outer || !ParentPanel)
	{
		return nullptr;
	}

	const FString NameFragment = MakeSafeWidgetNameFragment(SettingName);
	UHorizontalBox* const Row = NewObject<UHorizontalBox>(
		Outer,
		UHorizontalBox::StaticClass(),
		FName(*FString::Printf(TEXT("HB_%s_Row"), *NameFragment)));
	if (!Row || !ParentPanel->AddChild(Row))
	{
		return nullptr;
	}

	UTextBlock* const Label = CreateRuntimeTextBlock(
		Outer,
		FName(*FString::Printf(TEXT("TXT_%s_Label"), *NameFragment)),
		LabelText);
	if (Label)
	{
		Row->AddChild(Label);
	}

	UCheckBox* const Checkbox = NewObject<UCheckBox>(
		Outer,
		UCheckBox::StaticClass(),
		FName(*FString::Printf(TEXT("CHK_%s_Runtime"), *NameFragment)));
	if (Checkbox)
	{
		Row->AddChild(Checkbox);
	}

	return Checkbox;
}

UCheckBox* AddAccessibilityCheckboxToExistingRow(
	UObject* const Outer,
	UTextBlock* const PlaceholderText,
	const TCHAR* const SettingName)
{
	if (!Outer || !PlaceholderText)
	{
		return nullptr;
	}

	UPanelWidget* const ParentPanel = PlaceholderText->GetParent();
	if (!ParentPanel)
	{
		return nullptr;
	}

	PlaceholderText->SetText(FText::FromString(TEXT("Off")));
	const FString NameFragment = MakeSafeWidgetNameFragment(SettingName);
	UCheckBox* const Checkbox = NewObject<UCheckBox>(
		Outer,
		UCheckBox::StaticClass(),
		FName(*FString::Printf(TEXT("CHK_%s_Runtime"), *NameFragment)));
	if (!Checkbox || !ParentPanel->AddChild(Checkbox))
	{
		return nullptr;
	}

	return Checkbox;
}

template <typename TObjectType, uint32 NumFallbackPaths>
TObjectType* LoadOptionalAsset(const TSoftObjectPtr<TObjectType>& ConfiguredAsset, const TCHAR* const (&FallbackPaths)[NumFallbackPaths])
{
	if (!ConfiguredAsset.IsNull())
	{
		if (TObjectType* LoadedConfiguredAsset = ConfiguredAsset.LoadSynchronous())
		{
			return LoadedConfiguredAsset;
		}
	}

	for (const TCHAR* AssetPath : FallbackPaths)
	{
		if (!AssetPath || AssetPath[0] == TEXT('\0'))
		{
			continue;
		}

		if (TObjectType* LoadedFallbackAsset = Cast<TObjectType>(StaticLoadObject(TObjectType::StaticClass(), nullptr, AssetPath)))
		{
			return LoadedFallbackAsset;
		}
	}

	return nullptr;
}
}

void UWorldForgeSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (BTN_Back)
	{
		BTN_Back->OnClicked.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleBackClicked);
		BTN_Back->OnClicked.AddDynamic(this, &UWorldForgeSettingsWidget::HandleBackClicked);
	}

	if (BTN_Apply)
	{
		BTN_Apply->OnClicked.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleApplyClicked);
		BTN_Apply->OnClicked.AddDynamic(this, &UWorldForgeSettingsWidget::HandleApplyClicked);
	}

	if (BTN_Reset)
	{
		BTN_Reset->OnClicked.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleResetClicked);
		BTN_Reset->OnClicked.AddDynamic(this, &UWorldForgeSettingsWidget::HandleResetClicked);
	}

	EnsureResolutionDropdown();
	EnsureControlsFoundationWidgets();
	EnsureAccessibilityFoundationWidgets();
	BindSettingsWidgets();
	LogMissingSettingsWidgets();
	InitializeSettingsState();

	UE_LOG(LogTemp, Log, TEXT("[Settings] Constructed. BTN_Back=%s BTN_Apply=%s BTN_Reset=%s"),
		BindingStatus(BTN_Back), BindingStatus(BTN_Apply), BindingStatus(BTN_Reset));
}

void UWorldForgeSettingsWidget::SetPreviousWidget(UUserWidget* InWidget)
{
	PreviousWidget = InWidget;
}

void UWorldForgeSettingsWidget::HandleBackClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[Settings] Back clicked. Returning to Main Menu."));
	RestorePreviousWidget();
	DeactivateWidget();
	RemoveFromParent();
}

void UWorldForgeSettingsWidget::HandleApplyClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[Settings] Apply clicked."));
	SanitizeSettingsState(PendingSettings, MakeDefaultSettings(), false);

	CurrentSettings = PendingSettings;
	ApplyVideoSettings(CurrentSettings.ResolutionLabel, CurrentSettings.WindowModeLabel);
	ApplyVSyncSetting(this, CurrentSettings.bVSyncEnabled);

	// TODO: Route TextScale through a project-owned UI scale subsystem once one exists.
	UE_LOG(LogTemp, Log, TEXT("[Settings] Applied text/UI scale: %.2f"), CurrentSettings.TextScale);
	ApplyAccessibilitySettings(CurrentSettings);
	ApplyAudioSettings(CurrentSettings);
	SavePersistedSettings();
}

void UWorldForgeSettingsWidget::HandleResetClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[Settings] Reset clicked."));
	PendingSettings = CurrentSettings;
	RefreshUIFromPendingSettings();
	UE_LOG(LogTemp, Log, TEXT("[Settings] Reset pending settings to current values."));
}

void UWorldForgeSettingsWidget::RestorePreviousWidget()
{
	UUserWidget* Widget = PreviousWidget.Get();
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] No previous widget registered; nothing to restore."));
		return;
	}

	Widget->SetVisibility(ESlateVisibility::Visible);
	Widget->SetIsEnabled(true);

	if (UCommonActivatableWidget* CommonWidget = Cast<UCommonActivatableWidget>(Widget))
	{
		CommonWidget->ActivateWidget();
	}

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (PlayerController)
	{
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(Widget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PlayerController->SetInputMode(InputMode);
		PlayerController->bShowMouseCursor = true;
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Restored previous widget: %s"), *Widget->GetName());
}

void UWorldForgeSettingsWidget::HandleAutosaveChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bAutosaveEnabled = bIsChecked;
}

void UWorldForgeSettingsWidget::HandleTutorialHintsChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bTutorialHintsEnabled = bIsChecked;
}

void UWorldForgeSettingsWidget::HandlePauseOnMenuChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bPauseOnMenuEnabled = bIsChecked;
}

void UWorldForgeSettingsWidget::HandleVSyncChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bVSyncEnabled = bIsChecked;
	UE_LOG(LogTemp, Log, TEXT("[Settings] VSync pending changed: %s"), BoolStatus(PendingSettings.bVSyncEnabled));
}

void UWorldForgeSettingsWidget::HandleMasterVolumeChanged(const float InValue)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.MasterVolume = ClampVolume(InValue);
	UE_LOG(LogTemp, Log, TEXT("[Settings] Master volume pending changed: %.2f"), PendingSettings.MasterVolume);
}

void UWorldForgeSettingsWidget::HandleMusicVolumeChanged(const float InValue)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.MusicVolume = ClampVolume(InValue);
	UE_LOG(LogTemp, Log, TEXT("[Settings] Music volume pending changed: %.2f"), PendingSettings.MusicVolume);
}

void UWorldForgeSettingsWidget::HandleSfxVolumeChanged(const float InValue)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.SfxVolume = ClampVolume(InValue);
	UE_LOG(LogTemp, Log, TEXT("[Settings] SFX volume pending changed: %.2f"), PendingSettings.SfxVolume);
}

void UWorldForgeSettingsWidget::HandleAmbienceVolumeChanged(const float InValue)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.AmbienceVolume = ClampVolume(InValue);
	UE_LOG(LogTemp, Log, TEXT("[Settings] Ambience volume pending changed: %.2f"), PendingSettings.AmbienceVolume);
}

void UWorldForgeSettingsWidget::HandleTextScaleChanged(const float InValue)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.TextScale = ClampTextScale(InValue);
	PendingSettings.AccessibilityTextScale = PendingSettings.TextScale;
	UE_LOG(LogTemp, Log, TEXT("[Settings] Text scale pending changed: %.2f"), PendingSettings.TextScale);
}

void UWorldForgeSettingsWidget::HandleHighContrastChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bHighContrastMode = bIsChecked;
	UE_LOG(LogTemp, Log, TEXT("[Settings] High contrast pending changed: %s"), BoolStatus(PendingSettings.bHighContrastMode));
}

void UWorldForgeSettingsWidget::HandleColorblindAssistChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bColorblindAssist = bIsChecked;
	if (TXT_ColorblindModePlaceholder)
	{
		TXT_ColorblindModePlaceholder->SetText(FText::FromString(PendingSettings.bColorblindAssist ? TEXT("On") : TEXT("Off")));
	}
	UE_LOG(LogTemp, Log, TEXT("[Settings] Colorblind assist pending changed: %s"), BoolStatus(PendingSettings.bColorblindAssist));
}

void UWorldForgeSettingsWidget::HandleReduceMotionChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bReduceMotion = bIsChecked;
	UE_LOG(LogTemp, Log, TEXT("[Settings] Reduce motion pending changed: %s"), BoolStatus(PendingSettings.bReduceMotion));
}

void UWorldForgeSettingsWidget::HandleLargeTextModeChanged(const bool bIsChecked)
{
	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.bLargeTextMode = bIsChecked;
	UE_LOG(LogTemp, Log, TEXT("[Settings] Large text mode pending changed: %s"), BoolStatus(PendingSettings.bLargeTextMode));
}

void UWorldForgeSettingsWidget::HandleRebindPlaceholderClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[Settings] Key rebinding not implemented yet."));
}

void UWorldForgeSettingsWidget::HandleResolutionSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectionType;

	if (bIsRefreshingUI)
	{
		return;
	}

	FIntPoint ParsedResolution;
	if (!TryParseResolutionLabel(SelectedItem, ParsedResolution))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Ignored invalid resolution selection: %s"), *SelectedItem);
		return;
	}

	PendingSettings.ResolutionLabel = MakeResolutionLabel(ParsedResolution);

	if (TXT_ResolutionPlaceholder)
	{
		TXT_ResolutionPlaceholder->SetText(FText::FromString(PendingSettings.ResolutionLabel));
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Resolution pending changed: %s"), *PendingSettings.ResolutionLabel);
}

void UWorldForgeSettingsWidget::HandleWindowModeSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	(void)SelectionType;

	if (bIsRefreshingUI)
	{
		return;
	}

	PendingSettings.WindowModeLabel = NormalizeWindowModeLabel(SelectedItem);

	if (TXT_WindowModePlaceholder)
	{
		TXT_WindowModePlaceholder->SetText(FText::FromString(PendingSettings.WindowModeLabel));
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Window mode pending changed: %s"), *PendingSettings.WindowModeLabel);
}

void UWorldForgeSettingsWidget::EnsureResolutionDropdown()
{
	if (ResolutionDropdown)
	{
		return;
	}

	if (!TXT_ResolutionPlaceholder)
	{
		return;
	}

	UPanelWidget* const ParentPanel = TXT_ResolutionPlaceholder->GetParent();
	if (!ParentPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] ResolutionDropdown could not be created: TXT_ResolutionPlaceholder has no parent panel."));
		return;
	}

	ResolutionDropdown = NewObject<UComboBoxString>(this, UComboBoxString::StaticClass(), FName(TEXT("ResolutionDropdown")));
	if (!ResolutionDropdown)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] ResolutionDropdown could not be created."));
		return;
	}

	if (ParentPanel->AddChild(ResolutionDropdown))
	{
		TXT_ResolutionPlaceholder->SetVisibility(ESlateVisibility::Collapsed);
		ResolutionDropdown->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("[Settings] Created ResolutionDropdown beside TXT_ResolutionPlaceholder."));
		return;
	}

	const bool bRemovedPlaceholder = ParentPanel->RemoveChild(TXT_ResolutionPlaceholder);
	if (!bRemovedPlaceholder || !ParentPanel->AddChild(ResolutionDropdown))
	{
		if (bRemovedPlaceholder)
		{
			ParentPanel->AddChild(TXT_ResolutionPlaceholder);
		}

		UE_LOG(LogTemp, Warning, TEXT("[Settings] ResolutionDropdown could not be added near TXT_ResolutionPlaceholder."));
		ResolutionDropdown = nullptr;
		return;
	}

	ResolutionDropdown->SetVisibility(ESlateVisibility::Visible);
	UE_LOG(LogTemp, Log, TEXT("[Settings] Replaced TXT_ResolutionPlaceholder with ResolutionDropdown."));
}

void UWorldForgeSettingsWidget::EnsureControlsFoundationWidgets()
{
	if (bControlsFoundationCreated)
	{
		return;
	}

	if (TXT_KeybindsPlaceholder)
	{
		TXT_KeybindsPlaceholder->SetText(FText::FromString(TEXT("Rebinding not implemented yet")));
	}

	if (!VB_ControlsSection)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Controls foundation skipped: VB_ControlsSection is missing."));
		bControlsFoundationCreated = true;
		return;
	}

	RebindPlaceholderButtons.Reset();
	for (const FSettingsControlDisplayRow& DisplayRow : ReadOnlyControlDisplayRows)
	{
		if (UButton* const RebindButton = AddControlDisplayRow(this, VB_ControlsSection, DisplayRow))
		{
			RebindPlaceholderButtons.Add(RebindButton);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Controls display initialized with read-only default mappings; runtime rebinding is placeholder-only."));
	bControlsFoundationCreated = true;
}

void UWorldForgeSettingsWidget::EnsureAccessibilityFoundationWidgets()
{
	if (bAccessibilityFoundationCreated)
	{
		return;
	}

	if (!CHK_ReduceMotion && CHK_ReduceFlashingPlaceholder)
	{
		CHK_ReduceMotion = CHK_ReduceFlashingPlaceholder;
	}

	if (!CHK_ColorblindAssist)
	{
		CHK_ColorblindAssist = AddAccessibilityCheckboxToExistingRow(this, TXT_ColorblindModePlaceholder, TEXT("ColorblindAssist"));
	}

	if (!CHK_HighContrastMode)
	{
		CHK_HighContrastMode = AddAccessibilityCheckboxRow(
			this,
			VB_AccessibilitySection,
			TEXT("HighContrastMode"),
			FText::FromString(TEXT("High Contrast Mode")));
	}

	if (!CHK_ReduceMotion)
	{
		CHK_ReduceMotion = AddAccessibilityCheckboxRow(
			this,
			VB_AccessibilitySection,
			TEXT("ReduceMotion"),
			FText::FromString(TEXT("Reduce Motion")));
	}

	if (!CHK_LargeTextMode)
	{
		CHK_LargeTextMode = AddAccessibilityCheckboxRow(
			this,
			VB_AccessibilitySection,
			TEXT("LargeTextMode"),
			FText::FromString(TEXT("Large Text Mode")));
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Accessibility foundation initialized: HighContrast=%s Colorblind=%s ReduceMotion=%s LargeText=%s"),
		BindingStatus(CHK_HighContrastMode),
		BindingStatus(CHK_ColorblindAssist),
		BindingStatus(GetReduceMotionCheckbox()),
		BindingStatus(CHK_LargeTextMode));
	bAccessibilityFoundationCreated = true;
}

void UWorldForgeSettingsWidget::BindSettingsWidgets()
{
	if (CHK_AutosavePlaceholder)
	{
		CHK_AutosavePlaceholder->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleAutosaveChanged);
		CHK_AutosavePlaceholder->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleAutosaveChanged);
	}

	if (CHK_TutorialHintsPlaceholder)
	{
		CHK_TutorialHintsPlaceholder->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleTutorialHintsChanged);
		CHK_TutorialHintsPlaceholder->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleTutorialHintsChanged);
	}

	if (CHK_PauseOnMenuPlaceholder)
	{
		CHK_PauseOnMenuPlaceholder->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandlePauseOnMenuChanged);
		CHK_PauseOnMenuPlaceholder->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandlePauseOnMenuChanged);
	}

	if (CHK_VSyncPlaceholder)
	{
		CHK_VSyncPlaceholder->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleVSyncChanged);
		CHK_VSyncPlaceholder->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleVSyncChanged);
	}

	if (ResolutionDropdown)
	{
		ResolutionDropdown->OnSelectionChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleResolutionSelectionChanged);
		ResolutionDropdown->OnSelectionChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleResolutionSelectionChanged);
	}

	if (WindowModeDropdown)
	{
		WindowModeDropdown->OnSelectionChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleWindowModeSelectionChanged);
		WindowModeDropdown->OnSelectionChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleWindowModeSelectionChanged);
	}

	if (SLD_MasterVolume)
	{
		SLD_MasterVolume->OnValueChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleMasterVolumeChanged);
		SLD_MasterVolume->OnValueChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleMasterVolumeChanged);
	}

	if (SLD_MusicVolume)
	{
		SLD_MusicVolume->OnValueChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleMusicVolumeChanged);
		SLD_MusicVolume->OnValueChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleMusicVolumeChanged);
	}

	if (SLD_SFXVolume)
	{
		SLD_SFXVolume->OnValueChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleSfxVolumeChanged);
		SLD_SFXVolume->OnValueChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleSfxVolumeChanged);
	}

	if (SLD_AmbienceVolume)
	{
		SLD_AmbienceVolume->OnValueChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleAmbienceVolumeChanged);
		SLD_AmbienceVolume->OnValueChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleAmbienceVolumeChanged);
	}

	if (SLD_TextSize)
	{
		ConfigureTextScaleSlider(SLD_TextSize);
		SLD_TextSize->OnValueChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleTextScaleChanged);
		SLD_TextSize->OnValueChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleTextScaleChanged);
	}

	if (CHK_HighContrastMode)
	{
		CHK_HighContrastMode->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleHighContrastChanged);
		CHK_HighContrastMode->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleHighContrastChanged);
	}

	if (CHK_ColorblindAssist)
	{
		CHK_ColorblindAssist->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleColorblindAssistChanged);
		CHK_ColorblindAssist->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleColorblindAssistChanged);
	}

	if (UCheckBox* const ReduceMotionCheckbox = GetReduceMotionCheckbox())
	{
		ReduceMotionCheckbox->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleReduceMotionChanged);
		ReduceMotionCheckbox->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleReduceMotionChanged);
	}

	if (CHK_LargeTextMode)
	{
		CHK_LargeTextMode->OnCheckStateChanged.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleLargeTextModeChanged);
		CHK_LargeTextMode->OnCheckStateChanged.AddDynamic(this, &UWorldForgeSettingsWidget::HandleLargeTextModeChanged);
	}

	for (UButton* const RebindButton : RebindPlaceholderButtons)
	{
		if (!RebindButton)
		{
			continue;
		}

		RebindButton->OnClicked.RemoveDynamic(this, &UWorldForgeSettingsWidget::HandleRebindPlaceholderClicked);
		RebindButton->OnClicked.AddDynamic(this, &UWorldForgeSettingsWidget::HandleRebindPlaceholderClicked);
	}
}

void UWorldForgeSettingsWidget::InitializeSettingsState()
{
	CurrentSettings = MakeDefaultSettings();
	LoadPersistedSettings();
	PendingSettings = CurrentSettings;
	RefreshUIFromPendingSettings();
}

void UWorldForgeSettingsWidget::LoadPersistedSettings()
{
	const FWorldForgeSettingsState DefaultSettings = CurrentSettings;

	if (!UGameplayStatics::DoesSaveGameExist(UserSettingsSaveSlotName, UserSettingsSaveUserIndex))
	{
		SanitizeSettingsState(CurrentSettings, DefaultSettings, false);
		UE_LOG(LogTemp, Log, TEXT("[Settings] No persisted settings found. Using defaults."));
		return;
	}

	USaveGame* const LoadedSave = UGameplayStatics::LoadGameFromSlot(UserSettingsSaveSlotName, UserSettingsSaveUserIndex);
	const UWorldForgeUserSettingsSaveGame* const PersistedSettings = Cast<UWorldForgeUserSettingsSaveGame>(LoadedSave);
	if (!PersistedSettings)
	{
		SanitizeSettingsState(CurrentSettings, DefaultSettings, false);
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Persisted settings could not be loaded. Using defaults."));
		return;
	}

	CurrentSettings = MakeSettingsStateFromSaveGame(*PersistedSettings, DefaultSettings);
	SanitizeSettingsState(CurrentSettings, DefaultSettings, true);

	UE_LOG(LogTemp, Log, TEXT("[Settings] Loaded persisted settings."));
	UE_LOG(LogTemp, Log, TEXT("[Settings] Loaded values: %s"), *DescribeSettingsState(CurrentSettings));
}

void UWorldForgeSettingsWidget::SavePersistedSettings() const
{
	FWorldForgeSettingsState SettingsToSave = CurrentSettings;
	SanitizeSettingsState(SettingsToSave, MakeDefaultSettings(), false);

	UWorldForgeUserSettingsSaveGame* const SaveObject = Cast<UWorldForgeUserSettingsSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UWorldForgeUserSettingsSaveGame::StaticClass()));
	if (!SaveObject)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to save settings: save object could not be created."));
		return;
	}

	CopySettingsStateToSaveGame(SettingsToSave, *SaveObject);

	if (!UGameplayStatics::SaveGameToSlot(SaveObject, UserSettingsSaveSlotName, UserSettingsSaveUserIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Failed to save settings."));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[Settings] Saved settings."));
	UE_LOG(LogTemp, Log, TEXT("[Settings] Saved values: %s"), *DescribeSettingsState(SettingsToSave));
}

void UWorldForgeSettingsWidget::RefreshUIFromPendingSettings()
{
	bIsRefreshingUI = true;
	ON_SCOPE_EXIT
	{
		bIsRefreshingUI = false;
	};

	PendingSettings.TextScale = ClampTextScale(PendingSettings.TextScale);
	PendingSettings.AccessibilityTextScale = ClampTextScale(PendingSettings.AccessibilityTextScale);
	PendingSettings.WindowModeLabel = NormalizeWindowModeLabel(PendingSettings.WindowModeLabel);
	FIntPoint ParsedResolution;
	PendingSettings.ResolutionLabel = TryParseResolutionLabel(PendingSettings.ResolutionLabel, ParsedResolution)
		? MakeResolutionLabel(ParsedResolution)
		: GetDefaultResolutionLabel();

	if (CHK_AutosavePlaceholder)
	{
		CHK_AutosavePlaceholder->SetIsChecked(PendingSettings.bAutosaveEnabled);
	}

	if (CHK_TutorialHintsPlaceholder)
	{
		CHK_TutorialHintsPlaceholder->SetIsChecked(PendingSettings.bTutorialHintsEnabled);
	}

	if (CHK_PauseOnMenuPlaceholder)
	{
		CHK_PauseOnMenuPlaceholder->SetIsChecked(PendingSettings.bPauseOnMenuEnabled);
	}

	if (CHK_VSyncPlaceholder)
	{
		CHK_VSyncPlaceholder->SetIsChecked(PendingSettings.bVSyncEnabled);
	}

	if (SLD_MasterVolume)
	{
		SLD_MasterVolume->SetValue(PendingSettings.MasterVolume);
	}

	if (SLD_MusicVolume)
	{
		SLD_MusicVolume->SetValue(PendingSettings.MusicVolume);
	}

	if (SLD_SFXVolume)
	{
		SLD_SFXVolume->SetValue(PendingSettings.SfxVolume);
	}

	if (SLD_AmbienceVolume)
	{
		SLD_AmbienceVolume->SetValue(PendingSettings.AmbienceVolume);
	}

	if (SLD_TextSize)
	{
		ConfigureTextScaleSlider(SLD_TextSize);
		SLD_TextSize->SetValue(PendingSettings.TextScale);
	}

	if (CHK_HighContrastMode)
	{
		CHK_HighContrastMode->SetIsChecked(PendingSettings.bHighContrastMode);
	}

	if (CHK_ColorblindAssist)
	{
		CHK_ColorblindAssist->SetIsChecked(PendingSettings.bColorblindAssist);
	}

	if (UCheckBox* const ReduceMotionCheckbox = GetReduceMotionCheckbox())
	{
		ReduceMotionCheckbox->SetIsChecked(PendingSettings.bReduceMotion);
	}

	if (CHK_LargeTextMode)
	{
		CHK_LargeTextMode->SetIsChecked(PendingSettings.bLargeTextMode);
	}

	if (TXT_ColorblindModePlaceholder)
	{
		TXT_ColorblindModePlaceholder->SetText(FText::FromString(PendingSettings.bColorblindAssist ? TEXT("On") : TEXT("Off")));
	}

	if (TXT_ResolutionPlaceholder)
	{
		TXT_ResolutionPlaceholder->SetText(FText::FromString(PendingSettings.ResolutionLabel));
	}

	if (ResolutionDropdown)
	{
		ConfigureResolutionDropdown(ResolutionDropdown, PendingSettings.ResolutionLabel);
	}

	if (TXT_WindowModePlaceholder)
	{
		TXT_WindowModePlaceholder->SetText(FText::FromString(PendingSettings.WindowModeLabel));
	}

	if (WindowModeDropdown)
	{
		ConfigureWindowModeDropdown(WindowModeDropdown);
		WindowModeDropdown->SetSelectedOption(PendingSettings.WindowModeLabel);
	}
}

void UWorldForgeSettingsWidget::ApplyAudioSettings(const FWorldForgeSettingsState& SettingsState)
{
	const float MasterVolume = ClampVolume(SettingsState.MasterVolume);
	const float MusicVolume = ClampVolume(SettingsState.MusicVolume);
	const float SfxVolume = ClampVolume(SettingsState.SfxVolume);
	const float AmbienceVolume = ClampVolume(SettingsState.AmbienceVolume);

	// TODO: Point these exposed properties at project-owned audio assets once the broader settings pipeline is formalized.
	USoundMix* ResolvedSoundMix = LoadOptionalAsset(RuntimeSoundMix, RuntimeSoundMixFallbackPaths);
	USoundClass* ResolvedMasterSoundClass = LoadOptionalAsset(RuntimeMasterSoundClass, RuntimeMasterSoundClassFallbackPaths);
	USoundClass* ResolvedMusicSoundClass = LoadOptionalAsset(RuntimeMusicSoundClass, RuntimeMusicSoundClassFallbackPaths);
	USoundClass* ResolvedSfxSoundClass = LoadOptionalAsset(RuntimeSfxSoundClass, RuntimeSfxSoundClassFallbackPaths);
	USoundClass* ResolvedAmbienceSoundClass = LoadOptionalAsset(RuntimeAmbienceSoundClass, RuntimeAmbienceSoundClassFallbackPaths);

	if (!ResolvedSoundMix)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Audio runtime application skipped: no SoundMix/SoundClass assets configured."));
		return;
	}

	int32 AppliedOverrideCount = 0;

	if (ResolvedMasterSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, ResolvedSoundMix, ResolvedMasterSoundClass, MasterVolume, 1.0f, 0.0f, true);
		++AppliedOverrideCount;
	}

	if (ResolvedMusicSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, ResolvedSoundMix, ResolvedMusicSoundClass, MusicVolume, 1.0f, 0.0f, true);
		++AppliedOverrideCount;
	}

	if (ResolvedSfxSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, ResolvedSoundMix, ResolvedSfxSoundClass, SfxVolume, 1.0f, 0.0f, true);
		++AppliedOverrideCount;
	}

	if (ResolvedAmbienceSoundClass)
	{
		UGameplayStatics::SetSoundMixClassOverride(this, ResolvedSoundMix, ResolvedAmbienceSoundClass, AmbienceVolume, 1.0f, 0.0f, true);
		++AppliedOverrideCount;
	}

	if (AppliedOverrideCount == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Settings] Audio runtime application skipped: no SoundMix/SoundClass assets configured."));
		return;
	}

	if (ActiveRuntimeSoundMix.IsValid() && ActiveRuntimeSoundMix.Get() != ResolvedSoundMix)
	{
		UGameplayStatics::PopSoundMixModifier(this, ActiveRuntimeSoundMix.Get());
		ActiveRuntimeSoundMix.Reset();
	}

	if (ActiveRuntimeSoundMix.Get() != ResolvedSoundMix)
	{
		UGameplayStatics::PushSoundMixModifier(this, ResolvedSoundMix);
		ActiveRuntimeSoundMix = ResolvedSoundMix;
	}

	UE_LOG(
		LogTemp,
		Log,
		TEXT("[Settings] Applied audio settings: Master=%.2f Music=%.2f SFX=%.2f Ambience=%.2f"),
		MasterVolume,
		MusicVolume,
		SfxVolume,
		AmbienceVolume);
}

void UWorldForgeSettingsWidget::ApplyAccessibilitySettings(const FWorldForgeSettingsState& SettingsState) const
{
	UE_LOG(LogTemp, Log, TEXT("[Settings] Applied accessibility settings: HighContrast=%s Colorblind=%s ReduceMotion=%s LargeText=%s AccessibilityTextScale=%.2f"),
		BoolStatus(SettingsState.bHighContrastMode),
		BoolStatus(SettingsState.bColorblindAssist),
		BoolStatus(SettingsState.bReduceMotion),
		BoolStatus(SettingsState.bLargeTextMode),
		SettingsState.AccessibilityTextScale);
	UE_LOG(LogTemp, Log, TEXT("[Settings] Accessibility advanced effects are local/log-only in this foundation pass."));
}

void UWorldForgeSettingsWidget::LogMissingSettingsWidgets() const
{
	if (!CHK_AutosavePlaceholder)
	{
		LogMissingWidget(TEXT("CHK_AutosavePlaceholder"));
	}

	if (!CHK_TutorialHintsPlaceholder)
	{
		LogMissingWidget(TEXT("CHK_TutorialHintsPlaceholder"));
	}

	if (!CHK_PauseOnMenuPlaceholder)
	{
		LogMissingWidget(TEXT("CHK_PauseOnMenuPlaceholder"));
	}

	if (!CHK_VSyncPlaceholder)
	{
		LogMissingWidget(TEXT("CHK_VSyncPlaceholder"));
	}

	if (!SLD_MasterVolume)
	{
		LogMissingWidget(TEXT("SLD_MasterVolume"));
	}

	if (!SLD_MusicVolume)
	{
		LogMissingWidget(TEXT("SLD_MusicVolume"));
	}

	if (!SLD_SFXVolume)
	{
		LogMissingWidget(TEXT("SLD_SFXVolume"));
	}

	if (!SLD_AmbienceVolume)
	{
		LogMissingWidget(TEXT("SLD_AmbienceVolume"));
	}

	if (!SLD_TextSize)
	{
		LogMissingWidget(TEXT("SLD_TextSize"));
	}

	if (!TXT_ResolutionPlaceholder)
	{
		LogMissingWidget(TEXT("TXT_ResolutionPlaceholder"));
	}

	if (!ResolutionDropdown)
	{
		LogMissingWidget(TEXT("ResolutionDropdown"));
	}

	if (!TXT_WindowModePlaceholder)
	{
		LogMissingWidget(TEXT("TXT_WindowModePlaceholder"));
	}

	if (!WindowModeDropdown)
	{
		LogMissingWidget(TEXT("WindowModeDropdown"));
	}

	if (!VB_ControlsSection)
	{
		LogMissingWidget(TEXT("VB_ControlsSection"));
	}

	if (!TXT_KeybindsPlaceholder)
	{
		LogMissingWidget(TEXT("TXT_KeybindsPlaceholder"));
	}

	if (!VB_AccessibilitySection)
	{
		LogMissingWidget(TEXT("VB_AccessibilitySection"));
	}

	if (!CHK_HighContrastMode)
	{
		LogMissingWidget(TEXT("CHK_HighContrastMode"));
	}

	if (!CHK_ColorblindAssist)
	{
		LogMissingWidget(TEXT("CHK_ColorblindAssist"));
	}

	if (!GetReduceMotionCheckbox())
	{
		LogMissingWidget(TEXT("CHK_ReduceMotion"));
	}

	if (!CHK_LargeTextMode)
	{
		LogMissingWidget(TEXT("CHK_LargeTextMode"));
	}
}

FWorldForgeSettingsState UWorldForgeSettingsWidget::MakeDefaultSettings() const
{
	FWorldForgeSettingsState DefaultSettings;
	DefaultSettings.TextScale = DefaultTextScale;
	DefaultSettings.AccessibilityTextScale = DefaultAccessibilityTextScale;

	if (CHK_AutosavePlaceholder)
	{
		DefaultSettings.bAutosaveEnabled = CHK_AutosavePlaceholder->IsChecked();
	}

	if (CHK_TutorialHintsPlaceholder)
	{
		DefaultSettings.bTutorialHintsEnabled = CHK_TutorialHintsPlaceholder->IsChecked();
	}

	if (CHK_PauseOnMenuPlaceholder)
	{
		DefaultSettings.bPauseOnMenuEnabled = CHK_PauseOnMenuPlaceholder->IsChecked();
	}

	// VSync defaults to enabled; persisted state overrides it during construction.

	if (SLD_MasterVolume)
	{
		DefaultSettings.MasterVolume = SLD_MasterVolume->GetValue();
	}

	if (SLD_MusicVolume)
	{
		DefaultSettings.MusicVolume = SLD_MusicVolume->GetValue();
	}

	if (SLD_SFXVolume)
	{
		DefaultSettings.SfxVolume = SLD_SFXVolume->GetValue();
	}

	if (SLD_AmbienceVolume)
	{
		DefaultSettings.AmbienceVolume = SLD_AmbienceVolume->GetValue();
	}

	if (CHK_HighContrastMode)
	{
		DefaultSettings.bHighContrastMode = CHK_HighContrastMode->IsChecked();
	}

	if (CHK_ColorblindAssist)
	{
		DefaultSettings.bColorblindAssist = CHK_ColorblindAssist->IsChecked();
	}

	if (const UCheckBox* const ReduceMotionCheckbox = GetReduceMotionCheckbox())
	{
		DefaultSettings.bReduceMotion = ReduceMotionCheckbox->IsChecked();
	}

	if (CHK_LargeTextMode)
	{
		DefaultSettings.bLargeTextMode = CHK_LargeTextMode->IsChecked();
	}

	DefaultSettings.ResolutionLabel = GetDefaultResolutionLabel();

	const FString WindowModeDropdownText = WindowModeDropdown ? WindowModeDropdown->GetSelectedOption().TrimStartAndEnd() : FString();
	if (!WindowModeDropdownText.IsEmpty())
	{
		DefaultSettings.WindowModeLabel = NormalizeWindowModeLabel(WindowModeDropdownText);
	}
	else
	{
		const FString WindowModeText = GetTrimmedWidgetText(TXT_WindowModePlaceholder);
		if (!WindowModeText.IsEmpty())
		{
			DefaultSettings.WindowModeLabel = NormalizeWindowModeLabel(WindowModeText);
		}
	}

	DefaultSettings.WindowModeLabel = NormalizeWindowModeLabel(DefaultSettings.WindowModeLabel);

	return DefaultSettings;
}

UCheckBox* UWorldForgeSettingsWidget::GetReduceMotionCheckbox() const
{
	return CHK_ReduceMotion ? CHK_ReduceMotion : CHK_ReduceFlashingPlaceholder;
}

FString UWorldForgeSettingsWidget::DescribeSettingsState(const FWorldForgeSettingsState& SettingsState) const
{
	return FString::Printf(
		TEXT("bAutosaveEnabled=%s, bTutorialHintsEnabled=%s, bPauseOnMenuEnabled=%s, MasterVolume=%.2f, MusicVolume=%.2f, SfxVolume=%.2f, AmbienceVolume=%.2f, bVSyncEnabled=%s, ResolutionLabel=\"%s\", WindowModeLabel=\"%s\", TextScale=%.2f, bHighContrastMode=%s, bColorblindAssist=%s, bReduceMotion=%s, bLargeTextMode=%s, AccessibilityTextScale=%.2f"),
		BoolStatus(SettingsState.bAutosaveEnabled),
		BoolStatus(SettingsState.bTutorialHintsEnabled),
		BoolStatus(SettingsState.bPauseOnMenuEnabled),
		SettingsState.MasterVolume,
		SettingsState.MusicVolume,
		SettingsState.SfxVolume,
		SettingsState.AmbienceVolume,
		BoolStatus(SettingsState.bVSyncEnabled),
		*SettingsState.ResolutionLabel,
		*SettingsState.WindowModeLabel,
		SettingsState.TextScale,
		BoolStatus(SettingsState.bHighContrastMode),
		BoolStatus(SettingsState.bColorblindAssist),
		BoolStatus(SettingsState.bReduceMotion),
		BoolStatus(SettingsState.bLargeTextMode),
		SettingsState.AccessibilityTextScale);
}
