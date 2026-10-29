#include "SysMon.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include <dlfcn.h>
#include <stdio.h>
#include <dirent.h>

static FString ReadSmall(const FString& Path, int32 Max = 4096)
{
	FILE* F = fopen(TCHAR_TO_UTF8(*Path), "rb"); if (!F) return FString();
	TArray<ANSICHAR> Buf; Buf.SetNumZeroed(Max + 1);
	const size_t N = fread(Buf.GetData(), 1, Max, F); fclose(F); Buf[N] = 0;
	return FString(UTF8_TO_TCHAR(Buf.GetData()));
}
static double ReadNum(const FString& Path) { const FString S = ReadSmall(Path, 64).TrimStartAndEnd(); return S.IsEmpty() ? -1.0 : FCString::Atod(*S); }

FSysMon::FSysMon()
{
	// Locate hwmon nodes once.
	if (DIR* D = opendir("/sys/class/hwmon"))
	{
		while (dirent* E = readdir(D))
		{
			if (E->d_name[0] == '.') continue;
			const FString Dir = FString(TEXT("/sys/class/hwmon/")) + UTF8_TO_TCHAR(E->d_name);
			const FString Name = ReadSmall(Dir / TEXT("name"), 64).TrimStartAndEnd();
			if (CpuTempPath.IsEmpty() && (Name == TEXT("k10temp") || Name == TEXT("coretemp") || Name == TEXT("zenpower"))) CpuTempPath = Dir / TEXT("temp1_input");
			if (AmdDir.IsEmpty() && Name == TEXT("amdgpu")) AmdDir = Dir;
		}
		closedir(D);
	}
	for (int32 i = 0; i < 512; ++i)
	{
		const FString P = FString::Printf(TEXT("/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq"), i);
		if (ReadNum(P) < 0) break; FreqPaths.Add(P);
	}
	Thread = FRunnableThread::Create(this, TEXT("OmaBenchSysMon"), 128 * 1024, TPri_BelowNormal);
}
FSysMon::~FSysMon()
{
	bStop = true;
	if (Thread) { Thread->WaitForCompletion(); delete Thread; Thread = nullptr; }
	if (NvLib) { if (auto Sh = (int(*)())dlsym(NvLib, "nvmlShutdown")) Sh(); dlclose(NvLib); }
}

bool FSysMon::InitNvml()
{
	NvLib = dlopen("libnvidia-ml.so.1", RTLD_NOW | RTLD_LOCAL); if (!NvLib) return false;
	auto Init = (int(*)())dlsym(NvLib, "nvmlInit_v2");
	auto Handle = (int(*)(unsigned, void**))dlsym(NvLib, "nvmlDeviceGetHandleByIndex_v2");
	nvUtil = (int(*)(void*, void*))dlsym(NvLib, "nvmlDeviceGetUtilizationRates");
	nvMem = (int(*)(void*, void*))dlsym(NvLib, "nvmlDeviceGetMemoryInfo");
	nvTemp = (int(*)(void*, int, unsigned*))dlsym(NvLib, "nvmlDeviceGetTemperature");
	nvClock = (int(*)(void*, int, unsigned*))dlsym(NvLib, "nvmlDeviceGetClockInfo");
	nvPower = (int(*)(void*, unsigned*))dlsym(NvLib, "nvmlDeviceGetPowerUsage");
	if (!Init || !Handle || Init() != 0 || Handle(0, &NvDev) != 0) { dlclose(NvLib); NvLib = nullptr; NvDev = nullptr; return false; }
	return true;
}

uint32 FSysMon::Run()
{
	InitNvml();
	while (!bStop)
	{
		Sample();
		for (int32 i = 0; i < 10 && !bStop; ++i) FPlatformProcess::Sleep(0.05f); // 2 Hz, responsive stop
	}
	return 0;
}

void FSysMon::Sample()
{
	FSysSnapshot S;
	{ FScopeLock L(&Lock); S.CpuHist = Snap.CpuHist; S.GpuHist = Snap.GpuHist; S.Samples = Snap.Samples; }
	// CPU total from the aggregate /proc/stat line.
	{
		TArray<FString> T; ReadSmall(TEXT("/proc/stat"), 256).Left(256).ParseIntoArrayWS(T);
		if (T.Num() >= 8 && T[0] == TEXT("cpu"))
		{
			uint64 V[10] = {}; for (int32 i = 1; i < FMath::Min(T.Num(), 11); ++i) V[i - 1] = FCString::Strtoui64(*T[i], nullptr, 10);
			const uint64 Idle = V[3] + V[4], Total = V[0] + V[1] + V[2] + V[3] + V[4] + V[5] + V[6] + V[7];
			if (PrevTotal && Total > PrevTotal) S.CpuUtil = 100.f * (1.f - float(Idle - PrevIdle) / float(Total - PrevTotal));
			PrevIdle = Idle; PrevTotal = Total;
		}
	}
	if (!CpuTempPath.IsEmpty()) { const double t = ReadNum(CpuTempPath); if (t > 0) S.CpuTempC = t / 1000.0; }
	if (FreqPaths.Num()) { double Sum = 0; int32 N = 0; for (const FString& P : FreqPaths) { const double f = ReadNum(P); if (f > 0) { Sum += f; ++N; } } if (N) S.CpuMHz = Sum / N / 1000.0; }
	{
		const FString M = ReadSmall(TEXT("/proc/meminfo"), 2048); double Tot = -1, Av = -1;
		TArray<FString> Lines; M.ParseIntoArrayLines(Lines);
		for (const FString& Ln : Lines) { TArray<FString> W; Ln.ParseIntoArrayWS(W); if (W.Num() < 2) continue;
			if (W[0] == TEXT("MemTotal:")) Tot = FCString::Atod(*W[1]); else if (W[0] == TEXT("MemAvailable:")) Av = FCString::Atod(*W[1]); }
		if (Tot > 0 && Av >= 0) { S.RamTotalGiB = Tot / 1048576.0; S.RamUsedGiB = (Tot - Av) / 1048576.0; }
	}
	if (NvDev)
	{
		S.GpuSource = TEXT("NVML");
		struct { unsigned Gpu, Mem; } U; struct { unsigned long long Total, Free, Used; } Mm; unsigned v = 0;
		if (nvUtil && nvUtil(NvDev, &U) == 0) S.GpuUtil = U.Gpu;
		if (nvMem && nvMem(NvDev, &Mm) == 0) { S.VramUsedGiB = Mm.Used / 1073741824.0; S.VramTotalGiB = Mm.Total / 1073741824.0; }
		if (nvTemp && nvTemp(NvDev, 0, &v) == 0) S.GpuTempC = v;
		if (nvClock && nvClock(NvDev, 0, &v) == 0) S.GpuMHz = v;
		if (nvPower && nvPower(NvDev, &v) == 0) S.GpuPowerW = v / 1000.0;
	}
	else if (!AmdDir.IsEmpty())
	{
		S.GpuSource = TEXT("AMDGPU");
		const FString Dev = AmdDir / TEXT("device");
		S.GpuUtil = ReadNum(Dev / TEXT("gpu_busy_percent"));
		const double U = ReadNum(Dev / TEXT("mem_info_vram_used")), T = ReadNum(Dev / TEXT("mem_info_vram_total"));
		if (U >= 0 && T > 0) { S.VramUsedGiB = U / 1073741824.0; S.VramTotalGiB = T / 1073741824.0; }
		const double t = ReadNum(AmdDir / TEXT("temp1_input")); if (t > 0) S.GpuTempC = t / 1000.0;
		const double f = ReadNum(AmdDir / TEXT("freq1_input")); if (f > 0) S.GpuMHz = f / 1e6;
		double p = ReadNum(AmdDir / TEXT("power1_average")); if (p < 0) p = ReadNum(AmdDir / TEXT("power1_input")); if (p > 0) S.GpuPowerW = p / 1e6;
	}
	auto Push = [](TArray<float>& H, float V) { H.Add(FMath::Clamp(V, 0.f, 100.f)); if (H.Num() > HistLen) H.RemoveAt(0); };
	if (S.CpuUtil >= 0) Push(S.CpuHist, S.CpuUtil);
	if (S.GpuUtil >= 0) Push(S.GpuHist, S.GpuUtil);
	++S.Samples;
	FScopeLock L(&Lock);
	Snap = MoveTemp(S);
	if (bRecording)
	{
		Stats.CpuUtil.Add(Snap.CpuUtil); Stats.CpuTemp.Add(Snap.CpuTempC); Stats.CpuMHz.Add(Snap.CpuMHz); Stats.Ram.Add(Snap.RamUsedGiB);
		Stats.GpuUtil.Add(Snap.GpuUtil); Stats.GpuTemp.Add(Snap.GpuTempC); Stats.GpuMHz.Add(Snap.GpuMHz); Stats.GpuPower.Add(Snap.GpuPowerW); Stats.Vram.Add(Snap.VramUsedGiB);
	}
}
