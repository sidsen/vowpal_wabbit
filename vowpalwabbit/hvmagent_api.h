#pragma once

#include <string>
#include <winnt.h>
#include <computedefs.h>

extern "C" HRESULT HVMAgent_Init(
    int BufferSize,
    int PrimaryCoreCnt,
    int LoopDelay);

extern "C" UINT32 HVMAgent_NumLPs();

extern "C" UINT64 HVMAgent_BusyMaskPrimary();

extern "C" UINT64 HVMAgent_BusyMaskRaw();

extern "C" UINT32 HVMAgent_GetIdleCoreCount();

extern "C" HRESULT HVMAgent_UpdateHVMCores(GUID hvmGuid, UINT32 numHVMCores);

extern "C" HRESULT HVMAgent_SpinUS(UINT64 delayUS);

extern "C" HRESULT HVMAgent_CreateCpuGroup(GUID *guid, UINT64 affinity);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromFront(GUID *guid, UINT32 numCores);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromBack(GUID *guid, UINT32 numCores);

extern "C" void HVMAgent_PrintGuid(GUID guid);

extern "C" UINT32 HVMAgent_GetMinRootLPs();
extern "C" UINT64 HVMAgent_GetMinRootMask();
extern "C" UINT32 HVMAgent_GetPhysicalCoreCount();
extern "C" UINT32 HVMAgent_GetLogicalCoreCount();
extern "C" int HVMAgent_IsHyperThreaded();

extern "C" UINT64 HVMAgent_GenerateCoreAffinityFromBack(UINT32 cores);
extern "C" UINT64 HVMAgent_GenerateCoreAffinityFromFront(UINT32 cores);

extern "C" void sprintf_guid(GUID guid, char* buf);

extern "C" HRESULT HVMAgent_GetVMHandle(const std::wstring vmname, HCS_SYSTEM *handle);
extern "C" HRESULT HVMAgent_AssignCPUGroupToVM(HCS_SYSTEM vmHandle, GUID cpuGroup);
extern "C" HRESULT HVMAgent_PinPrimary(const std::wstring vmname, UINT32 numCores);
extern "C" HRESULT HVMAgent_AssignCpuGroup(GUID guid);
extern "C" HRESULT HVMAgent_SetCpuGroupCap(__in const GUID &cpuGroupId, __in const UINT64 &cpuGroupCap);
