#pragma once

#include <string>
#include <winnt.h>
#include <computedefs.h>

#define CHECK_CALL(exp, msg) do { \
    int status = (exp); \
    if (FAILED(status)) { \
        printf("FATAL: " msg " (error code: %d)\n", status); \
        return status; \
    } \
} while(0);

typedef struct _CpuInfo
{
    UINT64 MinRootMask;
    UINT64 NonMinRootMask;

    UINT32 MinRootLPs;
    UINT32 MinRootCores;

    UINT32 NonMinRootLPs;
    UINT32 NonMinRootCores;

    UINT32 PhysicalCores;
    UINT32 LogicalCores;

    BOOLEAN IsHyperThreaded;

} CpuInfo, *PCpuInfo;

extern "C" void sprintf_guid(GUID guid, char* buf);

extern "C" HRESULT HVMAgent_Init(CpuInfo *cpuInfo);
extern "C" void HVMAgent_ResetStrings();

extern "C" UINT64 HVMAgent_GenerateCoreAffinityFromBack(UINT32 cores);
extern "C" UINT64 HVMAgent_GenerateCoreAffinityFromFront(UINT32 cores);

extern "C" UINT64 HVMAgent_BusyMaskRaw();
extern "C" HRESULT HVMAgent_SpinUS(UINT64 delayUS);

extern "C" HRESULT HVMAgent_UpdateHVMCores(GUID hvmGuid, UINT32 numHVMCores);

extern "C" HRESULT HVMAgent_CreateCpuGroup(GUID *guid, UINT64 affinity);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromFront(GUID *guid, UINT32 numCores);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromBack(GUID *guid, UINT32 numCores);

extern "C" HRESULT HVMAgent_GetVMHandle(const std::wstring vmname, HCS_SYSTEM *handle);
extern "C" HRESULT HVMAgent_PinPrimary(const std::wstring vmname, UINT32 numCores);

extern "C" HRESULT HVMAgent_AssignCpuGroup(GUID guid);
extern "C" HRESULT HVMAgent_AssignCPUGroupToVM(HCS_SYSTEM vmHandle, GUID cpuGroup);
extern "C" HRESULT HVMAgent_SetCpuGroupCap(__in const GUID &cpuGroupId, __in const UINT64 &cpuGroupCap);