#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "WFFrontendGameMode.generated.h"

class UUserWidget;
class APlayerController;

/**
 * Frontend-only GameMode.
 *
 * Purpose:
 * - Used only by menu/frontend maps.
 * - Spawns the starting UI screen automatically.
 * - Prevents the frontend from depending on WorldGenBootstrapActor, VoxelWorld, ColonyManager, etc.
 *
 * Expected use:
 * - Create BP_FrontendGameMode from this class.
 * - Set MainConfigWidgetClass to WBP_WF_WorldConfig.
 * - Set Frontend_Map World Settings -> GameMode Override = BP_FrontendGameMode.
 */
UCLASS()
class WORLDFORGE_API AWFFrontendGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AWFFrontendGameMode();

protected:
	/**
	 * PostLogin is used instead of BeginPlay because the PlayerController is guaranteed here.
	 */
	virtual void PostLogin(APlayerController* NewPlayer) override;

	/**
	 * First widget shown when the frontend map loads.
	 *
	 * Set this in BP_FrontendGameMode defaults.
	 * Example:
	 *   WBP_WF_WorldConfig
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WorldForge|Frontend")
	TSubclassOf<UUserWidget> MainConfigWidgetClass;

private:
	/**
	 * Keeps the active widget alive while this frontend GameMode is active.
	 */
	UPROPERTY()
	TObjectPtr<UUserWidget> ActiveWidget;

	void SpawnFrontendWidget(APlayerController* PlayerController);
};