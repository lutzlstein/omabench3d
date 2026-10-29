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
static FLinearColor A(const FLinearColor& C, float Alpha) { return FLinearColor(C.R, C.G, C.B, Alpha); }

// Esper-style corner brackets: four L-shaped ticks, no filled frame.
void ABenchHUD::Brackets(float X, float Y, float W, float H, float L, const FLinearColor& C)
{
	const float T = 1.f;
	Canvas->K2_DrawLine(FVector2D(X, Y), FVector2D(X + L, Y), T, C);         Canvas->K2_DrawLine(FVector2D(X, Y), FVector2D(X, Y + L), T, C);
	Canvas->K2_DrawLine(FVector2D(X + W, Y), FVector2D(X + W - L, Y), T, C); Canvas->K2_DrawLine(FVector2D(X + W, Y), FVector2D(X + W, Y + L), T, C);
	Canvas->K2_DrawLine(FVector2D(X, Y + H), FVector2D(X + L, Y + H), T, C); Canvas->K2_DrawLine(FVector2D(X, Y + H), FVector2D(X, Y + H - L), T, C);
	Canvas->K2_DrawLine(FVector2D(X + W, Y + H), FVector2D(X + W - L, Y + H), T, C); Canvas->K2_DrawLine(FVector2D(X + W, Y + H), FVector2D(X + W, Y + H - L), T, C);
}
static void Tile(UCanvas* C, float X, float Y, float W, float H, const FLinearColor& Col)
{
	FCanvasTileItem T(FVector2D(X, Y), FVector2D(W, H), Col); T.BlendMode = SE_BLEND_Translucent; C->DrawItem(T);
}
void ABenchHUD::Rule(float X, float Y, float W, const FLinearColor& C) { Canvas->K2_DrawLine(FVector2D(X, Y), FVector2D(X + W, Y), 1.f, C); }

void ABenchHUD::DrawHUD()
{
	Super::DrawHUD();
	const ABenchGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<ABenchGameMode>() : nullptr;
	if (!GM || !GM->HudEnabled() || !Canvas || !MonoFont) return;
	const float Sc = FMath::Max(1.f, Canvas->ClipY / 1080.f) * 1.1f, LH = 19.f * Sc, X = 48.f * Sc, W = Canvas->ClipX, H = Canvas->ClipY;
	const FLinearColor Fg = GM->Pal(TEXT("foreground"), FLinearColor(0.9f, 0.8f, 0.6f));
	const FLinearColor Am = GM->Pal(TEXT("bright_yellow"), FLinearColor(1.f, 0.65f, 0.f));
	const FLinearColor Te = GM->Pal(TEXT("bright_cyan"), GM->Pal(TEXT("cyan"), FLinearColor(0.2f, 1.f, 0.85f)));
	const FLinearColor Mu = GM->Pal(TEXT("muted"), FLinearColor(0.4f, 0.4f, 0.4f));
	const double Now = FPlatformTime::Seconds(), T = GM->GetElapsed();
	const bool bBlink = FMath::Fmod(Now, 1.0) < 0.5;
	// Subtle phosphor flicker + interlace: alternate frames nudge text alpha/1px.
	const float Flick = 0.86f + 0.08f * FMath::Sin(Now * 53.0) + ((GFrameCounter & 7) == 0 ? -0.12f : 0.f);
	const float IL = (GFrameCounter & 1) ? 0.f : 1.f;
	auto L = [&](const FString& S, float x, float y, const FLinearColor& C, float s = 1.f) { Line(S, x, y + IL * 0.5f, A(C, C.A * Flick), Sc * s); };
	const FString Serial = FString::Printf(TEXT("OMB-T1 // NEXUS-%s"), *GM->GetRunId().Right(6));

	// Faint forensic grid + centre crosshair (lines only; ~30 draws).
	const FLinearColor Grid = A(Te, 0.07f); const FLinearColor Back(0.f, 0.f, 0.f, 0.7f);
	for (float gx = W / 8; gx < W - 1; gx += W / 8) Tile(Canvas, gx, 0, 1, H, Grid);
	for (float gy = H / 6; gy < H - 1; gy += H / 6) Tile(Canvas, 0, gy, W, 1, Grid);
	const FLinearColor Cr = A(Te, 0.22f); const float cx = W / 2, cy = H / 2, cl = 18 * Sc, cg = 6 * Sc;
	Canvas->K2_DrawLine(FVector2D(cx - cl, cy), FVector2D(cx - cg, cy), 1.f, Cr); Canvas->K2_DrawLine(FVector2D(cx + cg, cy), FVector2D(cx + cl, cy), 1.f, Cr);
	Canvas->K2_DrawLine(FVector2D(cx, cy - cl), FVector2D(cx, cy - cg), 1.f, Cr); Canvas->K2_DrawLine(FVector2D(cx, cy + cg), FVector2D(cx, cy + cl), 1.f, Cr);
	Brackets(16 * Sc, 16 * Sc, W - 32 * Sc, H - 32 * Sc, 40 * Sc, A(Am, 0.55f));

	if (GM->GetPhase() == EBenchPhase::Done)
	{
		const TArray<FString>& R = GM->GetResultLines();
		const float bx = X, by = 110 * Sc, bw = 820 * Sc, bh = (R.Num() + 4) * LH + 20 * Sc;
		Tile(Canvas, bx - 14 * Sc, by - 14 * Sc, bw, bh, Back); Brackets(bx - 14 * Sc, by - 14 * Sc, bw, bh, 22 * Sc, Am);
		L(TEXT("ESPER // ANALYSIS COMPLETE"), bx, by, Am); L(Serial, bx + bw - 330 * Sc, by, Mu, 0.8f);
		Rule(bx, by + LH * 1.1f, bw - 28 * Sc, A(Te, 0.4f));
		float Y = by + LH * 1.5f;
		for (const FString& S : R) { L(S, bx, Y, Fg); Y += LH; }
		Y += LH * 0.4f; L(FString::Printf(TEXT("SUBJECT %s // %s // RETIRED"), *GM->GetMonitorTag(), *GM->GetThemeName().ToUpper()), bx, Y, Te, 0.8f);
		if (bBlink) L(TEXT("_"), bx, Y + LH, Am);
		return;
	}
	if (T < 4.0)
	{
		const FString Boot[] = {
			TEXT("TYRELL-OMABENCH ESPER  v0.2  // LUTZLSTEIN SYSTEMS 2019-2026"),
			FString::Printf(TEXT("UE 5.8 :: VULKAN :: PALETTE %s"), *GM->GetThemeName().ToUpper()),
			FString::Printf(TEXT("GPU0 ... %s  [OK]"), *GRHIAdapterName.ToUpper()),
			FString::Printf(TEXT("SUBJECT %s ......... [LOCKED]"), *Serial),
			TEXT("VOIGHT-KAMPFF CALIBRATION .......... [OK]"),
			TEXT("> ENHANCE"),
		};
		const int32 Shown = FMath::Clamp(int32(T / 0.5) + 1, 1, 6);
		float Y = 90.f * Sc;
		Tile(Canvas, X - 14 * Sc, Y - LH * 1.6f, 900 * Sc, LH * 8.6f, Back);
		L(TEXT("BOOT // DIAG 00"), X, Y - LH * 1.3f, A(Te, 0.8f), 0.8f);
		for (int32 i = 0; i < Shown; ++i) { L(Boot[i], X, Y, i == 5 ? Am : (i == 3 ? Te : Fg)); Y += LH; }
		if (bBlink) L(TEXT("_"), X, Y, Am);
		return;
	}
	const bool bWarm = GM->GetPhase() != EBenchPhase::Running;
	const double Pct = FMath::Clamp(T / GM->GetTotal(), 0.0, 1.0), Fps = GM->GetLiveFps();
	// Header strip
	Tile(Canvas, X - 12 * Sc, 26 * Sc, 600 * Sc, LH * 2.4f, Back);
	L(Serial, X, 34 * Sc, Am);
	L(FString::Printf(TEXT("NIGHT CITY // %s // %s"), bWarm ? TEXT("CALIBRATE") : TEXT("SCAN"), *GM->GetMonitorTag()), X, 34 * Sc + LH, A(Te, 0.85f), 0.8f);
	// Forensic telemetry column, top-right, label/value pairs.
	const float px = W - 380 * Sc, py = 34 * Sc, pw = 320 * Sc;
	Tile(Canvas, px - 12 * Sc, py - 10 * Sc, pw + 4 * Sc, LH * 5.6f, Back);
	Brackets(px - 12 * Sc, py - 10 * Sc, pw + 4 * Sc, LH * 5.6f, 14 * Sc, A(Te, 0.7f));
	auto Row = [&](int32 i, const TCHAR* K, const FString& V) { L(K, px, py + i * LH, A(Fg, 0.65f), 0.85f); L(V, px + 140 * Sc, py + i * LH, i == 0 ? Am : Fg); };
	Row(0, TEXT("FPS"), FString::Printf(TEXT("%6.1f"), Fps));
	Row(1, TEXT("FRAME MS"), FString::Printf(TEXT("%6.2f"), Fps > 0 ? 1000.0 / Fps : 0.0));
	Row(2, TEXT("PASS"), FString::Printf(TEXT("  %d/%d"), GM->GetPass(), GM->GetPasses()));
	Row(3, TEXT("ELAPSED"), FString::Printf(TEXT("%s/%s"), *Clock(T), *Clock(GM->GetTotal())));
	Row(4, TEXT("GRID"), FString::Printf(TEXT(" %c%02d-%c%02d"), TCHAR('A' + int32(Pct * 7.99)), int32(Pct * 99), TCHAR('F' - int32(Pct * 5.99)), int32(Now * 7) % 100));
	// Progress: thin rule with tick marks.
	const float by = H - 52 * Sc, bw = W - 2 * X - 200 * Sc;
	Rule(X, by, bw, A(Mu, 0.6f)); Canvas->K2_DrawLine(FVector2D(X, by), FVector2D(X + bw * Pct, by), 2.f, Am);
	for (int32 k = 0; k <= 10; ++k) Canvas->K2_DrawLine(FVector2D(X + bw * k / 10, by - 4 * Sc), FVector2D(X + bw * k / 10, by + 4 * Sc), 1.f, A(Te, 0.4f));
	L(FString::Printf(TEXT("TRACK %3d%%"), int32(Pct * 100)), X, by + 8 * Sc, Te, 0.8f);
	L(TEXT("ESC // ABORT"), W - X - 150 * Sc, by - 8 * Sc, A(Te, 0.7f), 0.8f);
}
