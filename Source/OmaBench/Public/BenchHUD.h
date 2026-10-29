#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BenchHUD.generated.h"

// Terminal-style overlay: boot text, live run status, final readout.
UCLASS()
class OMABENCH_API ABenchHUD : public AHUD
{
	GENERATED_BODY()
public:
	ABenchHUD();
	virtual void DrawHUD() override;
	UPROPERTY() TObjectPtr<class UFont> MonoFont;
private:
	void Line(const FString& S, float X, float Y, const FLinearColor& C, float Scale);
};
