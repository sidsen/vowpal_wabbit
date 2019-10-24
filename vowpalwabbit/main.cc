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
using namespace VW::config;

#define MAX_CORES 64  // hardcoded for now (64-bit core busy mask)

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

enum Mode
{
  INVALID,
  IPI,
  NEWIPI,
  CPUGROUPS
};

//#define TOTAL_PRIMARY_CORE 6
#define MAX_HVM_CORES 24  // 12
#define MIN_HVM_CORES 1   // 6

/* process args */
const WCHAR ARG_CSV[] = L"--csv";
const WCHAR ARG_DURATION[] = L"--duration_sec";
const WCHAR ARG_BUFFER[] = L"--buffer";
const WCHAR ARG_DELAY_MS[] = L"--delay_ms";
const WCHAR ARG_REACTIVE_FIXED_BUFFER_MODE[] = L"--reactive_buffer_mode";
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
const WCHAR ARG_MODE_IPI[] = L"IPI";
const WCHAR ARG_MODE_CPUGROUPS[] = L"CpuGroups";
const WCHAR ARG_PRIMARY_SIZE[] = L"--primary_size";
const WCHAR ARG_DROP_BAD_FEATURES[] = L"--drop_bad_features";
const WCHAR ARG_READ_CPU_SLEEP_US[] = L"--read_cpu_sleep_us";
const WCHAR ARG_PRIMARY_NAMES[] = L"--primary_names";
const WCHAR ARG_UPDATE_PRIMARY[] = L"--update_primary";

std::wstring output_csv = L"";
int RUN_DURATION_SEC = 0;
int bufferSize = -1;
int FIXED_BUFFER_MODE = 0;
int REACTIVE_FIXED_BUFFER_MODE = 0;
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
int PRIMARY_SIZE = 0;

int FEEDBACK_MS = 0;
int FEEDBACK = 0;

int SLEEP_MS = 0;
int SLEEP_US = 0;
std::wstring MODE = L"";
int dropBadFeatures = 0;
int read_cpu_sleep_us = 0;
int updatePrimary = 1;

Mode mode;
std::wstring primaryNames = L"LatSensitive";  // L"LatSensitive-0,LatSensitive-1";
std::wstring secondaryName = L"CPUBully";
CpuInfo cpuInfo;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

#define shift argc--, argv++

void __cdecl process_args(int argc, __in_ecount(argc) WCHAR* argv[])
{
  shift;
  while (argc > 0)
  {
    if (0 == ::_wcsnicmp(argv[0], ARG_CSV, ARRAY_SIZE(ARG_CSV)))
    {
      output_csv = argv[1];
      wcout << "output_csv: " << output_csv << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_NAMES, ARRAY_SIZE(ARG_PRIMARY_NAMES)))
    {
      primaryNames = argv[1];
      wcout << "primaryNames: " << primaryNames << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DURATION, ARRAY_SIZE(ARG_DURATION)))
    {
      RUN_DURATION_SEC = _wtoi(argv[1]);
      wcout << "RUN_DURATION_SEC: " << RUN_DURATION_SEC << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_BUFFER, ARRAY_SIZE(ARG_BUFFER)))
    {
      bufferSize = _wtoi(argv[1]);
      FIXED_BUFFER_MODE = 1;
      wcout << "bufferSize: " << bufferSize << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_REACTIVE_FIXED_BUFFER_MODE, ARRAY_SIZE(ARG_REACTIVE_FIXED_BUFFER_MODE)))
    {
      REACTIVE_FIXED_BUFFER_MODE = _wtoi(argv[1]);
      wcout << "REACTIVE_FIXED_BUFFER_MODE: " << REACTIVE_FIXED_BUFFER_MODE << std::endl;
    }

    else if (0 == ::_wcsnicmp(argv[0], ARG_DELAY_MS, ARRAY_SIZE(ARG_DELAY_MS)))
    {
      DELAY_MS = _wtoi(argv[1]);
      wcout << "DELAY_MS: " << DELAY_MS << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MODE, ARRAY_SIZE(ARG_LEARNING_MODE)))
    {
      LEARNING_MODE = _wtoi(argv[1]);
      wcout << "LEARNING_MODE: " << LEARNING_MODE << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MS, ARRAY_SIZE(ARG_LEARNING_MS)))
    {
      LEARNING_MS = _wtoi(argv[1]);
      wcout << "LEARNING_MS: " << LEARNING_MS << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_TIMING, ARRAY_SIZE(ARG_TIMING)))
    {
      TIMING = _wtoi(argv[1]);
      wcout << "TIMING: " << TIMING << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DEBUG, ARRAY_SIZE(ARG_DEBUG)))
    {
      DEBUG = _wtoi(argv[1]);
      wcout << "DEBUG: " << DEBUG << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_NO_HARVESTING, ARRAY_SIZE(ARG_NO_HARVESTING)))
    {
      NO_HARVESTING = _wtoi(argv[1]);
      wcout << "NO_HARVESTING: " << NO_HARVESTING << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_ALONE, ARRAY_SIZE(ARG_PRIMARY_ALONE)))
    {
      PRIMARY_ALONE = _wtoi(argv[1]);
      wcout << "PRIMARY_ALONE: " << PRIMARY_ALONE << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_FEEDBACK, ARRAY_SIZE(ARG_FEEDBACK)))
    {
      FEEDBACK = _wtoi(argv[1]);
      wcout << "FEEDBACK: " << FEEDBACK << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_FEEDBACK_MS, ARRAY_SIZE(ARG_FEEDBACK_MS)))
    {
      FEEDBACK_MS = _wtoi(argv[1]);
      wcout << "FEEDBACK_MS: " << FEEDBACK_MS << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_SLEEP_MS, ARRAY_SIZE(ARG_SLEEP_MS)))
    {
      SLEEP_MS = _wtoi(argv[1]);
      SLEEP_US = SLEEP_MS * 1000;
      wcout << "SLEEP_MS: " << SLEEP_MS << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_MODE, ARRAY_SIZE(ARG_MODE)))
    {
      if (0 == ::_wcsnicmp(argv[1], ARG_MODE_CPUGROUPS, ARRAY_SIZE(ARG_MODE_CPUGROUPS)))
      {
        mode = CPUGROUPS;
        wcout << "MODE: CPUGROUPS" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_MODE_IPI, ARRAY_SIZE(ARG_MODE_IPI)))
      {
        mode = IPI;
        wcout << "MODE: IPI" << std::endl;
      }
      else
      {
        cout << "Invalid mode" << endl;
        exit(1);
      }
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_SIZE, ARRAY_SIZE(ARG_PRIMARY_SIZE)))
    {
      PRIMARY_SIZE = _wtoi(argv[1]);
      wcout << "PRIMARY_SIZE: " << PRIMARY_SIZE << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DROP_BAD_FEATURES, ARRAY_SIZE(ARG_DROP_BAD_FEATURES)))
    {
      dropBadFeatures = _wtoi(argv[1]);
      wcout << "dropBadFeatures: " << dropBadFeatures << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_READ_CPU_SLEEP_US, ARRAY_SIZE(ARG_READ_CPU_SLEEP_US)))
    {
      read_cpu_sleep_us = _wtoi(argv[1]);
      wcout << "read_cpu_sleep_us: " << read_cpu_sleep_us << std::endl;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_UPDATE_PRIMARY, ARRAY_SIZE(ARG_UPDATE_PRIMARY)))
    {
      updatePrimary = _wtoi(argv[1]);
      wcout << "updatePrimary: " << updatePrimary << std::endl;
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

  if (LEARNING_MODE != 0 || NO_HARVESTING != 0 || REACTIVE_FIXED_BUFFER_MODE != 0)
    FIXED_BUFFER_MODE = 0;

  wcout << "FIXED_BUFFER_MODE: " << FIXED_BUFFER_MODE << std::endl;
  wcout << "MAX_HVM_CORES: " << MAX_HVM_CORES << std::endl;
  wcout << "MIN_HVM_CORES: " << MIN_HVM_CORES << std::endl;
}

/* logging */
struct Record
{
  int updateCount;
  double time;
  int hvmBusy;
  int hvmCores;
  int primaryBusy;
  int primaryCores;
  string primaryCoresMask;
  string systemBusyMaskRaw;
  // int idleCoreCount;
  // int hvmCores;
  // UINT64 hvmMask;
  // UINT64 idleMask;
  // int primaryBusy;
  int min;
  int max;
  double avg;
  double stddev;
  int64_t med;
  int pred;
  int newPrimaryCores;
  int cpu_max;
  int overpredicted;
  int safeguard;
  int feedback_max;
  int updateModel;
  // int ipiFailCount;
};

extern BOOLEAN verbose;

#define MAX_RECORDS 10000000L
Record records[MAX_RECORDS];
UINT64 numLogEntries = 0;

/*
Record createRecord(int updateCount, float time, int hvmIdle, int hvmCores, int primaryIdle, int primaryBusy, int
primaryCores, int min, int max, int avg, int stddev, int med, int pred, int newPrimaryCores, int cpu_max, int
overpredicted, int safeguard, int feedback_max)
{
  Record r = {updateCount, float time, int hvmIdle, int hvmCores, int primaryIdle, int primaryBusy, int primaryCores,
      int min, int max, int avg, int stddev, int med, int pred, int newPrimaryCores, int cpu_max, int overpredicted,
      int safeguard, int feedback_max};
  return r;
}
*/

void writeLogs()
{
  FILE* output_fp = nullptr;
  if (!output_csv.empty())
  {
    output_fp = _wfopen(output_csv.c_str(), L"w+");
    ASSERT(output_fp != NULL);

    fprintf(output_fp,
        "iteration,time_sec,hvm_busy_cores,hvm_cores,primary_busy_cores,primary_cores,primary_cores_mask,system_busy_"
        "mask_raw,f_min,f_max,f_avg,f_stddev,f_med,"
        "pred_peak,upper_bound,cpu_max,overpredicted,safeguard,feedback_max,update_model\n");

    fflush(output_fp);

    // ASSERT(SetConsoleCtrlHandler(consoleHandler, TRUE));

    for (size_t i = 0; i < numLogEntries; i++)
    {
      Record r = records[i];
      fprintf(output_fp, "%d,%.3lf,%d,%d,%d,%d,%s,%s,%d,%d,%lf,%lf,%lld,%d,%d,%d,%d,%d,%d,%d\n", r.updateCount, r.time,
          r.hvmBusy, r.hvmCores, r.primaryBusy, r.primaryCores, r.primaryCoresMask.c_str(), r.systemBusyMaskRaw.c_str(),
          r.min, r.max, r.avg, r.stddev, r.med, r.pred, r.newPrimaryCores, r.cpu_max, r.overpredicted, r.safeguard,
          r.feedback_max, r.updateModel);
    }
    fflush(output_fp);
    cout << "logs written" << endl;
  }
}

vector<wstring> splitString(const std::wstring& str, WCHAR delim)
{
  vector<wstring> result;
  wstring temp;
  wstringstream wss(str);

  while (std::getline(wss, temp, delim))
  {
    if (!temp.empty())
    {
      result.push_back(temp);
    }
  }

  return result;
}

struct VMInfo
{
  void init(wstring _vmName, INT32 _numCores, BOOLEAN _primary = FALSE)
  {
    minCores = 1;
    maxCores = _numCores;
    curCores = _numCores;
    primary = _primary;

    setupCpuGroups();

    vector<wstring> vmNames = splitString(_vmName, L',');

    for (const wstring& vmName : vmNames)
    {
      wcout << L"Initializing handle for: " << vmName << L"with _numcore:" << _numCores << endl;
      HCS_SYSTEM handle;
      ASSERT(SUCCEEDED(HVMAgent_GetVMHandle(vmName, &handle)));
      handles.push_back(handle);

      ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroupToVM(handle, GUID_NULL)));

      if (mode == CPUGROUPS)
      {
        ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroupToVM(handle, groups[curCores])));
      }
      else if (mode == IPI)
      {
        ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroupToVM(handle, ipiGroup)));
      }
      else
      {
        ASSERT(FALSE);
      }
    }
  }

  void setupCpuGroups()
  {
    UINT32 minCores = 1;
    UINT32 maxCores = cpuInfo.NonMinRootCores;

    for (UINT32 i = minCores; i <= maxCores; i++)
    {
      if (primary)
      {
        masks[i] = HVMAgent_GenerateCoreAffinityFromFront(i);
      }
      else
      {
        masks[i] = HVMAgent_GenerateCoreAffinityFromBack(i);
      }
      wcout << L"Mask " << i << L": " << masks[i] << endl;
    }

    if (mode == CPUGROUPS)
    {
      for (UINT32 i = minCores; i <= maxCores; i++)
      {
        ASSERT(SUCCEEDED(HVMAgent_CreateCpuGroup(&groups[i], masks[i])));
        if (verbose)
        {
          PrintCpuGroupInfo(groups[i]);
        }
      }
    }
    else if (mode == IPI)
    {
      if (DEBUG)
      {
        cout << "masks[curCores]" << masks[curCores] << endl;
        cout << "curCores" << curCores << endl;
        if (ipiGroup == GUID_NULL)
          cout << "ipiGroup is null" << endl;
      }

      ASSERT(SUCCEEDED(HVMAgent_CreateCpuGroup(&ipiGroup, masks[curCores])));
      if (verbose)
      {
        PrintCpuGroupInfo(ipiGroup);
      }
    }
  }

  // assign new cpugroup to primary
  void updateCores(INT32 numCores)
  {
    curCores = numCores;
    if (primary && !updatePrimary)
    {
      return;
    }

    if (mode == CPUGROUPS)
    {
      for (const HCS_SYSTEM& handle : handles)
      {
        ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroupToVM(handle, GUID_NULL)));
        ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroupToVM(handle, groups[curCores])));
      }
    }
    else if (mode == IPI)
    {
      ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCoresUsingMask(ipiGroup, masks[numCores])));
    }
  }

  INT32 idleCores(UINT64 systemBusyMask) { return curCores - busyCores(systemBusyMask); }

  INT32 busyCores(UINT64 systemBusyMask) { return HVMAgent_CoreCount(systemBusyMask & masks[curCores]); }

  INT32 minCores;
  INT32 maxCores;
  INT32 curCores;
  vector<HCS_SYSTEM> handles;

  GUID ipiGroup;
  GUID groups[MAX_CORES];
  UINT64 masks[MAX_CORES];  // bit masks of cpugroup for a vm
  BOOLEAN primary;
};

VMInfo primary;
VMInfo hvm;

/************************/
// helper functions
/************************/

void updateCores(UINT32 primaryCores)  //, UINT32 hvmCores)
{
  hvm.updateCores(cpuInfo.NonMinRootCores - primaryCores);
  primary.updateCores(primaryCores);
}

void init()
{
  // initialize hvmagent
  ASSERT(SUCCEEDED(HVMAgent_Init(&cpuInfo)));
  std::cout << "HVMAgent initialized" << endl;

  /*
  if (cpuInfo.IsHyperThreaded)
  {
    PRIMARY_SIZE /= 2;
  }
  */

  primary.init(primaryNames, PRIMARY_SIZE, TRUE);
  hvm.init(secondaryName, cpuInfo.NonMinRootCores - PRIMARY_SIZE, FALSE);

  hvm.maxCores = cpuInfo.NonMinRootCores - bufferSize;
  hvm.minCores = hvm.curCores;  // initial size of hvm
}

UINT64 busyMaskCores(UINT64 busyLpMask)
{
  if (!cpuInfo.IsHyperThreaded)
  {
    return busyLpMask;
  }

  const UINT64 EvenBits = 0x5555555555555555;
  const UINT64 OddBits = 0xaaaaaaaaaaaaaaaa;

  UINT64 evenLpMask = (busyLpMask & EvenBits);
  UINT64 oddLpMask = (busyLpMask & OddBits);

  return evenLpMask | (evenLpMask << 1) | oddLpMask | (oddLpMask >> 1);
}

/*
  optional 1st arg = buffer size
*/
// int main(int argc, char* argv[])
int __cdecl wmain(int argc, __in_ecount(argc) WCHAR* argv[])
{
  verbose = FALSE;
  process_args(argc, argv);
  int sleep_us = SLEEP_MS * 1000;

  /************************/
  // initialize HVM agent
  /************************/
  init();

  cout << "HVM agent initialized" << endl;

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

  int delay_us;
  delay_us = DELAY_MS * 1000;

  /************************/
  // VW learning agent
  /************************/
  // initilize vw features
  size_t size;                 // 10 * 1000 / 50 = 200 readings per 10ms
  vector<int64_t> cpu_busy_a;  // num of busy cpu cores of primary vm --> reading every <1us
  vector<int64_t> cpu_busy_b;
  vector<int64_t> time_b;
  vector<int64_t> feature_compute_us;
  vector<int64_t> model_update_us;
  vector<int64_t> model_inference_us;
  vector<int64_t> read_primary_cpu_us;
  vector<int64_t> log_primary_cpu_us;

  int invoke_learning = 0;
  int first_window = 1;
  int learning_us = LEARNING_MS * 1000;
  int feedback_us = FEEDBACK_MS * 1000;

  int count = 0;
  int sum = 0;
  int max = 0;
  int feedback_max = 0;
  int min = primary.maxCores;
  int64_t med = 0;
  double stddev = 0;
  double avg = 0;
  string feature;
  int cpu_max;
  int correct_class;

  int pred = 0;
  int cost;
  int overpredicted = 1;
  int prevOverpredicted = 1;
  int safeguard = 0;
  int prevSafeguard = 0;
  int updateModel = 0;

  int buffer_empty_consecutive_count = 0;
  // initialize vw
  auto vw = VW::initialize("--csoaa " + to_string(primary.maxCores) + " --power_t 0 -l 0.1");
  std::cout << "vw initialized with " << primary.maxCores << " classes." << endl;

  string vwLabel, vwFeature, vwMsg;
  example* ex;
  example* ex_pred;

  /************************/
  // learning tweaks
  /************************/
  int use_curr_busy = 1;

  std::cout << "************************" << endl;
  std::cout << "always update learning model (even for under-predictions)" << endl;
  if (use_curr_busy)
    std::cout << "use current busy" << endl;
  else
    std::cout << "use max from previous window" << endl;
  std::cout << "************************" << endl;

  /************************/
  // main loop
  /************************/
  int debug_count = 0;
  auto start_1min = high_resolution_clock::now();

  CycleCounter timer;
  timer.Start();
  UINT64 systemBusyMask, systemBusyMaskRaw;
  INT32 primaryBusyCores, primaryCores, hvmBusyCores, hvmCores;
  INT32 newPrimaryCores = primary.maxCores;

  while (timer.ElapsedSeconds() < RUN_DURATION_SEC)
  {
    debug_count++;
    if (DEBUG && debug_count > 6)
      break;

    if (NO_HARVESTING)
    {
      start = high_resolution_clock::now();
      max = 0;  // reset max

      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        systemBusyMaskRaw = HVMAgent_BusyMaskRaw();
        systemBusyMask = busyMaskCores(systemBusyMaskRaw);
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (primaryBusyCores > max)
          max = primaryBusyCores;

        stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(stop - start);
        if (us_elapsed.count() >= learning_us)
          break;  // log max from every 2ms
      }

      records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
          primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMaskRaw).to_string(),
          0, 0, 0, 0, 0, 0, newPrimaryCores, max, 0, 0, 0, updateModel};
      ASSERT(numLogEntries < MAX_RECORDS);
    }

    else if (FIXED_BUFFER_MODE)
    {
      start = high_resolution_clock::now();
      max = 0;  // reset max

      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        systemBusyMaskRaw = HVMAgent_BusyMaskRaw();
        systemBusyMask = busyMaskCores(systemBusyMaskRaw);
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (primaryBusyCores > max)
          max = primaryBusyCores;

        stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(stop - start);
        if (us_elapsed.count() >= delay_us)
          break;  // log max from delay_us
      }

      newPrimaryCores = std::min(primaryBusyCores + bufferSize, primary.maxCores);  // re-compute primary size

      if (DEBUG)
      {
        cout << "hvmBusyCores " << hvmBusyCores << "hvmCores " << hvmCores << "primaryBusyCores " << primaryBusyCores
             << "primaryCores " << primaryCores << endl;
      }

      records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
          primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMaskRaw).to_string(),
          0, 0, 0, 0, 0, 0, newPrimaryCores, max, 0, 0, 0, updateModel};
      ASSERT(numLogEntries < MAX_RECORDS);

      if (newPrimaryCores != primary.curCores)
      {
        updateCores(newPrimaryCores);
        HVMAgent_SpinUS(sleep_us);  // sleep for 1ms for cpu affinity call (if issued) to take effects
        count++;
      }
    }

    else if (REACTIVE_FIXED_BUFFER_MODE)
    {
      start = high_resolution_clock::now();
      max = 0;  // reset max

      HVMAgent_SpinUS(read_cpu_sleep_us);
      systemBusyMaskRaw = HVMAgent_BusyMaskRaw();
      systemBusyMask = busyMaskCores(systemBusyMaskRaw);
      hvmBusyCores = hvm.busyCores(systemBusyMask);
      hvmCores = hvm.curCores;
      primaryBusyCores = primary.busyCores(systemBusyMask);
      primaryCores = primary.curCores;

      newPrimaryCores = std::min(primaryBusyCores + bufferSize, primary.maxCores);  // re-compute primary size

      if (newPrimaryCores != primary.curCores)
      {
        records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
            primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(),
            bitset<64>(systemBusyMaskRaw).to_string(), 0, 0, 0, 0, 0, 0, newPrimaryCores, max, 0, 0, 0, updateModel};
        ASSERT(numLogEntries < MAX_RECORDS);

        updateCores(newPrimaryCores);
        HVMAgent_SpinUS(sleep_us);  // sleep for 1ms for cpu affinity call (if issued) to take effects
        count++;
      }

      if (DEBUG)
      {
        cout << "hvmBusyCores " << hvmBusyCores << "hvmCores " << hvmCores << "primaryBusyCores " << primaryBusyCores
             << "primaryCores " << primaryCores << endl;
      }
    }

    else
    {
      learn_start = high_resolution_clock::now();
      feedback_start = high_resolution_clock::now();

      // resetting parameters
      overpredicted = 1;
      safeguard = 0;
      updateModel = 0;
      sum = 0;
      size = 0;
      // count = 0;
      max = 0;
      min = primary.maxCores;
      cpu_busy_a.clear();
      invoke_learning = 0;

      /* collect cpu data for vw learning window */
      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        if (TIMING)
          start = high_resolution_clock::now();

        systemBusyMaskRaw = HVMAgent_BusyMaskRaw();
        systemBusyMask = busyMaskCores(systemBusyMaskRaw);
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (TIMING)
        {
          stop = high_resolution_clock::now();
          duration = duration_cast<microseconds>(stop - start);
          read_primary_cpu_us.push_back((int)duration.count());
          start = high_resolution_clock::now();
        }

        // record cpu reading
        cpu_busy_a.push_back(primaryBusyCores);
        sum += primaryBusyCores;
        if (primaryBusyCores < min)
          min = primaryBusyCores;
        if (primaryBusyCores > max)
          max = primaryBusyCores;
        if (primaryBusyCores > feedback_max)
          feedback_max = primaryBusyCores;

        // check for underprediction (buffer empty)
        if (LEARNING_MODE == 3 || LEARNING_MODE == 4)
        {
          if (primaryBusyCores == primary.curCores && primary.curCores != primary.maxCores)
          {
            // buffer runs out (underprediction) --> trigger immediate safeguard
            overpredicted = 0;
            safeguard = 1;
            if (LEARNING_MODE == 4)
              break;  // stop data collection immediately for mode 4

            // give all cores back to primary for mode3
            newPrimaryCores = primary.maxCores;  // primary.maxCores - hvm.curCores;  // max # cores given to primary

            records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
                primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(),
                bitset<64>(systemBusyMaskRaw).to_string(), 0, 0, 0, 0, 0, 0, newPrimaryCores, max, 0, 0, 0,
                updateModel};
            ASSERT(numLogEntries < MAX_RECORDS);

            if (newPrimaryCores != primary.curCores)
            {
              // cout << "safeguard1" << endl;
              updateCores(newPrimaryCores);
              HVMAgent_SpinUS(sleep_us);
              learning_us += sleep_us;
              count++;
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
            if (feedback_max == primary.curCores && primary.curCores != primary.maxCores)
            {
              // underpred: increase cores for primary
              overpredicted = 0;
              newPrimaryCores = primary.curCores * 2;
            }
            else
            {
              overpredicted = 1;
              newPrimaryCores = primary.curCores - 1;
            }

            // adjust primary size
            newPrimaryCores = std::min(newPrimaryCores, primary.maxCores);
            // newPrimaryCores = std::max(newPrimaryCores, pred);
            // update hvm size
            if (newPrimaryCores != primary.curCores)
            {
              // cout << "safeguard1" << endl;
              updateCores(newPrimaryCores);
              HVMAgent_SpinUS(sleep_us);
              count++;
            }

            records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
                primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(),
                bitset<64>(systemBusyMaskRaw).to_string(), 0, 0, 0, 0, 0, 0, newPrimaryCores, max, 0, 0, 0,
                updateModel};
            ASSERT(numLogEntries < MAX_RECORDS);

            // resetting for the smaller feedback window
            feedback_max = 0;
            feedback_start = high_resolution_clock::now();
          }
        }

        if (TIMING)
        {
          stop = high_resolution_clock::now();
          duration = duration_cast<microseconds>(stop - start);
          log_primary_cpu_us.push_back((int)duration.count());
          start = high_resolution_clock::now();
        }

        learn_stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(learn_stop - learn_start);

        if (us_elapsed.count() >= learning_us)
        {
          learning_us = LEARNING_MS * 1000;
          break; // a learning window past
        }
      }

      /****** completed data collection window ******/

      /****** compute features ******/
      start = high_resolution_clock::now();
      size = cpu_busy_a.size();
      if (DEBUG)
      {
        cout << "cpu_busy_a.size()=" << size << endl;
      }
      avg = 1.0 * sum / size;
      // compute stddev
      double tmp_sum = 0;
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
        med = (cpu_busy_a[(size - 1) / 2] + cpu_busy_a[size / 2]) / 2;

      cpu_max = max;
      if (LEARNING_MODE == 7)
      {
        min = 1;
        max = 1;
        avg = 1;
        stddev = 1;
        med = 1;
      }

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

        newPrimaryCores = primary.maxCores;
        /* construct features for model update */
        vwFeature = "|busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +
            " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
      }
      else
      {
        start = high_resolution_clock::now();

        if (LEARNING_MODE == 1 || LEARNING_MODE == 7 || LEARNING_MODE == 2 || LEARNING_MODE == 5 ||
            LEARNING_MODE == 8 || LEARNING_MODE == 9 || LEARNING_MODE == 10 || LEARNING_MODE == 11)
        {  // mode 1&2 need to decide overprediction here
          if (cpu_max < primary.curCores || primary.curCores == primary.maxCores)
            overpredicted = 1;
          else
            overpredicted = 0;
        }

        if (LEARNING_MODE == 6)
        {
          if (cpu_max < primary.curCores || primary.curCores == primary.maxCores)
          {
            overpredicted = 1;
            buffer_empty_consecutive_count = 0;
          }
          else
          {
            overpredicted = 0;
            buffer_empty_consecutive_count += 1;
          }
        }

        if (LEARNING_MODE == 1 || LEARNING_MODE == 7 || LEARNING_MODE == 8 || LEARNING_MODE == 9)
        {
          // mode 1 always relies on predictions
          invoke_learning = 1;
          safeguard = 0;
        }
        else if (LEARNING_MODE == 6)
        {
          // mode 6 triggers safeguard/exploration if buffer empty for 3 past consecutive intervals
          if (buffer_empty_consecutive_count == 3)
          {
            invoke_learning = 0;
            safeguard = 1;
            buffer_empty_consecutive_count = 0;
          }
          else
          {
            invoke_learning = 1;
            safeguard = 0;
          }
        }
        else
        {
          // mode 2&3&4&5&8&9 can trigger safeguard
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

        invoke_learning = 1;
        if (invoke_learning)
        {  // use model prediction for next window only if overpredicted from the previous window
          // if (dropBadFeatures && prevSafeguard)
          //{
          // don't update the model
          // int update_model = 0;
          //}
          if (!dropBadFeatures || prevOverpredicted)
          {
            updateModel = 1;
            // update model if (NOT drop bad features) or (previous window buffer NOT empty) ==> not(drop bad features
            // and previous window buffer empty)
            /* create cost label */
            vwLabel.clear();

            correct_class = cpu_max;

            if (LEARNING_MODE == 9 || LEARNING_MODE == 11)
            {  // mode 9 tries new cost function
              correct_class = cpu_max + 1;
            }

            for (int k = 1; k < primary.maxCores + 1; k++)
            {
              if (k < correct_class)
                cost = correct_class - k + primary.maxCores;  // underprediction (worse --> higher cost)
              else
                cost = k - correct_class;  // prefect- & over-prediction
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
          pred = (int)VW::get_cost_sensitive_prediction(ex_pred);
          if (DEBUG)
            std::cout << "pred = " << pred << endl;
          VW::finish_example(*vw, *ex_pred);

          // read current busy status?
          if (use_curr_busy)
            newPrimaryCores = std::min(std::max(pred, primaryBusyCores + 1),
                primary.maxCores);  // keep at least 1 core in addition to current busy
          else
            newPrimaryCores = std::min(std::max(pred, max + 1),
                primary.maxCores);  // keep at least 1 core in addition to max from the past window

          if (LEARNING_MODE == 8 || LEARNING_MODE == 10)
          {
            newPrimaryCores = std::min(std::max(pred + 1, primaryBusyCores + 1), primary.maxCores);
            if (DEBUG)
              std::cout << "<debug> newPrimaryCores: " << newPrimaryCores << ",  primary.maxCores: " << primary.maxCores
                        << ",  pred: " << pred << endl;
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

          // cout << "numPrimaryCores: " << numPrimaryCoresPrev << " --> " << numPrimaryCores << "  hvm.curCores" <<
          // hvm.curCoresPrev << " --> " << hvm.curCores << endl;
        }

        if (safeguard)
        {
          // under-prediction
          pred = 0;
          if (DEBUG)
            std::cout << "UNDER-PREDICTION" << endl;
          newPrimaryCores = primary.maxCores;  // primary.maxCores - hvm.curCores;  // max # cores given to primary

          if (LEARNING_MODE == 5)
          {
            // mode 5 uses less aggressive safeguard
            newPrimaryCores = std::min(primary.maxCores, primary.curCores + 1);
          }

          // safeguard = 1;
        }
      }

      records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
          primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMaskRaw).to_string(),
          min, max, avg, stddev, med, pred, newPrimaryCores, cpu_max, overpredicted, safeguard, feedback_max,
          updateModel};

      ASSERT(numLogEntries < MAX_RECORDS);

      /****** update CPU affinity ******/
      if (newPrimaryCores != primary.curCores)
      {
        updateCores(newPrimaryCores);
        HVMAgent_SpinUS(sleep_us);
        count++;
      }

      // prevSafeguard = safeguard; //only workds for mode 2-4
      prevOverpredicted = overpredicted;  // works for all modes
      // HVMAgent_SpinUS(SLEEP_US); // sleep for 1ms for cpu affinity call (if issued) to take effect
    }
  }

  // write logs to file
  writeLogs();

  // write timing logs to file
  FILE* timing_fp = nullptr;
  if (TIMING)
  {
    // writing files to  D:\HarvestVM\abs-registry\Benchmarks\CPUBully
    cout << "writing timing logs" << endl;
    ofstream myfile;
    // myfile.open("D:\Results\timing\vw_timing_log.csv");
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
    // myfile_new.open("D:\Results\timing\cpu_timing_log.csv");
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
