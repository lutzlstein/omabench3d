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
	UPROPERTY() TObjectPtr<class UFont> GridFont;
	int32 GridFontPx = 0;
private:
	void Line(const FString& S, float X, float Y, const FLinearColor& C, float Scale);
	void Brackets(float X, float Y, float W, float H, float L, const FLinearColor& C);
	void EnsureFont(float Sc);
	void DrawSysMon(const class ABenchGameMode* GM, float Right, float Top, float Sc, float& OutBottom);
	void Rule(float X, float Y, float W, const FLinearColor& C);
};
