#include "BenchHUD.h"
#include "BenchGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "RHI.h"
#include "UObject/ConstructorHelpers.h"

ABenchHUD::ABenchHUD()
{
	static ConstructorHelpers::FObjectFinder<UFont> Mono(TEXT("/Engine/EngineFonts/DroidSansMono.DroidSansMono"));
	MonoFont = Mono.Object;
}

void ABenchHUD::Line(const FString& S, float X, float Y, const FLinearColor& C, float Scale)
{
	FCanvasTextItem T(FVector2D(X, Y), FText::FromString(S), MonoFont, C);
	T.Scale = FVector2D(Scale); T.EnableShadow(FLinearColor(C.R, C.G, C.B, 0.35f), FVector2D(0, 0)); T.bOutlined = false;
	Canvas->DrawItem(T);
}

static FString Clock(double S) { const int32 I = FMath::Max(0, FMath::FloorToInt(S)); return FString::Printf(TEXT("%02d:%02d"), I / 60, I % 60); }

void ABenchHUD::DrawHUD()
{
	Super::DrawHUD();
	const ABenchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABenchGameMode>() : nullptr;
	if (!GM || !GM->HudEnabled() || !Canvas || !MonoFont) return;
	const float Sc = FMath::Max(1.f, Canvas->ClipY / 1080.f) * 1.25f, LH = 20.f * Sc, X = 36.f * Sc;
	const FLinearColor Fg = GM->Pal(TEXT("foreground"), FLinearColor(0.2f, 1.f, 0.4f));
	const FLinearColor Ac = GM->Pal(TEXT("accent"), FLinearColor(1.f, 0.7f, 0.f));
	const FLinearColor Am = GM->Pal(TEXT("bright_yellow"), FLinearColor(1.f, 0.7f, 0.f));
	const FLinearColor Mu = GM->Pal(TEXT("muted"), FLinearColor(0.3f, 0.5f, 0.3f));
	const double T = GM->GetElapsed();
	const bool bBlink = FMath::Fmod(FPlatformTime::Seconds(), 1.0) < 0.5;

	if (GM->GetPhase() == EBenchPhase::Done)
	{
		// Final readout panel
		Canvas->K2_DrawBox(FVector2D(X - 12*Sc, 120*Sc - 12*Sc), FVector2D(760*Sc, (GM->GetResultLines().Num() + 2) * LH + 24*Sc), 2.f, Ac);
		float Y = 120.f * Sc;
		Line(TEXT("> OMABENCH :: RUN COMPLETE"), X, Y, Am, Sc); Y += LH * 1.5f;
		for (const FString& L : GM->GetResultLines()) { Line(L, X, Y, Fg, Sc); Y += LH; }
		if (bBlink) Line(TEXT("_"), X, Y, Fg, Sc);
		return;
	}
	if (T < 4.0)
	{
		// Boot text, typed out line by line during unscored warmup.
		const FString Boot[] = {
			TEXT("OMABENCH BIOS v0.1.0  (C) 1984-2026 LUTZLSTEIN SYSTEMS"),
			FString::Printf(TEXT("UNREAL ENGINE 5.8 // VULKAN // THEME %s"), *GM->GetThemeName().ToUpper()),
			FString::Printf(TEXT("GPU0 ... %s  [OK]"), *GRHIAdapterName.ToUpper()),
			TEXT("LOADING T1 :: NIGHT CITY ............. [OK]"),
			TEXT("CALIBRATING PHOSPHOR ................ [OK]"),
			TEXT("> ENGAGE"),
		};
		const int32 Shown = FMath::Clamp(int32(T / 0.5) + 1, 1, 6);
		float Y = 80.f * Sc;
		for (int32 i = 0; i < Shown; ++i) { Line(Boot[i], X, Y, i == 5 ? Am : Fg, Sc); Y += LH; }
		if (bBlink) Line(TEXT("_"), X, Y, Fg, Sc);
		return;
	}
	const bool bWarm = GM->GetPhase() != EBenchPhase::Running;
	const double Pct = FMath::Clamp(T / GM->GetTotal(), 0.0, 1.0);
	Line(FString::Printf(TEXT("[OMABENCH] T1 NIGHT CITY  PASS %d/%d  %s"), GM->GetPass(), GM->GetPasses(), bWarm ? TEXT("WARMUP") : TEXT("SCORING")), X, 28*Sc, Am, Sc);
	Line(FString::Printf(TEXT("T+%s/%s  FPS %4.0f"), *Clock(T), *Clock(GM->GetTotal()), GM->GetLiveFps()), X, 28*Sc + LH, Fg, Sc);
	const int32 Bars = 32, Fill = int32(Pct * Bars);
	Line(FString::Printf(TEXT("[%s%s] %3d%%"), *FString::ChrN(Fill, '#'), *FString::ChrN(Bars - Fill, '.'), int32(Pct * 100)), X, Canvas->ClipY - 48*Sc, Ac, Sc);
	Line(TEXT("ESC: ABORT"), Canvas->ClipX - 180*Sc, Canvas->ClipY - 48*Sc, Mu, Sc);
}
