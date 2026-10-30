// Builds the T3 "Downpour" Niagara systems entirely from code via Niagara's external-edit API
// (UE 5.8, experimental). Every system: GPU compute sim, fixed seed + determinism, fixed tick delta,
// fixed bounds. Run: UnrealEditor-Cmd omabench.uproject -run=OmaFx  (art/build.sh does this).
#include "OmaFxCommandlet.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraTypes.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "EditorAssetLibrary.h"
#include "JsonObjectConverter.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "ViewModels/NiagaraSystemViewModel.h"

DEFINE_LOG_CATEGORY_STATIC(LogOmaFx, Log, All);
using U = UNiagaraExternalEditUtilities;

namespace
{
const TCHAR* ModPath(const FString& Short)
{
	static TMap<FString, FString> M = {
		{TEXT("SpawnRate"), TEXT("/Niagara/Modules/Emitter/SpawnRate.SpawnRate")},
		{TEXT("SpawnBurst"), TEXT("/Niagara/Modules/Emitter/SpawnBurst_Instantaneous.SpawnBurst_Instantaneous")},
		{TEXT("Shape"), TEXT("/Niagara/Modules/Spawn/Location/V2/ShapeLocation.ShapeLocation")},
		{TEXT("AddVelocity"), TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity")},
		{TEXT("Cone"), TEXT("/Niagara/Modules/Spawn/Velocity/AddVelocityInCone.AddVelocityInCone")},
		{TEXT("Gravity"), TEXT("/Niagara/Modules/Update/Forces/GravityForce.GravityForce")},
		{TEXT("Drag"), TEXT("/Niagara/Modules/Update/Forces/Drag.Drag")},
		{TEXT("Curl"), TEXT("/Niagara/Modules/Update/Forces/V2/CurlNoiseForce.CurlNoiseForce")},
		{TEXT("Solve"), TEXT("/Niagara/Modules/Solvers/SolveForcesAndVelocity.SolveForcesAndVelocity")},
		{TEXT("Collision"), TEXT("/Niagara/Modules/Collision/Collision.Collision")},
		{TEXT("ScaleColor"), TEXT("/Niagara/Modules/Update/Color/ScaleColor.ScaleColor")},
		{TEXT("ScaleSize"), TEXT("/Niagara/Modules/Update/Size/ScaleSpriteSize.ScaleSpriteSize")},
	};
	return *M.FindChecked(Short);
}

struct FSysBuilder
{
	UNiagaraSystem* Sys = nullptr;
	FNiagaraExternalEditContext* C = nullptr;
	TOptional<FNiagaraExternalEditContext> Own;
	void Refresh() { Flush(TEXT("pre-refresh")); Own.Reset(); Own.Emplace(Sys); C = &Own.GetValue(); }
	FName E;
	int32 Errors = 0;
	FString Log;

	void Flush(const FString& What)
	{
		for (const FText& T : C->Errors) { UE_LOG(LogOmaFx, Warning, TEXT("[%s] %s: %s"), *Sys->GetName(), *What, *T.ToString()); ++Errors; }
		C->Errors.Reset();
	}
	static FName ScriptOf(const TCHAR* S)
	{
		const FString X(S);
		if (X == TEXT("EU")) return TEXT("EmitterUpdateScript");
		if (X == TEXT("PS")) return TEXT("ParticleSpawnScript");
		return TEXT("ParticleUpdateScript");
	}
	FName Add(const TCHAR* Script, const TCHAR* Mod)
	{
		UNiagaraScript* S = LoadObject<UNiagaraScript>(nullptr, ModPath(Mod));
		FNiagaraExt_ModuleTopology T;
		U::AddModule(FNiagaraExt_StackItemReference(Sys, E, ScriptOf(Script)), S, T, *C);
		Flush(FString::Printf(TEXT("add %s"), Mod));
		FString Names; for (const FNiagaraExt_StackInputTopology& I : T.Inputs) Names += I.Name.ToString() + (I.bIsStaticSwitch ? TEXT("(s)") : TEXT("")) + TEXT("; ");
		Log += FString::Printf(TEXT("%s/%s -> %s: %s\n"), Script, Mod, *T.ModuleName.ToString(), *Names);
		return T.ModuleName;
	}
	// Value spec: "=hlsl", enum display/name fragment, bool, number, or comma list (vec2/3/color).
	void Set(const TCHAR* Script, FName Mod, const TCHAR* Input, const FString& V)
	{
		FNiagaraExt_StackItemReference R(Sys, E, ScriptOf(Script), Mod); R.InputNameStack = { FName(Input) };
		FNiagaraExt_StackInputSchema Sc; U::GetStackInputSchema(R, Sc, *C);
		if (C->HasErrors()) { Flush(FString::Printf(TEXT("schema %s.%s"), *Mod.ToString(), Input)); return; }
		const FNiagaraTypeDefinition& Ty = Sc.Type;
		FNiagaraExt_StackInputValue Val;
		TArray<FString> P; V.ParseIntoArray(P, TEXT(","));
		auto F = [&](int32 i) { return P.IsValidIndex(i) ? FCString::Atof(*P[i]) : 0.f; };
		if (V.StartsWith(TEXT("@")))
		{
			Val.InitializeAs<FNiagaraExt_StackInputData_Linked>(); auto& L = Val.GetMutable<FNiagaraExt_StackInputData_Linked>().LinkedVariable; L.Name = FName(V.Mid(1)); L.Type = Ty;
		}
		else if (V.StartsWith(TEXT("=")))
		{
			Val.InitializeAs<FNiagaraExt_StackInputData_HlslExpression>(); Val.GetMutable<FNiagaraExt_StackInputData_HlslExpression>().HlslExpression = V.Mid(1);
		}
		else if (Ty.IsEnum())
		{
			UEnum* En = Ty.GetEnum(); int64 Found = INDEX_NONE;
			for (int32 i = 0; i < En->NumEnums() - 1 && Found == INDEX_NONE; ++i)
				if (En->GetDisplayNameTextByIndex(i).ToString().Equals(V, ESearchCase::IgnoreCase) || En->GetNameStringByIndex(i).Equals(V, ESearchCase::IgnoreCase)) Found = i;
			for (int32 i = 0; i < En->NumEnums() - 1 && Found == INDEX_NONE; ++i)
				if (En->GetDisplayNameTextByIndex(i).ToString().Contains(V)) Found = i;
			if (Found == INDEX_NONE)
			{
				FString All; for (int32 i = 0; i < En->NumEnums() - 1; ++i) All += En->GetDisplayNameTextByIndex(i).ToString() + TEXT("|");
				UE_LOG(LogOmaFx, Warning, TEXT("[%s] enum %s.%s: '%s' not in %s"), *Sys->GetName(), *Mod.ToString(), Input, *V, *All); ++Errors; return;
			}
			{ FString All; for (int32 i = 0; i < En->NumEnums() - 1; ++i) All += En->GetDisplayNameTextByIndex(i).ToString() + TEXT("|"); Log += FString::Printf(TEXT("  enum %s.%s = %s of [%s]\n"), *Mod.ToString(), Input, *En->GetDisplayNameTextByIndex(Found).ToString(), *All); }
			Val.InitializeAs<FNiagaraExt_StackInputData_Enum>(); auto& D = Val.GetMutable<FNiagaraExt_StackInputData_Enum>(); D.Enum = En; D.EnumName = FName(*En->GetNameByIndex(Found).ToString());
		}
		else if (Ty == FNiagaraTypeDefinition::GetBoolDef()) { FNiagaraBool B; B.SetValue(V.ToBool()); Val.InitializeAs<FNiagaraBool>(B); }
		else if (Ty == FNiagaraTypeDefinition::GetFloatDef()) { FNiagaraFloat X; X.Value = F(0); Val.InitializeAs<FNiagaraFloat>(X); }
		else if (Ty == FNiagaraTypeDefinition::GetIntDef()) { FNiagaraInt32 X; X.Value = FCString::Atoi(*V); Val.InitializeAs<FNiagaraInt32>(X); }
		else if (Ty == FNiagaraTypeDefinition::GetVec2Def()) { FVector2f X(F(0), F(1)); Val.InitializeAs(Ty.GetScriptStruct(), (const uint8*)&X); }
		else if (Ty == FNiagaraTypeDefinition::GetVec3Def()) { FVector3f X(F(0), F(1), F(2)); Val.InitializeAs(Ty.GetScriptStruct(), (const uint8*)&X); }
		else if (Ty == FNiagaraTypeDefinition::GetPositionDef()) { FNiagaraPosition X(FVector3f(F(0), F(1), F(2))); Val.InitializeAs<FNiagaraPosition>(X); }
		else if (Ty == FNiagaraTypeDefinition::GetColorDef()) Val.InitializeAs<FLinearColor>(FLinearColor(F(0), F(1), F(2), P.Num() > 3 ? F(3) : 1.f));
		else { UE_LOG(LogOmaFx, Warning, TEXT("[%s] %s.%s: unsupported type %s"), *Sys->GetName(), *Mod.ToString(), Input, *Ty.GetName()); ++Errors; return; }
		U::SetStackInputData(R, Val, *C);
		if (!V.StartsWith(TEXT("=")) && (Ty.IsEnum() || Ty == FNiagaraTypeDefinition::GetBoolDef())) Refresh();
		Flush(FString::Printf(TEXT("set %s.%s=%s"), *Mod.ToString(), Input, *V));
	}
	// Merge a JSON object fragment into a property blob.
	static FString Merge(const FString& Blob, const FString& Frag)
	{
		TSharedPtr<FJsonObject> A, B;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Blob), A); FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Frag), B);
		if (!A.IsValid()) A = MakeShared<FJsonObject>();
		if (B.IsValid()) for (const auto& KV : B->Values) A->SetField(KV.Key, KV.Value);
		FString O; FJsonSerializer::Serialize(A.ToSharedRef(), TJsonWriterFactory<>::Create(&O)); return O;
	}
	void Emitter(const FString& Frag)
	{
		FNiagaraExt_StackItemReference R(Sys, E); FNiagaraExt_EmitterData D; U::GetEmitterData(R, D, *C);
		D.PropertyValues = Merge(D.PropertyValues, Frag); U::SetEmitterData(R, D, *C); Flush(TEXT("emitter data"));
	}
	void Renderer(TSubclassOf<UNiagaraRendererProperties> Cls, const FString& Frag)
	{
		FNiagaraExt_RendererRef RR; U::AddRenderer(FNiagaraExt_StackItemReference(Sys, E), Cls, RR, *C); Flush(TEXT("add renderer"));
		FNiagaraExt_StackItemReference R(Sys, E); R.RendererIndex = RR.RendererIndex;
		FNiagaraExt_RendererData D; U::GetRendererData(R, D, *C); D.PropertyValues = Merge(D.PropertyValues, Frag); U::SetRendererData(R, D, *C); Flush(TEXT("renderer data"));
	}
};

UNiagaraSystem* NewSystem(const FString& Name, FNiagaraExternalEditContext& Ctx)
{
	const FString Dir = TEXT("/Game/OmaBench/Fx"), Path = Dir / Name;
	if (UEditorAssetLibrary::DoesAssetExist(Path)) UEditorAssetLibrary::DeleteAsset(Path);
	return U::CreateNiagaraSystem(Name, Dir, nullptr, Ctx);
}

FString Bounds(float X, float Y, float Z0, float Z1)
{
	return FString::Printf(TEXT("{\"SimTarget\":\"GPUComputeSim\",\"bDeterminism\":true,\"bLocalSpace\":false,\"CalculateBoundsMode\":\"Fixed\",\"FixedBounds\":{\"Min\":{\"X\":%f,\"Y\":%f,\"Z\":%f},\"Max\":{\"X\":%f,\"Y\":%f,\"Z\":%f},\"IsValid\":1},\"AllocationMode\":\"AutomaticEstimate\"}"), -X, -Y, Z0, X, Y, Z1);
}
// MinCam > 0: cull sprites whose centre is closer than MinCam to the camera (a big lit puff right at the lens is a
// full-screen quad of translucent overdraw for almost no visual gain).
const TCHAR* Sprite(const TCHAR* Mat, bool bVel, bool bSort = false, float MinCam = 0.f)
{
	static FString S; S = FString::Printf(TEXT("{\"Material\":\"%s\",\"Alignment\":\"%s\",\"SortMode\":\"%s\"%s}"), Mat, bVel ? TEXT("VelocityAligned") : TEXT("Unaligned"), bSort ? TEXT("ViewDistance") : TEXT("None"),
		MinCam > 0.f ? *FString::Printf(TEXT(",\"bEnableCameraDistanceCulling\":true,\"MinCameraDistance\":%f,\"MaxCameraDistance\":100000.0"), MinCam) : TEXT(""));
	return *S;
}
}

int32 UOmaFxCommandlet::Main(const FString& Params)
{
	UNiagaraEmitter* Tpl = LoadObject<UNiagaraEmitter>(nullptr, TEXT("/Niagara/DefaultAssets/Templates/Emitters/Minimal.Minimal"));
	if (!Tpl) { UE_LOG(LogOmaFx, Error, TEXT("Minimal emitter template missing")); return 1; }
	int32 TotalErr = 0; FString Report;

	auto Build = [&](const FString& Name, int32 Seed, TFunctionRef<void(FSysBuilder&)> Fn)
	{
		FNiagaraExternalEditContext Ctx; UNiagaraSystem* Sys = NewSystem(Name, Ctx);
		if (!Sys) { UE_LOG(LogOmaFx, Error, TEXT("create %s failed"), *Name); ++TotalErr; return; }
		FSysBuilder B; B.Sys = Sys; B.Own.Emplace(Sys); B.C = &B.Own.GetValue();
		FNiagaraExt_EmitterTopology Topo; U::AddEmitter(Tpl, TEXT("E"), Topo, *B.C); B.E = TEXT("E"); B.Flush(TEXT("add emitter"));
		// System: deterministic seed, fixed 90 Hz sim step (frame-rate independent particle work), warm start.
		FNiagaraExt_SystemData SD; U::GetSystemData(Sys, SD, *B.C);
		SD.PropertyValues = FSysBuilder::Merge(SD.PropertyValues, FString::Printf(TEXT("{\"bDeterminism\":true,\"RandomSeed\":%d,\"bFixedTickDelta\":true,\"FixedTickDeltaTime\":0.0111111,\"WarmupTime\":3.0,\"WarmupTickDelta\":0.0333333}"), Seed));
		U::SetSystemData(Sys, SD, *B.C); B.Flush(TEXT("system data"));
		B.Emitter(FString::Printf(TEXT("{\"RandomSeed\":%d}"), Seed));
		// Runtime-tunable user parameters (BenchDownpour sets them; defaults = benchmark load).
		auto UF = [&](const TCHAR* N, float D) { FNiagaraExt_UserVariable UV; UV.Name = FName(FString(TEXT("User.")) + N); UV.Type = FNiagaraTypeDefinition::GetFloatDef(); UV.DefaultValue.Set(UV.Type, FNiagaraVariant(&D, sizeof(D))); U::AddUserVariable(Sys, UV, *B.C); };
		auto UI = [&](const TCHAR* N, int32 D) { FNiagaraExt_UserVariable UV; UV.Name = FName(FString(TEXT("User.")) + N); UV.Type = FNiagaraTypeDefinition::GetIntDef(); UV.DefaultValue.Set(UV.Type, FNiagaraVariant(&D, sizeof(D))); U::AddUserVariable(Sys, UV, *B.C); };
		if (Name == TEXT("NS_Rain")) UF(TEXT("RainRate"), 400000.f);
		if (Name == TEXT("NS_Splash")) UF(TEXT("SplashRate"), 120000.f);
		if (Name == TEXT("NS_Steam")) UF(TEXT("SteamRate"), 600.f);
		if (Name == TEXT("NS_Smoke")) UF(TEXT("SmokeRate"), 300.f);
		B.Flush(TEXT("user vars"));
		Fn(B);
		Sys->RequestCompile(false); Sys->WaitForCompilationComplete();
		const bool bSaved = UEditorAssetLibrary::SaveLoadedAsset(Sys, false);
		UE_LOG(LogOmaFx, Display, TEXT("OMAFX %s built: %d errors, saved=%d"), *Name, B.Errors, bSaved ? 1 : 0);
		TotalErr += B.Errors; Report += TEXT("== ") + Name + TEXT("\n") + B.Log;
	};

	// Rain: ~550k live streaks in a 60 x 60 x 30 m box around the camera; die on depth-buffer collision.
	Build(TEXT("NS_Rain"), 101, [](FSysBuilder& B) {
		B.Emitter(Bounds(4000, 4000, -1500, 3500));
		FName Sr = B.Add(TEXT("EU"), TEXT("SpawnRate")); B.Set(TEXT("EU"), Sr, TEXT("SpawnRate"), TEXT("@User.RainRate"));
		FName In = TEXT("InitializeParticle");
		B.Set(TEXT("PS"), In, TEXT("Lifetime"), TEXT("1.4"));
		B.Set(TEXT("PS"), In, TEXT("Color Mode"), TEXT("Direct Set"));
		B.Set(TEXT("PS"), In, TEXT("Color"), TEXT("0.8,0.86,1.0,0.6"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size Mode"), TEXT("Non-Uniform"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size"), TEXT("1.6,70"));
		FName Sh = B.Add(TEXT("PS"), TEXT("Shape"));
		B.Set(TEXT("PS"), Sh, TEXT("Shape Primitive"), TEXT("Box"));
		B.Set(TEXT("PS"), Sh, TEXT("Box Size"), TEXT("6000,6000,3000"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset Mode"), TEXT("Default"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset"), TEXT("0,0,1500"));
		FName Av = B.Add(TEXT("PS"), TEXT("AddVelocity")); B.Set(TEXT("PS"), Av, TEXT("Velocity"), TEXT("140,60,-2600"));
		FName Gr = B.Add(TEXT("PU"), TEXT("Gravity"));
		FName So = B.Add(TEXT("PU"), TEXT("Solve"));
		FName Co = B.Add(TEXT("PU"), TEXT("Collision"));
		B.Set(TEXT("PU"), Co, TEXT("KillOnCollision"), TEXT("true")); B.Set(TEXT("PU"), Co, TEXT("Kill On Collision"), TEXT("true"));
		B.Renderer(UNiagaraSpriteRendererProperties::StaticClass(), Sprite(TEXT("/Game/OmaBench/Materials/M_FxRain.M_FxRain"), true));
		(void)Gr; (void)So;
	});
	// Splashes: short-lived crowns on the street plane around the camera (~70k live), bounce off depth.
	Build(TEXT("NS_Splash"), 202, [](FSysBuilder& B) {
		B.Emitter(Bounds(4000, 4000, -400, 400));
		FName Sr = B.Add(TEXT("EU"), TEXT("SpawnRate")); B.Set(TEXT("EU"), Sr, TEXT("SpawnRate"), TEXT("@User.SplashRate"));
		FName In = TEXT("InitializeParticle");
		B.Set(TEXT("PS"), In, TEXT("Lifetime"), TEXT("0.35"));
		B.Set(TEXT("PS"), In, TEXT("Color Mode"), TEXT("Direct Set"));
		B.Set(TEXT("PS"), In, TEXT("Color"), TEXT("0.7,0.78,0.9,0.7"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size Mode"), TEXT("Uniform"));
		B.Set(TEXT("PS"), In, TEXT("Uniform Sprite Size"), TEXT("2.2"));
		FName Sh = B.Add(TEXT("PS"), TEXT("Shape"));
		B.Set(TEXT("PS"), Sh, TEXT("Shape Primitive"), TEXT("Box"));
		B.Set(TEXT("PS"), Sh, TEXT("Box Size"), TEXT("5000,5000,2"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset Mode"), TEXT("Default"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset"), TEXT("=float3(0, 0, 4 - Engine.Owner.Position.z)"));
		FName Cn = B.Add(TEXT("PS"), TEXT("Cone")); B.Set(TEXT("PS"), Cn, TEXT("Cone Angle"), TEXT("35")); B.Set(TEXT("PS"), Cn, TEXT("Velocity Strength"), TEXT("260"));
		B.Add(TEXT("PU"), TEXT("Gravity")); B.Add(TEXT("PU"), TEXT("Solve"));
		FName Co = B.Add(TEXT("PU"), TEXT("Collision")); B.Set(TEXT("PU"), Co, TEXT("Restitution"), TEXT("0.3"));
		// Shelter mask: no splashes where the rain cannot reach the street (under the two overpass decks and the
		// stall awnings along both kerbs); the particles still simulate, so the compute load is unchanged.
		FName Sc = B.Add(TEXT("PU"), TEXT("ScaleColor"));
		B.Set(TEXT("PU"), Sc, TEXT("Scale Alpha"), TEXT("=((abs(Particles.Position.x) < 1600 && (abs(Particles.Position.y - 2500) < 380 || abs(Particles.Position.y - 5600) < 380)) || (abs(Particles.Position.x) > 1040 && abs(Particles.Position.x) < 1320 && Particles.Position.y > 650 && Particles.Position.y < 8450 && abs(Particles.Position.y - 6000) > 300)) ? 0.0 : 1.0"));
		B.Renderer(UNiagaraSpriteRendererProperties::StaticClass(), Sprite(TEXT("/Game/OmaBench/Materials/M_FxRain.M_FxRain"), true));
	});
	// Sparks from a failing neon sign: bursty, bouncing on the wet street (depth-buffer collision).
	Build(TEXT("NS_Sparks"), 303, [](FSysBuilder& B) {
		B.Emitter(Bounds(1500, 1500, -1200, 400));
		FName Sr = B.Add(TEXT("EU"), TEXT("SpawnRate"));
		B.Set(TEXT("EU"), Sr, TEXT("SpawnRate"), TEXT("=(frac(Engine.Time * 0.43 + 0.37) < 0.14) ? 4000.0 : 0.0"));
		FName In = TEXT("InitializeParticle");
		B.Set(TEXT("PS"), In, TEXT("Lifetime"), TEXT("1.8"));
		B.Set(TEXT("PS"), In, TEXT("Color Mode"), TEXT("Direct Set"));
		B.Set(TEXT("PS"), In, TEXT("Color"), TEXT("12,5,1.2,1"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size Mode"), TEXT("Non-Uniform"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size"), TEXT("1.4,9"));
		FName Sh = B.Add(TEXT("PS"), TEXT("Shape")); B.Set(TEXT("PS"), Sh, TEXT("Shape Primitive"), TEXT("Sphere")); B.Set(TEXT("PS"), Sh, TEXT("Sphere Radius"), TEXT("25"));
		FName Cn = B.Add(TEXT("PS"), TEXT("Cone")); B.Set(TEXT("PS"), Cn, TEXT("Cone Angle"), TEXT("80")); B.Set(TEXT("PS"), Cn, TEXT("Velocity Strength"), TEXT("420"));
		B.Set(TEXT("PS"), Cn, TEXT("Cone Axis"), TEXT("0,-0.3,-1"));
		B.Add(TEXT("PU"), TEXT("Gravity"));
		FName Dr = B.Add(TEXT("PU"), TEXT("Drag")); B.Set(TEXT("PU"), Dr, TEXT("Drag"), TEXT("0.6"));
		B.Add(TEXT("PU"), TEXT("Solve"));
		FName Co = B.Add(TEXT("PU"), TEXT("Collision")); B.Set(TEXT("PU"), Co, TEXT("Restitution"), TEXT("0.45")); B.Set(TEXT("PU"), Co, TEXT("Friction"), TEXT("0.2"));
		FName Sc = B.Add(TEXT("PU"), TEXT("ScaleColor")); (void)Sc;
		B.Renderer(UNiagaraSpriteRendererProperties::StaticClass(), Sprite(TEXT("/Game/OmaBench/Materials/M_FxSpark.M_FxSpark"), true));
	});
	// Vent steam: rising, curl-noise churned, growing lit puffs (heavy translucent overdraw near the lens).
	Build(TEXT("NS_Steam"), 404, [](FSysBuilder& B) {
		B.Emitter(Bounds(2500, 2500, -100, 2500));
		FName Sr = B.Add(TEXT("EU"), TEXT("SpawnRate")); B.Set(TEXT("EU"), Sr, TEXT("SpawnRate"), TEXT("@User.SteamRate"));
		FName In = TEXT("InitializeParticle");
		B.Set(TEXT("PS"), In, TEXT("Lifetime"), TEXT("4.5"));
		B.Set(TEXT("PS"), In, TEXT("Color Mode"), TEXT("Direct Set"));
		B.Set(TEXT("PS"), In, TEXT("Color"), TEXT("0.55,0.6,0.68,0.05"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size Mode"), TEXT("Uniform"));
		B.Set(TEXT("PS"), In, TEXT("Uniform Sprite Size"), TEXT("70"));
		FName Sh = B.Add(TEXT("PS"), TEXT("Shape")); B.Set(TEXT("PS"), Sh, TEXT("Shape Primitive"), TEXT("Sphere")); B.Set(TEXT("PS"), Sh, TEXT("Sphere Radius"), TEXT("40"));
		FName Cn = B.Add(TEXT("PS"), TEXT("Cone")); B.Set(TEXT("PS"), Cn, TEXT("Cone Angle"), TEXT("20")); B.Set(TEXT("PS"), Cn, TEXT("Velocity Strength"), TEXT("180"));
		FName Cu = B.Add(TEXT("PU"), TEXT("Curl")); B.Set(TEXT("PU"), Cu, TEXT("Noise Strength"), TEXT("220")); B.Set(TEXT("PU"), Cu, TEXT("Noise Frequency"), TEXT("0.004"));
		FName Dr = B.Add(TEXT("PU"), TEXT("Drag")); B.Set(TEXT("PU"), Dr, TEXT("Drag"), TEXT("0.8"));
		B.Add(TEXT("PU"), TEXT("Solve"));
		FName Ss = B.Add(TEXT("PU"), TEXT("ScaleSize")); B.Set(TEXT("PU"), Ss, TEXT("Scale Sprite Size Mode"), TEXT("Uniform")); B.Set(TEXT("PU"), Ss, TEXT("Uniform Scale Factor"), TEXT("=1.0 + Particles.NormalizedAge * 5.0"));
		FName Sc = B.Add(TEXT("PU"), TEXT("ScaleColor")); B.Set(TEXT("PU"), Sc, TEXT("Scale Alpha"), TEXT("=saturate(Particles.NormalizedAge * 6.0) * (1.0 - Particles.NormalizedAge)"));
		B.Renderer(UNiagaraSpriteRendererProperties::StaticClass(), Sprite(TEXT("/Game/OmaBench/Materials/M_FxSmoke.M_FxSmoke"), false, true, 220.f));
	});
	// Street smoke/haze: big slow lit sprites filling the market canyon around the camera.
	Build(TEXT("NS_Smoke"), 505, [](FSysBuilder& B) {
		B.Emitter(Bounds(5000, 5000, -600, 2500));
		FName Sr = B.Add(TEXT("EU"), TEXT("SpawnRate")); B.Set(TEXT("EU"), Sr, TEXT("SpawnRate"), TEXT("@User.SmokeRate"));
		FName In = TEXT("InitializeParticle");
		B.Set(TEXT("PS"), In, TEXT("Lifetime"), TEXT("9"));
		B.Set(TEXT("PS"), In, TEXT("Color Mode"), TEXT("Direct Set"));
		B.Set(TEXT("PS"), In, TEXT("Color"), TEXT("0.3,0.33,0.4,0.012"));
		B.Set(TEXT("PS"), In, TEXT("Sprite Size Mode"), TEXT("Uniform"));
		B.Set(TEXT("PS"), In, TEXT("Uniform Sprite Size"), TEXT("650"));
		FName Sh = B.Add(TEXT("PS"), TEXT("Shape")); B.Set(TEXT("PS"), Sh, TEXT("Shape Primitive"), TEXT("Box"));
		B.Set(TEXT("PS"), Sh, TEXT("Box Size"), TEXT("3000,7000,700"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset Mode"), TEXT("Default"));
		B.Set(TEXT("PS"), Sh, TEXT("Offset"), TEXT("=float3(0, 1500, 300 - Engine.Owner.Position.z)"));
		FName Cu = B.Add(TEXT("PU"), TEXT("Curl")); B.Set(TEXT("PU"), Cu, TEXT("Noise Strength"), TEXT("60")); B.Set(TEXT("PU"), Cu, TEXT("Noise Frequency"), TEXT("0.001"));
		FName Dr = B.Add(TEXT("PU"), TEXT("Drag")); B.Set(TEXT("PU"), Dr, TEXT("Drag"), TEXT("1.0"));
		B.Add(TEXT("PU"), TEXT("Solve"));
		FName Sc = B.Add(TEXT("PU"), TEXT("ScaleColor")); B.Set(TEXT("PU"), Sc, TEXT("Scale Alpha"), TEXT("=sin(Particles.NormalizedAge * 3.14159)"));
		B.Renderer(UNiagaraSpriteRendererProperties::StaticClass(), Sprite(TEXT("/Game/OmaBench/Materials/M_FxSmoke.M_FxSmoke"), false, true, 500.f));
	});
	FFileHelper::SaveStringToFile(Report, *(FPaths::ProjectSavedDir() / TEXT("omafx_report.txt")));
	UE_LOG(LogOmaFx, Display, TEXT("OMAFX done, %d errors"), TotalErr);
	return TotalErr ? 1 : 0;
}
