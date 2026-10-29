// T2 "Skyline": a wet skyscraper rooftop inside the City Night city. A police spinner lands on the
// helipad and lifts off again while traffic threads the towers; glass curtain-wall neighbours mirror
// the neon. All meshes come from art/blender (procedural, open source); placement is fixed-seed.
#include "BenchGameMode.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmaSky, Log, All);

namespace
{
	UStaticMesh* LoadMesh(const TCHAR* Name)
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Game/OmaBench/Meshes/%s.%s"), Name, Name));
		if (!M) UE_LOG(LogOmaSky, Error, TEXT("Missing mesh %s (run art/build.sh)"), Name);
		return M;
	}
	void BindSlots(UStaticMeshComponent* C, const TMap<FName, UMaterialInterface*>& Slots)
	{
		const UStaticMesh* M = C->GetStaticMesh(); if (!M) return;
		const TArray<FStaticMaterial>& SM = M->GetStaticMaterials();
		for (int32 i = 0; i < SM.Num(); ++i)
		{
			FString N = SM[i].MaterialSlotName.ToString(); int32 Dot; if (N.FindChar(TEXT('.'), Dot)) N.LeftInline(Dot);
			if (UMaterialInterface* const* Mat = Slots.Find(FName(*N))) C->SetMaterial(i, *Mat);
		}
	}
	double Smooth(double x) { x = FMath::Clamp(x, 0.0, 1.0); return x * x * (3 - 2 * x); }
}

void ABenchGameMode::BuildSkyline()
{
	UWorld* W = GetWorld();
	FRandomStream Rng(7331);
	FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FLinearColor Cyan = Pal(TEXT("bright_cyan"), FLinearColor(0, 0.9f, 1)), Mag = Pal(TEXT("bright_magenta"), FLinearColor(1, 0.1f, 0.6f));
	const FLinearColor Amber = Pal(TEXT("bright_yellow"), FLinearColor(1, 0.7f, 0.1f)), Accent = Pal(TEXT("accent"), FLinearColor(0.5f, 0.5f, 0.9f));
	const FLinearColor Neon[] = { Cyan, Mag, Amber, Pal(TEXT("bright_green"), FLinearColor(0.2f, 1, 0.4f)), Accent };
	const FLinearColor Warm(1, 0.92f, 0.8f), Red(1, 0.06f, 0.04f), Blue(0.12f, 0.3f, 1);

	auto BlockTop = [&](int32 X0, int32 X1, int32 Y0, int32 Y1) { float M = 0; for (int32 x = X0; x <= X1; ++x) for (int32 y = Y0; y <= Y1; ++y) if (const float* T = CellTop.Find(FIntPoint(x, y))) M = FMath::Max(M, *T); return M; };
	// Hero tower takes over the block east of the central avenue (cells x 2..4, y 1..5).
	const float RoofZ = FMath::Max(16000.f, BlockTop(2, 4, 1, 5) + 300.f);
	RoofCentre = FVector(3000, 3000, 0); RoofTop = RoofZ + 60.f;
	const FVector2D Half(1450, 2450);

	AActor* Site = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	USceneComponent* Root = NewObject<USceneComponent>(Site, TEXT("SkyRoot")); Root->SetMobility(EComponentMobility::Static);
	Site->SetRootComponent(Root); Root->RegisterComponent();
	auto MID = [&](UMaterialInterface* Base, TFunctionRef<void(UMaterialInstanceDynamic*)> F) { UMaterialInstanceDynamic* M = UMaterialInstanceDynamic::Create(Base ? Base : BaseMaterial.Get(), Site); F(M); return M; };
	auto ChromeV = [&](FLinearColor B, float R, float Mt) { return MID(ChromeMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Base"), B); M->SetScalarParameterValue(TEXT("Roughness"), R); M->SetScalarParameterValue(TEXT("Metallic"), Mt); }); };
	auto Emit = [&](FLinearColor C, float I) { return MID(EmitMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), C * I); }); };
	UMaterialInterface* Chrome = ChromeV(FLinearColor(0.85f, 0.86f, 0.9f), 0.08f, 1);
	UMaterialInterface* Dark = ChromeV(FLinearColor(0.03f, 0.03f, 0.035f), 0.3f, 0.2f);
	UMaterialInterface* Wet = ChromeV(FLinearColor(0.035f, 0.036f, 0.04f), 0.05f, 0.0f);          // rain-slick roof membrane
	UMaterialInterface* CurtainWall = ChromeV(FLinearColor(0.03f, 0.04f, 0.05f), 0.03f, 1.0f);      // mirror glass neighbours
	UMaterialInterface* Marking = ChromeV(FLinearColor(0.85f, 0.62f, 0.05f), 0.25f, 0.0f);
	UMaterialInterface* Glass = GlassMaterial ? GlassMaterial.Get() : BaseMaterial.Get();
	TMap<FName, UMaterialInterface*> Common = { {TEXT("Chrome"), Chrome}, {TEXT("Dark"), Dark}, {TEXT("Glass"), Glass}, {TEXT("Marking"), Marking},
		{TEXT("Paint"), ChromeV(FLinearColor(0.12f, 0.12f, 0.13f), 0.35f, 0.5f)}, {TEXT("EmitWhite"), Emit(Warm, 20)}, {TEXT("EmitRed"), Emit(Red, 25)},
		{TEXT("EmitBlue"), Emit(Blue, 25)}, {TEXT("EmitAmber"), Emit(Amber, 15)}, {TEXT("EmitGlyph"), Emit(Cyan, 22)},
		{TEXT("Screen"), MID(HoloMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("Color"), Mag * 2.f); })} };

	int32 NC = 0;
	auto Place = [&](AActor* Owner, UStaticMesh* Mesh, const FVector& Loc, float Yaw, const TMap<FName, UMaterialInterface*>& Slots, float Scale = 1.f, bool bMovable = false) {
		if (!Mesh) return (UStaticMeshComponent*)nullptr;
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner, *FString::Printf(TEXT("S%d"), NC++));
		C->SetStaticMesh(Mesh); C->SetMobility(bMovable ? EComponentMobility::Movable : EComponentMobility::Static); C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetRelativeTransform(FTransform(FRotator(0, Yaw, 0), Loc, FVector(Scale)));
		if (USceneComponent* RC = Owner->GetRootComponent()) C->SetupAttachment(RC);
		BindSlots(C, Slots); C->RegisterComponent(); return C; };
	auto Box = [&](UMaterialInterface* M, const FVector& Loc, const FVector& Size) { TMap<FName, UMaterialInterface*> S; UStaticMeshComponent* C = Place(Site, CubeMesh, Loc, 0, S, 1.f); C->SetRelativeScale3D(Size / 100.f); C->SetMaterial(0, M); return C; };
	auto Light = [&](auto* L, const FVector& Loc, const FRotator& Rot, FLinearColor Col, float I, float R, bool bShadow) {
		L->SetupAttachment(Root); L->SetMobility(EComponentMobility::Movable); L->RegisterComponent(); L->SetWorldLocationAndRotation(Loc, Rot);
		L->SetLightColor(Col); L->SetIntensity(I); L->SetAttenuationRadius(R); L->SetCastShadows(bShadow); return L; };

	// Hero tower shell (lit windows, same material family as the city) + wet roof slab + parapet.
	{
		UMaterialInterface* TowerM = MID(TowerMaterial, [&](UMaterialInstanceDynamic* M){ M->SetVectorParameterValue(TEXT("WindowColor"), Warm); });
		Box(TowerM, FVector(RoofCentre.X, RoofCentre.Y, RoofZ / 2), FVector(Half.X * 2, Half.Y * 2, RoofZ));
		Box(Wet, FVector(RoofCentre.X, RoofCentre.Y, RoofZ + 30), FVector(Half.X * 2, Half.Y * 2, 60));
		for (int32 s = -1; s <= 1; s += 2)
		{
			Box(Dark, FVector(RoofCentre.X + s * Half.X, RoofCentre.Y, RoofTop + 20), FVector(40, Half.Y * 2, 40));
			Box(Dark, FVector(RoofCentre.X, RoofCentre.Y + s * Half.Y, RoofTop + 20), FVector(Half.X * 2, 40, 40));
		}
		// Puddle sheen bands (even glossier) for reflections to land on.
		for (int32 i = 0; i < 7; ++i) Box(ChromeV(FLinearColor(0.02f, 0.02f, 0.025f), 0.01f, 0.f), FVector(RoofCentre.X + Rng.FRandRange(-1100, 1100), RoofCentre.Y + Rng.FRandRange(-2000, 2000), RoofTop + 1), FVector(Rng.FRandRange(200, 600), Rng.FRandRange(150, 500), 2));
	}
	// Railings along the parapet (4 m segments).
	if (UStaticMesh* Rail = LoadMesh(TEXT("SM_Railing")))
	{
		for (float y = -Half.Y + 200; y < Half.Y; y += 400) for (int32 s = -1; s <= 1; s += 2) Place(Site, Rail, FVector(RoofCentre.X + s * (Half.X - 10), RoofCentre.Y + y, RoofTop + 40), 90, Common);
		for (float x = -Half.X + 200; x < Half.X; x += 400) for (int32 s = -1; s <= 1; s += 2) Place(Site, Rail, FVector(RoofCentre.X + x, RoofCentre.Y + s * (Half.Y - 10), RoofTop + 40), 0, Common);
	}
	const FVector Pad = FVector(RoofCentre.X + 300, RoofCentre.Y + 900, RoofTop);
	Place(Site, LoadMesh(TEXT("SM_Helipad")), Pad, 0, Common);
	UStaticMesh* Hvac = LoadMesh(TEXT("SM_HVAC"));
	for (const FVector& L : { FVector(-900, -1700, 0), FVector(-900, -1150, 0), FVector(-300, -1700, 0), FVector(300, -1700, 0) }) Place(Site, Hvac, RoofCentre + L + FVector(0, 0, RoofTop), 0, Common);
	Place(Site, LoadMesh(TEXT("SM_WaterTank")), RoofCentre + FVector(950, -1500, RoofTop), 20, Common);
	Place(Site, LoadMesh(TEXT("SM_Antenna")), RoofCentre + FVector(-1150, 2150, RoofTop), 0, Common);
	Place(Site, LoadMesh(TEXT("SM_Antenna")), RoofCentre + FVector(1150, -2200, RoofTop), 45, Common, 1.3f);
	// Signs: rune glyphs only (invented, no words). Vertical blades hang off the avenue facade (-X), one standing sign + billboard on the roof.
	auto SignSlots = [&](FLinearColor C) { TMap<FName, UMaterialInterface*> S = Common; S.Add(TEXT("EmitGlyph"), Emit(C, 24)); return S; };
	UStaticMesh* SignV[2] = { LoadMesh(TEXT("SM_SignV1")), LoadMesh(TEXT("SM_SignV2")) };
	const float Fx = RoofCentre.X - Half.X - 30;
	for (int32 i = 0; i < 4; ++i)
	{
		const FLinearColor C = Neon[i % 5];
		const FVector L(Fx, RoofCentre.Y - 1800 + i * 1200, RoofZ - 1100 - (i % 2) * 1400);
		Place(Site, SignV[i % 2], L, -90, SignSlots(C), 1.4f);
		Light(NewObject<UPointLightComponent>(Site, *FString::Printf(TEXT("SignL%d"), i)), L + FVector(-300, 0, 500), FRotator::ZeroRotator, C, 60000, 2200, false);
	}
	Place(Site, LoadMesh(TEXT("SM_SignH1")), RoofCentre + FVector(-1250, -600, RoofTop + 220), -90, SignSlots(Mag), 1.1f);
	Place(Site, LoadMesh(TEXT("SM_Billboard")), RoofCentre + FVector(1150, 1500, RoofTop), -90, Common, 1.2f);
	Light(NewObject<URectLightComponent>(Site, TEXT("BillboardL")), RoofCentre + FVector(900, 1500, RoofTop + 720), FRotator(0, 180, 0), Mag, 30000, 3000, true);
	// Pad floodlights (RT shadows) + a cool rim from the city glow.
	for (int32 k = 0; k < 4; ++k)
	{
		const float A = k * 90.f + 45.f; const FVector Pos = Pad + FVector(FMath::Cos(FMath::DegreesToRadians(A)) * 1100, FMath::Sin(FMath::DegreesToRadians(A)) * 1100, 450);
		USpotLightComponent* S = Light(NewObject<USpotLightComponent>(Site, *FString::Printf(TEXT("Flood%d"), k)), Pos, (Pad - Pos).Rotation(), Warm, 90000, 3000, true);
		S->SetOuterConeAngle(40); S->SetInnerConeAngle(20);
	}
	Light(NewObject<UPointLightComponent>(Site, TEXT("RoofFill")), RoofCentre + FVector(0, 0, RoofTop + 1500), FRotator::ZeroRotator, Accent, 40000, 4500, false);

	// Glass curtain-wall neighbours: mirror slabs over the adjacent blocks (taller across the avenue).
	struct FBlk { int32 X0, X1, Y0, Y1; float Min; };
	const FBlk Blocks[] = { {-4, -2, 1, 5, RoofZ + 6000}, {-4, -2, 7, 11, RoofZ + 1500}, {2, 4, 7, 11, RoofZ - 2500}, {-4, -2, -5, -1, RoofZ - 1000} };
	for (const FBlk& B : Blocks)
	{
		const float Hh = FMath::Max(B.Min, BlockTop(B.X0, B.X1, B.Y0, B.Y1) + 200.f);
		const FVector C((B.X0 + B.X1) * 500.f, (B.Y0 + B.Y1) * 500.f, Hh / 2);
		const FVector S((B.X1 - B.X0) * 1000.f + 900.f, (B.Y1 - B.Y0) * 1000.f + 900.f, Hh);
		Box(CurtainWall, C, S);
		for (int32 f = 0; f < 4; ++f) Box(Emit(Neon[(f + B.X0 + 8) % 5], 6), C + FVector(0, 0, Hh / 2 - 300 - f * 2600), S + FVector(12, 12, -S.Z + 25)); // neon crown bands
	}
	UE_LOG(LogOmaSky, Log, TEXT("T2 skyline: roof at %.0f (block top %.0f), %d components"), RoofTop, BlockTop(2, 4, 1, 5), NC);

	// Hero police spinner: own flashing light-bar materials + matching point lights + belly searchlight.
	UStaticMesh* SpinnerM = LoadMesh(TEXT("SM_Spinner"));
	Hero = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
	{
		USceneComponent* HR = NewObject<USceneComponent>(Hero, TEXT("HeroRoot")); HR->SetMobility(EComponentMobility::Movable); Hero->SetRootComponent(HR); HR->RegisterComponent();
		HeroFlash = { Emit(Red, 30), Emit(Blue, 30) };
		TMap<FName, UMaterialInterface*> HS = Common;
		HS.Add(TEXT("Paint"), ChromeV(FLinearColor(0.05f, 0.06f, 0.1f), 0.12f, 0.7f)); HS.Add(TEXT("EmitRed"), HeroFlash[0]); HS.Add(TEXT("EmitBlue"), HeroFlash[1]);
		Place(Hero, SpinnerM, FVector::ZeroVector, 0, HS, 1.f, true);
		for (int32 i = 0; i < 2; ++i)
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(Hero, i ? TEXT("HeroBlue") : TEXT("HeroRed")); L->SetupAttachment(HR); L->SetMobility(EComponentMobility::Movable);
			L->SetRelativeLocation(FVector(-175, i ? 22 : -22, 150)); L->SetLightColor(i ? Blue : Red); L->SetAttenuationRadius(2500); L->SetCastShadows(true); L->RegisterComponent(); HeroLights.Add(L);
		}
		USpotLightComponent* Search = NewObject<USpotLightComponent>(Hero, TEXT("HeroSearch")); Search->SetupAttachment(HR); Search->SetMobility(EComponentMobility::Movable);
		Search->SetRelativeLocationAndRotation(FVector(250, 0, 30), FRotator(-60, 0, 0)); Search->SetIntensity(150000); Search->SetAttenuationRadius(6000);
		Search->SetOuterConeAngle(14); Search->SetInnerConeAngle(6); Search->SetLightColor(Warm); Search->SetCastShadows(true); Search->RegisterComponent();
	}
	// Traffic between the towers: civilian cars + a few patrol spinners on fixed lanes.
	UStaticMesh* Fleet[3] = { LoadMesh(TEXT("SM_CarA")), LoadMesh(TEXT("SM_CarB")), SpinnerM };
	const FLinearColor Liv[] = { {0.5f,0.52f,0.55f}, {0.35f,0.05f,0.04f}, {0.04f,0.04f,0.05f}, {0.6f,0.45f,0.15f}, {0.08f,0.18f,0.25f}, {0.8f,0.8f,0.82f} };
	for (int32 i = 0; i < 30; ++i)
	{
		FCarLane L; const int32 Kind = i % 3;
		if (Kind == 0) { L.Origin = FVector(Rng.FRandRange(-700, 700), -40000, RoofZ + Rng.FRandRange(-7000, 2500)); L.Dir = FVector(0, (i % 2) ? 1 : -1, 0); if (L.Dir.Y < 0) L.Origin.Y = 40000; }
		else { const float Y = (Rng.RandRange(0, 2)) * 6000.f; L.Origin = FVector(-40000, Y + Rng.FRandRange(-300, 300), RoofZ + Rng.FRandRange(-5000, 4000)); L.Dir = FVector((i % 2) ? 1 : -1, 0, 0); if (L.Dir.X < 0) L.Origin.X = 40000; }
		L.Speed = Rng.FRandRange(1800, 4200); L.Len = 80000; L.Phase = Rng.FRandRange(0, L.Len);
		AActor* Car = W->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, P);
		USceneComponent* CR = NewObject<USceneComponent>(Car, TEXT("R")); CR->SetMobility(EComponentMobility::Movable); Car->SetRootComponent(CR); CR->RegisterComponent();
		TMap<FName, UMaterialInterface*> CS = Common; CS.Add(TEXT("Paint"), ChromeV(Liv[i % 6], 0.15f, 0.6f));
		const int32 MeshI = (i % 7 == 3) ? 2 : (i % 2);
		if (UStaticMeshComponent* B = Place(Car, Fleet[MeshI], FVector::ZeroVector, L.Dir.Rotation().Yaw, CS, 1.f, true)) B->SetCastShadow(i % 3 == 0);
		if (i % 3 == 0)
		{
			UPointLightComponent* HL = NewObject<UPointLightComponent>(Car, TEXT("HL")); HL->SetupAttachment(CR); HL->SetMobility(EComponentMobility::Movable);
			HL->SetRelativeLocation(L.Dir * 400 + FVector(0, 0, 40)); HL->SetLightColor(Warm); HL->SetIntensity(25000); HL->SetAttenuationRadius(1600); HL->SetCastShadows(false); HL->RegisterComponent();
		}
		TrafficLanes.Add(L); Traffic.Add(Car);
	}
}

// Deterministic 60 s: hero approach -> touchdown -> parked -> lift-off -> departure; camera crosses the
// roof toward the pad, then walks to the avenue edge and floats out over it looking down the canyon.
void ABenchGameMode::UpdateSkyline(double T)
{
	const FVector Pad(RoofCentre.X + 300, RoofCentre.Y + 900, RoofTop + 28);
	// Hero path.
	FVector HP; float HYaw = 0, HPitch = 0;
	const FVector A = Pad + FVector(-14000, -9000, 5000), Hov = Pad + FVector(0, 0, 450), Lift = Pad + FVector(0, 0, 900), Away = Pad + FVector(-16000, 6000, 3500);
	if (T < 20) { const double s = Smooth(T / 20); HP = FMath::Lerp(A, Hov, s); HYaw = FMath::Lerp((Hov - A).Rotation().Yaw, 0.0, Smooth((T - 8) / 12)); HPitch = -6 * (1 - s); }
	else if (T < 26) { HP = FMath::Lerp(Hov, Pad, Smooth((T - 20) / 6)); }
	else if (T < 40) { HP = Pad; }
	else if (T < 46) { HP = FMath::Lerp(Pad, Lift, Smooth((T - 40) / 6)); }
	else { const double s = (T - 46) / 14; HP = FMath::Lerp(Lift, Away, s * s); HYaw = FMath::Lerp(0.0, (Away - Lift).Rotation().Yaw, Smooth(s * 2)); HPitch = -8 * Smooth(s * 3); }
	if (Hero) Hero->SetActorLocationAndRotation(HP, FRotator(HPitch, HYaw, 0));
	// Light bar: alternating red/blue strobe (double flash), deterministic in T.
	const double Ph = FMath::Fmod(T * 2.5, 2.0);
	const bool bRed = Ph < 1.0; const double Sub = FMath::Fmod(Ph, 1.0); const float On = (Sub < 0.18 || (Sub > 0.3 && Sub < 0.48)) ? 1.f : 0.08f;
	if (HeroFlash.Num() == 2) { HeroFlash[0]->SetVectorParameterValue(TEXT("Color"), FLinearColor(1, 0.06f, 0.04f) * 30 * (bRed ? On : 0.08f)); HeroFlash[1]->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.12f, 0.3f, 1) * 30 * (bRed ? 0.08f : On)); }
	if (HeroLights.Num() == 2) { HeroLights[0]->SetIntensity(bRed ? 80000 * On : 0); HeroLights[1]->SetIntensity(bRed ? 0 : 80000 * On); }
	// Traffic.
	for (int32 i = 0; i < Traffic.Num(); ++i)
	{
		const FCarLane& L = TrafficLanes[i];
		Traffic[i]->SetActorLocation(L.Origin + L.Dir * FMath::Fmod(L.Phase + L.Speed * T, (double)L.Len) + FVector(0, 0, 60 * FMath::Sin(T * 0.7 + i)));
	}
	// Camera: Catmull-Rom through roof-local keys (x, y, height above roof).
	if (!Camera) return;
	static const double Kt[] = { 0, 14, 28, 40, 50, 60 };
	static const FVector Kp[] = { {1250,-2250,190}, {750,-900,240}, {-350,350,280}, {-1150,1250,360}, {-2100,1450,650}, {-3300,1500,450} };
	int32 k = 0; while (k < 4 && T > Kt[k + 1]) ++k;
	const double u = (T - Kt[k]) / (Kt[k + 1] - Kt[k]);
	auto Key = [&](int32 i) { i = FMath::Clamp(i, 0, 5); return FVector(RoofCentre.X, RoofCentre.Y, RoofTop) + Kp[i]; };
	const FVector P0 = Key(k - 1), P1 = Key(k), P2 = Key(k + 1), P3 = Key(k + 2);
	const double u2 = u * u, u3 = u2 * u;
	const FVector Pos = 0.5 * ((2 * P1) + (-P0 + P2) * u + (2 * P0 - 5 * P1 + 4 * P2 - P3) * u2 + (-P0 + 3 * P1 - 3 * P2 + P3) * u3);
	const FVector Canyon(0, -14000, RoofTop - 11000);
	const FVector Look = FMath::Lerp(HP + FVector(0, 0, 120), Canyon, Smooth((T - 38) / 14));
	FRotator R = (Look - Pos).Rotation(); R.Roll = 3.f * FMath::Sin(T * 0.21);
	Camera->SetActorLocationAndRotation(Pos, R);
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController()) if (PC->GetViewTarget() != Camera) PC->SetViewTarget(Camera);
}
