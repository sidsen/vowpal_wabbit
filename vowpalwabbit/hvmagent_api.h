#pragma once

#include <string>
#include <iostream>
#include <sstream>
#include <vector>
#include <cguid.h>
#include <computedefs.h>
#include <winnt.h>

#define MAX_CORES 64

#define CHECK(exp) do { \
    BOOL status = (exp); \
    if (!status) { \
        printf("FATAL: %s:%d " #exp "\n", __FILE__, __LINE__); \
        return E_FAIL; \
    } \
} while(0);

#define CHECK_CALL(exp, msg) do { \
    int status = (exp); \
    if (FAILED(status)) { \
        printf("FATAL: " msg " (error code: %d)\n", status); \
        return status; \
    } \
} while(0);

#if 0
#define HV_MAXIMUM_PROCESSORS       128
#define CPU_SET_SHIFT 6
#define CPU_SET_MASK 63
#define CPU_SET_QWORD_COUNT (((HV_MAXIMUM_PROCESSORS - 1) >> CPU_SET_SHIFT) + 1)

typedef struct _CPU_SET
{
    UINT64 ProcessorSet[CPU_SET_QWORD_COUNT];
} CPU_SET;
#else
#define CPU_SET UINT64
#endif

typedef struct _CpuInfo
{
    CPU_SET MinRootMask;
    CPU_SET NonMinRootMask;

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
extern "C" HRESULT HVMAgent_InitWithMinRootMask(CpuInfo *cpuInfo, CPU_SET minRootMask);
extern "C" CpuInfo HVMAgent_GetCpuInfo();
extern "C" void HVMAgent_ResetStrings();

extern "C" CPU_SET HVMAgent_GenerateCoreAffinityFromBack(UINT32 cores);
extern "C" CPU_SET HVMAgent_GenerateCoreAffinityFromFront(UINT32 cores);

extern "C" CPU_SET HVMAgent_BusyMaskRaw();
extern "C" CPU_SET HVMAgent_BusyMaskCores();
extern "C" UINT32 HVMAgent_CoreCount(CPU_SET mask);
extern "C" HRESULT HVMAgent_SpinUS(UINT64 delayUS);

extern "C" HRESULT HVMAgent_UpdateHVMCores(GUID guid, UINT32 numHVMCores);
extern "C" HRESULT HVMAgent_UpdateHVMCoresUsingMask(GUID guid, CPU_SET affinityMask);

extern "C" HRESULT HVMAgent_CreateCpuGroup(GUID *guid, CPU_SET affinity);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromFront(GUID *guid, UINT32 numCores);
extern "C" HRESULT HVMAgent_CreateCpuGroupFromBack(GUID *guid, UINT32 numCores);

extern "C" HRESULT HVMAgent_GetVMHandle(const std::wstring vmname, HCS_SYSTEM *handle);
extern "C" HRESULT HVMAgent_PinPrimary(const std::wstring vmname, UINT32 numCores, GUID *guid);

extern "C" HRESULT HVMAgent_AssignCpuGroup(GUID guid);
extern "C" HRESULT HVMAgent_AssignCpuGroupToVM(HCS_SYSTEM vmHandle, GUID cpuGroup);
extern "C" HRESULT HVMAgent_SetCpuGroupCap(__in const GUID &cpuGroupId, __in const UINT64 &cpuGroupCap);
extern "C" HRESULT PrintCpuGroupInfo(GUID guid);

static std::vector<std::wstring> splitString(const std::wstring& str, WCHAR delim)
{
    std::vector<std::wstring> result;
    std::wstring temp;
    std::wstringstream wss(str);

    while (std::getline(wss, temp, delim))
    {
        if (!temp.empty())
        {
            result.push_back(temp);
        }
    }

    return result;
}

enum Mode
{
    INVALID,
    IPI,
    IPI_HOLES,
    CPUGROUPS
};

struct VMInfo
{
    HRESULT init(Mode _mode, std::wstring _vmName, INT32 _numCores, BOOLEAN _primary = FALSE, BOOLEAN _disjointCpuGroups = FALSE)
    {
        mode = _mode;
        curCores = _numCores;
        maxCores = minCores = curCores;
        primary = _primary;
        disjointCpuGroups = _disjointCpuGroups;

        cpuInfo = HVMAgent_GetCpuInfo();

        setupCpuGroups();

        std::vector<std::wstring> vmNames = splitString(_vmName, L',');

        for (const std::wstring &vmName : vmNames)
        {
            std::wcout << L"Initializing handle for: " << vmName << L"with _numcore:" << _numCores << std::endl;
            HCS_SYSTEM handle;
            CHECK_CALL(HVMAgent_GetVMHandle(vmName, &handle), "Failed to get VM handle");

            handles.push_back(handle);

            CHECK_CALL(HVMAgent_AssignCpuGroupToVM(handle, GUID_NULL), "Failed to unbind CpuGroup from VM");

            if (mode == CPUGROUPS)
            {
                CHECK_CALL(HVMAgent_AssignCpuGroupToVM(handle, groups[curCores]), "Failed to assign CpuGroup to VM");
            }
            else if (mode == IPI || mode == IPI_HOLES)
            {
                CHECK_CALL(HVMAgent_AssignCpuGroupToVM(handle, ipiGroup), "Failed to assign CpuGroup to VM");
            }
            else
            {
                CHECK_CALL(E_FAIL, "NOT REACHABLE");
            }
        }
        return S_OK;
    }

    HRESULT setupCpuGroups()
    {
        for (UINT32 i = 1; i <= cpuInfo.NonMinRootCores; i++)
        {
            if (primary)
            {
                masks[i] = HVMAgent_GenerateCoreAffinityFromFront(i);
            }
            else
            {
                masks[i] = HVMAgent_GenerateCoreAffinityFromBack(i);
            }
            std::wcout << L"Mask " << i << L": " << std::hex << masks[i] << L" " <<std::dec << masks[i] << std::endl;
        }

        curCoreMask = masks[curCores];

        if (mode == CPUGROUPS)
        {
            for (UINT32 i = 1; i <= cpuInfo.NonMinRootCores; i++)
            {
                CHECK_CALL(HVMAgent_CreateCpuGroup(&groups[i], masks[i]), "Failed to create CpuGroup");
            }
        }
        else if (mode == IPI || mode == IPI_HOLES)
        {
            CHECK_CALL(HVMAgent_CreateCpuGroup(&ipiGroup, masks[curCores]), "Failed to create CpuGroup for IPI");
        }

        return S_OK;
    }

    // assign new cpugroup to primary
    HRESULT updateCores(INT32 numCores)
    {
        curCores = numCores;
        curCoreMask = masks[curCores];

        if (primary && !disjointCpuGroups)
        {
            return S_OK;
        }

        if (mode == CPUGROUPS)
        {
            for (const HCS_SYSTEM &handle : handles)
            {
                CHECK_CALL(HVMAgent_AssignCpuGroupToVM(handle, GUID_NULL), "Failed to assign unbind VM from CpuGroup");
                CHECK_CALL(HVMAgent_AssignCpuGroupToVM(handle, groups[curCores]), "Failed to bind CpuGroup to VM");
            }
        }
        else if (mode == IPI)
        {
            CHECK_CALL(HVMAgent_UpdateHVMCoresUsingMask(ipiGroup, masks[numCores]), "Failed to update CpuGroup affinity");
        }
        else
        {
            CHECK(FALSE);
        }

        return S_OK;
    }

    CPU_SET coreIdToMask(UINT32 coreId)
    {
        if (cpuInfo.IsHyperThreaded)
        {
            return 0x3ULL << (coreId * 2);
        }

        return 0x1ULL << coreId;
    }

    HRESULT extractHvmCores(UINT32 numCores, CPU_SET *outputMask)
    {
        CHECK(mode == IPI_HOLES);
        CHECK(!primary);
        curCores -= numCores;
        CPU_SET result = 0;
        for (UINT32 coreId = 0; coreId < cpuInfo.PhysicalCores && numCores > 0; coreId++)
        {
            CPU_SET coreMask = coreIdToMask(coreId);
            if (curCoreMask & coreMask)
            {
                // printf("curCoreMask: %#016llx;  coreMask: %#016x11x \n", curCoreMask, coreMask);
                CHECK((curCoreMask & coreMask) == coreMask);
                result |= coreMask;
                numCores--;
            }
        }

        CHECK(numCores == 0);

        curCoreMask &= ~result;

        CHECK_CALL(HVMAgent_UpdateHVMCoresUsingMask(ipiGroup, curCoreMask), "");

        *outputMask = result;
        return S_OK;
    }

    HRESULT extractIdleCores(CPU_SET systemBusyMask, UINT32 numCores, CPU_SET *outputMask)
    {
        CHECK(mode == IPI_HOLES);
        CPU_SET result = 0;

        // printf("systemBusyMask: %#016llx;  coreId: %d; coreMask: %#016llx \n", systemBusyMask, numCores, curCoreMask);
        curCores -= numCores;

        CPU_SET idleMask = ~systemBusyMask & curCoreMask;
        for (INT32 coreId = cpuInfo.PhysicalCores - 1; coreId >= 0 && numCores > 0; coreId--)
        {
            CPU_SET coreMask = coreIdToMask(coreId);
            // The core belongs to the primary and is currently idle.
            if (coreMask & idleMask)
            {
                // printf("idleMask: %#016llx;  coreId: %d; coreMask: %#016llx result: %#016llx\n", idleMask, coreId, coreMask, result);
                CHECK((coreMask & idleMask) == coreMask);
                result |= coreMask;
                numCores--;
            }
        }

        curCoreMask &= ~result;

        *outputMask = result;
        CHECK(numCores == 0);
        return S_OK;
    }

    HRESULT addCores(UINT32 numCores, CPU_SET mask)
    {
        CHECK(mode == IPI_HOLES);
        curCores += numCores;
        curCoreMask |= mask;

        CHECK(numCores == HVMAgent_CoreCount(mask));

        if (!primary)
        {
            CHECK_CALL(HVMAgent_UpdateHVMCoresUsingMask(ipiGroup, curCoreMask), "");
        }
        return S_OK;
    }

    UINT64 busyCoreMask(CPU_SET systemBusyMask)
    {
        return systemBusyMask & curCoreMask;
    }

    UINT32 busyCores(CPU_SET systemBusyMask)
    {
        return HVMAgent_CoreCount(busyCoreMask(systemBusyMask));
    }

    UINT32 curCores;
    UINT32 minCores;
    UINT32 maxCores;
    CPU_SET curCoreMask;
    std::vector<HCS_SYSTEM> handles;

    Mode mode;
    GUID ipiGroup;
    GUID groups[MAX_CORES];
    CPU_SET masks[MAX_CORES]; // bit masks of cpugroup for a vm
    BOOLEAN primary;
    BOOLEAN disjointCpuGroups;
    CpuInfo cpuInfo;
};
