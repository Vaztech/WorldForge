#include "UI/WorldForgeEmbarkSetupWidget.h"

#include "UI/WorldForgeEmbarkationManager.h"
#include "UI/WorldForgeEmbarkLoadoutWidget.h"
#include "Visual/ColonistVisualManager.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/ContentWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/PanelWidget.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Widget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "PaperFlipbook.h"
#include "PaperSprite.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "UObject/UnrealType.h"

namespace
{
constexpr float EmbarkInputWidth = 640.0f;
constexpr float EmbarkTabContentWidth = 1000.0f;
constexpr float EmbarkTabContentHeight = 600.0f;
constexpr float RaceDetailsPanelWidth = 560.0f;
constexpr float RaceDetailsWrapWidth = 500.0f;
constexpr float RacePreviewStatusWrapWidth = 430.0f;
constexpr int32 EmbarkMaxSeedLength = 32;
constexpr int32 EmbarkSeedLogPreviewLength = 64;
constexpr const TCHAR* EmbarkLoadoutWidgetPath = TEXT("/Game/WorldForge/WidgetsBlueprints/WBP_WF_EmbarkLoadout.WBP_WF_EmbarkLoadout_C");
constexpr const TCHAR* RaceDwarf = TEXT("Dwarf");
constexpr const TCHAR* RaceHighElf = TEXT("HighElf");
constexpr const TCHAR* RaceConstructFolk = TEXT("ConstructFolk");
constexpr const TCHAR* RaceNamekian = TEXT("Namekian");
constexpr const TCHAR* RaceFaeKin = TEXT("FaeKin");
constexpr const TCHAR* RaceTidecaller = TEXT("Tidecaller");
constexpr const TCHAR* RaceOni = TEXT("Oni");
constexpr const TCHAR* RaceDarkElf = TEXT("DarkElf");

int32 ResolveWorldSizeGridFromLabel(const FString& Label)
{
	const FString NormalizedLabel = Label.TrimStartAndEnd();

	if (NormalizedLabel.Equals(TEXT("Small"), ESearchCase::IgnoreCase))
	{
		return 128;
	}

	if (NormalizedLabel.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
	{
		return 256;
	}

	if (NormalizedLabel.Equals(TEXT("Large"), ESearchCase::IgnoreCase))
	{
		return 512;
	}

	if (NormalizedLabel.Equals(TEXT("OMG"), ESearchCase::IgnoreCase))
	{
		return 1024;
	}

	return 256;
}

const TCHAR* ColonyModeToLogString(EWorldForgeColonyMode ColonyMode)
{
	return ColonyMode == EWorldForgeColonyMode::SingleRace
		? TEXT("SingleRace")
		: TEXT("Mixed");
}

const TCHAR* EmbarkSetupBindingStatus(const UObject* Widget)
{
	return Widget ? TEXT("OK") : TEXT("NULL");
}

FString MakeShortSeedString(const uint32 SeedValue)
{
	return FString::Printf(TEXT("WF_%08X"), SeedValue);
}

bool IsAllowedManualSeedCharacter(const TCHAR Character)
{
	return (Character >= TCHAR('0') && Character <= TCHAR('9'))
		|| (Character >= TCHAR('A') && Character <= TCHAR('Z'))
		|| (Character >= TCHAR('a') && Character <= TCHAR('z'))
		|| Character == TCHAR('_')
		|| Character == TCHAR('-');
}

FString MakeSeedLogPreview(const FString& SeedString)
{
	if (SeedString.Len() <= EmbarkSeedLogPreviewLength)
	{
		return SeedString;
	}

	return FString::Printf(TEXT("%s..."), *SeedString.Left(EmbarkSeedLogPreviewLength));
}

bool IsHexCharacter(const TCHAR Character)
{
	return (Character >= TCHAR('0') && Character <= TCHAR('9'))
		|| (Character >= TCHAR('A') && Character <= TCHAR('F'))
		|| (Character >= TCHAR('a') && Character <= TCHAR('f'));
}

bool LooksLikeGuidSeed(const FString& SeedString)
{
	const FString TrimmedSeed = SeedString.TrimStartAndEnd();
	if (TrimmedSeed.Len() < 32)
	{
		return false;
	}

	int32 HexCharacterCount = 0;
	for (int32 Index = 0; Index < TrimmedSeed.Len(); ++Index)
	{
		const TCHAR Character = TrimmedSeed[Index];
		if (IsHexCharacter(Character))
		{
			++HexCharacterCount;
			continue;
		}

		if (Character == TCHAR('-') || Character == TCHAR('{') || Character == TCHAR('}'))
		{
			continue;
		}

		return false;
	}

	return HexCharacterCount >= 32;
}

FString MakeShortSeedFromText(const FString& SeedSource)
{
	const FString SourceText = SeedSource.TrimStartAndEnd().IsEmpty()
		? FString(TEXT("WorldForge"))
		: SeedSource.TrimStartAndEnd();
	return MakeShortSeedString(static_cast<uint32>(GetTypeHash(SourceText)));
}

FString GenerateRandomShortSeedString()
{
	const uint32 SeedValue = static_cast<uint32>(FPlatformTime::Cycles())
		^ (static_cast<uint32>(FMath::Rand()) << 16)
		^ static_cast<uint32>(FMath::Rand());
	return MakeShortSeedString(SeedValue);
}

FGameplayTag RequestRaceTag(const TCHAR* RaceName)
{
	const FString TagPath = FString::Printf(TEXT("WorldForge.Race.%s"), RaceName);
	return FGameplayTag::RequestGameplayTag(FName(*TagPath), false);
}

FString RaceLeafFromTag(FGameplayTag RaceTag)
{
	FString RacePath = RaceTag.ToString();
	FString RaceLeaf;
	if (RacePath.Split(TEXT("."), nullptr, &RaceLeaf, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return RaceLeaf;
	}

	return RacePath;
}

FString HumanizeIdentifier(const FString& Identifier)
{
	FString Result;
	Result.Reserve(Identifier.Len() + 4);

	for (int32 Index = 0; Index < Identifier.Len(); ++Index)
	{
		const TCHAR Character = Identifier[Index];
		const TCHAR PreviousCharacter = Index > 0 ? Identifier[Index - 1] : 0;
		const TCHAR NextCharacter = Index + 1 < Identifier.Len() ? Identifier[Index + 1] : 0;
		const bool bNeedsSpace = Index > 0
			&& FChar::IsUpper(Character)
			&& PreviousCharacter != TCHAR(' ')
			&& (FChar::IsLower(PreviousCharacter) || (NextCharacter != 0 && FChar::IsLower(NextCharacter)));

		if (bNeedsSpace)
		{
			Result.AppendChar(TCHAR(' '));
		}

		Result.AppendChar(Character);
	}

	return Result;
}

FText RaceDisplayNameFromName(const TCHAR* RaceName)
{
	return FText::FromString(HumanizeIdentifier(RaceName));
}

FText RaceDisplayNameFromTag(FGameplayTag RaceTag)
{
	return RaceTag.IsValid()
		? FText::FromString(HumanizeIdentifier(RaceLeafFromTag(RaceTag)))
		: FText::FromString(TEXT("Race"));
}

bool IsSupportedRaceTag(FGameplayTag RaceTag)
{
	const FString RaceLeaf = RaceLeafFromTag(RaceTag);
	return RaceLeaf.Equals(RaceDwarf, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceHighElf, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceConstructFolk, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceNamekian, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceFaeKin, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceTidecaller, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceOni, ESearchCase::IgnoreCase)
		|| RaceLeaf.Equals(RaceDarkElf, ESearchCase::IgnoreCase);
}

FString RaceDataAssetPathForTag(FGameplayTag RaceTag)
{
	const FString RaceLeaf = RaceLeafFromTag(RaceTag);
	if (RaceLeaf.Equals(RaceDwarf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_Dwarf.DA_Race_Dwarf");
	}

	if (RaceLeaf.Equals(RaceHighElf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_HighElf.DA_Race_HighElf");
	}

	if (RaceLeaf.Equals(RaceConstructFolk, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_ConstructFolk.DA_Race_ConstructFolk");
	}

	if (RaceLeaf.Equals(RaceNamekian, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_Namekian.DA_Race_Namekian");
	}

	if (RaceLeaf.Equals(RaceFaeKin, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_FaeKin.DA_Race_FaeKin");
	}

	if (RaceLeaf.Equals(RaceTidecaller, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_Tidecaller.DA_Race_Tidecaller");
	}

	if (RaceLeaf.Equals(RaceOni, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_Oni.DA_Race_Oni");
	}

	if (RaceLeaf.Equals(RaceDarkElf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Races/DA_Race_DarkElf.DA_Race_DarkElf");
	}

	return FString();
}

FString IdlePreviewAssetPathForTag(FGameplayTag RaceTag)
{
	const FString RaceLeaf = RaceLeafFromTag(RaceTag);
	if (RaceLeaf.Equals(RaceDwarf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/Dwarf_Male/SpriteAnimations/FB_Dwarf_Idle.FB_Dwarf_Idle");
	}

	if (RaceLeaf.Equals(RaceHighElf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/HighElf_Male/SpriteAnimations/FB_HighElf_Idle.FB_HighElf_Idle");
	}

	if (RaceLeaf.Equals(RaceConstructFolk, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/construct_folk/male/construct_folk_male_idle_8x1.construct_folk_male_idle_8x1");
	}

	if (RaceLeaf.Equals(RaceNamekian, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/namekian/male/namekian_male_idle_8x1.namekian_male_idle_8x1");
	}

	if (RaceLeaf.Equals(RaceFaeKin, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/fae_kin/male/fae_kin_male_idle_8x1.fae_kin_male_idle_8x1");
	}

	if (RaceLeaf.Equals(RaceTidecaller, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/tidecaller/male/tidecaller_male_idle_8x1.tidecaller_male_idle_8x1");
	}

	if (RaceLeaf.Equals(RaceOni, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/oni/male/oni_male_idle_8x1.oni_male_idle_8x1");
	}

	if (RaceLeaf.Equals(RaceDarkElf, ESearchCase::IgnoreCase))
	{
		return TEXT("/Game/WorldForge/Characters/Sprites/dark_elf/male/dark_elf_male_idle_8x1.dark_elf_male_idle_8x1");
	}

	return FString();
}

void SetNumericProperty(UObject* Object, const FName PropertyName, const double Value)
{
	if (!Object)
	{
		return;
	}

	if (FNumericProperty* NumericProperty = FindFProperty<FNumericProperty>(Object->GetClass(), PropertyName))
	{
		void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		if (NumericProperty->IsFloatingPoint())
		{
			NumericProperty->SetFloatingPointPropertyValue(ValuePtr, Value);
		}
		else if (NumericProperty->IsInteger())
		{
			NumericProperty->SetIntPropertyValue(ValuePtr, static_cast<int64>(Value));
		}
	}
}

void SetSlateColorProperty(UObject* Object, const FName PropertyName, const FSlateColor& Color)
{
	if (!Object)
	{
		return;
	}

	if (FStructProperty* StructProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName))
	{
		void* ValuePtr = StructProperty->ContainerPtrToValuePtr<void>(Object);
		if (StructProperty->Struct == TBaseStructure<FSlateColor>::Get())
		{
			*static_cast<FSlateColor*>(ValuePtr) = Color;
		}
	}
}

void ApplyEditableTextBoxStyle(UEditableTextBox* TextBox)
{
	if (!TextBox)
	{
		return;
	}

	SetNumericProperty(TextBox, TEXT("MinimumDesiredWidth"), EmbarkInputWidth);

	const FSlateColor DarkText(FLinearColor(0.015f, 0.018f, 0.022f, 1.0f));
	if (FStructProperty* StyleProperty = FindFProperty<FStructProperty>(TextBox->GetClass(), TEXT("WidgetStyle")))
	{
		void* StylePtr = StyleProperty->ContainerPtrToValuePtr<void>(TextBox);
		if (StyleProperty->Struct == FEditableTextBoxStyle::StaticStruct())
		{
			FEditableTextBoxStyle* Style = static_cast<FEditableTextBoxStyle*>(StylePtr);
			Style->SetForegroundColor(DarkText);
			Style->SetReadOnlyForegroundColor(DarkText);
		}
	}

	SetSlateColorProperty(TextBox, TEXT("ForegroundColor"), DarkText);
	SetSlateColorProperty(TextBox, TEXT("ReadOnlyForegroundColor"), DarkText);

	if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(TextBox->Slot))
	{
		VerticalSlot->SetHorizontalAlignment(HAlign_Left);
	}

	TextBox->SynchronizeProperties();
}

void AlignWidgetLeft(UWidget* Widget)
{
	if (Widget)
	{
		if (UVerticalBoxSlot* VerticalSlot = Cast<UVerticalBoxSlot>(Widget->Slot))
		{
			VerticalSlot->SetHorizontalAlignment(HAlign_Left);
		}
	}
}

void ApplyTextWrap(UTextBlock* TextBlock, const float WrapTextAt, const bool bClipToBounds = false)
{
	if (!TextBlock)
	{
		return;
	}

	TextBlock->SetAutoWrapText(true);
	TextBlock->SetWrapTextAt(WrapTextAt);
	if (bClipToBounds)
	{
		TextBlock->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	}
	TextBlock->SynchronizeProperties();
}

UObject* LoadRaceDataAsset(FGameplayTag RaceTag)
{
	const FString AssetPath = RaceDataAssetPathForTag(RaceTag);
	return AssetPath.IsEmpty() ? nullptr : StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
}

bool ResolveRaceIdleVisualDefinition(const UWorld* World, FGameplayTag RaceTag, FWorldForgeColonistVisualRow& OutDefinition)
{
	if (!World || !RaceTag.IsValid())
	{
		return false;
	}

	const UColonistVisualManager* VisualManager = World->GetSubsystem<UColonistVisualManager>();
	return VisualManager && VisualManager->GetVisualDefinitionForRace(RaceTag, NAME_None, OutDefinition);
}

FString ResolveRaceIdleVisualAssetPath(const UWorld* World, FGameplayTag RaceTag)
{
	FWorldForgeColonistVisualRow VisualDefinition;
	if (ResolveRaceIdleVisualDefinition(World, RaceTag, VisualDefinition) && !VisualDefinition.IdleVisualAsset.IsNull())
	{
		return VisualDefinition.IdleVisualAsset.ToSoftObjectPath().ToString();
	}

	return IdlePreviewAssetPathForTag(RaceTag);
}

double ReadNumericProperty(const UObject* Object, const FName PropertyName, const double DefaultValue = 1.0)
{
	if (!Object)
	{
		return DefaultValue;
	}

	if (const FNumericProperty* NumericProperty = FindFProperty<FNumericProperty>(Object->GetClass(), PropertyName))
	{
		const void* ValuePtr = NumericProperty->ContainerPtrToValuePtr<void>(Object);
		if (NumericProperty->IsFloatingPoint())
		{
			return NumericProperty->GetFloatingPointPropertyValue(ValuePtr);
		}

		if (NumericProperty->IsInteger())
		{
			return static_cast<double>(NumericProperty->GetSignedIntPropertyValue(ValuePtr));
		}
	}

	return DefaultValue;
}

FGameplayTag ReadGameplayTagProperty(const UObject* Object, const FName PropertyName)
{
	if (!Object)
	{
		return FGameplayTag();
	}

	if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName))
	{
		if (StructProperty->Struct == FGameplayTag::StaticStruct())
		{
			return *StructProperty->ContainerPtrToValuePtr<FGameplayTag>(Object);
		}
	}

	return FGameplayTag();
}

FGameplayTagContainer ReadGameplayTagContainerProperty(const UObject* Object, const FName PropertyName)
{
	if (!Object)
	{
		return FGameplayTagContainer();
	}

	if (const FStructProperty* StructProperty = FindFProperty<FStructProperty>(Object->GetClass(), PropertyName))
	{
		if (StructProperty->Struct == FGameplayTagContainer::StaticStruct())
		{
			return *StructProperty->ContainerPtrToValuePtr<FGameplayTagContainer>(Object);
		}
	}

	return FGameplayTagContainer();
}

void AddModifierLine(TArray<FString>& Lines, const TCHAR* Label, const double Value, const bool bStrength)
{
	const double Delta = Value - 1.0;
	if ((bStrength && Delta > 0.05) || (!bStrength && Delta < -0.05))
	{
		Lines.Add(FString::Printf(TEXT("%s x%.1f"), Label, Value));
	}
}

FString BuildStrengthsText(const UObject* RaceData)
{
	if (!RaceData)
	{
		return TEXT("Strengths pending.");
	}

	TArray<FString> Lines;
	AddModifierLine(Lines, TEXT("Mining"), ReadNumericProperty(RaceData, TEXT("MiningMod")), true);
	AddModifierLine(Lines, TEXT("Crafting"), ReadNumericProperty(RaceData, TEXT("CraftingMod")), true);
	AddModifierLine(Lines, TEXT("Building"), ReadNumericProperty(RaceData, TEXT("BuildingMod")), true);
	AddModifierLine(Lines, TEXT("Combat"), ReadNumericProperty(RaceData, TEXT("CombatMod")), true);
	AddModifierLine(Lines, TEXT("Magic learning"), ReadNumericProperty(RaceData, TEXT("MagicLearningMod")), true);
	AddModifierLine(Lines, TEXT("Hauling"), ReadNumericProperty(RaceData, TEXT("HaulingMod")), true);
	AddModifierLine(Lines, TEXT("Medicine"), ReadNumericProperty(RaceData, TEXT("MedicineMod")), true);
	AddModifierLine(Lines, TEXT("Research"), ReadNumericProperty(RaceData, TEXT("ResearchMod")), true);

	const double BaseHealth = ReadNumericProperty(RaceData, TEXT("BaseHealthPoints"), 100.0);
	if (BaseHealth > 100.0)
	{
		Lines.Add(FString::Printf(TEXT("Health %.0f"), BaseHealth));
	}

	const double MoveSpeed = ReadNumericProperty(RaceData, TEXT("BaseMoveSpeed"), 1.0);
	if (MoveSpeed > 1.05)
	{
		Lines.Add(FString::Printf(TEXT("Move speed x%.1f"), MoveSpeed));
	}

	return Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("Strengths pending.");
}

FString BuildWeaknessesText(const UObject* RaceData)
{
	if (!RaceData)
	{
		return TEXT("Weaknesses pending.");
	}

	TArray<FString> Lines;
	AddModifierLine(Lines, TEXT("Mining"), ReadNumericProperty(RaceData, TEXT("MiningMod")), false);
	AddModifierLine(Lines, TEXT("Crafting"), ReadNumericProperty(RaceData, TEXT("CraftingMod")), false);
	AddModifierLine(Lines, TEXT("Building"), ReadNumericProperty(RaceData, TEXT("BuildingMod")), false);
	AddModifierLine(Lines, TEXT("Combat"), ReadNumericProperty(RaceData, TEXT("CombatMod")), false);
	AddModifierLine(Lines, TEXT("Magic learning"), ReadNumericProperty(RaceData, TEXT("MagicLearningMod")), false);
	AddModifierLine(Lines, TEXT("Hauling"), ReadNumericProperty(RaceData, TEXT("HaulingMod")), false);
	AddModifierLine(Lines, TEXT("Medicine"), ReadNumericProperty(RaceData, TEXT("MedicineMod")), false);
	AddModifierLine(Lines, TEXT("Research"), ReadNumericProperty(RaceData, TEXT("ResearchMod")), false);

	const double BaseHealth = ReadNumericProperty(RaceData, TEXT("BaseHealthPoints"), 100.0);
	if (BaseHealth < 100.0)
	{
		Lines.Add(FString::Printf(TEXT("Health %.0f"), BaseHealth));
	}

	const double MoveSpeed = ReadNumericProperty(RaceData, TEXT("BaseMoveSpeed"), 1.0);
	if (MoveSpeed < 0.95)
	{
		Lines.Add(FString::Printf(TEXT("Move speed x%.1f"), MoveSpeed));
	}

	return Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("Weaknesses pending.");
}

FString BuildTraitsText(const UObject* RaceData)
{
	if (!RaceData)
	{
		return TEXT("Traits pending.");
	}

	TArray<FString> Lines;
	const FGameplayTag UniqueMechanic = ReadGameplayTagProperty(RaceData, TEXT("UniqueMechanic"));
	if (UniqueMechanic.IsValid())
	{
		Lines.Add(FString::Printf(TEXT("Mechanic: %s"), *UniqueMechanic.ToString()));
	}

	const FGameplayTagContainer BiomeAffinities = ReadGameplayTagContainerProperty(RaceData, TEXT("BiomeAffinities"));
	if (!BiomeAffinities.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Biome affinity: %s"), *BiomeAffinities.ToStringSimple(false)));
	}

	const FGameplayTagContainer MagicAffinities = ReadGameplayTagContainerProperty(RaceData, TEXT("MagicAffinities"));
	if (!MagicAffinities.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Magic affinity: %s"), *MagicAffinities.ToStringSimple(false)));
	}

	const FGameplayTagContainer CosmicResonance = ReadGameplayTagContainerProperty(RaceData, TEXT("CosmicResonance"));
	if (!CosmicResonance.IsEmpty())
	{
		Lines.Add(FString::Printf(TEXT("Cosmic resonance: %s"), *CosmicResonance.ToStringSimple(false)));
	}

	return Lines.Num() > 0 ? FString::Join(Lines, TEXT("\n")) : TEXT("Traits pending.");
}

FString BuildColonistSkillsText(const FWorldForgeEmbarkColonist& Colonist)
{
	if (Colonist.Skills.Num() == 0)
	{
		return TEXT("None");
	}

	TArray<FName> SkillNames;
	Colonist.Skills.GetKeys(SkillNames);
	SkillNames.Sort([](const FName& Left, const FName& Right)
	{
		return Left.ToString() < Right.ToString();
	});

	TArray<FString> Lines;
	Lines.Reserve(SkillNames.Num());
	for (const FName& SkillName : SkillNames)
	{
		const int32* SkillValue = Colonist.Skills.Find(SkillName);
		Lines.Add(FString::Printf(TEXT("%s: %d"), *SkillName.ToString(), SkillValue ? *SkillValue : 0));
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString BuildColonistTraitsText(const FWorldForgeEmbarkColonist& Colonist)
{
	if (Colonist.Traits.Num() == 0)
	{
		return TEXT("None");
	}

	TArray<FString> Traits = Colonist.Traits;
	Traits.Sort();
	return FString::Join(Traits, TEXT(", "));
}

FString ColonistRaceDisplayString(FGameplayTag RaceTag)
{
	return RaceTag.IsValid()
		? RaceDisplayNameFromTag(RaceTag).ToString()
		: FString(TEXT("Unknown"));
}

void SetTextBlockString(UTextBlock* TextBlock, const FString& Text)
{
	if (TextBlock)
	{
		TextBlock->SetText(FText::FromString(Text));
	}
}

void SetButtonLabelText(UButton* Button, UTextBlock* BoundTextBlock, const FText& Label)
{
	UTextBlock* LabelText = BoundTextBlock;
	if (!LabelText && Button)
	{
		LabelText = Cast<UTextBlock>(Button->GetContent());
	}

	if (LabelText)
	{
		LabelText->SetText(Label);
	}
}
}

void UWorldForgeEmbarkColonistRowClickProxy::Initialize(UWorldForgeEmbarkSetupWidget* InOwnerWidget, const int32 InColonistIndex)
{
	OwnerWidget = InOwnerWidget;
	ColonistIndex = InColonistIndex;
}

void UWorldForgeEmbarkColonistRowClickProxy::HandleClicked()
{
	if (UWorldForgeEmbarkSetupWidget* Widget = OwnerWidget.Get())
	{
		Widget->SelectStartingColonist(ColonistIndex);
	}
}

void UWorldForgeEmbarkSetupWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
}

void UWorldForgeEmbarkSetupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	LogBindingStatus();
	ReorderTabsToTop();
	ApplyInputAndLayoutStyle();
	WrapRaceDetailsInFixedSizeBox();
	ApplyStableTabLayout();
	NormalizeInitialSeedDisplay();
	BindControls();
	if (BTN_RerollSelectedColonist)
	{
		BTN_RerollSelectedColonist->SetIsEnabled(false);
	}
	SetActiveEmbarkTab(EEmbarkSetupTab::World);
	DisableLegacyNextButton();
	InitializeRaceDisplay();
}

void UWorldForgeEmbarkSetupWidget::BindControls()
{
	if (WorldNameInput)
	{
		WorldNameInput->OnTextChanged.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldNameTextChanged);
		WorldNameInput->OnTextChanged.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldNameTextChanged);
		WorldNameInput->OnTextCommitted.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldNameTextCommitted);
		WorldNameInput->OnTextCommitted.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldNameTextCommitted);
	}

	if (SeedInput)
	{
		SeedInput->OnTextChanged.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSeedTextChanged);
		SeedInput->OnTextChanged.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSeedTextChanged);
		SeedInput->OnTextCommitted.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSeedTextCommitted);
		SeedInput->OnTextCommitted.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSeedTextCommitted);
	}

	if (BTN_RandomizeSeed)
	{
		BTN_RandomizeSeed->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRandomizeSeedClicked);
		BTN_RandomizeSeed->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRandomizeSeedClicked);
	}

	if (WorldSizeDropdown)
	{
		WorldSizeDropdown->OnSelectionChanged.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldSizeSelectionChanged);
		WorldSizeDropdown->OnSelectionChanged.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleWorldSizeSelectionChanged);
	}

	if (BTN_TabWorld)
	{
		BTN_TabWorld->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabWorldClicked);
		BTN_TabWorld->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabWorldClicked);
	}

	if (BTN_TabColony)
	{
		BTN_TabColony->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabColonyClicked);
		BTN_TabColony->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabColonyClicked);
	}

	if (BTN_TabColonists)
	{
		BTN_TabColonists->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabColonistsClicked);
		BTN_TabColonists->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleTabColonistsClicked);
	}

	if (BTN_MixedColony)
	{
		BTN_MixedColony->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleMixedColonyClicked);
		BTN_MixedColony->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleMixedColonyClicked);
	}

	if (BTN_SingleRaceColony)
	{
		BTN_SingleRaceColony->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSingleRaceColonyClicked);
		BTN_SingleRaceColony->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleSingleRaceColonyClicked);
	}

	if (BTN_Back)
	{
		BTN_Back->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleBackClicked);
		BTN_Back->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleBackClicked);
	}

	if (BTN_Continue)
	{
		BTN_Continue->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleContinueClicked);
		BTN_Continue->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleContinueClicked);
	}

	if (BTN_RacePlaceholder_Dwarf)
	{
		BTN_RacePlaceholder_Dwarf->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceDwarfClicked);
		BTN_RacePlaceholder_Dwarf->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceDwarfClicked);
	}

	if (BTN_RacePlaceholder_HighElf)
	{
		BTN_RacePlaceholder_HighElf->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceHighElfClicked);
		BTN_RacePlaceholder_HighElf->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceHighElfClicked);
	}

	if (BTN_Race_ConstructFolk)
	{
		BTN_Race_ConstructFolk->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceConstructFolkClicked);
		BTN_Race_ConstructFolk->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceConstructFolkClicked);
	}

	if (BTN_Race_Namekian)
	{
		BTN_Race_Namekian->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceNamekianClicked);
		BTN_Race_Namekian->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceNamekianClicked);
	}

	if (BTN_Race_FaeKin)
	{
		BTN_Race_FaeKin->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceFaeKinClicked);
		BTN_Race_FaeKin->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceFaeKinClicked);
	}

	if (BTN_Race_Tidecaller)
	{
		BTN_Race_Tidecaller->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceTidecallerClicked);
		BTN_Race_Tidecaller->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceTidecallerClicked);
	}

	if (BTN_Race_Oni)
	{
		BTN_Race_Oni->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceOniClicked);
		BTN_Race_Oni->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceOniClicked);
	}

	if (BTN_Race_DarkElf)
	{
		BTN_Race_DarkElf->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceDarkElfClicked);
		BTN_Race_DarkElf->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRaceDarkElfClicked);
	}

	if (BTN_ColonistPlaceholder_01)
	{
		BTN_ColonistPlaceholder_01->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist01Clicked);
		BTN_ColonistPlaceholder_01->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist01Clicked);
	}

	if (BTN_ColonistPlaceholder_02)
	{
		BTN_ColonistPlaceholder_02->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist02Clicked);
		BTN_ColonistPlaceholder_02->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist02Clicked);
	}

	if (BTN_ColonistPlaceholder_03)
	{
		BTN_ColonistPlaceholder_03->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist03Clicked);
		BTN_ColonistPlaceholder_03->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleColonist03Clicked);
	}

	if (BTN_RerollSelectedColonist)
	{
		BTN_RerollSelectedColonist->OnClicked.RemoveDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRerollSelectedColonistClicked);
		BTN_RerollSelectedColonist->OnClicked.AddDynamic(this, &UWorldForgeEmbarkSetupWidget::HandleRerollSelectedColonistClicked);
		BTN_RerollSelectedColonist->SetIsEnabled(false);
	}
}

void UWorldForgeEmbarkSetupWidget::LogBindingStatus() const
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] WorldNameInput=%s"), EmbarkSetupBindingStatus(WorldNameInput));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] SeedInput=%s"), EmbarkSetupBindingStatus(SeedInput));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_RandomizeSeed=%s"), EmbarkSetupBindingStatus(BTN_RandomizeSeed));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] WorldSizeDropdown=%s"), EmbarkSetupBindingStatus(WorldSizeDropdown));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_TabWorld=%s"), EmbarkSetupBindingStatus(BTN_TabWorld));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_TabColony=%s"), EmbarkSetupBindingStatus(BTN_TabColony));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_TabColonists=%s"), EmbarkSetupBindingStatus(BTN_TabColonists));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_TabWorld=%s"), EmbarkSetupBindingStatus(PNL_TabWorld));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_TabColony=%s"), EmbarkSetupBindingStatus(PNL_TabColony));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_TabColonists=%s"), EmbarkSetupBindingStatus(PNL_TabColonists));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_StartingColonists=%s"), EmbarkSetupBindingStatus(PNL_StartingColonists));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_ColonistList=%s"), EmbarkSetupBindingStatus(PNL_ColonistList));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] VB_ColonistList=%s"), EmbarkSetupBindingStatus(VB_ColonistList));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_ColonistPlaceholder_01=%s"), EmbarkSetupBindingStatus(BTN_ColonistPlaceholder_01));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_ColonistPlaceholder_02=%s"), EmbarkSetupBindingStatus(BTN_ColonistPlaceholder_02));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_ColonistPlaceholder_03=%s"), EmbarkSetupBindingStatus(BTN_ColonistPlaceholder_03));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_RerollSelectedColonist=%s"), EmbarkSetupBindingStatus(BTN_RerollSelectedColonist));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] PNL_RaceDetails=%s"), EmbarkSetupBindingStatus(PNL_RaceDetails));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_MixedColony=%s"), EmbarkSetupBindingStatus(BTN_MixedColony));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_SingleRaceColony=%s"), EmbarkSetupBindingStatus(BTN_SingleRaceColony));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_Back=%s"), EmbarkSetupBindingStatus(BTN_Back));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] BTN_Continue=%s"), EmbarkSetupBindingStatus(BTN_Continue));
}

void UWorldForgeEmbarkSetupWidget::DisableLegacyNextButton()
{
	if (NextButton)
	{
		NextButton->SetIsEnabled(false);
		NextButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UWorldForgeEmbarkSetupWidget::ApplyInputAndLayoutStyle()
{
	ApplyEditableTextBoxStyle(WorldNameInput);
	ApplyEditableTextBoxStyle(SeedInput);

	AlignWidgetLeft(WorldSizeDropdown);
	AlignWidgetLeft(BTN_RandomizeSeed);

	if (WorldSizeDropdown)
	{
		SetSlateColorProperty(WorldSizeDropdown, TEXT("ForegroundColor"), FSlateColor(FLinearColor(0.015f, 0.018f, 0.022f, 1.0f)));

		if (WorldSizeDropdown->GetOptionCount() == 0)
		{
			WorldSizeDropdown->AddOption(TEXT("Small"));
			WorldSizeDropdown->AddOption(TEXT("Medium"));
			WorldSizeDropdown->AddOption(TEXT("Large"));
			WorldSizeDropdown->AddOption(TEXT("OMG"));
		}

		SetNumericProperty(WorldSizeDropdown, TEXT("MinDesiredWidth"), static_cast<double>(EmbarkInputWidth));

		if (WorldSizeDropdown->GetSelectedOption().IsEmpty())
		{
			WorldSizeDropdown->SetSelectedOption(TEXT("Medium"));
			ApplyWorldSizeSelection(TEXT("Medium"));
		}

		WorldSizeDropdown->SynchronizeProperties();
	}
}

void UWorldForgeEmbarkSetupWidget::WrapRaceDetailsInFixedSizeBox()
{
	if (!WidgetTree || !PNL_RaceDetails)
	{
		return;
	}

	if (Cast<USizeBox>(PNL_RaceDetails->GetParent()))
	{
		return;
	}

	UPanelWidget* ParentPanel = PNL_RaceDetails->GetParent();
	if (!ParentPanel)
	{
		return;
	}

	int32 RaceDetailsIndex = INDEX_NONE;
	for (int32 ChildIndex = 0; ChildIndex < ParentPanel->GetChildrenCount(); ++ChildIndex)
	{
		if (ParentPanel->GetChildAt(ChildIndex) == PNL_RaceDetails)
		{
			RaceDetailsIndex = ChildIndex;
			break;
		}
	}

	if (RaceDetailsIndex == INDEX_NONE)
	{
		return;
	}

	USizeBox* FixedRaceDetailsBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("SB_RaceDetailsFixed_Runtime"));
	if (!FixedRaceDetailsBox)
	{
		return;
	}

	FixedRaceDetailsBox->SetWidthOverride(RaceDetailsPanelWidth);
	FixedRaceDetailsBox->SetMinDesiredWidth(RaceDetailsPanelWidth);
	FixedRaceDetailsBox->SetMaxDesiredWidth(RaceDetailsPanelWidth);
	FixedRaceDetailsBox->SetClipping(EWidgetClipping::ClipToBoundsAlways);

	ParentPanel->RemoveChild(PNL_RaceDetails);
	ParentPanel->InsertChildAt(RaceDetailsIndex, FixedRaceDetailsBox);
	FixedRaceDetailsBox->AddChild(PNL_RaceDetails);

	if (UHorizontalBoxSlot* WrapperSlot = Cast<UHorizontalBoxSlot>(FixedRaceDetailsBox->Slot))
	{
		FSlateChildSize WrapperSize;
		WrapperSize.SizeRule = ESlateSizeRule::Automatic;
		WrapperSize.Value = 1.0f;
		WrapperSlot->SetSize(WrapperSize);
		WrapperSlot->SetHorizontalAlignment(HAlign_Fill);
		WrapperSlot->SetVerticalAlignment(VAlign_Fill);
	}
}

void UWorldForgeEmbarkSetupWidget::ApplyStableTabLayout()
{
	if (USizeBox* TabContentSizeBox = Cast<USizeBox>(SB_TabContentArea))
	{
		TabContentSizeBox->SetWidthOverride(EmbarkTabContentWidth);
		TabContentSizeBox->SetHeightOverride(EmbarkTabContentHeight);
		TabContentSizeBox->SetMinDesiredWidth(EmbarkTabContentWidth);
		TabContentSizeBox->SetMinDesiredHeight(EmbarkTabContentHeight);
	}

	if (SB_TabContentArea)
	{
		SB_TabContentArea->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	}

	if (PNL_RaceSelection)
	{
		PNL_RaceSelection->SetClipping(EWidgetClipping::ClipToBoundsAlways);
	}

	if (PNL_RaceDetails)
	{
		PNL_RaceDetails->SetClipping(EWidgetClipping::ClipToBoundsAlways);

		if (UHorizontalBoxSlot* RaceDetailsSlot = Cast<UHorizontalBoxSlot>(PNL_RaceDetails->Slot))
		{
			FSlateChildSize RaceDetailsSize;
			RaceDetailsSize.SizeRule = ESlateSizeRule::Fill;
			RaceDetailsSize.Value = 1.0f;
			RaceDetailsSlot->SetSize(RaceDetailsSize);
			RaceDetailsSlot->SetHorizontalAlignment(HAlign_Fill);
			RaceDetailsSlot->SetVerticalAlignment(VAlign_Fill);
		}
	}

	ApplyTextWrap(TXT_RaceDescription, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_RaceStrengths, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_RaceWeaknesses, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_RaceTraits, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_RacePreviewStatus, RacePreviewStatusWrapWidth, true);
	ApplyTextWrap(TXT_ColonistBackstory, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_ColonistSkills, RaceDetailsWrapWidth);
	ApplyTextWrap(TXT_ColonistTraits, RaceDetailsWrapWidth);
}

void UWorldForgeEmbarkSetupWidget::NormalizeInitialSeedDisplay()
{
	if (!SeedInput)
	{
		return;
	}

	const FString ExistingSeedText = SeedInput->GetText().ToString();
	const FString SanitizedSeedString = LooksLikeGuidSeed(ExistingSeedText)
		? SanitizeSeedInput(MakeShortSeedFromText(ExistingSeedText))
		: SanitizeSeedInput(ExistingSeedText);
	if (ExistingSeedText.Equals(SanitizedSeedString, ESearchCase::CaseSensitive))
	{
		return;
	}

	bApplyingSanitizedSeedText = true;
	SeedInput->SetText(FText::FromString(SanitizedSeedString));
	bApplyingSanitizedSeedText = false;

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetSeedString(SanitizedSeedString);
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Seed sanitized from '%s' to '%s'"),
		*MakeSeedLogPreview(ExistingSeedText),
		*SanitizedSeedString);
}

void UWorldForgeEmbarkSetupWidget::ReorderTabsToTop()
{
	if (!HB_EmbarkTabs)
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] HB_EmbarkTabs not bound; skipping reorder."));
		return;
	}

	UPanelWidget* Parent = HB_EmbarkTabs->GetParent();
	if (!Parent)
	{
		return;
	}

	// Remove from current position and reinsert near the top.
	// Target index 2: after title and subtitle when the asset uses separate header text widgets.
	Parent->RemoveChild(HB_EmbarkTabs);
	const int32 TargetIndex = FMath::Min(2, Parent->GetChildrenCount());
	Parent->InsertChildAt(TargetIndex, HB_EmbarkTabs);

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] HB_EmbarkTabs reordered to index %d."), TargetIndex);
}

void UWorldForgeEmbarkSetupWidget::SetActiveEmbarkTab(const EEmbarkSetupTab ActiveTab)
{
	const bool bHasAnyTabWidget = BTN_TabWorld || BTN_TabColony || BTN_TabColonists || PNL_TabWorld || PNL_TabColony || PNL_TabColonists;
	if (!bHasAnyTabWidget)
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Tab widgets are not present yet; tab bridge is idle."));
		return;
	}

	const bool bWorldActive = ActiveTab == EEmbarkSetupTab::World;
	const bool bColonyActive = ActiveTab == EEmbarkSetupTab::Colony;
	const bool bColonistsActive = ActiveTab == EEmbarkSetupTab::Colonists;

	if (PNL_TabWorld)
	{
		PNL_TabWorld->SetVisibility(bWorldActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (PNL_TabColony)
	{
		PNL_TabColony->SetVisibility(bColonyActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (PNL_TabColonists)
	{
		PNL_TabColonists->SetVisibility(bColonistsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// Toggle existing content section panels by active tab
	if (PNL_WorldSetup)
	{
		PNL_WorldSetup->SetVisibility(bWorldActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (SeedInput)
	{
		SeedInput->SetVisibility(bWorldActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (WorldSizeDropdown)
	{
		WorldSizeDropdown->SetVisibility(bWorldActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (PNL_ColonyMode)
	{
		PNL_ColonyMode->SetVisibility(bColonyActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (PNL_RaceSelection)
	{
		PNL_RaceSelection->SetVisibility(bColonyActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (PNL_StartingColonists)
	{
		PNL_StartingColonists->SetVisibility(bColonistsActive ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// Keep the tab content area visible; WBP_WF_WorldConfig owns the panel layout.
	if (SB_TabContentArea)
	{
		SB_TabContentArea->SetVisibility(ESlateVisibility::Visible);
	}

	const FLinearColor ActiveButtonTint(0.32f, 0.38f, 0.48f, 1.0f);
	const FLinearColor InactiveButtonTint(0.12f, 0.135f, 0.155f, 1.0f);

	if (BTN_TabWorld)
	{
		BTN_TabWorld->SetBackgroundColor(bWorldActive ? ActiveButtonTint : InactiveButtonTint);
	}

	if (BTN_TabColony)
	{
		BTN_TabColony->SetBackgroundColor(bColonyActive ? ActiveButtonTint : InactiveButtonTint);
	}

	if (BTN_TabColonists)
	{
		BTN_TabColonists->SetBackgroundColor(bColonistsActive ? ActiveButtonTint : InactiveButtonTint);
	}

	const TCHAR* ActiveTabName = TEXT("World");
	if (bColonyActive)
	{
		ActiveTabName = TEXT("Colony");
	}
	else if (bColonistsActive)
	{
		ActiveTabName = TEXT("Colonists");
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Tab changed: %s"), ActiveTabName);

	if (bColonistsActive)
	{
		OpenColonistsTab();
	}
}

void UWorldForgeEmbarkSetupWidget::InitializeRaceDisplay()
{
	if (TXT_RaceListHeader)
	{
		TXT_RaceListHeader->SetText(FText::FromString(TEXT("Race")));
	}

	ConfigureRaceButton(BTN_RacePlaceholder_Dwarf, RaceDisplayNameFromName(RaceDwarf), true);
	ConfigureRaceButton(BTN_RacePlaceholder_HighElf, RaceDisplayNameFromName(RaceHighElf), true);
	ConfigureRaceButton(BTN_RacePlaceholder_Human, FText::FromString(TEXT("Human")), false);
	ConfigureRaceButton(BTN_Race_ConstructFolk, RaceDisplayNameFromName(RaceConstructFolk), true);
	ConfigureRaceButton(BTN_Race_Namekian, RaceDisplayNameFromName(RaceNamekian), true);
	ConfigureRaceButton(BTN_Race_FaeKin, RaceDisplayNameFromName(RaceFaeKin), true);
	ConfigureRaceButton(BTN_Race_Tidecaller, RaceDisplayNameFromName(RaceTidecaller), true);
	ConfigureRaceButton(BTN_Race_Oni, RaceDisplayNameFromName(RaceOni), true);
	ConfigureRaceButton(BTN_Race_DarkElf, RaceDisplayNameFromName(RaceDarkElf), true);

	FGameplayTag InitialRaceTag = RequestRaceTag(RaceDwarf);
	if (const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		const FGameplayTag ManagerRaceTag = Manager->GetSelectedRaceTag();
		if (ManagerRaceTag.IsValid() && IsSupportedRaceTag(ManagerRaceTag))
		{
			InitialRaceTag = ManagerRaceTag;
		}
	}

	UpdateRaceDetails(InitialRaceTag);
}

void UWorldForgeEmbarkSetupWidget::ConfigureRaceButton(UButton* Button, const FText& Label, const bool bEnabled)
{
	if (!Button)
	{
		return;
	}

	Button->SetVisibility(bEnabled ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	Button->SetIsEnabled(bEnabled);
	Button->SetBackgroundColor(FLinearColor(0.12f, 0.135f, 0.155f, 1.0f));

	if (UTextBlock* LabelText = Cast<UTextBlock>(Button->GetContent()))
	{
		LabelText->SetText(Label);
		LabelText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.99f, 1.0f, 1.0f)));
	}
}

void UWorldForgeEmbarkSetupWidget::OpenColonistsTab()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Colonists tab opened."));

	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	TArray<FWorldForgeEmbarkColonist> Colonists;
	if (Manager)
	{
		Manager->EnsureStartingColonistsGenerated();
		Colonists = Manager->GetStartingColonists();

		if (Colonists.Num() == 0)
		{
			Manager->GenerateStartingColonists();
			Colonists = Manager->GetStartingColonists();
		}
	}

	const int32 ColonistCount = Colonists.Num();

	RebuildColonistList(Colonists);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Displayed starting colonists. Count=%d"), ColonistCount);

	if (ColonistCount <= 0)
	{
		SelectedColonistIndex = INDEX_NONE;
		RefreshSelectedColonistDetails();
		return;
	}

	if (!Colonists.IsValidIndex(SelectedColonistIndex))
	{
		SelectStartingColonist(0);
		return;
	}

	RefreshSelectedColonistDetails();
}

UPanelWidget* UWorldForgeEmbarkSetupWidget::GetColonistListPanel() const
{
	if (PNL_ColonistList)
	{
		return PNL_ColonistList;
	}

	if (VB_ColonistList)
	{
		return VB_ColonistList;
	}

	if (!WidgetTree)
	{
		return nullptr;
	}

	if (UPanelWidget* Panel = Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("PNL_ColonistList"))))
	{
		return Panel;
	}

	return Cast<UPanelWidget>(WidgetTree->FindWidget(TEXT("VB_ColonistList")));
}

void UWorldForgeEmbarkSetupWidget::RebuildColonistList(const TArray<FWorldForgeEmbarkColonist>& Colonists)
{
	UPanelWidget* ColonistListPanel = GetColonistListPanel();
	if (!ColonistListPanel)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Colonist list panel is not available."));
		return;
	}

	ColonistRowClickProxies.Reset();
	ColonistListPanel->ClearChildren();

	if (TXT_ColonistListHeader)
	{
		TXT_ColonistListHeader->SetText(FText::FromString(TEXT("Starting Colonists")));
		ColonistListPanel->AddChild(TXT_ColonistListHeader);

		if (UVerticalBoxSlot* HeaderSlot = Cast<UVerticalBoxSlot>(TXT_ColonistListHeader->Slot))
		{
			HeaderSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			HeaderSlot->SetHorizontalAlignment(HAlign_Left);
		}
	}

	for (int32 ColonistIndex = 0; ColonistIndex < Colonists.Num(); ++ColonistIndex)
	{
		if (UButton* RowButton = CreateColonistRowButton(ColonistIndex, Colonists[ColonistIndex]))
		{
			ColonistListPanel->AddChild(RowButton);

			if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(RowButton->Slot))
			{
				RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
				RowSlot->SetHorizontalAlignment(HAlign_Fill);
			}
		}
	}
}

UButton* UWorldForgeEmbarkSetupWidget::CreateColonistRowButton(const int32 ColonistIndex, const FWorldForgeEmbarkColonist& Colonist)
{
	if (!WidgetTree)
	{
		return nullptr;
	}

	UButton* RowButton = WidgetTree->ConstructWidget<UButton>(
		UButton::StaticClass(),
		*FString::Printf(TEXT("BTN_StartingColonist_%d_Runtime"), ColonistIndex));
	UTextBlock* RowText = WidgetTree->ConstructWidget<UTextBlock>(
		UTextBlock::StaticClass(),
		*FString::Printf(TEXT("TXT_StartingColonist_%d_Runtime"), ColonistIndex));
	if (!RowButton || !RowText)
	{
		return RowButton;
	}

	const FString RoleDisplay = Colonist.RoleId.IsNone()
		? FString(TEXT("Unassigned"))
		: HumanizeIdentifier(Colonist.RoleId.ToString());
	const FString RowLabel = FString::Printf(
		TEXT("%s\n%s | %s"),
		*Colonist.Name,
		*ColonistRaceDisplayString(Colonist.RaceTag),
		*RoleDisplay);
	const bool bSelected = ColonistIndex == SelectedColonistIndex;

	RowText->SetText(FText::FromString(RowLabel));
	RowText->SetJustification(ETextJustify::Left);
	RowText->SetAutoWrapText(true);
	RowText->SetColorAndOpacity(FSlateColor(FLinearColor(0.98f, 0.99f, 1.0f, 1.0f)));

	RowButton->SetBackgroundColor(
		bSelected
			? FLinearColor(0.32f, 0.38f, 0.48f, 1.0f)
			: FLinearColor(0.12f, 0.135f, 0.155f, 1.0f));
	RowButton->SetContent(RowText);

	UWorldForgeEmbarkColonistRowClickProxy* ClickProxy = NewObject<UWorldForgeEmbarkColonistRowClickProxy>(this);
	ClickProxy->Initialize(this, ColonistIndex);
	RowButton->OnClicked.AddDynamic(ClickProxy, &UWorldForgeEmbarkColonistRowClickProxy::HandleClicked);
	ColonistRowClickProxies.Add(ClickProxy);

	return RowButton;
}

void UWorldForgeEmbarkSetupWidget::RefreshColonistButtons()
{
	const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	const TArray<FWorldForgeEmbarkColonist> Colonists = Manager
		? Manager->GetStartingColonists()
		: TArray<FWorldForgeEmbarkColonist>();
	RebuildColonistList(Colonists);
}

void UWorldForgeEmbarkSetupWidget::RefreshColonistButton(const int32 ColonistIndex)
{
	RefreshColonistButtons();
}

void UWorldForgeEmbarkSetupWidget::SelectStartingColonist(const int32 ColonistIndex)
{
	FWorldForgeEmbarkColonist Colonist;
	const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager || !Manager->GetStartingColonist(ColonistIndex, Colonist))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Selected colonist index=%d failed; colonist unavailable."), ColonistIndex);
		return;
	}

	SelectedColonistIndex = ColonistIndex;
	RefreshColonistButtons();
	RefreshSelectedColonistDetails();

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Selected colonist Index=%d Name=%s"),
		SelectedColonistIndex,
		*Colonist.Name);
}

void UWorldForgeEmbarkSetupWidget::RefreshSelectedColonistDetails()
{
	FWorldForgeEmbarkColonist Colonist;
	const UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager || !Manager->GetStartingColonist(SelectedColonistIndex, Colonist))
	{
		SetTextBlockString(TXT_ColonistName, FString(TEXT("Name:")));
		SetTextBlockString(TXT_ColonistRace, FString(TEXT("Race:")));
		SetTextBlockString(TXT_ColonistAge, FString(TEXT("Age:")));
		SetTextBlockString(TXT_ColonistBackstory, FString(TEXT("Backstory:")));
		SetTextBlockString(TXT_ColonistSkills, FString(TEXT("Skills:")));
		SetTextBlockString(TXT_ColonistTraits, FString(TEXT("Traits:")));
		if (BTN_RerollSelectedColonist)
		{
			BTN_RerollSelectedColonist->SetIsEnabled(false);
		}
		return;
	}

	if (BTN_RerollSelectedColonist)
	{
		BTN_RerollSelectedColonist->SetIsEnabled(true);
	}

	SetTextBlockString(TXT_ColonistName, FString::Printf(TEXT("Name: %s"), *Colonist.Name));
	SetTextBlockString(TXT_ColonistRace, FString::Printf(TEXT("Race: %s"), *ColonistRaceDisplayString(Colonist.RaceTag)));
	SetTextBlockString(TXT_ColonistAge, FString::Printf(TEXT("Age: %d"), Colonist.Age));
	SetTextBlockString(TXT_ColonistBackstory, FString::Printf(TEXT("Backstory: %s"), *Colonist.Backstory));
	SetTextBlockString(TXT_ColonistSkills, FString::Printf(TEXT("Skills:\n%s"), *BuildColonistSkillsText(Colonist)));
	SetTextBlockString(TXT_ColonistTraits, FString::Printf(TEXT("Traits: %s"), *BuildColonistTraitsText(Colonist)));
}

void UWorldForgeEmbarkSetupWidget::HandleWorldNameTextChanged(const FText& InText)
{
	ApplyWorldNameText(InText);
}

void UWorldForgeEmbarkSetupWidget::HandleWorldNameTextCommitted(const FText& InText, ETextCommit::Type /*CommitMethod*/)
{
	ApplyWorldNameText(InText);
}

void UWorldForgeEmbarkSetupWidget::HandleSeedTextChanged(const FText& InText)
{
	ApplySeedText(InText);
}

void UWorldForgeEmbarkSetupWidget::HandleSeedTextCommitted(const FText& InText, ETextCommit::Type /*CommitMethod*/)
{
	ApplySeedText(InText);
}

void UWorldForgeEmbarkSetupWidget::HandleRandomizeSeedClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Randomize Seed clicked."));

	const FString NewSeedString = GenerateRandomShortSeedString();

	if (SeedInput)
	{
		SeedInput->SetText(FText::FromString(NewSeedString));
	}

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetSeedString(NewSeedString);
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Random seed generated: %s"), *NewSeedString);
}

void UWorldForgeEmbarkSetupWidget::HandleWorldSizeSelectionChanged(FString SelectedItem, ESelectInfo::Type /*SelectionType*/)
{
	ApplyWorldSizeSelection(SelectedItem);
}

void UWorldForgeEmbarkSetupWidget::HandleTabWorldClicked()
{
	SetActiveEmbarkTab(EEmbarkSetupTab::World);
}

void UWorldForgeEmbarkSetupWidget::HandleTabColonyClicked()
{
	SetActiveEmbarkTab(EEmbarkSetupTab::Colony);
}

void UWorldForgeEmbarkSetupWidget::HandleTabColonistsClicked()
{
	SetActiveEmbarkTab(EEmbarkSetupTab::Colonists);
}

void UWorldForgeEmbarkSetupWidget::HandleMixedColonyClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Mixed Colony clicked."));

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetColonyMode(EWorldForgeColonyMode::Mixed);
	}
}

void UWorldForgeEmbarkSetupWidget::HandleSingleRaceColonyClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Single Race Colony clicked."));

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetColonyMode(EWorldForgeColonyMode::SingleRace);
	}
}

void UWorldForgeEmbarkSetupWidget::SetPreviousWidget(UUserWidget* InWidget)
{
	PreviousWidget = InWidget;
}

void UWorldForgeEmbarkSetupWidget::RestorePreviousWidget()
{
	UUserWidget* Widget = PreviousWidget.Get();
	if (!Widget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] No previous widget registered; nothing to restore."));
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

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Restored previous widget: %s"), *Widget->GetName());
}

void UWorldForgeEmbarkSetupWidget::HandleBackClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Back clicked."));
	RestorePreviousWidget();
	DeactivateWidget();
	RemoveFromParent();
}

void UWorldForgeEmbarkSetupWidget::HandleContinueClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Continue clicked."));

	CommitCurrentFieldsToManager();

	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Continue blocked: WorldForgeEmbarkationManager is unavailable."));
		return;
	}

	FString ValidationReason;
	if (!Manager->IsEmbarkWorldConfigValid(ValidationReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Continue blocked: %s"), *ValidationReason);
		return;
	}

	const FWorldForgeEmbarkConfig CurrentConfig = Manager->GetEmbarkConfig();

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Continue accepted. Opening Embark Loadout."));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] WorldName='%s'"), *CurrentConfig.WorldName);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] SaveSlotName='%s'"), *CurrentConfig.SaveSlotName);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] SeedString='%s'"), *CurrentConfig.SeedString);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] ResolvedSeed=%d"), CurrentConfig.ResolvedSeed);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] WorldSizeGrid=%d"), CurrentConfig.WorldSizeGrid);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] ColonyMode=%s"), ColonyModeToLogString(CurrentConfig.ColonyMode));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] SelectedRaceTag='%s'"), *CurrentConfig.SelectedRaceTag.ToString());

	if (!OpenEmbarkLoadout())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Continue accepted but Embark Loadout could not be opened."));
	}
}

void UWorldForgeEmbarkSetupWidget::HandleRaceDwarfClicked()
{
	SelectRaceByName(RaceDwarf);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceHighElfClicked()
{
	SelectRaceByName(RaceHighElf);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceConstructFolkClicked()
{
	SelectRaceByName(RaceConstructFolk);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceNamekianClicked()
{
	SelectRaceByName(RaceNamekian);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceFaeKinClicked()
{
	SelectRaceByName(RaceFaeKin);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceTidecallerClicked()
{
	SelectRaceByName(RaceTidecaller);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceOniClicked()
{
	SelectRaceByName(RaceOni);
}

void UWorldForgeEmbarkSetupWidget::HandleRaceDarkElfClicked()
{
	SelectRaceByName(RaceDarkElf);
}

void UWorldForgeEmbarkSetupWidget::HandleColonist01Clicked()
{
	SelectStartingColonist(0);
}

void UWorldForgeEmbarkSetupWidget::HandleColonist02Clicked()
{
	SelectStartingColonist(1);
}

void UWorldForgeEmbarkSetupWidget::HandleColonist03Clicked()
{
	SelectStartingColonist(2);
}

void UWorldForgeEmbarkSetupWidget::HandleRerollSelectedColonistClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Reroll selected colonist clicked. Index=%d"), SelectedColonistIndex);

	if (SelectedColonistIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Reroll failed. No colonist selected."));
		return;
	}

	UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager();
	if (!Manager)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Reroll failed. Index=%d"), SelectedColonistIndex);
		return;
	}

	const int32 ColonistIndex = SelectedColonistIndex;
	if (!Manager->RerollStartingColonist(ColonistIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Reroll failed. Index=%d"), ColonistIndex);
		return;
	}

	RefreshColonistButton(ColonistIndex);
	RefreshSelectedColonistDetails();
}

void UWorldForgeEmbarkSetupWidget::SelectRaceByName(const TCHAR* RaceName)
{
	const FGameplayTag RaceTag = RequestRaceTag(RaceName);
	if (!RaceTag.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Race tag not registered: WorldForge.Race.%s"), RaceName);
		return;
	}

	SelectRaceByTag(RaceTag);
}

void UWorldForgeEmbarkSetupWidget::SelectRaceByTag(FGameplayTag RaceTag)
{
	if (!RaceTag.IsValid() || !IsSupportedRaceTag(RaceTag))
	{
		return;
	}

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetSelectedRaceTag(RaceTag);
	}

	UpdateRaceDetails(RaceTag);
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Race selected: %s"), *RaceTag.ToString());
}

void UWorldForgeEmbarkSetupWidget::UpdateRaceDetails(FGameplayTag RaceTag)
{
	if (!RaceTag.IsValid())
	{
		RaceTag = RequestRaceTag(RaceDwarf);
	}

	const FString RaceDataAssetPath = RaceDataAssetPathForTag(RaceTag);
	UObject* RaceData = LoadRaceDataAsset(RaceTag);

	if (TXT_RaceName)
	{
		TXT_RaceName->SetText(RaceDisplayNameFromTag(RaceTag));
	}

	if (TXT_RaceDescription)
	{
		TXT_RaceDescription->SetText(FText::FromString(TEXT("Description pending.")));
	}

	if (TXT_RaceStrengths)
	{
		TXT_RaceStrengths->SetText(FText::FromString(BuildStrengthsText(RaceData)));
	}

	if (TXT_RaceWeaknesses)
	{
		TXT_RaceWeaknesses->SetText(FText::FromString(BuildWeaknessesText(RaceData)));
	}

	if (TXT_RaceTraits)
	{
		TXT_RaceTraits->SetText(FText::FromString(BuildTraitsText(RaceData)));
	}

	UpdateRacePreviewForTag(RaceTag);

	if (RaceData)
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] RaceData resolved. Race=%s Asset=%s"), *RaceTag.ToString(), *RaceDataAssetPath);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] RaceData missing. Race=%s"), *RaceTag.ToString());
	}
}

void UWorldForgeEmbarkSetupWidget::UpdateRacePreviewForTag(FGameplayTag RaceTag)
{
	if (!RaceTag.IsValid())
	{
		RaceTag = RequestRaceTag(RaceDwarf);
	}

	const FString ResolvedAssetPath = ResolveRaceIdleVisualAssetPath(GetWorld(), RaceTag);
	const FString RaceName = RaceDisplayNameFromTag(RaceTag).ToString();

	if (IMG_RacePreview)
	{
		FSlateBrush PlaceholderBrush = IMG_RacePreview->GetBrush();
		PlaceholderBrush.SetResourceObject(nullptr);
		PlaceholderBrush.ImageSize = FVector2D(220.0f, 180.0f);
		IMG_RacePreview->SetBrush(PlaceholderBrush);
		IMG_RacePreview->SetDesiredSizeOverride(FVector2D(220.0f, 180.0f));
		IMG_RacePreview->SetColorAndOpacity(FLinearColor(0.08f, 0.09f, 0.105f, 1.0f));
		IMG_RacePreview->SetVisibility(ESlateVisibility::Visible);
	}

	if (TXT_RacePreviewStatus)
	{
		TXT_RacePreviewStatus->SetColorAndOpacity(FSlateColor(FLinearColor(0.82f, 0.86f, 0.9f, 1.0f)));
	}

	UPaperFlipbook* IdleFlipbook = nullptr;
	const bool bResolvedFlipbook = ResolveIdleFlipbookForRace(RaceTag, IdleFlipbook);
	const bool bAppliedStaticPreview = bResolvedFlipbook && ApplyStaticFlipbookPreview(IdleFlipbook);

	if (!ResolvedAssetPath.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Race preview resolved. Race=%s Asset=%s"), *RaceTag.ToString(), *ResolvedAssetPath);
	}

	if (bAppliedStaticPreview)
	{
		if (TXT_RacePreviewStatus)
		{
			TXT_RacePreviewStatus->SetText(FText::FromString(FString::Printf(TEXT("Preview: %s idle frame"), *RaceName)));
		}

		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetupWidget] Race preview static frame applied. Race=%s"), *RaceTag.ToString());
		return;
	}

	const FString FallbackReason = ResolvedAssetPath.IsEmpty()
		? TEXT("No idle visual asset found.")
		: TEXT("Static PaperFlipbook preview requires follow-up support for unsupported or missing first-frame sprite data.");

	if (TXT_RacePreviewStatus)
	{
		const FString PreviewStatus = ResolvedAssetPath.IsEmpty()
			? TEXT("Preview pending. No idle visual asset found.")
			: FString::Printf(TEXT("Preview asset found: %s"), *ResolvedAssetPath);
		TXT_RacePreviewStatus->SetText(FText::FromString(PreviewStatus));
	}

	UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetupWidget] Race preview fallback placeholder. Reason=%s"), *FallbackReason);
}

bool UWorldForgeEmbarkSetupWidget::ResolveIdleFlipbookForRace(FGameplayTag RaceTag, UPaperFlipbook*& OutFlipbook) const
{
	OutFlipbook = nullptr;

	if (!RaceTag.IsValid())
	{
		return false;
	}

	FWorldForgeColonistVisualRow VisualDefinition;
	if (ResolveRaceIdleVisualDefinition(GetWorld(), RaceTag, VisualDefinition) && !VisualDefinition.IdleVisualAsset.IsNull())
	{
		if (UObject* LoadedObject = VisualDefinition.IdleVisualAsset.LoadSynchronous())
		{
			OutFlipbook = Cast<UPaperFlipbook>(LoadedObject);
			if (OutFlipbook)
			{
				return true;
			}
		}
	}

	const FString FallbackAssetPath = IdlePreviewAssetPathForTag(RaceTag);
	if (FallbackAssetPath.IsEmpty())
	{
		return false;
	}

	OutFlipbook = LoadObject<UPaperFlipbook>(nullptr, *FallbackAssetPath);
	return OutFlipbook != nullptr;
}

bool UWorldForgeEmbarkSetupWidget::ApplyStaticFlipbookPreview(UPaperFlipbook* Flipbook)
{
	if (!IMG_RacePreview || !Flipbook || Flipbook->GetNumFrames() <= 0)
	{
		return false;
	}

	UPaperSprite* FirstFrameSprite = Flipbook->GetSpriteAtFrame(0);
	ISlateTextureAtlasInterface* AtlasInterfaceObject = Cast<ISlateTextureAtlasInterface>(FirstFrameSprite);
	if (!FirstFrameSprite || !AtlasInterfaceObject)
	{
		return false;
	}

	TScriptInterface<ISlateTextureAtlasInterface> AtlasInterface;
	AtlasInterface.SetObject(FirstFrameSprite);
	AtlasInterface.SetInterface(AtlasInterfaceObject);

	IMG_RacePreview->SetBrushFromAtlasInterface(AtlasInterface, false);
	IMG_RacePreview->SetDesiredSizeOverride(FVector2D(220.0f, 180.0f));
	IMG_RacePreview->SetColorAndOpacity(FLinearColor::White);
	IMG_RacePreview->SetVisibility(ESlateVisibility::Visible);
	return true;
}

UWorldForgeEmbarkationManager* UWorldForgeEmbarkSetupWidget::GetEmbarkationManager() const
{
	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UWorldForgeEmbarkationManager>() : nullptr;
}

TSubclassOf<UUserWidget> UWorldForgeEmbarkSetupWidget::ResolveEmbarkLoadoutWidgetClass() const
{
	if (EmbarkLoadoutWidgetClass)
	{
		const UClass* ConfiguredClass = EmbarkLoadoutWidgetClass.Get();
		const UClass* ConfiguredParentClass = ConfiguredClass ? ConfiguredClass->GetSuperClass() : nullptr;
		const FString ConfiguredClassPath = ConfiguredClass ? ConfiguredClass->GetPathName() : FString(TEXT("NULL"));
		const FString ConfiguredParentClassName = ConfiguredParentClass ? ConfiguredParentClass->GetName() : FString(TEXT("NULL"));
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Configured EmbarkLoadoutWidgetClass=%s ParentClass=%s"),
			*ConfiguredClassPath,
			*ConfiguredParentClassName);

		if (ConfiguredClass && ConfiguredClass->IsChildOf(UWorldForgeEmbarkLoadoutWidget::StaticClass()))
		{
			return EmbarkLoadoutWidgetClass;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkSetup] Configured EmbarkLoadoutWidgetClass is not parented to WorldForgeEmbarkLoadoutWidget; falling back to %s"),
			EmbarkLoadoutWidgetPath);
	}

	UClass* LoadedClass = LoadClass<UUserWidget>(nullptr, EmbarkLoadoutWidgetPath);
	if (!LoadedClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Embark loadout widget class not found at %s"), EmbarkLoadoutWidgetPath);
		return nullptr;
	}

	const UClass* LoadedParentClass = LoadedClass->GetSuperClass();
	const FString LoadedParentClassName = LoadedParentClass ? LoadedParentClass->GetName() : FString(TEXT("NULL"));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Loaded EmbarkLoadoutWidgetClass=%s ParentClass=%s"),
		*LoadedClass->GetPathName(),
		*LoadedParentClassName);

	if (!LoadedClass->IsChildOf(UWorldForgeEmbarkLoadoutWidget::StaticClass()))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[EmbarkSetup] WBP_WF_EmbarkLoadout is not parented to WorldForgeEmbarkLoadoutWidget; C++ bindings are not active."));
	}

	return LoadedClass;
}

bool UWorldForgeEmbarkSetupWidget::OpenEmbarkLoadout()
{
	TSubclassOf<UUserWidget> LoadoutClass = ResolveEmbarkLoadoutWidgetClass();
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] LoadoutWidgetClass path used: %s"),
		LoadoutClass ? *LoadoutClass->GetPathName() : EmbarkLoadoutWidgetPath);
	if (!LoadoutClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] CreateWidget failure: LoadoutWidgetClass is NULL."));
		return false;
	}

	const UClass* LoadoutParentClass = LoadoutClass->GetSuperClass();
	const FString LoadoutParentClassName = LoadoutParentClass ? LoadoutParentClass->GetName() : FString(TEXT("NULL"));
	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Loadout parent class: %s"), *LoadoutParentClassName);

	APlayerController* PlayerController = GetOwningPlayer();
	if (!PlayerController && GetWorld())
	{
		PlayerController = GetWorld()->GetFirstPlayerController();
	}

	if (!PlayerController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Cannot open Embark Loadout: PlayerController is unavailable."));
		return false;
	}

	UUserWidget* LoadoutWidget = CreateWidget<UUserWidget>(PlayerController, LoadoutClass);
	if (!LoadoutWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] CreateWidget failure: Failed to create Embark Loadout widget."));
		return false;
	}

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] CreateWidget success: %s"), *LoadoutWidget->GetName());

	if (UWorldForgeEmbarkLoadoutWidget* LoadoutBridge = Cast<UWorldForgeEmbarkLoadoutWidget>(LoadoutWidget))
	{
		LoadoutBridge->SetSetupWidgetToRestore(this);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] Embark Loadout widget is not parented to WorldForgeEmbarkLoadoutWidget; Back restore will use fallback behavior."));
	}

	LoadoutWidget->AddToViewport(1000);
	if (LoadoutWidget->IsInViewport())
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] AddToViewport success: %s"), *LoadoutWidget->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[EmbarkSetup] AddToViewport failure: %s is not in viewport after AddToViewport."), *LoadoutWidget->GetName());
	}

	if (UCommonActivatableWidget* CommonLoadoutWidget = Cast<UCommonActivatableWidget>(LoadoutWidget))
	{
		CommonLoadoutWidget->ActivateWidget();
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] CommonUI ActivateWidget called for Embark Loadout."));
	}

	SetVisibility(ESlateVisibility::Collapsed);
	SetIsEnabled(false);
	ActiveEmbarkLoadoutWidget = LoadoutWidget;

	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(LoadoutWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PlayerController->SetInputMode(InputMode);
	PlayerController->bShowMouseCursor = true;

	UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Embark Loadout opened: %s"), *LoadoutWidget->GetName());
	return true;
}

void UWorldForgeEmbarkSetupWidget::ApplyWorldNameText(const FText& InText)
{
	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetWorldName(InText.ToString());
	}
}

FString UWorldForgeEmbarkSetupWidget::SanitizeSeedInput(const FString& RawSeed) const
{
	const FString TrimmedSeed = RawSeed.TrimStartAndEnd();
	FString SanitizedSeed;
	SanitizedSeed.Reserve(FMath::Min(TrimmedSeed.Len(), EmbarkMaxSeedLength));

	for (int32 Index = 0; Index < TrimmedSeed.Len() && SanitizedSeed.Len() < EmbarkMaxSeedLength; ++Index)
	{
		const TCHAR Character = TrimmedSeed[Index];
		if (IsAllowedManualSeedCharacter(Character))
		{
			SanitizedSeed.AppendChar(Character);
		}
	}

	return SanitizedSeed.IsEmpty() ? FString(TEXT("12345")) : SanitizedSeed;
}

void UWorldForgeEmbarkSetupWidget::ApplySeedText(const FText& InText)
{
	const FString RawSeedString = InText.ToString();
	const FString SanitizedSeedString = SanitizeSeedInput(RawSeedString);

	if (!RawSeedString.Equals(SanitizedSeedString, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogTemp, Log, TEXT("[EmbarkSetup] Seed sanitized from '%s' to '%s'"),
			*MakeSeedLogPreview(RawSeedString),
			*SanitizedSeedString);
	}

	if (SeedInput && !SeedInput->GetText().ToString().Equals(SanitizedSeedString, ESearchCase::CaseSensitive) && !bApplyingSanitizedSeedText)
	{
		bApplyingSanitizedSeedText = true;
		SeedInput->SetText(FText::FromString(SanitizedSeedString));
		bApplyingSanitizedSeedText = false;
	}

	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetSeedString(SanitizedSeedString);
	}
}

void UWorldForgeEmbarkSetupWidget::ApplyWorldSizeSelection(const FString& SelectedItem)
{
	if (UWorldForgeEmbarkationManager* Manager = GetEmbarkationManager())
	{
		Manager->SetWorldSize(ResolveWorldSizeGridFromLabel(SelectedItem));
	}
}

void UWorldForgeEmbarkSetupWidget::CommitCurrentFieldsToManager()
{
	if (WorldNameInput)
	{
		ApplyWorldNameText(WorldNameInput->GetText());
	}

	if (SeedInput)
	{
		ApplySeedText(SeedInput->GetText());
	}

	if (WorldSizeDropdown)
	{
		ApplyWorldSizeSelection(WorldSizeDropdown->GetSelectedOption());
	}
}
