#include "BenchGameMode.h"
#include "BenchHUD.h"
#include "Engine/LocalFogVolume.h"
#include "Components/LocalFogVolumeComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "SysMon.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "InputCoreTypes.h"
#include "EngineUtils.h"
#include "Engine/PostProcessVolume.h"
#include "Camera/CameraActor.h"
#include "Components/InstancedStaticMeshComponent.h"

// Esc pre-processor: sees the key before the viewport, console or any binding, in
// fullscreen and windowed, regardless of which Slate widget has focus.
class FEscAbortProcessor : public IInputProcessor
{
public:
	TWeakObjectPtr<ABenchGameMode> GM;
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleKeyDownEvent(FSlateApplication&, const FKeyEvent& E) override
	{
		if (E.GetKey() == EKeys::Escape) { if (GM.IsValid()) GM->AbortRun(); return true; }
		return false;
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("OmaBenchEscAbort"); }
};

DEFINE_LOG_CATEGORY_STATIC(LogOmaBench, Log, All);
static const TCHAR* GAppVersion = TEXT("0.1.0");

ABenchGameMode::ABenchGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	DefaultPawnClass = nullptr;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Mat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tower(TEXT("/Game/OmaBench/Materials/M_Tower.M_Tower"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Sign(TEXT("/Game/OmaBench/Materials/M_Sign.M_Sign"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Street(TEXT("/Game/OmaBench/Materials/M_Street.M_Street"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Rain(TEXT("/Game/OmaBench/Materials/M_Rain.M_Rain"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Crt(TEXT("/Game/OmaBench/Materials/PP_CRT.PP_CRT"));
	CubeMesh = Cube.Object; BaseMaterial = Mat.Object;
	TowerMaterial = Tower.Object; SignMaterial = Sign.Object; StreetMaterial = Street.Object; RainMaterial = Rain.Object; CrtMaterial = Crt.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MChrome(TEXT("/Game/OmaBench/Materials/M_Chrome.M_Chrome"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MGlass(TEXT("/Game/OmaBench/Materials/M_Glass.M_Glass"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MEmit(TEXT("/Game/OmaBench/Materials/M_Emit.M_Emit"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MHolo(TEXT("/Game/OmaBench/Materials/M_Holo.M_Holo"));
	ChromeMaterial = MChrome.Object; GlassMaterial = MGlass.Object; EmitMaterial = MEmit.Object; HoloMaterial = MHolo.Object;
	HUDClass = ABenchHUD::StaticClass();
}

void ABenchGameMode::ParseArgs()
{
	const TCHAR* Cmd = FCommandLine::Get();
	FParse::Value(Cmd, TEXT("-passes="), Passes);
	if (FParse::Param(Cmd, TEXT("quick"))) Passes = 1;
	Passes = FMath::Clamp(Passes, 1, 10);
	FParse::Value(Cmd, TEXT("-warmup="), WarmupS);
	FParse::Value(Cmd, TEXT("-duration="), DurationS);
	FString Res;
	if (FParse::Value(Cmd, TEXT("-res="), Res)) { FString A, B; if (Res.Split(TEXT("x"), &A, &B)) { ResX = FCString::Atoi(*A); ResY = FCString::Atoi(*B); } }
	bFullscreen = FParse::Param(Cmd, TEXT("fullscreen"));
	bNoHud = FParse::Param(Cmd, TEXT("nohud"));
	bNoSysmon = FParse::Param(Cmd, TEXT("nosysmon"));
	FParse::Value(Cmd, TEXT("-monitor="), MonitorName); FParse::Value(Cmd, TEXT("-refresh="), MonitorHz);
	bNoPostFx = FParse::Param(Cmd, TEXT("nopostfx"));
	PixelSize = FMath::Max(1, FMath::RoundToInt(ResY / 360.f));
	FParse::Value(Cmd, TEXT("-pixel="), PixelSize); PixelSize = FMath::Clamp(PixelSize, 0, 16);
	FString Tests; FParse::Value(Cmd, TEXT("-tests="), Tests, /*bShouldStopOnSeparator*/ false);
	{
		TArray<FString> Parts; Tests.ToUpper().ParseIntoArray(Parts, TEXT(","));
		for (const FString& T : Parts) if ((T == TEXT("T1") || T == TEXT("T2") || T == TEXT("T3")) && !TestIds.Contains(T)) TestIds.Add(T);
		else if (!T.IsEmpty() && T != TEXT("T1") && T != TEXT("T2") && T != TEXT("T3")) UE_LOG(LogOmaBench, Warning, TEXT("Unknown/unimplemented test %s skipped."), *T);
		if (TestIds.IsEmpty()) TestIds.Add(TEXT("T1"));
	}
	const FString Dir = FPaths::Combine(FPlatformMisc::GetEnvironmentVariable(TEXT("HOME")), TEXT(".local/share/omabench3d/results"));
	if (!FParse::Value(Cmd, TEXT("-runid="), RunId)) RunId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
	if (!FParse::Value(Cmd, TEXT("-results="), ResultsPath)) ResultsPath = FPaths::Combine(Dir, RunId + TEXT(".json"));
	if (!FParse::Value(Cmd, TEXT("-progress="), ProgressPath)) ProgressPath = FPaths::Combine(Dir, TEXT("progress.json"));
	StartedAt = FDateTime::UtcNow().ToIso8601();
}

void ABenchGameMode::ApplyVideoSettings()
{
	auto Set = [](const TCHAR* N, const TCHAR* V){ if (IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(N)) C->Set(V, ECVF_SetByCode); };
	Set(TEXT("r.VSync"), TEXT("0")); Set(TEXT("t.MaxFPS"), TEXT("0")); Set(TEXT("r.DynamicRes.OperationMode"), TEXT("0"));
	Set(TEXT("r.ScreenPercentage"), TEXT("100")); Set(TEXT("r.MotionBlurQuality"), TEXT("0"));
	if (UGameUserSettings* S = GEngine ? GEngine->GetGameUserSettings() : nullptr)
	{
		S->SetOverallScalabilityLevel(3);
		S->SetVSyncEnabled(false);
		S->SetFrameRateLimit(0.f);
		S->SetScreenResolution(FIntPoint(ResX, ResY));
		S->SetFullscreenMode(bFullscreen ? EWindowMode::Fullscreen : EWindowMode::Windowed);
		S->ApplySettings(false);
	}
	if (GEngine) GEngine->bUseFixedFrameRate = false, GEngine->bSmoothFrameRate = false;
}

void ABenchGameMode::LoadPalette()
{
	// Omarchy theme palette (sRGB hex in colors.toml). Fallback: green/amber phosphor.
	Palette.Add(TEXT("background"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#020a04"))));
	Palette.Add(TEXT("foreground"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ff66"))));
	Palette.Add(TEXT("accent"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ff66"))));
	Palette.Add(TEXT("bright_yellow"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ffb000"))));
	Palette.Add(TEXT("bright_cyan"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#33ffcc"))));
	Palette.Add(TEXT("bright_magenta"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#ff9933"))));
	Palette.Add(TEXT("muted"), FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("#2f6b3f"))));
	const FString Home = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	// -themedir=PATH (dir holding colors.toml) and -themename=NAME let the launcher pick
	// a preset or the live Omarchy theme explicitly. Cosmetic only: same GPU work.
	FString ThemeDirArg, ThemeNameArg;
	FParse::Value(FCommandLine::Get(), TEXT("-themedir="), ThemeDirArg);
	FParse::Value(FCommandLine::Get(), TEXT("-themename="), ThemeNameArg);
	TArray<FString> Cands;
	if (!ThemeDirArg.IsEmpty()) Cands.Add(ThemeDirArg);
	Cands.Add(Home / TEXT(".config/omarchy/current/theme")); Cands.Add(Home / TEXT(".local/state/omarchy/current/theme"));
	for (const FString& Dir : Cands)
	{
		TArray<FString> Lines;
		if (!FFileHelper::LoadFileToStringArray(Lines, *(Dir / TEXT("colors.toml")))) continue;
		for (const FString& L : Lines)
		{
			FString K, V; if (!L.Split(TEXT("="), &K, &V)) continue;
			K.TrimStartAndEndInline(); V.TrimStartAndEndInline(); V.ReplaceInline(TEXT("\""), TEXT(""));
			if (V.StartsWith(TEXT("#")) && V.Len() == 7) Palette.Add(K, FLinearColor::FromSRGBColor(FColor::FromHex(V)));
		}
		FString Name; if (FFileHelper::LoadFileToString(Name, *(FPaths::GetPath(Dir) / TEXT("theme.name")))) ThemeName = Name.TrimStartAndEnd().Left(48);
		else ThemeName = TEXT("omarchy");
		if (!ThemeNameArg.IsEmpty()) ThemeName = ThemeNameArg.Left(48);
		break;
	}
	UE_LOG(LogOmaBench, Log, TEXT("Palette: %s (%d colors)"), *ThemeName, Palette.Num());
}

// Camera route waypoints on the street grid (x = k*5000 avenues, y = k*6000 streets).
static const FVector2D GRoute[] = { {0,-30000}, {0,-12000}, {10000,-12000}, {10000,12000}, {-10000,12000}, {-10000,30000}, {0,30000}, {0,42000} };
// Distance from P to the route polyline; OutQ = closest point on it.
static float RouteDist(const FVector2D& P, FVector2D& OutQ)
{
	float Best = FLT_MAX;
	for (int32 i = 1; i < UE_ARRAY_COUNT(GRoute); ++i)
	{
		const FVector2D A = GRoute[i-1], AB = GRoute[i] - A;
		const float t = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / AB.SizeSquared(), 0.f, 1.f);
		const FVector2D Q = A + AB * t; const float D = FVector2D::Distance(P, Q);
		if (D < Best) { Best = D; OutQ = Q; }
	}
	return Best;
}
static float RouteLength() { float L = 0; for (int32 i = 1; i < UE_ARRAY_COUNT(GRoute); ++i) L += FVector2D::Distance(GRoute[i-1], GRoute[i]); return L; }
static FVector2D RoutePoint(float D, FVector2D* Dir = nullptr)
{
	for (int32 i = 1; i < UE_ARRAY_COUNT(GRoute); ++i)
	{
		const float L = FVector2D::Distance(GRoute[i-1], GRoute[i]);
		if (D <= L || i == UE_ARRAY_COUNT(GRoute) - 1) { if (Dir) *Dir = (GRoute[i] - GRoute[i-1]).GetSafeNormal(); return GRoute[i-1] + (GRoute[i] - GRoute[i-1]) * FMath::Clamp(D / L, 0.f, 1.f); }
		D -= L;
	}
	return GRoute[0];
}

void ABenchGameMode::BuildScene()
{
	// City Night megacity. Everything comes from one seeded RNG, so the scene is identical every run.
	UWorld* W = GetWorld();
	FRandomStream Rng(2077);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FLinearColor Neon[] = { Pal(TEXT("bright_magenta"), FLinearColor(1,0.1f,0.6f)), Pal(TEXT("bright_cyan"), FLinearColor(0,0.9f,1)),
		Pal(TEXT("bright_yellow"), FLinearColor(1,0.8f,0)), Pal(TEXT("bright_red"), FLinearColor(1,0.2f,0.2f)), Pal(TEXT("bright_blue"), FLinearColor(0.3f,0.4f,1)) };
	const FLinearColor Warm = Pal(TEXT("light_foreground"), FLinearColor(1,0.8f,0.6f));
	const FLinearColor Accent = Pal(TEXT("accent"), FLinearColor(0.5f,0.5f,0.9f));

	AActor* City = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USceneComponent* Root = NewObject<USceneComponent>(City, TEXT("Root")); Root->SetMobility(EComponentMobility::Static); City->SetRootComponent(Root); Root->RegisterComponent();
	auto MakeHISM = [&](UStaticMesh* Mesh, UMaterialInterface* Base, const FString& Name, TFunctionRef<void(UMaterialInstanceDynamic*)> Setup)
	{
		UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(City, *Name);
		H->SetStaticMesh(Mesh); H->SetupAttachment(Root);
		UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base ? Base : BaseMaterial.Get(), City); Setup(M);
		H->SetMaterial(0, M); H->SetCollisionEnabled(ECollisionEnabled::NoCollision); H->SetMobility(EComponentMobility::Static);
		H->RegisterComponent(); return H;
	};
	UHierarchicalInstancedStaticMeshComponent* Ground = MakeHISM(CubeMesh, StreetMaterial, TEXT("Street"), [](UMaterialInstanceDynamic*){});
	UHierarchicalInstancedStaticMeshComponent* Towers[2] = {
		MakeHISM(CubeMesh, TowerMaterial, TEXT("TowersWarm"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Warm); }),
		MakeHISM(CubeMesh, TowerMaterial, TEXT("TowersCool"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Neon[1]); M->SetScalarParameterValue(TEXT("LitFraction"), 0.3f); }) };
	UHierarchicalInstancedStaticMeshComponent* Signs[5];
	for (int32 i = 0; i < 5; ++i) Signs[i] = MakeHISM(CubeMesh, SignMaterial, FString::Printf(TEXT("Signs%d"), i), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), Neon[i]); });
	UHierarchicalInstancedStaticMeshComponent* Clutter = MakeHISM(CubeMesh, TowerMaterial, TEXT("Clutter"), [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Neon[2]); M->SetScalarParameterValue(TEXT("LitFraction"), 0.15f); });
	Ground->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0,0,-50), FVector(1400,1400,1)));

	const int32 N = 110; const float Cell = 1000.f; int32 Count = 0, SignCount = 0;
	TArray<FVector> SignLights;
	for (int32 X = 0; X < N; ++X) for (int32 Y = 0; Y < N; ++Y)
	{
		const int32 GX = X - N/2, GY = Y - N/2;
		const FVector Base(GX * Cell, GY * Cell, 0);
		const bool bAvenue = FMath::Abs(GX) <= 1;           // the canyon the camera flies through
		// Boulevards the camera turns through (3 cells wide, like the avenue).
		const bool bBoulevard = FMath::Abs(FMath::Abs(GX) - 10) <= 1 || FMath::Abs(GY + 12) <= 1 || FMath::Abs(GY - 12) <= 1 || FMath::Abs(GY - 30) <= 1;
		const bool bStreet = bAvenue || bBoulevard || (GX % 5 == 0) || (GY % 6 == 0);
		if (bStreet)
		{
			// street clutter: kiosks, parked vans, barriers
			if (!bAvenue || Rng.FRand() < 0.5f)
				for (int32 k = 0; k < 2; ++k)
				{ Clutter->AddInstance(FTransform(FRotator(0, Rng.FRandRange(0,180), 0), Base + FVector(Rng.FRandRange(-400,400), Rng.FRandRange(-400,400), 60), FVector(Rng.FRandRange(0.6f,3.f), Rng.FRandRange(0.6f,1.5f), Rng.FRandRange(0.8f,2.f)))); ++Count; }
			continue;
		}
		FVector2D RouteQ; const bool bNearRoute = RouteDist(FVector2D(Base.X, Base.Y), RouteQ) < 2600.f;
		// Megablock tower: taller near the canyon, stacked setbacks.
		const float Near = FMath::Clamp(1.f - FMath::Abs(GX) / 30.f, 0.15f, 1.f);
		const int32 Stack = Rng.RandRange(3, 8); float Z = 0;
		UHierarchicalInstancedStaticMeshComponent* T = Towers[Rng.RandRange(0, 1)];
		float Wd = Rng.FRandRange(6.5f, 9.2f);
		for (int32 S = 0; S < Stack; ++S)
		{
			const float H = Rng.FRandRange(800, 3200) * (0.4f + Near);
			T->AddInstance(FTransform(FRotator::ZeroRotator, Base + FVector(0,0,Z + H*0.5f), FVector(Wd, Wd * Rng.FRandRange(0.8f,1.f), H/100.f)));
			// neon billboards on faces, biased toward the canyon side
			const int32 NS = Rng.RandRange(0, 3) + (bNearRoute ? 1 : 0);
			for (int32 k = 0; k < NS; ++k)
			{
				// Face the camera route if this block borders it (any leg, incl. side streets
				// after each turn); otherwise face the central avenue as before.
				const bool bOnX = !bNearRoute || FMath::Abs(RouteQ.X - Base.X) >= FMath::Abs(RouteQ.Y - Base.Y);
				const float Side = bNearRoute ? (bOnX ? FMath::Sign(RouteQ.X - Base.X) : FMath::Sign(RouteQ.Y - Base.Y)) : ((GX > 0) ? -1.f : 1.f);
				const bool bFacing = Rng.FRand() < 0.7f;
				const float Off = Wd * 50.f + 40.f;
				const FVector FaceOff = bOnX ? FVector(Side * Off, Rng.FRandRange(-250,250), 0) : FVector(Rng.FRandRange(-250,250), Side * Off, 0);
				const FVector SideOff = bOnX ? FVector(Rng.FRandRange(-250,250), (Rng.FRand()<0.5f?-1:1) * Off, 0) : FVector((Rng.FRand()<0.5f?-1:1) * Off, Rng.FRandRange(-250,250), 0);
				const FVector Pos = Base + (bFacing ? FaceOff : SideOff) + FVector(0,0,Z + Rng.FRandRange(0.15f,0.85f)*H);
				const FVector Flat(Rng.FRandRange(1.f,4.f), 0.1f, Rng.FRandRange(0.6f,2.f)), Tall(0.1f, Rng.FRandRange(1.f,4.f), Rng.FRandRange(1.5f,6.f));
				const FVector Scale = (bFacing == bOnX) ? Tall : FVector(Flat.X, Flat.Y, Flat.Z);
				Signs[Rng.RandRange(0, 4)]->AddInstance(FTransform(FRotator::ZeroRotator, Pos, bOnX ? Scale : FVector(Scale.Y, Scale.X, Scale.Z))); ++SignCount;
				if (bFacing && (FMath::Abs(GX) <= 4 || bNearRoute) && Pos.Z < 9000) SignLights.Add(Pos + (bOnX ? FVector(Side * 150, 0, 0) : FVector(0, Side * 150, 0)));
			}
			Z += H; Wd *= Rng.FRandRange(0.72f, 0.95f); ++Count; CellTop.FindOrAdd(FIntPoint(GX, GY)) = Z;
		}
	}
	// Moonless sky, dim accent-tinted fill, dense wet haze.
	ADirectionalLight* Sun = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform(FRotator(-20, 70, 0)), P);
	Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sun->GetLightComponent()->SetIntensity(0.25f); Sun->GetLightComponent()->SetLightColor(Accent);
	Cast<UDirectionalLightComponent>(Sun->GetLightComponent())->bAtmosphereSunLight = true;
	AActor* Sky = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USkyAtmosphereComponent* Atmo = NewObject<USkyAtmosphereComponent>(Sky, TEXT("Atmo")); Sky->SetRootComponent(Atmo); Atmo->RegisterComponent();
	ASkyLight* SkyL = W->SpawnActor<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity, P);
	SkyL->GetLightComponent()->SetMobility(EComponentMobility::Movable); SkyL->GetLightComponent()->bRealTimeCapture = true;
	SkyL->GetLightComponent()->SetIntensity(0.4f); SkyL->GetLightComponent()->RecaptureSky();
	AExponentialHeightFog* Fog = W->SpawnActor<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity, P);
	UExponentialHeightFogComponent* FogC = Fog->GetComponent(); CityFog = FogC;
	// Street-bound haze: dense at ground level, falls off fast so the skyline neon still reads.
	FogC->SetVolumetricFog(true); FogC->SetFogDensity(0.2f); FogC->SetFogHeightFalloff(1.1f);
	FogC->SetFogInscatteringColor(Accent * 0.12f); FogC->SetVolumetricFogAlbedo(FColor(225, 215, 235)); FogC->SetVolumetricFogExtinctionScale(3.0f);
	FogC->SetVolumetricFogScatteringDistribution(0.75f); FogC->SetVolumetricFogDistance(12000.f);
	FogC->SecondFogData.FogDensity = 1.1f; FogC->SecondFogData.FogHeightFalloff = 3.5f; FogC->SecondFogData.FogHeightOffset = 0.f; // steam layer hugging the street
	FogC->MarkRenderStateDirty();
	// Local steam pockets along the avenue (fixed positions).
	for (int32 i = 0; i < 14; ++i)
	{
		ALocalFogVolume* LF = W->SpawnActor<ALocalFogVolume>(ALocalFogVolume::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(Rng.FRandRange(-900, 900), -45000 + i * 6900, 0), FVector(12, 12, 4)), P);
		if (ULocalFogVolumeComponent* C = LF ? LF->GetComponentByClass<ULocalFogVolumeComponent>() : nullptr)
		{ C->SetHeightFogExtinction(2.0f); C->SetHeightFogFalloff(450.f); C->SetRadialFogExtinction(0.f); }
	}
	// Steam pockets along the whole camera route, every 5 km, incl. each side street.
	for (float D = 2500.f; D < RouteLength(); D += 5000.f)
	{
		const FVector2D Q = RoutePoint(D);
		ALocalFogVolume* LF = W->SpawnActor<ALocalFogVolume>(ALocalFogVolume::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(Q.X + Rng.FRandRange(-500, 500), Q.Y + Rng.FRandRange(-500, 500), 0), FVector(12, 12, 4)), P);
		if (ULocalFogVolumeComponent* C = LF ? LF->GetComponentByClass<ULocalFogVolumeComponent>() : nullptr)
		{ C->SetHeightFogExtinction(2.0f); C->SetHeightFogFalloff(450.f); C->SetRadialFogExtinction(0.f); }
	}
	// Sign spill lights along the canyon (shadowed) + scattered street lights.
	int32 NumLights = 0;
	for (int32 i = 0; i < SignLights.Num() && NumLights < 200; i += 2)
	{
		APointLight* PL = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(SignLights[i]), P);
		UPointLightComponent* C = Cast<UPointLightComponent>(PL->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable); C->SetIntensity(Rng.FRandRange(40000, 120000)); C->SetAttenuationRadius(3500);
		C->SetLightColor(Neon[Rng.RandRange(0, 4)]); C->SetCastShadows(NumLights < 140); C->SetVolumetricScatteringIntensity(3.f); ++NumLights;
	}
	// Street lights: along the route every ~1.2 km alternating sides, then the old avenue scatter.
	TArray<FVector> StreetLights;
	for (float D = 600.f; D < RouteLength(); D += 1200.f)
	{
		FVector2D Dir; const FVector2D Q = RoutePoint(D, &Dir); const FVector2D Nrm(-Dir.Y, Dir.X);
		const float Sd = (int32(D / 1200.f) % 2) ? 1.f : -1.f;
		StreetLights.Add(FVector(Q + Nrm * Sd * Rng.FRandRange(900, 1300), Rng.FRandRange(150, 500)));
	}
	for (int32 L = 0; L < 40; ++L) StreetLights.Add(FVector(Rng.FRandRange(-1500, 1500), Rng.FRandRange(-50000, 50000), Rng.FRandRange(150, 500)));
	for (const FVector& Pos : StreetLights)
	{
		APointLight* PL = W->SpawnActor<APointLight>(APointLight::StaticClass(), FTransform(Pos), P);
		UPointLightComponent* C = Cast<UPointLightComponent>(PL->GetLightComponent());
		C->SetMobility(EComponentMobility::Movable); C->SetIntensity(30000); C->SetAttenuationRadius(2200);
		C->SetLightColor(Warm); C->SetCastShadows(false); ++NumLights;
	}
	// Flying traffic: emissive pods with unshadowed lights on deterministic lanes.
	UMaterialInstanceDynamic* CarMat[2];
	for (int32 i = 0; i < 2; ++i) { CarMat[i] = UMaterialInstanceDynamic::Create(SignMaterial, this); CarMat[i]->SetVectorParameterValue(TEXT("Color"), i ? FLinearColor(1,0.05f,0.05f) : FLinearColor(1,0.95f,0.85f)); CarMat[i]->SetScalarParameterValue(TEXT("Glow"), 40.f); }
	for (int32 i = 0; i < 64; ++i)
	{
		FCarLane Lane; const bool bAlong = i < 48;
		Lane.Origin = bAlong ? FVector(Rng.FRandRange(-1100, 1100), (i % 2) ? -55000 : 55000, Rng.FRandRange(1200, 6000)) : FVector(-55000, (Rng.RandRange(-9, 9)) * 6 * Cell, Rng.FRandRange(2500, 8000));
		Lane.Dir = bAlong ? FVector(0, (i % 2) ? 1 : -1, 0) : FVector(1, 0, 0);
		Lane.Speed = Rng.FRandRange(1500, 4000); Lane.Len = 110000; Lane.Phase = Rng.FRandRange(0, Lane.Len);
		Lanes.Add(Lane);
		AActor* Car = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		UStaticMeshComponent* Body = NewObject<UStaticMeshComponent>(Car, TEXT("Body"));
		Body->SetMobility(EComponentMobility::Movable); Body->SetStaticMesh(CubeMesh); Body->SetWorldScale3D(bAlong ? FVector(0.7f, 1.8f, 0.4f) : FVector(1.8f, 0.7f, 0.4f));
		Body->SetMaterial(0, CarMat[(i % 2) ? 1 : 0]); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision); Body->SetCastShadow(false);
		Car->SetRootComponent(Body); Body->RegisterComponent();
		UPointLightComponent* CL = NewObject<UPointLightComponent>(Car, TEXT("Light"));
		CL->SetupAttachment(Body); CL->SetMobility(EComponentMobility::Movable); CL->SetIntensity(20000); CL->SetAttenuationRadius(1800);
		CL->SetLightColor((i % 2) ? FLinearColor(1,0.1f,0.1f) : FLinearColor(1,0.95f,0.85f)); CL->SetCastShadows(false); CL->RegisterComponent();
		Cars.Add(Car); NumLights++;
	}
	UpdateCars(0);
	UE_LOG(LogOmaBench, Log, TEXT("T1 city night: %d building/clutter instances, %d neon signs, %d lights, %d flying cars"), Count, SignCount, NumLights, Cars.Num());
	// Retro post: bloom, slight chromatic fringe, vignette, scanline material. Fixed values for repeatability.
	APostProcessVolume* PPV = W->SpawnActor<APostProcessVolume>(APostProcessVolume::StaticClass(), FTransform::Identity, P);
	PPV->bUnbound = true; FPostProcessSettings& S = PPV->Settings;
	S.bOverride_BloomIntensity = true; S.BloomIntensity = 1.2f; S.bOverride_BloomThreshold = true; S.BloomThreshold = 0.8f;
	S.bOverride_VignetteIntensity = true; S.VignetteIntensity = bNoPostFx ? 0.f : 0.7f;
	S.bOverride_SceneFringeIntensity = true; S.SceneFringeIntensity = bNoPostFx ? 0.f : 1.2f;
	S.bOverride_AutoExposureMinBrightness = true; S.AutoExposureMinBrightness = 1.f;
	S.bOverride_AutoExposureMaxBrightness = true; S.AutoExposureMaxBrightness = 1.f; // fixed exposure -> consistent frames
	S.bOverride_MotionBlurAmount = true; S.MotionBlurAmount = 0.f;
	if (CrtMaterial && !bNoPostFx)
	{
		UMaterialInstanceDynamic* Crt = UMaterialInstanceDynamic::Create(CrtMaterial, this);
		// Pixel size scales with output height so the virtual grid is ~360 rows at the default (4 px @ 1440p, 3 @ 1080p).
		Crt->SetScalarParameterValue(TEXT("PixelSize"), float(PixelSize));
		Crt->SetScalarParameterValue(TEXT("Levels"), PixelSize > 0 ? 10.f : 0.f);
		Crt->SetScalarParameterValue(TEXT("Scanline"), PixelSize > 1 ? 0.3f : 0.2f);
		S.AddBlendable(Crt, 1.f);
	}
	UE_LOG(LogOmaBench, Log, TEXT("PostFX=%d pixel=%d"), !bNoPostFx, PixelSize);
	Camera = W->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, P);
	Camera->GetCameraComponent()->SetFieldOfView(80.f); Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	// Rain: moderate shower of 9000 additive streaks in a 6 km x 6 km x 4 km box that follows the camera (fixed seed).
	if (RainMaterial)
	{
		UInstancedStaticMeshComponent* RainC = NewObject<UInstancedStaticMeshComponent>(Camera, TEXT("Rain"));
		RainC->SetStaticMesh(CubeMesh);
		// The default 0.35-0.55 emissive was quantised to black by the CRT dither (Levels) except
		// for streaks right in front of the lens; keep it subtle (below the 0.8 bloom threshold), theme tinted.
		UMaterialInstanceDynamic* RainM = UMaterialInstanceDynamic::Create(RainMaterial, Camera);
		RainM->SetVectorParameterValue(TEXT("Color"), Pal(TEXT("foreground"), FLinearColor(0.7f,0.8f,1.f)) * 0.06f); // subtle, below bloom threshold
		RainC->SetMaterial(0, RainM); RainC->SetMobility(EComponentMobility::Movable);
		RainC->SetupAttachment(Camera->GetRootComponent()); RainC->SetUsingAbsoluteRotation(true);
		RainC->SetCollisionEnabled(ECollisionEnabled::NoCollision); RainC->SetCastShadow(false); RainC->SetBoundsScale(40.f); RainC->bAllowCullDistanceVolume = false;
		RainC->InstancingRandomSeed = 4242; RainC->RegisterComponent();
		FRandomStream RR(4242);
		// Camera-relative 3 km x 3 km box; WPO drops each streak 0..BoxHeight below +2000.
		for (int32 i = 0; i < 14000; ++i)
			RainC->AddInstance(FTransform(FRotator::ZeroRotator, FVector(RR.FRandRange(-1500, 1500), RR.FRandRange(-1500, 1500), 2000.f), FVector(0.012f, 0.012f, 1.6f)));
		RainC->bNeverDistanceCull = true; RainC->SetCullDistances(0, 0); RainC->bUseAsOccluder = false;
	}
	UpdateCamera(0);
}

void ABenchGameMode::UpdateCars(double T)
{
	for (int32 i = 0; i < Cars.Num(); ++i)
	{
		const FCarLane& L = Lanes[i];
		const double D = FMath::Fmod(L.Phase + L.Speed * T, (double)L.Len);
		Cars[i]->SetActorLocation(L.Origin + L.Dir * D);
	}
}

// City-grid flight path: street waypoints with 90-degree corners, each rounded by a
// circular fillet, sampled once into a dense polyline and walked at constant speed.
// Streets are at GX%5==0 (x = k*5000) and GY%6==0 (y = k*6000), so every leg stays in a canyon.
static const TArray<FVector2D>& CityPath(TArray<float>& OutCum)
{
	static TArray<FVector2D> Pts; static TArray<float> Cum;
	if (Pts.Num() == 0)
	{
		const FVector2D* W = GRoute; const int32 NW = UE_ARRAY_COUNT(GRoute); const float R = 420.f;
		Pts.Add(W[0]);
		for (int32 i = 1; i < NW - 1; ++i)
		{
			const FVector2D In = (W[i] - W[i-1]).GetSafeNormal(), Out = (W[i+1] - W[i]).GetSafeNormal();
			const FVector2D A = W[i] - In * R, B = W[i] + Out * R, C = A + Out * R; // arc centre
			Pts.Add(A);
			for (int32 k = 1; k < 12; ++k)
			{
				const float t = k / 12.f * HALF_PI;
				Pts.Add(C - Out * R * FMath::Cos(t) + In * R * FMath::Sin(t));
			}
			Pts.Add(B);
		}
		Pts.Add(W[NW-1]);
		Cum.SetNum(Pts.Num()); Cum[0] = 0;
		for (int32 i = 1; i < Pts.Num(); ++i) Cum[i] = Cum[i-1] + FVector2D::Distance(Pts[i-1], Pts[i]);
	}
	OutCum = Cum; return Pts;
}
static FVector2D PathAt(float D, FVector2D* Dir = nullptr)
{
	TArray<float> Cum; const TArray<FVector2D>& P = CityPath(Cum);
	D = FMath::Clamp(D, 0.f, Cum.Last());
	int32 i = 1; while (i < Cum.Num() - 1 && Cum[i] < D) ++i;
	const float a = (D - Cum[i-1]) / FMath::Max(1.f, Cum[i] - Cum[i-1]);
	if (Dir) *Dir = (P[i] - P[i-1]).GetSafeNormal();
	return FMath::Lerp(P[i-1], P[i], a);
}

void ABenchGameMode::UpdateCamera(double T)
{
	if (!Camera) return;
	// 60 s loop over the street grid (5 corners), deterministic in T.
	TArray<float> Cum; CityPath(Cum); const float Len = Cum.Last();
	const double u = FMath::Clamp(T / 60.0, 0.0, 1.0);
	auto PosAt = [&](double uu){ const FVector2D XY = PathAt(float(uu * Len)); return FVector(XY.X, XY.Y, 1900 + 700*FMath::Sin(2*PI*uu*2) + 300*FMath::Sin(2*PI*uu*5)); };
	const FVector Pos = PosAt(u);
	// Look ahead along the path (smooths the corner yaw) and bank with the turn rate.
	const FVector Ahead = PosAt(FMath::Min(1.0, u + 900.0 / Len)) - FVector(0,0,200);
	FVector2D D0, D1; PathAt(float(u * Len), &D0); PathAt(float(u * Len) + 700.f, &D1);
	const float Turn = FMath::Clamp(float(D0.X * D1.Y - D0.Y * D1.X), -1.f, 1.f);
	FRotator R = (Ahead - Pos).Rotation(); R.Roll = -14.f * Turn;
	Camera->SetActorLocationAndRotation(Pos, R);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
}

// Per-test ray tracing config, applied at runtime when a test activates.
// T1 City Night keeps the project defaults (HW Lumen GI + reflections, surface-cache lighting,
// 1 reflection bounce, raster shadow maps). T2 Skyline turns everything up: hit-lighting
// reflections, multi-bounce reflections/refraction, translucent front-layer reflections, RT shadows.
TSharedRef<FJsonObject> ABenchGameMode::ApplyRtConfig(const FString& Id)
{
	const bool b2 = Id == TEXT("T2");
	const TPair<const TCHAR*, const TCHAR*> Cfg[] = {
		{ TEXT("r.Lumen.HardwareRayTracing"), TEXT("1") },
		{ TEXT("r.Lumen.HardwareRayTracing.LightingMode"), b2 ? TEXT("2") : TEXT("0") },
		{ TEXT("r.Lumen.Reflections.HardwareRayTracing"), TEXT("1") },
		{ TEXT("r.Lumen.Reflections.ScreenTraces"), b2 ? TEXT("0") : TEXT("1") },
		{ TEXT("r.Lumen.Reflections.MaxRoughnessToTrace"), b2 ? TEXT("0.6") : TEXT("0.4") },
		{ TEXT("r.Lumen.Reflections.MaxReflectionBounces"), b2 ? TEXT("4") : TEXT("1") },
		{ TEXT("r.Lumen.Reflections.MaxRefractionBounces"), b2 ? TEXT("2") : TEXT("0") },
		{ TEXT("r.Lumen.TranslucencyReflections.FrontLayer.Enable"), b2 ? TEXT("1") : TEXT("0") },
		{ TEXT("r.RayTracing.Shadows"), b2 ? TEXT("1") : TEXT("0") },
		{ TEXT("r.RayTracing.Shadows.SamplesPerPixel"), b2 ? TEXT("2") : TEXT("1") },
	};
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("profile"), b2 ? TEXT("hwrt-max") : TEXT("hw-lumen-default"));
	TSharedRef<FJsonObject> Vars = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Missing;
	for (const auto& KV : Cfg)
	{
		IConsoleVariable* C = IConsoleManager::Get().FindConsoleVariable(KV.Key);
		if (!C) { Missing.Add(MakeShared<FJsonValueString>(KV.Key)); continue; }
		C->Set(KV.Value, ECVF_SetByCode);
		Vars->SetStringField(KV.Key, C->GetString());
	}
	O->SetObjectField(TEXT("cvars"), Vars); O->SetArrayField(TEXT("unsupported"), Missing);
	O->SetBoolField(TEXT("rhiRayTracing"), IsRayTracingEnabled());
	UE_LOG(LogOmaBench, Display, TEXT("OmaBench %s RT profile applied (%d cvars missing, RHI RT=%d)"), *Id, Missing.Num(), IsRayTracingEnabled() ? 1 : 0);
	return O;
}

void ABenchGameMode::ActivateTest(int32 Index)
{
	CurrentTest = Index;
	const FString Id = GetTestId();
	// T2 Skyline reuses the City Night city as its surroundings; only T1's placeholder cube traffic is swapped out.
	for (AActor* A : T1Actors) if (A) A->SetActorHiddenInGame(Id != TEXT("T1") && Cars.Contains(A));
	for (AActor* A : T2Actors) if (A) A->SetActorHiddenInGame(Id != TEXT("T2"));
	for (AActor* A : T3Actors) if (A) A->SetActorHiddenInGame(Id != TEXT("T3"));
	SetDownpourActive(Id == TEXT("T3"));
	if (CityFog)
	{
		const bool b3 = Id == TEXT("T3");   // T3 brings its own smoke/steam; keep the global haze lighter there
		CityFog->SetFogDensity(b3 ? 0.05f : 0.2f); CityFog->SetVolumetricFogExtinctionScale(b3 ? 0.8f : 3.0f);
		CityFog->SecondFogData.FogDensity = b3 ? 0.15f : 1.1f; CityFog->MarkRenderStateDirty();
	}
	if (RtConfigs.Num() <= Index) RtConfigs.SetNum(Index + 1);
	RtConfigs[Index] = ApplyRtConfig(Id);
	PathTime = 0;
	if (Id == TEXT("T2")) UpdateSkyline(0); else if (Id == TEXT("T3")) UpdateDownpour(0); else UpdateCamera(0);
}

void ABenchGameMode::BeginPlay()
{
	Super::BeginPlay();
	ParseArgs();
	LoadPalette();
	ApplyVideoSettings();
	{
		auto Snap = [&](){ TSet<AActor*> S; for (TActorIterator<AActor> It(GetWorld()); It; ++It) S.Add(*It); return S; };
		TSet<AActor*> Before = Snap();
		BuildScene();
		TSet<AActor*> Mid = Snap();
		for (AActor* A : Mid) if (!Before.Contains(A) && A != Camera && !A->IsA<APostProcessVolume>()) T1Actors.Add(A);
		if (TestIds.Contains(TEXT("T2")))
		{
			BuildSkyline();
			for (AActor* A : Snap()) if (!Mid.Contains(A)) T2Actors.Add(A);
		}
		if (TestIds.Contains(TEXT("T3")))
		{
			TSet<AActor*> Pre = Snap();
			BuildDownpour();
			for (AActor* A : Snap()) if (!Pre.Contains(A)) T3Actors.Add(A);
		}
		ActivateTest(0);
	}
	if (!bNoSysmon) SysMon = MakeShared<FSysMon>();
	if (FSlateApplication::IsInitialized())
	{
		TSharedRef<FEscAbortProcessor> P = MakeShared<FEscAbortProcessor>(); P->GM = this; EscProc = P;
		FSlateApplication::Get().RegisterInputPreProcessor(EscProc, 0);
		FSlateApplication::Get().SetAllUserFocusToGameViewport();
	}
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) { PC->SetInputMode(FInputModeGameOnly()); PC->bShowMouseCursor = false; PC->CurrentMouseCursor = EMouseCursor::None; }
	// Hide the OS pointer over the game window for the whole run (screenshots + no distraction).
	if (FSlateApplication::IsInitialized()) if (TSharedPtr<ICursor> Cur = FSlateApplication::Get().GetPlatformCursor()) { Cur->SetType(EMouseCursor::None); Cur->Show(false); }
	UE_LOG(LogOmaBench, Log, TEXT("OmaBench %s run %s: passes=%d warmup=%.0fs duration=%.0fs res=%dx%d fullscreen=%d results=%s"),
		GAppVersion, *RunId, Passes, WarmupS, DurationS, ResX, ResY, bFullscreen, *ResultsPath);
}

void ABenchGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (EscProc.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(EscProc);
	// Stopped from outside (omabench-ctl stop / SIGTERM) before finishing: record it as aborted too.
	if (Phase != EBenchPhase::Done && !RunId.IsEmpty()) AbortRun();
	EscProc.Reset(); SysMon.Reset();
	Super::EndPlay(Reason);
}

void ABenchGameMode::AbortRun()
{
	if (bExitRequested) return;
	if (Phase == EBenchPhase::Done)
	{
		// Already aborted, or on the results screen: Esc just skips the hold.
		if (!bAborted) { bExitRequested = true; FPlatformMisc::RequestExitWithStatus(false, ExitCode); }
		return;
	}
	const TCHAR* At = Phase == EBenchPhase::Running ? TEXT("measuring") : (Phase == EBenchPhase::Warmup ? (TotalElapsed < 4.0 ? TEXT("boot") : TEXT("warmup")) : TEXT("boot"));
	bAborted = true; Phase = EBenchPhase::Done; DoneHold = 0; ExitCode = 2;
	if (SysMon) SysMon->SetRecording(false);
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	FillCommon(Root);
	Root->SetBoolField(TEXT("aborted"), true); Root->SetStringField(TEXT("abortedAt"), At);
	Root->SetNumberField(TEXT("elapsedS"), FMath::RoundToDouble(TotalElapsed * 10) / 10);
	Root->SetArrayField(TEXT("tests"), {});
	TSharedRef<FJsonObject> Scores = MakeShared<FJsonObject>(); Scores->SetField(TEXT("gpu"), MakeShared<FJsonValueNull>()); Scores->SetField(TEXT("cpu"), MakeShared<FJsonValueNull>());
	Root->SetObjectField(TEXT("scores"), Scores); Root->SetBoolField(TEXT("stable"), false);
	WriteJsonAtomic(ResultsPath, Root);
	WriteProgress(TEXT("aborted"));
	UE_LOG(LogOmaBench, Display, TEXT("OmaBench aborted by user (Esc) during %s at %.1fs; results=%s exit=2"), At, TotalElapsed, *ResultsPath);
	ResultLines = { FString::Printf(TEXT("RUN ABORTED DURING %s"), *FString(At).ToUpper()), FString::Printf(TEXT("ELAPSED ........... %.1f S"), TotalElapsed),
		TEXT("SCORE ............. NONE"), FString::Printf(TEXT("RESULTS -> %s"), *FPaths::GetCleanFilename(ResultsPath)) };
}

void ABenchGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const double Dt = FApp::GetDeltaTime(); // real wall-clock frame time
	FpsAccumT += FApp::GetDeltaTime(); ++FpsAccumN;
	if (FpsAccumT >= 0.5) { LiveFps = FpsAccumN / FpsAccumT; FpsAccumT = 0; FpsAccumN = 0; }
	if (Phase == EBenchPhase::Done)
	{
		DoneHold += FApp::GetDeltaTime();
		if (!bExitRequested && DoneHold >= (bAborted ? 1.0 : (bNoHud ? 0.0 : 4.0))) { bExitRequested = true; FPlatformMisc::RequestExitWithStatus(false, ExitCode); }
		return;
	}
	if (Phase == EBenchPhase::Init) { if (++InitFrames > 5) { Phase = EBenchPhase::Warmup; PhaseTime = 0; } return; }
	TotalElapsed += Dt; PhaseTime += Dt; PathTime += Dt;
	if (GetTestId() == TEXT("T2")) UpdateSkyline(FMath::Fmod(PathTime, 60.0));
	else if (GetTestId() == TEXT("T3")) UpdateDownpour(FMath::Fmod(PathTime, 60.0));
	else { UpdateCamera(FMath::Fmod(PathTime, 60.0)); UpdateCars(PathTime); }
	if (Phase == EBenchPhase::Warmup)
	{
		if (PhaseTime >= WarmupS) { if (SysMon) SysMon->SetRecording(true); Phase = EBenchPhase::Running; PhaseTime = 0; PathTime = 0; CurrentFrames.Reset(); CurrentFrames.Reserve(100000); }
	}
	else if (Phase == EBenchPhase::Running)
	{
		if (CurrentFrames.Num() < 2000000) CurrentFrames.Add(float(Dt * 1000.0));
		if (PhaseTime >= DurationS)
		{
			PassFrames.Add(MoveTemp(CurrentFrames)); CurrentFrames = {};
			if (++CurrentPass >= Passes)
			{
				TestPassFrames.Add(MoveTemp(PassFrames)); PassFrames = {};
				if (CurrentTest + 1 >= TestIds.Num()) { FinishRun(); return; }
				if (SysMon) SysMon->SetRecording(false);
				CurrentPass = 0; ActivateTest(CurrentTest + 1);
				Phase = EBenchPhase::Warmup; PhaseTime = 0; return;
			}
			PhaseTime = 0; PathTime = 0; CurrentFrames.Reserve(100000);
		}
	}
	if (TotalElapsed - LastProgress >= 1.0) { LastProgress = TotalElapsed; WriteProgress(Phase == EBenchPhase::Warmup ? TEXT("warmup") : TEXT("running")); }
}

bool ABenchGameMode::WriteJsonAtomic(const FString& Path, const TSharedRef<FJsonObject>& Obj)
{
	FString Out; auto Wr = TJsonWriterFactory<>::Create(&Out); FJsonSerializer::Serialize(Obj, Wr);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const FString Tmp = Path + TEXT(".tmp");
	return FFileHelper::SaveStringToFile(Out, *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM) && IFileManager::Get().Move(*Path, *Tmp, true, true);
}

void ABenchGameMode::WriteProgress(const TCHAR* State)
{
	TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("schema"), 1); O->SetStringField(TEXT("runId"), RunId); O->SetStringField(TEXT("state"), State);
	O->SetStringField(TEXT("test"), GetTestId()); O->SetStringField(TEXT("testName"), GetTestName());
	O->SetNumberField(TEXT("testIndex"), CurrentTest + 1); O->SetNumberField(TEXT("testCount"), TestIds.Num()); O->SetNumberField(TEXT("pass"), FMath::Min(CurrentPass + 1, Passes)); O->SetNumberField(TEXT("passes"), Passes);
	O->SetNumberField(TEXT("elapsedS"), FMath::RoundToDouble(TotalElapsed * 10) / 10); O->SetNumberField(TEXT("totalS"), GetTotal());
	WriteJsonAtomic(ProgressPath, O);
}

struct FPassStats { double AvgFps=0, P1=0, P01=0, Std=0, Cov=0, Score=0, AvgMs=0; int32 Frames=0; };
static FPassStats Compute(const TArray<float>& F)
{
	FPassStats S; S.Frames = F.Num(); if (F.Num() < 2) return S;
	double Sum = 0; for (float v : F) Sum += v; S.AvgMs = Sum / F.Num();
	S.AvgFps = 1000.0 * F.Num() / Sum; // harmonic mean of per-frame FPS
	double Var = 0; for (float v : F) Var += FMath::Square(v - S.AvgMs); S.Std = FMath::Sqrt(Var / (F.Num() - 1)); S.Cov = S.Std / S.AvgMs;
	TArray<float> Sorted = F; Sorted.Sort();
	auto Pct = [&](double p){ return Sorted[FMath::Clamp(int32(FMath::CeilToDouble(p * Sorted.Num())) - 1, 0, Sorted.Num()-1)]; };
	S.P1 = 1000.0 / Pct(0.99); S.P01 = 1000.0 / Pct(0.999);
	const double K = 100.0; // provisional; calibrate against reference GPU after M3
	S.Score = K * S.AvgFps * FMath::Clamp(1.0 - S.Cov, 0.8, 1.0);
	return S;
}
static double R2(double v){ return FMath::RoundToDouble(v * 100) / 100; }

void ABenchGameMode::FillCommon(const TSharedRef<FJsonObject>& Root) const
{
	Root->SetNumberField(TEXT("schema"), 1); Root->SetStringField(TEXT("runId"), RunId);
	Root->SetStringField(TEXT("appVersion"), GAppVersion); Root->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString(EVersionComponent::Patch));
	Root->SetStringField(TEXT("startedAt"), StartedAt);
	TSharedRef<FJsonObject> Env = MakeShared<FJsonObject>();
	Env->SetStringField(TEXT("gpu"), GRHIAdapterName); Env->SetStringField(TEXT("driver"), GRHIAdapterUserDriverVersion);
	Env->SetStringField(TEXT("kernel"), FPlatformMisc::GetOSVersion()); Env->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
	Env->SetNumberField(TEXT("ramGb"), (double)FPlatformMemory::GetConstants().TotalPhysicalGB);
	Env->SetBoolField(TEXT("vsync"), IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))->GetInt() != 0); Env->SetNumberField(TEXT("maxFps"), IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat());
	Env->SetStringField(TEXT("monitor"), MonitorName.IsEmpty() ? TEXT("unknown") : MonitorName.Left(32));
	Env->SetNumberField(TEXT("refreshHz"), FCString::Atod(*MonitorHz));
	Env->SetStringField(TEXT("rhi"), GDynamicRHI ? FString(GDynamicRHI->GetName()) : TEXT("unknown"));
	Env->SetStringField(TEXT("theme"), ThemeName);
	Root->SetObjectField(TEXT("env"), Env);
	TSharedRef<FJsonObject> Set = MakeShared<FJsonObject>();
	Set->SetStringField(TEXT("res"), FString::Printf(TEXT("%dx%d"), ResX, ResY)); Set->SetNumberField(TEXT("passes"), Passes);
	Set->SetStringField(TEXT("preset"), Passes == 1 ? TEXT("quick") : TEXT("standard")); Set->SetBoolField(TEXT("fullscreen"), bFullscreen);
	Set->SetNumberField(TEXT("warmupS"), WarmupS); Set->SetStringField(TEXT("theme"), ThemeName); Set->SetBoolField(TEXT("postFx"), !bNoPostFx); Set->SetNumberField(TEXT("pixel"), bNoPostFx ? 0 : PixelSize); Set->SetBoolField(TEXT("hud"), !bNoHud); Set->SetBoolField(TEXT("sysmon"), !bNoSysmon); Set->SetNumberField(TEXT("durationS"), DurationS);
	{ TArray<TSharedPtr<FJsonValue>> TA; for (const FString& T : TestIds) TA.Add(MakeShared<FJsonValueString>(T)); Set->SetArrayField(TEXT("tests"), TA); }
	Set->SetStringField(TEXT("gpuScoreMethod"), TEXT("geomean(test scores); test score = 100*avgFps*clamp(1-cov,0.8,1), median of passes"));
	Root->SetObjectField(TEXT("settings"), Set);
}

void ABenchGameMode::FinishRun()
{
	Phase = EBenchPhase::Done;
	if (SysMon) SysMon->SetRecording(false);
	auto StatObj = [](const FPassStats& S){ TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("score"), FMath::RoundToDouble(S.Score)); O->SetNumberField(TEXT("avgFps"), R2(S.AvgFps));
		O->SetNumberField(TEXT("p1LowFps"), R2(S.P1)); O->SetNumberField(TEXT("p01LowFps"), R2(S.P01));
		O->SetNumberField(TEXT("frameTimeMsAvg"), R2(S.AvgMs)); O->SetNumberField(TEXT("frameTimeMsStd"), R2(S.Std));
		O->SetNumberField(TEXT("cov"), FMath::RoundToDouble(S.Cov * 10000) / 10000); O->SetNumberField(TEXT("frames"), S.Frames); return O; };

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	FillCommon(Root);
	Root->SetBoolField(TEXT("aborted"), false);
	if (SysMon)
	{
		const FSysStats St = SysMon->GetStats(); const FSysSnapshot Sn = SysMon->Get();
		auto M = [](const FSysStat& S, double Mul = 1.0) { TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
			if (S.N) { O->SetNumberField(TEXT("avg"), R2(S.Sum / S.N * Mul)); O->SetNumberField(TEXT("peak"), R2(S.Peak * Mul)); }
			else { O->SetField(TEXT("avg"), MakeShared<FJsonValueNull>()); O->SetField(TEXT("peak"), MakeShared<FJsonValueNull>()); } return O; };
		TSharedRef<FJsonObject> G = MakeShared<FJsonObject>(); G->SetStringField(TEXT("source"), Sn.GpuSource.IsEmpty() ? TEXT("none") : Sn.GpuSource);
		G->SetObjectField(TEXT("utilPct"), M(St.GpuUtil)); G->SetObjectField(TEXT("tempC"), M(St.GpuTemp)); G->SetObjectField(TEXT("clockMHz"), M(St.GpuMHz));
		G->SetObjectField(TEXT("powerW"), M(St.GpuPower)); G->SetObjectField(TEXT("vramGiB"), M(St.Vram)); if (Sn.VramTotalGiB > 0) G->SetNumberField(TEXT("vramTotalGiB"), R2(Sn.VramTotalGiB));
		TSharedRef<FJsonObject> Cp = MakeShared<FJsonObject>(); Cp->SetObjectField(TEXT("utilPct"), M(St.CpuUtil)); Cp->SetObjectField(TEXT("tempC"), M(St.CpuTemp)); Cp->SetObjectField(TEXT("clockMHz"), M(St.CpuMHz));
		TSharedRef<FJsonObject> Sy = MakeShared<FJsonObject>(); Sy->SetNumberField(TEXT("samples"), St.GpuUtil.N > 0 ? St.GpuUtil.N : St.CpuUtil.N); Sy->SetNumberField(TEXT("hz"), 2);
		Sy->SetObjectField(TEXT("gpu"), G); Sy->SetObjectField(TEXT("cpu"), Cp); Sy->SetObjectField(TEXT("ramGiB"), M(St.Ram)); if (Sn.RamTotalGiB > 0) Sy->SetNumberField(TEXT("ramTotalGiB"), R2(Sn.RamTotalGiB));
		Root->SetObjectField(TEXT("sysmon"), Sy);
	}
	TArray<TSharedPtr<FJsonValue>> TestArr; TSharedRef<FJsonObject> PerTest = MakeShared<FJsonObject>();
	double LogSum = 0; bool bStable = true; ResultLines.Reset();
	for (int32 t = 0; t < TestPassFrames.Num(); ++t)
	{
		TArray<FPassStats> Stats; for (auto& F : TestPassFrames[t]) Stats.Add(Compute(F));
		TArray<int32> Idx; for (int32 i = 0; i < Stats.Num(); ++i) Idx.Add(i);
		Idx.Sort([&](int32 a, int32 b){ return Stats[a].Score < Stats[b].Score; });
		const FPassStats& Med = Stats[Idx[Idx.Num()/2]];
		const bool bS = Med.Score > 0 && (Stats[Idx.Last()].Score - Stats[Idx[0]].Score) / Med.Score <= 0.03;
		bStable &= bS; LogSum += FMath::Loge(FMath::Max(1.0, Med.Score));
		const FString Id = TestIds[t];
		TSharedRef<FJsonObject> T = StatObj(Med); T->SetStringField(TEXT("id"), Id); T->SetStringField(TEXT("name"), TestName(Id)); T->SetBoolField(TEXT("skipped"), false); T->SetBoolField(TEXT("stable"), bS);
		TArray<TSharedPtr<FJsonValue>> PassArr; for (auto& S : Stats) PassArr.Add(MakeShared<FJsonValueObject>(StatObj(S)));
		T->SetArrayField(TEXT("passes"), PassArr);
		if (RtConfigs.IsValidIndex(t) && RtConfigs[t].IsValid()) T->SetObjectField(TEXT("rtConfig"), RtConfigs[t]);
		TestArr.Add(MakeShared<FJsonValueObject>(T)); PerTest->SetNumberField(Id, FMath::RoundToDouble(Med.Score));
		UE_LOG(LogOmaBench, Display, TEXT("OmaBench %s: score=%.0f avgFps=%.1f p1=%.1f"), *Id, Med.Score, Med.AvgFps, Med.P1);
		ResultLines.Add(FString::Printf(TEXT("%s %-18s %6.0f  %6.1f FPS  1%% %6.1f"), *Id, *TestName(Id).ToUpper(), Med.Score, Med.AvgFps, Med.P1));
	}
	const double Gpu = TestPassFrames.Num() ? FMath::Exp(LogSum / TestPassFrames.Num()) : 0.0;
	Root->SetArrayField(TEXT("tests"), TestArr);
	TSharedRef<FJsonObject> Scores = MakeShared<FJsonObject>(); Scores->SetNumberField(TEXT("gpu"), FMath::RoundToDouble(Gpu)); Scores->SetField(TEXT("cpu"), MakeShared<FJsonValueNull>());
	Scores->SetObjectField(TEXT("tests"), PerTest); Scores->SetStringField(TEXT("gpuMethod"), TEXT("geomean"));
	Root->SetObjectField(TEXT("scores"), Scores); Root->SetBoolField(TEXT("stable"), bStable);
	const bool bOk = WriteJsonAtomic(ResultsPath, Root);
	WriteProgress(bOk ? TEXT("done") : TEXT("failed"));
	UE_LOG(LogOmaBench, Display, TEXT("OmaBench done: score=%.0f tests=%d results=%s ok=%d"), Gpu, TestPassFrames.Num(), *ResultsPath, bOk);
	ExitCode = bOk ? 0 : 1;
	ResultLines.Insert(FString::Printf(TEXT("GPU SCORE ......... %.0f%s"), Gpu, TestPassFrames.Num() > 1 ? TEXT("  (GEOMEAN)") : TEXT("")), 0);
	ResultLines.Add(FString::Printf(TEXT("STABLE ............ %s"), bStable ? TEXT("YES") : TEXT("NO")));
	ResultLines.Add(FString::Printf(TEXT("RESULTS -> %s"), *FPaths::GetCleanFilename(ResultsPath)));
}
