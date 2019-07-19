#pragma once

#include <string>
#include <winnt.h>

extern "C" HRESULT HVMAgent_Init(
    int BufferSize,
    int PrimaryCoreCnt,
    int LoopDelay);

extern "C" UINT32 HVMAgent_NumLPs();

extern "C" UINT64 HVMAgent_BusyMaskPrimary();

extern "C" UINT64 HVMAgent_BusyMaskRaw();

extern "C" UINT32 HVMAgent_GetIdleCoreCount();

extern "C" HRESULT HVMAgent_UpdateHVMCores(GUID hvmGuid, UINT32 numHVMCores);

extern "C" HRESULT HVMAgent_SpinUS(int delayUS);

extern "C" HRESULT HVMAgent_CreateCpuGroup(GUID *guid, UINT32 numCores);

extern "C" HRESULT HVMAgent_AssignCpuGroup(GUID guid);

extern "C" void HVMAgent_PrintGuid(GUID guid);

extern "C" UINT32 HVMAgent_GetMinRootLPs();
extern "C" UINT64 HVMAgent_GetMinRootMask();
extern "C" UINT32 HVMAgent_GetPhysicalCoreCount();
extern "C" UINT32 HVMAgent_GetLogicalCoreCount();
extern "C" int HVMAgent_IsHyperThreaded();

extern "C" UINT64 HVMAgent_GenerateMaskFromBack(UINT32 cores);