/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD
license as described in the file LICENSE.
 */
#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <sys/timeb.h>
#include "parse_args.h"
#include "parse_regressor.h"
#include "accumulate.h"
#include "best_constant.h"
#include "vw_exception.h"
#include <fstream>

#include "options.h"
#include "options_boost_po.h"

#include <iostream>
#include <stdio.h>
#include <windows.h>
#include <winternl.h>
#include <assert.h>

#include <cstring>
#include <string>

#include "hvmagent_api.h"

#include <bitset>
#include "vw.h"

#include <math.h>
#include <cguid.h>
#include <chrono>
#include <algorithm>

#include "cyclecounter.h"

using namespace std::chrono;
using namespace std;

#define ASSERT(exp)                                          \
  do                                                         \
  {                                                          \
    bool status = (exp);                                     \
    if (!status)                                             \
    {                                                        \
      printf("%s:%d FATAL: %s\n", __FILE__, __LINE__, #exp); \
      exit(1);                                               \
    }                                                        \
  } while (0);

using namespace std;
using namespace VW::config;

UINT32 bitcount(UINT64 mask)
{
  UINT32 count = 0;
  while (mask)
  {
    mask &= (mask - 1);
    count++;
  }
  return count;
}

#define MAX_MASKS 1024
UINT64 hvmMasks[MAX_MASKS] = {0};

//#define TOTAL_PRIMARY_CORE 6
#define MAX_HVM_CORES 24//12
#define MIN_HVM_CORES 1//6


CpuInfo cpuInfo;
std::unordered_map<UINT32, GUID> cpuGroups;

UINT64 primaryMask = 0;
UINT64 hvmMask = 0;
UINT64 idleCoreMask = 0;
UINT64 idleCoreCount = 0;
GUID hvmGuid = GUID_NULL;
GUID primaryGuid = GUID_NULL;
UINT64 busyMaskPrimary = 0;
UINT64 numCoresBusyPrimary = 0;
int safeguard = 0;

enum Mode
{
  INVALID,
  IPI,
  NEWIPI,
  CPUGROUPS
};


int maxHvmCores = MAX_HVM_CORES;  // 6+6 (# of non-min-root cores)
int minHvmCores = MIN_HVM_CORES;
int numHvmCores = minHvmCores;  // # cores given to HVM
INT32 prevHvmCores = numHvmCores;
int totalPrimaryCores = 0;
Mode mode;
std::wstring primaryName = L"LatSensitive";


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

const WCHAR ARG_CSV[] = L"--csv";
const WCHAR ARG_DURATION[] = L"--duration_sec";
const WCHAR ARG_BUFFER[] = L"--buffer";
const WCHAR ARG_DELAY_MS[] = L"--delay_ms";
const WCHAR ARG_LEARNING_MODE[] = L"--learning_mode";
const WCHAR ARG_LEARNING_MS[] = L"--learning_ms";
const WCHAR ARG_TIMING[] = L"--timing";
const WCHAR ARG_DEBUG[] = L"--debug";
const WCHAR ARG_NO_HARVESTING[] = L"--no_harvesting";
const WCHAR ARG_PRIMARY_ALONE[] = L"--primary_alone";
const WCHAR ARG_FEEDBACK[] = L"--feedback";
const WCHAR ARG_FEEDBACK_MS[] = L"--feedback_ms";
const WCHAR ARG_SLEEP_MS[] = L"--sleep_ms";
const WCHAR ARG_MODE[] = L"--mode";
const WCHAR ARG_PRIMARY_SIZE[] = L"--primary_size";
const WCHAR ARG_DROP_BAD_FEATURES[] = L"--drop_bad_features";
const WCHAR ARG_READ_CPU_SLEEP_US[] = L"--read_cpu_sleep_us";

std::wstring output_csv = L"";
int RUN_DURATION_SEC = 0;
int bufferSize = -1;
int FIXED_BUFFER_MODE = 0;
int DELAY_MS = 0;
int LEARNING_MODE = 0;
// 1: fixed rate learning without safeguard
// 2: fixed rate learning with safeguard
// 3: moving rate learning
int LEARNING_MS = 0;  // prediction window in ms
int TIMING = 0;       // measure vw timing
int DEBUG = 0;        // print debug messages
int NO_HARVESTING = 0;
int PRIMARY_ALONE = 0;

int FEEDBACK_MS = 0;
int FEEDBACK = 0;

int SLEEP_MS = 0;
int SLEEP_US = 0;
std::wstring MODE = L"";
int dropBadFeatures = 0;
int read_cpu_sleep_us = 0;

#define shift argc--, argv++

void __cdecl process_args(int argc, __in_ecount(argc) WCHAR* argv[])
{
  shift;
  while (argc > 0)
  {
    if (0 == ::_wcsnicmp(argv[0], ARG_CSV, ARRAY_SIZE(ARG_CSV)))
    {
      output_csv = argv[1];
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DURATION, ARRAY_SIZE(ARG_DURATION)))
    {
      RUN_DURATION_SEC = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_BUFFER, ARRAY_SIZE(ARG_BUFFER)))
    {
      bufferSize = _wtoi(argv[1]);
      FIXED_BUFFER_MODE = 1;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DELAY_MS, ARRAY_SIZE(ARG_DELAY_MS)))
    {
      DELAY_MS = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MODE, ARRAY_SIZE(ARG_LEARNING_MODE)))
    {
      LEARNING_MODE = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MS, ARRAY_SIZE(ARG_LEARNING_MS)))
    {
      LEARNING_MS = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_TIMING, ARRAY_SIZE(ARG_TIMING)))
    {
      TIMING = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DEBUG, ARRAY_SIZE(ARG_DEBUG)))
    {
      DEBUG = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_NO_HARVESTING, ARRAY_SIZE(ARG_NO_HARVESTING)))
    {
      NO_HARVESTING = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_ALONE, ARRAY_SIZE(ARG_PRIMARY_ALONE)))
    {
      PRIMARY_ALONE = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_FEEDBACK, ARRAY_SIZE(ARG_FEEDBACK)))
    {
      FEEDBACK = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_FEEDBACK_MS, ARRAY_SIZE(ARG_FEEDBACK_MS)))
    {
      FEEDBACK_MS = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_SLEEP_MS, ARRAY_SIZE(ARG_SLEEP_MS)))
    {
      SLEEP_MS = _wtoi(argv[1]);
      SLEEP_US = SLEEP_MS * 1000;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_MODE, ARRAY_SIZE(ARG_MODE)))
    {
      MODE = argv[1];
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_SIZE, ARRAY_SIZE(ARG_PRIMARY_SIZE)))
    {
      totalPrimaryCores = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DROP_BAD_FEATURES, ARRAY_SIZE(ARG_DROP_BAD_FEATURES)))
    {
      dropBadFeatures = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_READ_CPU_SLEEP_US, ARRAY_SIZE(ARG_READ_CPU_SLEEP_US)))
    {
      read_cpu_sleep_us = _wtoi(argv[1]);
    }
    else
    {
      wprintf(L"Unknown argument: %s\n", argv[0]);
      cout << argv[0] << endl;
      // print_help();
      exit(1);
    }

    shift;
    shift;
  }

  if (LEARNING_MODE != 0)
    FIXED_BUFFER_MODE = 0;

  wcout << L"Parameters:" << L" run_duration_sec" << RUN_DURATION_SEC << L" sleep_ms:" << SLEEP_MS << L" buffer size:"
        << bufferSize << L" fixed buffer:" << FIXED_BUFFER_MODE << L" delay_ms:" << DELAY_MS << L" csv:" << output_csv
        << L" learning_mode:" << LEARNING_MODE << L" learning_ms:" << LEARNING_MS << L" debug:" << DEBUG << L" timing:"
        << TIMING << L" no_harvesting: " << NO_HARVESTING << L" primary_alone:" << PRIMARY_ALONE << L" feedback:"
        << FEEDBACK << L" feedback_ms:" << FEEDBACK_MS << L" mode:" << MODE << L" totalPrimaryCores:"
        << totalPrimaryCores << L" dropBadFeatures:" << dropBadFeatures << L" read_cpu_sleep_us:" << read_cpu_sleep_us << std::endl;

  wcout << "MAX_HVM_CORES " << MAX_HVM_CORES << std::endl;
  wcout << "MIN_HVM_CORES" << MIN_HVM_CORES << std::endl;

}

/************************/
// helper functions
/************************/

// pred is overpredicted if pred>busyCores or pred=totalPrimaryCores
int overpredcited(int busyCores, int pred, int totalPrimaryCores)
{
  if (pred == totalPrimaryCores || pred > busyCores)
    return 1;
  else
    return 0;
}


void setupCpuGroups(Mode mode, INT32 numHvmCores, INT32 maxHvmCores, GUID* hvmGuid)
{
  if (mode == CPUGROUPS)
  {
    for (INT32 i = 1; i <= maxHvmCores; i++)
    {
      GUID guid = GUID_NULL;
      ASSERT(HVMAgent_CreateCpuGroupFromBack(&guid, i) == S_OK);
      cpuGroups[i] = guid;

      char buf[128];
      sprintf_guid(guid, buf);

      wcout << L"Created CPU group for " << i << " cores: " << buf << endl;
    }

    *hvmGuid = cpuGroups[numHvmCores];
  }
  else if (mode == IPI || mode == NEWIPI)
  {
    ASSERT(SUCCEEDED(HVMAgent_CreateCpuGroupFromBack(hvmGuid, numHvmCores)));
    char buf[128];
    sprintf_guid(*hvmGuid, buf);
    wcout << L"Created CPU group for " << numHvmCores << " cores: " << buf << endl;
  }

  // CLSIDFromString(L"9123CF97-AD5D-46F2-BFA3-F36950100051", (LPCLSID)&hvmGuid);
  ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroup(*hvmGuid)));
  std::cout << "CPUgroup for HVM created" << endl;

  // ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
  // std::cout << "CPUgroup for HVM successfully updated" << endl;

  printf("INFO: CONFIG: created new CPU group for HarvestVM with %d minimum cores\n", numHvmCores);
  printf("INFO: starting main loop\n");
  fflush(stdout);
}

void update_hvm(Mode mode, INT32 numCores)
{
  if (mode == CPUGROUPS)
  {
    ASSERT(HVMAgent_AssignCpuGroup(cpuGroups[numCores]) == S_OK);
  }
  else if (mode == IPI)
  {
    ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numCores)));
  }
  else if (mode == NEWIPI)
  {
    if (numCores < prevHvmCores)
    {
      INT32 delta = prevHvmCores - numCores;
      // Shrinking Hvm, growing primary
      // Shrink Hvm from front:
      for (INT32 i = 0; i < (INT32)cpuInfo.PhysicalCores && delta > 0; i++)
      {
        UINT64 coreMask = 1LL << i;
        if (hvmMask & coreMask)
        {
          hvmMask &= ~coreMask;
          primaryMask |= coreMask;
          delta--;
        }
      }
    }
    else
    {
      INT32 delta = numCores - prevHvmCores;
      // Growing Hvm, shrinking primary
      // Grow Hvm by picking cores from the idle mask;
      UINT64 andMask = idleCoreMask & primaryMask;
      if (!andMask)
      {
        printf(
            "WARNING: update_hvm; safeguard=%d, idleCoreMask=0x%x, primaryMask=0x%x, idleCoreMask&primaryMask=0x%x\n",
            safeguard, idleCoreMask, primaryMask, andMask);
      }
      ASSERT(andMask);

      for (INT32 i = (INT32)cpuInfo.PhysicalCores - 1; i >= 0 && delta > 0; i--)
      {
        UINT64 coreMask = 1LL << i;
        if (idleCoreMask & coreMask)
        {
          hvmMask |= coreMask;
          primaryMask &= ~coreMask;
          delta--;
        }
      }
    }
    //cout << "hvmMask: " << hvmMask << " primaryMask: " << primaryMask << endl;
    ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCoresUsingMask(hvmGuid, hvmMask)));
    ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCoresUsingMask(primaryGuid, primaryMask)));
  }
  else 
  {
    ASSERT(FALSE);
  }
}

void resetCpuGroups(double elapsedSeconds)
{
  HVMAgent_ResetStrings();
  ASSERT(SUCCEEDED(HVMAgent_CreateCpuGroupFromBack(&hvmGuid, minHvmCores)));
  ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroup(hvmGuid)));

  numHvmCores = prevHvmCores = minHvmCores;
  hvmMask = hvmMasks[minHvmCores];

  primaryMask = HVMAgent_GenerateCoreAffinityFromFront(totalPrimaryCores);

  ASSERT(SUCCEEDED(HVMAgent_PinPrimary(primaryName, totalPrimaryCores, &primaryGuid)));

  char buf[128];
  sprintf_guid(hvmGuid, buf);
  printf("%.4lf: WARNING: Potential IPI failure; created new CpuGroup with %d cores: %s\n", elapsedSeconds, numHvmCores,
      buf);
}

void updateIdleMask(INT32 curHvmCores)
{
    // Mark Minroot cores are busy.
    UINT64 busyCoreMask = HVMAgent_BusyMaskRaw() | cpuInfo.MinRootMask;

    // Mark HVM Cores are busy.
    if (mode == NEWIPI)
    {
        busyCoreMask |= hvmMask;
    }
    else
    {
        busyCoreMask |= hvmMasks[curHvmCores];
    }

    idleCoreMask = primaryMask & ~busyCoreMask;
    idleCoreCount = bitcount(idleCoreMask);

    if (cpuInfo.IsHyperThreaded)
    {
        idleCoreCount /= 2;
    }

    busyMaskPrimary = primaryMask & ~idleCoreMask;
    numCoresBusyPrimary = bitcount(busyMaskPrimary);

    //if (idleCoreCount == 0)
    //  printf("Idle cores: %d, idle mask: %#06x, busyCoreMask: %#06x, busyMaskPrimary: %#06x, numCoresBusyPrimary: %d \n", idleCoreCount, idleCoreMask, busyCoreMask, busyMaskPrimary, numCoresBusyPrimary);
}

    //idleCoreCount = cpuInfo.PhysicalCores - busyCount;

UINT32 getNewHvmCores(INT32 curHvmCores) //, INT32 *idleCoreCount)
{
    updateIdleMask(curHvmCores);

    INT32 newHvmCores = curHvmCores + idleCoreCount - bufferSize;

    newHvmCores = min(newHvmCores, maxHvmCores); // curHvmCores + 1);
    newHvmCores = max(newHvmCores, minHvmCores);

    return newHvmCores;
}
/*
  optional 1st arg = buffer size
*/
// int main(int argc, char* argv[])
int __cdecl wmain(int argc, __in_ecount(argc) WCHAR* argv[])
{
  process_args(argc, argv);
  int sleep_us = SLEEP_MS * 1000;

  /************************/
  // set up logging
  /************************/
  // std::wstring output_csv = L"hvmagent.csv";
  FILE* output_fp = nullptr;
  if (!output_csv.empty())
  {
    output_fp = _wfopen(output_csv.c_str(), L"w+");
    ASSERT(output_fp != NULL);
    if (FIXED_BUFFER_MODE || NO_HARVESTING)
      fprintf(output_fp,
          "iteration,time_sec,idle_cores,hvm_cores,hvm_mask,hypercall_time_us,primary_busy_cores,cpu_max,"
          "potential_ipi_failure\n");
    else
      fprintf(output_fp,
          "iteration,time_sec,idle_cores,hvm_cores,hvm_mask,hypercall_time_us,primary_busy_cores,f_min,f_max,"
          "f_avg,f_stddev,f_med,pred_peak,upper_bound,cpu_max,overpredicted,safeguard,feedback_max, potential_ipi_failure\n");

    fflush(output_fp);
  }

  /************************/
  // set up latency measurements
  /************************/
  auto start = high_resolution_clock::now();
  auto stop = high_resolution_clock::now();
  auto learn_start = high_resolution_clock::now();
  auto learn_stop = high_resolution_clock::now();
  auto feedback_start = high_resolution_clock::now();
  auto feedback_stop = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(stop - start);
  auto us_elapsed = duration_cast<microseconds>(stop - start);
  // auto time_data_collection, time_feature_computation, time_model_udpate, time_model_inference, time_cpugroup_update;

  /************************/
  // HVMagent
  /************************/
  UINT64 rawBusyMask, busyMask, nonIdleMask;
  int busyMaskCount, busyMaskAllCount;
  int numCoresBusy;
  int numCoresBusyAll;

  // initialize hvmagent
  ASSERT(SUCCEEDED(HVMAgent_Init(&cpuInfo)));

  std::cout << "HVMAgent initialized" << endl;

  int delay_us;
  delay_us = DELAY_MS * 1000;

  // read server CPU info from HVMagent
  int totalCores = cpuInfo.PhysicalCores;      // # physical cores on the whole server (min root included)
  UINT32 minRootCores = cpuInfo.MinRootCores;  // # cores for min root
  UINT64 minRootMask = cpuInfo.MinRootMask;    // 0000001111111100000011111111 (16 cores for minroot)

  std::cout << "totalCores " << totalCores << endl;
  std::cout << "minRootCores " << minRootCores << endl;
  std::cout << "totalPrimaryCores " << totalPrimaryCores << endl;

  // create hvm masks
  // assign initial cores to vms

  int numPrimaryCores = totalPrimaryCores;  // totalPrimaryCores - numHvmCores =  # cores given to primary
  int busyPrimaryCores;

  // pin primary vm
  primaryMask = HVMAgent_GenerateCoreAffinityFromFront(totalPrimaryCores);
  std::cout << "bufferSize " << bufferSize << endl;
  std::cout << "totalPrimaryCores " << totalPrimaryCores << endl;
  if (bufferSize == totalPrimaryCores)
  {
    std::cout << "Dont pinned primary vm to " << totalPrimaryCores << "cores" << endl;
  }
  else
  {
    ASSERT(SUCCEEDED(HVMAgent_PinPrimary(primaryName, totalPrimaryCores, &primaryGuid)));
    std::cout << "Pinned primary vm to " << totalPrimaryCores << "cores" << endl;
  }
    

  for (UINT32 i = 1; i <= cpuInfo.PhysicalCores; i++)
  {
    hvmMasks[i] = HVMAgent_GenerateCoreAffinityFromBack(i);  // 1111110000000011000000000000 (8cores for hvm)
    printf("Created bully mask with %d cores: %x\n", i, hvmMasks[i]);
  }
  hvmMask = hvmMasks[numHvmCores];

  if (MODE == L"IPI")
    mode = IPI;
  else if (MODE == L"CPUGROUPS")
    mode = CPUGROUPS;
  else if (MODE == L"NEWIPI")
    mode = NEWIPI;
  else
  {
    cout << "Invalid mode" << endl;
    exit(1);
  }

  setupCpuGroups(mode, numHvmCores, maxHvmCores, &hvmGuid);

  /************************/
  // VW learning agent
  /************************/
  // initilize vw features
  int size;                // 10 * 1000 / 50 = 200 readings per 10ms
  vector<int> cpu_busy_a;  // num of busy cpu cores of primary vm --> reading every <1us
  vector<int> cpu_busy_b;
  vector<int> time_b;
  vector<int> feature_compute_us;
  vector<int> model_update_us;
  vector<int> model_inference_us;
  vector<int> read_primary_cpu_us;
  vector<int> log_primary_cpu_us;


  int invoke_learning = 0;
  int first_window = 1;
  int learning_us = LEARNING_MS * 1000;
  int feedback_us = FEEDBACK_MS * 1000;

  int count = 0;
  int sum = 0;
  int max = 0;
  int feedback_max = 0;
  int min = totalCores;
  int med = 0;
  float stddev = 0;
  float avg = 0;
  string feature;

  int pred = 0;
  int cost;
  int overpredicted = 0;
  //int safeguard = 0;
  int prevSafeguard = 0;

  // initialize vw
  auto vw = VW::initialize("--csoaa " + to_string(totalPrimaryCores) + " --power_t 0 -l 0.1");
  std::cout << "vw initialized with " << totalPrimaryCores << " classes." << endl;

  string vwLabel, vwFeature, vwMsg;
  example* ex;
  example* ex_pred;

  /************************/
  // main loop
  /************************/
  int debug_count = 0;
  auto start_1min = high_resolution_clock::now();

  CycleCounter timer;
  timer.Start();

  int ipiTimeoutMS = 100;
  int ipiFailCount = 0;
  double prevIpiTimestampMS = timer.ElapsedMS();

  while (timer.ElapsedSeconds() < RUN_DURATION_SEC)
  {
    debug_count++;
    if (DEBUG && debug_count > 6)
      break;

    if (NO_HARVESTING)
    {
      ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
      start = high_resolution_clock::now();
      max = 0;  // reset max
      INT32 newHvmCores = 0;

      while (true)
      {
        newHvmCores = getNewHvmCores(numHvmCores);
        HVMAgent_SpinUS(read_cpu_sleep_us);

        if (numCoresBusyPrimary > max)
          max = numCoresBusyPrimary;

        stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(stop - start);
        if (us_elapsed.count() >= learning_us)
          break;  // log max from every 2ms
      }

      //numHvmCores = newHvmCores;

      if (output_fp)
      {
        double time = timer.ElapsedUS() / 1000000.0;
        fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d\n", count, time, idleCoreCount, numHvmCores,
            hvmMasks[numHvmCores], 0, numCoresBusyPrimary, max, ipiFailCount);
      }
    }

    else if (FIXED_BUFFER_MODE)
    {
      start = high_resolution_clock::now();
      max = 0;  // reset max
      INT32 newHvmCores = 0;

      while (true)
      {
        newHvmCores = getNewHvmCores(numHvmCores);

        if (numCoresBusyPrimary > max)
          max = numCoresBusyPrimary;

        stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(stop - start);
        if (us_elapsed.count() >= delay_us)
          break;  // log max from delay_us

      }

      numHvmCores = newHvmCores;

      //numHvmCores = numHvmCores + idleCoreCount - bufferSize;
      numHvmCores = std::min(numHvmCores, maxHvmCores);  // curHvmCores + 1);
      numHvmCores = std::max(numHvmCores, minHvmCores);

      if (numHvmCores == prevHvmCores && (mode == IPI || mode == NEWIPI)  &&
          timer.ElapsedMS() > prevIpiTimestampMS + ipiTimeoutMS)
      {
        if (bufferSize != totalPrimaryCores) //don't invoke for cpubully alone
          resetCpuGroups(timer.ElapsedSeconds()); 

        prevIpiTimestampMS = timer.ElapsedMS();
        ipiFailCount++;
        sleep_us += 10000;  // 10ms for CPUGROUP_TIMEOUT;
      }
      else if (prevHvmCores != numHvmCores)
      {
        update_hvm(mode, numHvmCores);
        count++;
        prevHvmCores = numHvmCores;

        prevIpiTimestampMS = timer.ElapsedMS();
      }

      if (output_fp)
      {
        double time = timer.ElapsedUS() / 1000000.0;
        fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d\n", count, time, idleCoreCount, numHvmCores,
            hvmMasks[numHvmCores], 0, numCoresBusyPrimary, max, ipiFailCount);
      }

      HVMAgent_SpinUS(sleep_us);  // sleep for 1ms for cpu affinity call (if issued) to take effects
      sleep_us = SLEEP_US;
    }

    else
    {
      learn_start = high_resolution_clock::now();
      feedback_start = high_resolution_clock::now();

      // resetting parameters
      overpredicted = 1;
      sum = 0;
      size = 0;
      // count = 0;
      max = 0;
      min = totalCores;
      cpu_busy_a.clear();
      invoke_learning = 0;

      /* collect cpu data for vw learning window */
      while (true)
      {
        if (TIMING)
          start = high_resolution_clock::now();

        if (mode == NEWIPI)
          updateIdleMask(numHvmCores);
        else
        {
          rawBusyMask = HVMAgent_BusyMaskRaw();
          busyMaskPrimary = rawBusyMask & primaryMask;

          // std::bitset<28> busyMaskAllBit(busyMaskPrimary);
          numCoresBusyPrimary = bitcount(busyMaskPrimary);

          // nonIdleMask = rawBusyMask | minRootMask | hvmMasks[numHvmCores];
          // idleCoreCount = totalCores - bitcount(nonIdleMask);
        }
        // HVMAgent_SpinUS(read_cpu_sleep_us);

        if (TIMING)
        {
          stop = high_resolution_clock::now();
          duration = duration_cast<microseconds>(stop - start);
          read_primary_cpu_us.push_back(duration.count());
          start = high_resolution_clock::now();
        }
        

        // record cpu reading
        cpu_busy_a.push_back(numCoresBusyPrimary);
        sum += numCoresBusyPrimary;
        if (numCoresBusyPrimary < min)
          min = numCoresBusyPrimary;
        if (numCoresBusyPrimary > max)
          max = numCoresBusyPrimary;
        if (numCoresBusyPrimary > feedback_max)
          feedback_max = numCoresBusyPrimary;

        // check for underprediction (buffer empty)
        if (LEARNING_MODE == 3 || LEARNING_MODE == 4)
        {
          if (numCoresBusyPrimary == numPrimaryCores && numPrimaryCores != totalPrimaryCores)
          {
            // buffer runs out (underprediction) --> trigger immediate safeguard
            overpredicted = 0;
            safeguard = 1;
            if (LEARNING_MODE == 4)
              break;  // stop data collection immediately for mode 4

            // give all cores back to primary for mode3
            numHvmCores = minHvmCores;            // min guaranteed # cores for HVM
            numPrimaryCores = totalPrimaryCores;  // totalPrimaryCores - numHvmCores;  // max # cores given to primary

            if (prevHvmCores != numHvmCores)
            {
              //cout << "safeguard1" << endl; 
              update_hvm(mode, numHvmCores);
              HVMAgent_SpinUS(sleep_us);
              count++;
              prevHvmCores = numHvmCores;
              if (output_fp)
              {
                double time = timer.ElapsedUS() / 1000000.0;
                fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time,
                    idleCoreCount, numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev,
                    med, pred, numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
              }
            }
            else
            {
              if (output_fp)
              {
                double time = timer.ElapsedUS() / 1000000.0;
                fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time,
                    idleCoreCount, numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev,
                    med, pred, numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
              }
            }
          }
        }

        // feedback loop
        if (FEEDBACK)
        {
          feedback_stop = high_resolution_clock::now();
          us_elapsed = duration_cast<microseconds>(feedback_stop - feedback_start);
          if (us_elapsed.count() >= feedback_us)
          {  // trigger feedback
            if (feedback_max == numPrimaryCores && numPrimaryCores != totalPrimaryCores)
            {
              // underpred: increase cores for primary
              overpredicted = 0;
              numPrimaryCores = numPrimaryCores * 2;
            }
            else
            {
              overpredicted = 1;
              numPrimaryCores--;
            }
            // adjust primary size
            numPrimaryCores = std::min(numPrimaryCores, totalPrimaryCores);
            numPrimaryCores = std::max(numPrimaryCores, pred);
            numHvmCores = std::max(minHvmCores, maxHvmCores - numPrimaryCores);  // #cores to give to HVM
            // update hvm size
            if (prevHvmCores != numHvmCores)
            {
              update_hvm(mode, numHvmCores);
              HVMAgent_SpinUS(sleep_us);
              count++;
              prevHvmCores = numHvmCores;
              if (output_fp)
              {
                double time = timer.ElapsedUS() / 1000000.0;
                fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time,
                    idleCoreCount, numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev,
                    med, pred, numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
              }
            }
            else
            {
              if (output_fp)
              {
                double time = timer.ElapsedUS() / 1000000.0;
                fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time,
                    idleCoreCount, numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev,
                    med, pred, numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
              }
            }

            // resetting for the smaller feedback window
            feedback_max = 0;
            feedback_start = high_resolution_clock::now();
          }
        }

        if (TIMING)
        {
          stop = high_resolution_clock::now();
          duration = duration_cast<microseconds>(stop - start);
          log_primary_cpu_us.push_back(duration.count());
          start = high_resolution_clock::now();
        }
        

        learn_stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(learn_stop - learn_start);
        if (us_elapsed.count() >= learning_us)
          break;  // a learning window past
      }

      /****** completed data collection window ******/

      /****** compute features ******/
      start = high_resolution_clock::now();
      size = cpu_busy_a.size();
      avg = 1.0 * sum / size;
      // compute stddev
      float tmp_sum = 0;
      for (int i = 0; i < size; i++)
      {
        tmp_sum += (cpu_busy_a[i] - avg) * (cpu_busy_a[i] - avg);
      }
      tmp_sum = 1.0 * tmp_sum / size;
      stddev = sqrt(tmp_sum);
      // compute median
      std::sort(cpu_busy_a.begin(), cpu_busy_a.end());
      if (size % 2 != 0)
        med = cpu_busy_a[size / 2];
      else
        med = (cpu_busy_a[(size - 1) / 2] + cpu_busy_a[size / 2]) / 2.0;

      if (TIMING)
      {
        stop = high_resolution_clock::now();
        duration = duration_cast<microseconds>(stop - start);
        // std::cout << duration.count() << endl;
        // time_feature_computation = duration_cast<microseconds>(stop - start);
        // std::cout << "time_feature_computation (us) " << duration.count() << endl;
        feature_compute_us.push_back(duration.count());
      }

      /****** compute CPU affinity ******/
      if (first_window)
      {
        // first iteration gives all cores to primary VM
        if (DEBUG)
          std::cout << "Using vw learning" << endl;
        first_window = 0;
        numHvmCores = minHvmCores;  // min guaranteed # cores for HVM
        numPrimaryCores = totalPrimaryCores;

        /* construct features for model update */
        vwFeature = "|busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +  " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
      }
      else
      {
        start = high_resolution_clock::now();

        if (LEARNING_MODE == 1 || LEARNING_MODE == 2)
        {  // mode 1&2 need to decide overprediction here
          if (max < numPrimaryCores || numPrimaryCores == totalPrimaryCores)
            overpredicted = 1;
          else
            overpredicted = 0;
        }

        if (LEARNING_MODE == 1)
        {
          // mode 1 always relies on predictions
          invoke_learning = 1;
          safeguard = 0;
        }
        else
        {
          // mode 2&3&4 can trigger safeguard
          if (overpredicted)
          {
            invoke_learning = 1;
            safeguard = 0;
          }
          else
          {
            invoke_learning = 0;
            safeguard = 1;
          }
        }

        if (DEBUG)
          cout << "invoke_learning: " << invoke_learning << " safeguard:" << safeguard << endl;

        if (invoke_learning)
        {  // use model prediction for next window only if overpredicted from the previous window
          //if (dropBadFeatures && prevSafeguard)
          //{
            // don't update the model
            //int update_model = 0;
          //}
          if (!dropBadFeatures || !prevSafeguard) {
            /* create cost label */
            vwLabel.clear();
            for (int k = 1; k < totalPrimaryCores + 1; k++)
            {
              if (k < max)
                cost = max - k + totalPrimaryCores;  // underprediction (worse --> higher cost)
              else
                cost = k - max;  // prefect- & over-prediction
              vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
            }

            /* create vw training data */
            // vwLabel = "1:3 2:0 3:1";
            vwMsg = vwLabel + vwFeature;  // vwLabel from current window, features generated from previous window
            ex = VW::read_example(*vw, vwMsg.c_str());  // update vw model with features from previous window
            if (DEBUG)
              std::cout << "vwMsg to update model: " << vwMsg.c_str() << endl;
            // example* ex = VW::read_example(*vw, "1:3 2:0 3:-1 |busy_cores_prev_interval min:0 max:0 avg:0 stddev:0
            // med:0");

            /* udpate vw model */
            vw->learn(*ex);
            VW::finish_example(*vw, *ex);
          }
          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            // std::cout << duration.count() << endl;
            // time_model_udpate = duration_cast<microseconds>(stop - start);
            // std::cout << "time_model_udpate (us) " << duration.count() << endl;
            model_update_us.push_back(duration.count());
            start = high_resolution_clock::now();
          }

          /* construct features for prediction */
          vwFeature = "|busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +
              " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
          if (DEBUG)
            std::cout << "vwFeature: " << vwFeature.c_str() << endl;
          ex_pred = VW::read_example(*vw, vwFeature.c_str());

          /* get prediction --> # cores to primary VM */
          vw->predict(*ex_pred);
          pred = VW::get_cost_sensitive_prediction(ex_pred);
          if (DEBUG)
            std::cout << "pred = " << pred << endl;
          VW::finish_example(*vw, *ex_pred);

          if (mode == NEWIPI)
          {
            int numPrimaryCoresPrev = numPrimaryCores;
            updateIdleMask(numHvmCores);
            if (!(idleCoreMask & primaryMask))
              printf( "WARNING in learning: update_hvm; safeguard=%d, idleCoreMask=0x%x, primaryMask=0x%x, idleCoreMask&primaryMask=0x%x, numCoresBusyPrimary=%d\n", safeguard, idleCoreMask, primaryMask, idleCoreMask & primaryMask, numCoresBusyPrimary);
            numPrimaryCores = std::min(std::max(pred, int(numCoresBusyPrimary) + 1), totalPrimaryCores);  // keep at least 1 core busy
          }
          else
          {
            numPrimaryCores = std::min(pred, totalPrimaryCores);
          }
          



          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            // std::cout << duration.count() << endl;
            // time_model_inference = duration_cast<microseconds>(stop - start);
            // std::cout << "time_model_inference (us) " << duration.count() << endl;
            model_inference_us.push_back(duration.count());
            start = high_resolution_clock::now();
          }

          /* calculate hvm cores using prediction */
          int numHvmCoresPrev = numHvmCores; 
          numHvmCores = std::max(minHvmCores, maxHvmCores - numPrimaryCores);  // #cores to give to HVM

          //cout << "numPrimaryCores: " << numPrimaryCoresPrev << " --> " << numPrimaryCores << "  numHvmCores" << numHvmCoresPrev << " --> " << numHvmCores << endl;

        }
        else
        {
          // under-prediction
          pred = 0;
          if (DEBUG)
            std::cout << "UNDER-PREDICTION" << endl;
          numHvmCores = minHvmCores;            // min guaranteed # cores for HVM
          numPrimaryCores = totalPrimaryCores;  // totalPrimaryCores - numHvmCores;  // max # cores given to primary
          // safeguard = 1;
        }
      }

      /****** update CPU affinity ******/
      if (prevHvmCores != numHvmCores)
      {
        //cout << "call update: invoke_learning: " << invoke_learning << " safeguard:" << safeguard << " numPrimaryCores: " << numPrimaryCores << "  numHvmCores" << numHvmCores<< endl;
        update_hvm(mode, numHvmCores);
        //printf("called update: primaryMask=0x%x\n", primaryMask);
        //cout << "called update: primaryMask: " << primaryMask << " numPrimaryCores: " << numPrimaryCores << "  numHvmCores" << numHvmCores << endl;


        HVMAgent_SpinUS(sleep_us);
        count++;
        prevHvmCores = numHvmCores;
        if (output_fp)
        {
          double time = timer.ElapsedUS() / 1000000.0;
          fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time, idleCoreCount,
              numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev, med, pred,
              numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
        }
      }
      else
      {
        if (numHvmCores == prevHvmCores && (mode == IPI || mode == NEWIPI ) && timer.ElapsedMS() > prevIpiTimestampMS + ipiTimeoutMS)
        {
          resetCpuGroups(timer.ElapsedSeconds());

          prevIpiTimestampMS = timer.ElapsedMS();
          ipiFailCount++;
          //sleep_us += 10000;  
          HVMAgent_SpinUS(10000); // 10ms for CPUGROUP_TIMEOUT;
        }
        if (output_fp)
        {
          double time = timer.ElapsedUS() / 1000000.0;
          fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d,%d,%d\n", count, time, idleCoreCount,
              numHvmCores, hvmMasks[numHvmCores], 0, numCoresBusyPrimary, min, max, avg, stddev, med, pred,
              numPrimaryCores, max, overpredicted, safeguard, feedback_max, ipiFailCount);
        }
      }

      prevSafeguard = safeguard;
      // HVMAgent_SpinUS(SLEEP_US); // sleep for 1ms for cpu affinity call (if issued) to take effect
    }
  }

  if (output_fp)
  {
    fflush(output_fp);
    printf("Logs flushed\n");
  }


  FILE* timing_fp = nullptr;

  if (TIMING)
  {
    cout << "writing timing logs" << endl;
    ofstream myfile;
    //myfile.open("D:\Results\timing\vw_timing_log.csv");
    myfile.open("vw_timing_log.csv");
    myfile << "feature_compute_us,model_update_us,model_inference_us\n";
    cout << feature_compute_us.size() << endl;
    cout << model_update_us.size() << endl;   
    cout << model_inference_us.size() << endl;
    for (int i = 0; i < model_update_us.size(); i++)
      myfile << feature_compute_us[i] << "," << model_update_us[i] << "," << model_inference_us[i] << "\n";
    myfile.close();
    cout << "vw_timing_log.csv written" << endl;

    ofstream myfile_new;
    //myfile_new.open("D:\Results\timing\cpu_timing_log.csv");
    myfile_new.open("cpu_timing_log.csv");
    myfile_new << "read_primary_cpu_us,log_primary_cpu_us\n";
    cout << read_primary_cpu_us.size() << endl;
    cout << log_primary_cpu_us.size() << endl;
    for (int i = 0; i < read_primary_cpu_us.size(); i++)
      myfile_new << read_primary_cpu_us[i] << "," << log_primary_cpu_us[i] << "\n";
    myfile_new.close();
    cout << "cpu_timing_log.csv written" << endl;

  }


  printf("Exiting\n");
  fflush(stdout);
  exit(0);

  return 0;
}
