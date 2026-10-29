#include "BenchHUD.h"
#include "BenchGameMode.h"
#include "Engine/Canvas.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "RHI.h"
#include "UObject/ConstructorHelpers.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "SysMon.h"

// Bitcount Grid Double (OFL, Content/OmaBench/Fonts/OFL.txt), loaded straight from the
// staged TTF as a runtime composite font; rebuilt at the pixel size the canvas needs so
// the bitmap-style glyphs stay crisp. Falls back to DroidSansMono if the file is absent.
void ABenchHUD::EnsureFont(float Sc)
{
	const int32 Px = FMath::Clamp(FMath::RoundToInt(13.f * Sc), 8, 64);
	if (GridFont && GridFontPx == Px) return;
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("OmaBench/Fonts/BitcountGridDouble-Medium.ttf"));
	if (!FPaths::FileExists(Path)) { GridFontPx = Px; return; }
	UFont* F = NewObject<UFont>(this);
	F->FontCacheType = EFontCacheType::Runtime;
	FTypefaceEntry& E = F->CompositeFont.DefaultTypeface.Fonts.AddDefaulted_GetRef();
	E.Name = TEXT("Regular");
	E.Font = FFontData(Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
	F->LegacyFontSize = Px;
	GridFont = F; GridFontPx = Px;
}

ABenchHUD::ABenchHUD()
{
	static ConstructorHelpers::FObjectFinder<UFont> Mono(TEXT("/Engine/EngineFonts/DroidSansMono.DroidSansMono"));
	MonoFont = Mono.Object;
}

static float GridScale = 1.f;
void ABenchHUD::Line(const FString& S, float X, float Y, const FLinearColor& C, float Scale)
{
	FCanvasTextItem T(FVector2D(X, Y), FText::FromString(S), GridFont ? GridFont.Get() : MonoFont.Get(), C);
	T.Scale = FVector2D(GridFont ? Scale / FMath::Max(1.f, GridScale) : Scale); T.EnableShadow(FLinearColor(C.R, C.G, C.B, 0.35f), FVector2D(0, 0)); T.bOutlined = false;
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
	const float Sc = FMath::Max(1.f, Canvas->ClipY / 1080.f) * 1.1f, LH = 20.f * Sc, X = 48.f * Sc, W = Canvas->ClipX, H = Canvas->ClipY;
	EnsureFont(Sc); GridScale = Sc;
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
	const FString Serial = FString::Printf(TEXT("OMB3D-%s // NEXUS-%s"), *GM->GetTestId(), *GM->GetRunId().Right(6));

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
		L(GM->IsAborted() ? TEXT("ESPER // RUN ABORTED") : TEXT("ESPER // ANALYSIS COMPLETE"), bx, by, Am); L(Serial, bx + bw - 330 * Sc, by, Mu, 0.8f);
		Rule(bx, by + LH * 1.1f, bw - 28 * Sc, A(Te, 0.4f));
		float Y = by + LH * 1.5f;
		for (const FString& S : R) { L(S, bx, Y, Fg); Y += LH; }
		Y += LH * 0.4f; L(FString::Printf(TEXT("SUBJECT %s // %s // %s"), *GM->GetMonitorTag(), *GM->GetThemeName().ToUpper(), GM->IsAborted() ? TEXT("ESCAPED") : TEXT("RETIRED")), bx, Y, Te, 0.8f);
		if (GM->IsAborted()) { Y += LH; }
		if (bBlink) L(TEXT("_"), bx, Y + LH, Am);
		return;
	}
	if (T < 4.0)
	{
		const FString Boot[] = {
			TEXT("TYRELL-OMABENCH ESPER  v0.2  // LUTZLSTEIN SYSTEMS 2019-2026"),
			FString::Printf(TEXT("UE 5.8 :: VULKAN :: PALETTE %s"), *GM->GetThemeName().ToUpper()),
			FString::Printf(TEXT("GPU0 ... %s  [OK]"), *GRHIAdapterName.ToUpper()),
			FString::Printf(TEXT("SCENE %s :: %s // %s [LOCKED]"), *GM->GetTestId(), *GM->GetTestName().ToUpper(), *Serial),
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
	L(FString::Printf(TEXT("%s%s // %s // %s"), GM->GetTestCount() > 1 ? *FString::Printf(TEXT("[%d/%d] "), GM->GetTestIndex() + 1, GM->GetTestCount()) : TEXT(""), *GM->GetTestName().ToUpper(), bWarm ? TEXT("CALIBRATE") : TEXT("SCAN"), *GM->GetMonitorTag()), X, 34 * Sc + LH, A(Te, 0.85f), 0.8f);
	// Forensic telemetry column, top-right, label/value pairs.
	float SysBottom = 24 * Sc;
	if (GM->GetSysMon()) DrawSysMon(GM, W - 40 * Sc, 34 * Sc, Sc, SysBottom);
	const float px = W - 380 * Sc, py = SysBottom + 24 * Sc, pw = 320 * Sc;
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

// btop-style SYSMON box: total CPU bar + sparkline, GPU load/VRAM/temp/clock/power, RAM.
void ABenchHUD::DrawSysMon(const ABenchGameMode* GM, float Right, float Top, float Sc, float& OutBottom)
{
	const FSysSnapshot S = GM->GetSysMon()->Get();
	const FLinearColor Fg = GM->Pal(TEXT("foreground"), FLinearColor(0.9f, 0.8f, 0.6f));
	const FLinearColor Am = GM->Pal(TEXT("bright_yellow"), FLinearColor(1.f, 0.65f, 0.f));
	const FLinearColor Te = GM->Pal(TEXT("bright_cyan"), GM->Pal(TEXT("cyan"), FLinearColor(0.2f, 1.f, 0.85f)));
	const FLinearColor Rd = GM->Pal(TEXT("bright_red"), FLinearColor(1.f, 0.3f, 0.3f));
	const float W = 440 * Sc, X = Right - W, LH = 20 * Sc, Pad = 12 * Sc, BarW = 150 * Sc, BarH = 9 * Sc, SpH = 30 * Sc;
	const float H = Pad * 2 + LH * 6 + SpH * 2 + 10 * Sc;
	Tile(Canvas, X, Top, W, H, FLinearColor(0, 0, 0, 0.72f));
	Brackets(X, Top, W, H, 14 * Sc, A(Te, 0.75f));
	auto Txt = [&](const FString& Str, float x, float y, const FLinearColor& C, float s = 1.f) { Line(Str, x, y, C, Sc * s); };
	auto Heat = [&](float P) { return P < 50 ? Te : (P < 85 ? Am : Rd); };
	auto Bar = [&](float x, float y, float P) {
		Tile(Canvas, x, y, BarW, BarH, A(Te, 0.12f));
		const int32 Cells = 20, Fill = P < 0 ? 0 : FMath::Clamp(FMath::RoundToInt(P / 100.f * Cells), 0, Cells);
		const float cw = BarW / Cells;
		for (int32 i = 0; i < Fill; ++i) Tile(Canvas, x + i * cw + 1, y + 1, cw - 2, BarH - 2, A(Heat(100.f * (i + 1) / Cells), 0.9f));
	};
	auto Spark = [&](float x, float y, float w, const TArray<float>& Hst, const FLinearColor& C) {
		Tile(Canvas, x, y, w, SpH, A(C, 0.05f));
		const float cw = w / FSysMon::HistLen;
		for (int32 i = 0; i < Hst.Num(); ++i) { const float h = FMath::Max(1.f, SpH * Hst[i] / 100.f);
			const float xi = x + w - (Hst.Num() - i) * cw; Tile(Canvas, xi, y + SpH - h, FMath::Max(1.f, cw - 1), h, A(Heat(Hst[i]), 0.75f)); }
	};
#define V(v, F) ((v) < 0 ? FString(TEXT("--")) : FString::Printf(TEXT(F), (v)))
	float y = Top + Pad; const float lx = X + Pad, bx = X + Pad + 70 * Sc, vx = bx + BarW + 12 * Sc;
	Txt(TEXT("SYSMON"), lx, y, Am, 0.9f); Txt(FString::Printf(TEXT("2HZ  %s"), S.GpuSource.IsEmpty() ? TEXT("NO GPU SENSOR") : *S.GpuSource), X + W - Pad - 150 * Sc, y, A(Fg, 0.5f), 0.75f);
	y += LH + 2 * Sc;
	Txt(TEXT("CPU"), lx, y, Te, 0.9f); Bar(bx, y + 4 * Sc, S.CpuUtil);
	Txt(FString::Printf(TEXT("%s  %s  %s"), *V(S.CpuUtil, "%3.0f%%"), *V(S.CpuTempC, "%2.0fC"), *V(S.CpuMHz / 1000.f, "%.1fGHZ")), vx, y, Fg, 0.85f);
	y += LH; Spark(lx, y, W - 2 * Pad, S.CpuHist, Te); y += SpH + 6 * Sc;
	Txt(TEXT("GPU"), lx, y, Te, 0.9f); Bar(bx, y + 4 * Sc, S.GpuUtil);
	Txt(FString::Printf(TEXT("%s  %s  %s"), *V(S.GpuUtil, "%3.0f%%"), *V(S.GpuTempC, "%2.0fC"), *V(S.GpuPowerW, "%3.0fW")), vx, y, Fg, 0.85f);
	y += LH; Txt(FString::Printf(TEXT("CLK %s"), *V(S.GpuMHz, "%4.0fMHZ")), bx, y, A(Fg, 0.7f), 0.8f);
	y += LH; Spark(lx, y, W - 2 * Pad, S.GpuHist, Am); y += SpH + 6 * Sc;
	auto MemRow = [&](const TCHAR* K, float U, float T) {
		Txt(K, lx, y, Te, 0.9f); Bar(bx, y + 4 * Sc, T > 0 ? 100.f * U / T : -1.f);
		Txt(T > 0 ? FString::Printf(TEXT("%.1f/%.0fG"), U, T) : FString(TEXT("--")), vx, y, Fg, 0.85f); y += LH; };
	MemRow(TEXT("VRAM"), S.VramUsedGiB, S.VramTotalGiB);
	MemRow(TEXT("RAM"), S.RamUsedGiB, S.RamTotalGiB);
	OutBottom = Top + H;
#undef V
}
