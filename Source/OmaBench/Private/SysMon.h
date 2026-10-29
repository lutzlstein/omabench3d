#pragma once
#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"

// btop-style system sampler. Runs on its own thread at ~2 Hz; the game thread only
// copies the latest snapshot under a short lock. Linux sources: /proc, sysfs/hwmon,
// NVML (dlopen'd libnvidia-ml.so.1) or amdgpu hwmon as a fallback. Missing values are -1.
struct FSysSnapshot
{
	float CpuUtil = -1, CpuTempC = -1, CpuMHz = -1;
	float RamUsedGiB = -1, RamTotalGiB = -1;
	float GpuUtil = -1, GpuTempC = -1, GpuMHz = -1, GpuPowerW = -1, VramUsedGiB = -1, VramTotalGiB = -1;
	FString GpuSource;
	TArray<float> CpuHist, GpuHist; // oldest -> newest, 0..100
	int32 Samples = 0;
};
struct FSysStat { double Sum = 0; float Peak = -1; int32 N = 0; void Add(float V) { if (V < 0) return; Sum += V; ++N; Peak = FMath::Max(Peak, V); } };
struct FSysStats { FSysStat CpuUtil, CpuTemp, CpuMHz, Ram, GpuUtil, GpuTemp, GpuMHz, GpuPower, Vram; };

class FSysMon : public FRunnable
{
public:
	FSysMon();
	virtual ~FSysMon() override;
	virtual uint32 Run() override;
	virtual void Stop() override { bStop = true; }
	FSysSnapshot Get() const { FScopeLock L(&Lock); return Snap; }
	FSysStats GetStats() const { FScopeLock L(&Lock); return Stats; }
	void SetRecording(bool b) { bRecording = b; }
	static constexpr int32 HistLen = 60;
private:
	void Sample();
	bool InitNvml();
	FRunnableThread* Thread = nullptr;
	FThreadSafeBool bStop = false, bRecording = false;
	mutable FCriticalSection Lock;
	FSysSnapshot Snap; FSysStats Stats;
	// sampler-thread-only state
	uint64 PrevIdle = 0, PrevTotal = 0;
	FString CpuTempPath, AmdDir;
	TArray<FString> FreqPaths;
	void* NvLib = nullptr; void* NvDev = nullptr;
	int (*nvUtil)(void*, void*) = nullptr; int (*nvMem)(void*, void*) = nullptr; int (*nvTemp)(void*, int, unsigned*) = nullptr;
	int (*nvClock)(void*, int, unsigned*) = nullptr; int (*nvPower)(void*, unsigned*) = nullptr;
};
