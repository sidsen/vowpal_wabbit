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
#include "learning_api.h"
#include "helper.h"

#include <bitset>
#include "vw.h"

#include <math.h>
#include <cguid.h>
#include <chrono>
#include <algorithm>
#include <ctime>

#include "cyclecounter.h"

using namespace std::chrono;
using namespace std;

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

//#define TOTAL_PRIMARY_CORE 6
#define MAX_HVM_CORES 24  // 12
#define MIN_HVM_CORES 1   // 6

/************************/
// Input Parsing
/************************/
const WCHAR ARG_CSV[] = L"--csv";
const WCHAR ARG_DURATION[] = L"--duration_sec";
const WCHAR ARG_BUFFER[] = L"--buffer";
const WCHAR ARG_DELAY_MS[] = L"--delay_ms";
const WCHAR ARG_REACTIVE_FIXED_BUFFER[] = L"--reactive_buffer_mode";
const WCHAR ARG_LEARNING_MODE[] = L"--learning_mode";
const WCHAR ARG_LEARNING_ALGO[] = L"--learning_algo";
const WCHAR ARG_LEARNING_ALGO_CSOAA[] = L"csoaa";
const WCHAR ARG_LEARNING_ALGO_REG[] = L"reg";
const WCHAR ARG_LEARNING_ALGO_CB[] = L"cb";
const WCHAR ARG_LEARNING_PRED_ONE_OVER[] = L"--pred_one_over";
const WCHAR ARG_FIXED_DELAY[] = L"--fixed_delay";
const WCHAR ARG_LEARNING_MS[] = L"--learning_ms";
const WCHAR ARG_TIMING[] = L"--timing";
const WCHAR ARG_DEBUG[] = L"--debug";
const WCHAR ARG_NO_HARVESTING[] = L"--no_harvesting";
const WCHAR ARG_USE_PREV_PEAK[] = L"--use_prev_peak";
const WCHAR ARG_PRIMARY_ALONE[] = L"--primary_alone";
const WCHAR ARG_FEEDBACK[] = L"--feedback";
const WCHAR ARG_FEEDBACK_MS[] = L"--feedback_ms";
const WCHAR ARG_SLEEP_MS[] = L"--sleep_ms";
const WCHAR ARG_MODE[] = L"--mode";
const WCHAR ARG_MODE_IPI[] = L"IPI";
const WCHAR ARG_MODE_IPI_HOLES[] = L"IPI_HOLES";
const WCHAR ARG_MODE_DISJOINT_IPI_HOLES[] = L"DISJOINT_IPI_HOLES";
const WCHAR ARG_MODE_CPUGROUPS[] = L"CpuGroups";
const WCHAR ARG_MODE_DISJOINT_CPUGROUPS[] = L"DISJOINT_CpuGroups";
const WCHAR ARG_PRIMARY_SIZE[] = L"--primary_size";
const WCHAR ARG_MINROOT_MASK[] = L"--minroot_mask";
const WCHAR ARG_DROP_BAD_FEATURES[] = L"--drop_bad_features";
const WCHAR ARG_READ_CPU_SLEEP_US[] = L"--read_cpu_sleep_us";
const WCHAR ARG_PRIMARY_NAMES[] = L"--primary_names";
const WCHAR ARG_HVM_NAMES[] = L"--hvm_names";
const WCHAR ARG_UPDATE_PRIMARY[] = L"--update_primary";
const WCHAR ARG_DEBUG_PEAK[] = L"--debug_peak";
const WCHAR ARG_LOGGING[] = L"--logging";
const WCHAR ARG_COST_FUNCTION[] = L"--cost_function";
const WCHAR ARG_NO_PRED[] = L"--no_pred";
const WCHAR ARG_PRED_PLUS_ONE[] = L"--pred_plus_one";
const WCHAR ARG_PRED_PLUS_OFFSET[] = L"--pred_plus_offset";
const WCHAR ARG_LEARNING_RATE[] = L"--learning_rate";
const WCHAR ARG_DISABLE_HARVEST[] = L"--disable_harvest";
const WCHAR ARG_CHECK_DISPATCH_MS[] = L"--check_dispatch_ms";
const WCHAR ARG_REENABLE_HARVEST_PERIODIC[] = L"--reenable_harvest_periodic";
const WCHAR ARG_REENABLE_HARVEST_SEC[] = L"--reenable_harvest_sec";
const WCHAR ARG_BUCKET[] = L"--bucket";
const WCHAR ARG_PERC[] = L"--perc";

std::wstring output_csv = L"";
int RUN_DURATION_SEC = 0;
int bufferSize = -1;
int FIXED_BUFFER = 0;
int REACTIVE_FIXED_BUFFER = 0;
float DELAY_MS = 0;
int LEARNING = 0;
int LEARNING_MODE = 0;
LearningAlgo LEARNING_ALGO = CSOAA;  // use CSOAA as the learning algo by default
int COST_FUNCTION = 0;
float LEARNING_RATE = 0.1;

int DISABLE_HARVEST = 0;
int CHECK_DISPATCH_MS = 1000;  // default 1 sec
int REENABLE_HARVEST_PERIODIC = 1;
int REENABLE_HARVEST_SEC = 10;  // default 10 sec
int BUCKET = 0;
BucketId bucketIdThresh = Bucket0;
float PERC = 99.9;

int PRED_ONE_OVER = 0;
int FIXED_DELAY = 0;
int LEARNING_MS = 0;  // prediction window in ms
int TIMING = 0;       // measure vw timing
int DEBUG = 0;        // print debug messages
int NO_HARVESTING = 0;
int USE_PREV_PEAK = 0;
int PRIMARY_ALONE = 0;
int PRIMARY_SIZE = 0;

int FEEDBACK_MS = 0;
int FEEDBACK = 0;

float SLEEP_MS = 0;
std::wstring MODE = L"";
int dropBadFeatures = 0;
int read_cpu_sleep_us = 0;
int updatePrimary = 1;
int DEBUG_PEAK = 0;
int LOGGING = 1;
int NO_PRED = 0;
int PRED_PLUS_ONE = 0;
int PRED_PLUS_OFFSET = 1;
int FLAT_USAGE = 0;
Mode mode;
BOOL disjointCpuGroups = FALSE;
UINT64 minRootMask = 0;
std::wstring primaryNames = L"LatSensitive";  // L"LatSensitive-0,LatSensitive-1";
std::wstring hvmNames = L"CPUBully";
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
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRIMARY_NAMES, ARRAY_SIZE(ARG_PRIMARY_NAMES)))
    {
      primaryNames = argv[1];
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_HVM_NAMES, ARRAY_SIZE(ARG_HVM_NAMES)))
    {
      hvmNames = argv[1];
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DURATION, ARRAY_SIZE(ARG_DURATION)))
    {
      RUN_DURATION_SEC = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_BUFFER, ARRAY_SIZE(ARG_BUFFER)))
    {
      bufferSize = _wtoi(argv[1]);
      FIXED_BUFFER = 1;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_REACTIVE_FIXED_BUFFER, ARRAY_SIZE(ARG_REACTIVE_FIXED_BUFFER)))
    {
      REACTIVE_FIXED_BUFFER = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DELAY_MS, ARRAY_SIZE(ARG_DELAY_MS)))
    {
      DELAY_MS = _wtof(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MODE, ARRAY_SIZE(ARG_LEARNING_MODE)))
    {
      LEARNING_MODE = _wtoi(argv[1]);
      if (LEARNING_MODE != 0)
        LEARNING = 1;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_PRED_ONE_OVER, ARRAY_SIZE(ARG_LEARNING_PRED_ONE_OVER)))
    {
      PRED_ONE_OVER = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_FIXED_DELAY, ARRAY_SIZE(ARG_FIXED_DELAY)))
    {
      FIXED_DELAY = _wtoi(argv[1]);
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
    else if (0 == ::_wcsnicmp(argv[0], ARG_USE_PREV_PEAK, ARRAY_SIZE(ARG_USE_PREV_PEAK)))
    {
      USE_PREV_PEAK = _wtoi(argv[1]);
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
      SLEEP_MS = _wtof(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_ALGO, ARRAY_SIZE(ARG_LEARNING_ALGO)))
    {
      if (0 == ::_wcsnicmp(argv[1], ARG_LEARNING_ALGO_CSOAA, ARRAY_SIZE(ARG_LEARNING_ALGO_CSOAA)))
      {
        LEARNING_ALGO = CSOAA;
        wcout << "LEARNING_ALGO: CSOAA" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_LEARNING_ALGO_REG, ARRAY_SIZE(ARG_LEARNING_ALGO_REG)))
      {
        LEARNING_ALGO = REG;
        wcout << "LEARNING_ALGO: REG" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_LEARNING_ALGO_CB, ARRAY_SIZE(ARG_LEARNING_ALGO_CB)))
      {
        LEARNING_ALGO = CBANDIT;
        wcout << "LEARNING_ALGO: CBANDIT" << std::endl;
      }
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_MODE, ARRAY_SIZE(ARG_MODE)))
    {
      if (0 == ::_wcsnicmp(argv[1], ARG_MODE_CPUGROUPS, ARRAY_SIZE(ARG_MODE_CPUGROUPS)))
      {
        mode = CPUGROUPS;
        wcout << "MODE: CPUGROUPS" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_MODE_DISJOINT_CPUGROUPS, ARRAY_SIZE(ARG_MODE_DISJOINT_CPUGROUPS)))
      {
        mode = CPUGROUPS;
        disjointCpuGroups = TRUE;
        wcout << "MODE: DISJOINT_CPUGROUPS" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_MODE_IPI, ARRAY_SIZE(ARG_MODE_IPI)))
      {
        mode = IPI;
        wcout << "MODE: IPI" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_MODE_IPI_HOLES, ARRAY_SIZE(ARG_MODE_IPI_HOLES)))
      {
        mode = IPI_HOLES;
        wcout << "MODE: IPI_HOLES" << std::endl;
      }
      else if (0 == ::_wcsnicmp(argv[1], ARG_MODE_DISJOINT_IPI_HOLES, ARRAY_SIZE(ARG_MODE_DISJOINT_IPI_HOLES)))
      {
        mode = IPI_HOLES;
        disjointCpuGroups = TRUE;
        wcout << "MODE: DISJOINT_IPI_HOLES" << std::endl;
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
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_MINROOT_MASK, ARRAY_SIZE(ARG_MINROOT_MASK)))
    {
      minRootMask = _wcstoui64(argv[1], NULL, 0);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DROP_BAD_FEATURES, ARRAY_SIZE(ARG_DROP_BAD_FEATURES)))
    {
      dropBadFeatures = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_READ_CPU_SLEEP_US, ARRAY_SIZE(ARG_READ_CPU_SLEEP_US)))
    {
      read_cpu_sleep_us = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_UPDATE_PRIMARY, ARRAY_SIZE(ARG_UPDATE_PRIMARY)))
    {
      updatePrimary = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DEBUG_PEAK, ARRAY_SIZE(ARG_DEBUG_PEAK)))
    {
      DEBUG_PEAK = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LOGGING, ARRAY_SIZE(ARG_LOGGING)))
    {
      LOGGING = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_COST_FUNCTION, ARRAY_SIZE(ARG_COST_FUNCTION)))
    {
      COST_FUNCTION = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_NO_PRED, ARRAY_SIZE(ARG_NO_PRED)))
    {
      NO_PRED = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRED_PLUS_ONE, ARRAY_SIZE(ARG_PRED_PLUS_ONE)))
    {
      PRED_PLUS_ONE = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PRED_PLUS_OFFSET, ARRAY_SIZE(ARG_PRED_PLUS_OFFSET)))
    {
      PRED_PLUS_OFFSET = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_RATE, ARRAY_SIZE(ARG_LEARNING_RATE)))
    {
      LEARNING_RATE = _wtof(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DISABLE_HARVEST, ARRAY_SIZE(ARG_DISABLE_HARVEST)))
    {
      DISABLE_HARVEST = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_CHECK_DISPATCH_MS, ARRAY_SIZE(ARG_CHECK_DISPATCH_MS)))
    {
      CHECK_DISPATCH_MS = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_REENABLE_HARVEST_PERIODIC, ARRAY_SIZE(ARG_REENABLE_HARVEST_PERIODIC)))
    {
      REENABLE_HARVEST_PERIODIC = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_REENABLE_HARVEST_SEC, ARRAY_SIZE(ARG_REENABLE_HARVEST_SEC)))
    {
      REENABLE_HARVEST_SEC = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_BUCKET, ARRAY_SIZE(ARG_BUCKET)))
    {
      BUCKET = _wtoi(argv[1]);
      switch (BUCKET)
      {
        case 17:
          bucketIdThresh = BucketX7;
          break;
        case 16:
          bucketIdThresh = BucketX6;
          break;
        case 15:
          bucketIdThresh = BucketX5;
          break;
        case 14:
          bucketIdThresh = BucketX4;
          break;
        case 13:
          bucketIdThresh = BucketX3;
          break;
        case 12:
          bucketIdThresh = BucketX2;
          break;
        case 11:
          bucketIdThresh = BucketX1;
          break;
        case 0:
          bucketIdThresh = Bucket0;
          break;
        case 1:
          bucketIdThresh = Bucket1;
          break;
        case 2:
          bucketIdThresh = Bucket2;
          break;
        case 3:
          bucketIdThresh = Bucket3;
          break;
        case 4:
          bucketIdThresh = Bucket4;
          break;
        case 5:
          bucketIdThresh = Bucket5;
          break;
        case 6:
          bucketIdThresh = Bucket6;
          break;
        default:
          bucketIdThresh = Bucket0;
      }
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_PERC, ARRAY_SIZE(ARG_PERC)))
    {
      PERC = _wtof(argv[1]);
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

  if (!LEARNING || !NO_HARVESTING || !REACTIVE_FIXED_BUFFER || !USE_PREV_PEAK)
    FIXED_BUFFER = 0;

  wcout << "output_csv: " << output_csv << std::endl;
  wcout << "primaryNames: " << primaryNames << std::endl;
  wcout << "hvmNames: " << hvmNames << std::endl;
  wcout << "RUN_DURATION_SEC: " << RUN_DURATION_SEC << std::endl;
  wcout << "bufferSize: " << bufferSize << std::endl;
  wcout << "REACTIVE_FIXED_BUFFER: " << REACTIVE_FIXED_BUFFER << std::endl;
  wcout << "DELAY_MS: " << DELAY_MS << std::endl;
  wcout << "LEARNING_MODE: " << LEARNING_MODE << std::endl;
  wcout << "PRED_ONE_OVER: " << PRED_ONE_OVER << std::endl;
  wcout << "FIXED_DELAY: " << FIXED_DELAY << std::endl;
  wcout << "LEARNING_MS: " << LEARNING_MS << std::endl;
  wcout << "TIMING: " << TIMING << std::endl;
  wcout << "DEBUG: " << DEBUG << std::endl;
  wcout << "NO_HARVESTING: " << NO_HARVESTING << std::endl;
  wcout << "PRIMARY_ALONE: " << PRIMARY_ALONE << std::endl;
  wcout << "FEEDBACK: " << FEEDBACK << std::endl;
  wcout << "FEEDBACK_MS: " << FEEDBACK_MS << std::endl;
  wcout << "SLEEP_MS: " << SLEEP_MS << std::endl;
  wcout << "PRIMARY_SIZE: " << PRIMARY_SIZE << std::endl;
  wcout << "MINROOT_MASK: " << minRootMask << std::endl;
  wcout << "dropBadFeatures: " << dropBadFeatures << std::endl;
  wcout << "read_cpu_sleep_us: " << read_cpu_sleep_us << std::endl;
  wcout << "updatePrimary: " << updatePrimary << std::endl;
  wcout << "DEBUG_PEAK: " << DEBUG_PEAK << std::endl;
  wcout << "LOGGING: " << LOGGING << std::endl;
  wcout << "COST_FUNCTION: " << COST_FUNCTION << std::endl;
  wcout << "NO_PRED: " << NO_PRED << std::endl;
  wcout << "PRED_PLUS_ONE: " << PRED_PLUS_ONE << std::endl;
  wcout << "PRED_PLUS_OFFSET: " << PRED_PLUS_OFFSET << std::endl;
  wcout << "LEARNING_RATE: " << LEARNING_RATE << std::endl;
  wcout << "DISABLE_HARVEST: " << DISABLE_HARVEST << std::endl;
  wcout << "CHECK_DISPATCH_MS: " << CHECK_DISPATCH_MS << std::endl;
  wcout << "REENABLE_HARVEST_PERIODIC: " << REENABLE_HARVEST_PERIODIC << std::endl;
  wcout << "REENABLE_HARVEST_SEC: " << REENABLE_HARVEST_SEC << std::endl;
  cout << "bucketIdThresh: " << BucketIdMapA[bucketIdThresh] << std::endl;
  cout << "PERC: " << PERC << std::endl;

  wcout << "FIXED_BUFFER: " << FIXED_BUFFER << std::endl;
  wcout << "MAX_HVM_CORES: " << MAX_HVM_CORES << std::endl;
  wcout << "MIN_HVM_CORES: " << MIN_HVM_CORES << std::endl;
}

/************************/
// logging functions
/************************/
struct Record
{
  int updateCount;
  double time;
  int hvmBusy;
  int hvmCores;
  int primaryBusy;
  int primaryCores;
  string primaryCoresMask;
  string systemBusyMask;
  int min;
  int max;
  double avg;
  double stddev;
  double med;
  int pred;
  int newPrimaryCores;
  int cpu_max;
  int overpredicted;
  int safeguard;
  int feedback_max;
  int updateModel;
  BucketId bucketId = Bucket6;
};

struct CpuLog
{
  int hvmBusy;
  int hvmCores;
  int primaryBusy;
  int primaryCores;
  string primaryCoresMask;
  string systemBusyMask;
};

struct RecordCPU
{
  double time;
  int primaryBusy;
  int primaryCores;
};

extern BOOLEAN verbose;

#define MAX_RECORDS 10000000L
Record records[MAX_RECORDS];
UINT64 numLogEntries = 0;
RecordCPU recordsCPU[20000000];
UINT64 numLogEntriesCPU = 0;

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
        "pred_peak,upper_bound,cpu_max,overpredicted,safeguard,feedback_max,update_model,bucketId\n");
    fflush(output_fp);

    // ASSERT(SetConsoleCtrlHandler(consoleHandler, TRUE));

    cout << "logs written" << endl;
    for (size_t i = 0; i < numLogEntries; i++)
    {
      Record r = records[i];
      const std::string& bucketIdStr = BucketIdMapA[r.bucketId];

      if (DEBUG_PEAK)
        fprintf(output_fp, "%d,%.6lf,%d,%d,%d,%d,%s,%s,%d,%d,%lf,%lf,%lf,%d,%d,%d,%d,%d,%d,%d,%s\n", r.updateCount,
            r.time, r.hvmBusy, r.hvmCores, r.primaryBusy, r.primaryCores, r.primaryCoresMask.c_str(),
            r.systemBusyMask.c_str(), r.min, r.max, r.avg, r.stddev, r.med, r.pred, r.newPrimaryCores, r.cpu_max,
            r.overpredicted, r.safeguard, r.feedback_max, r.updateModel, bucketIdStr.c_str());
      else
        fprintf(output_fp, "%d,%.3lf,%d,%d,%d,%d,%s,%s,%d,%d,%lf,%lf,%lf,%d,%d,%d,%d,%d,%d,%d,%s\n", r.updateCount,
            r.time, r.hvmBusy, r.hvmCores, r.primaryBusy, r.primaryCores, r.primaryCoresMask.c_str(),
            r.systemBusyMask.c_str(), r.min, r.max, r.avg, r.stddev, r.med, r.pred, r.newPrimaryCores, r.cpu_max,
            r.overpredicted, r.safeguard, r.feedback_max, r.updateModel, bucketIdStr.c_str());
    }

    fflush(output_fp);
    cout << "logs written" << endl;
  }

  if (DEBUG_PEAK)
  {
    wstring output_cpu_csv = L"";
    for (int i = 0; i < output_csv.size() - 4; i++)
    {
      output_cpu_csv += output_csv.c_str()[i];
    }
    output_cpu_csv += L"_cpu.csv";

    FILE* output_cpu_fp = nullptr;
    output_cpu_fp = _wfopen(output_cpu_csv.c_str(), L"w+");
    ASSERT(output_cpu_fp != NULL);
    fprintf(output_cpu_fp, "window,primary_busy_cores,primary_cores\n");
    for (size_t i = 0; i < numLogEntriesCPU; i++)
    {
      RecordCPU r = recordsCPU[i];
      fprintf(output_cpu_fp, "%.6lf,%d\n", r.time, r.primaryBusy, r.primaryCores);
    }
    fflush(output_fp);
    cout << "busy cpu logs written" << endl;
  }
}

VMInfo primary;
VMInfo hvm;

/************************/
// helper functions
/************************/

void updateCores(INT32 newPrimaryCores, UINT64 systemBusyMask)  //, UINT32 hvmCores)
{
  if (mode == IPI_HOLES)
  {
    UINT64 coreMask;
    ASSERT(newPrimaryCores != primary.curCores);

    UINT32 delta = abs((INT32)newPrimaryCores - (INT32)primary.curCores);

    if (newPrimaryCores > (INT32)primary.curCores)
    {
      // Shrinking the hvm.
      ASSERT(SUCCEEDED(hvm.extractHvmCores(delta, &coreMask)));

      // Grow the primary.
      ASSERT(SUCCEEDED(primary.addCores(delta, coreMask)));
    }
    else
    {
      // Extract idle cores from primary.
      ASSERT(SUCCEEDED(primary.extractIdleCores(systemBusyMask, delta, &coreMask)));

      // Give extra cores to hvm.
      ASSERT(SUCCEEDED(hvm.addCores(delta, coreMask)));
    }
  }
  else
  {
    hvm.updateCores(cpuInfo.NonMinRootCores - newPrimaryCores);
    primary.updateCores(newPrimaryCores);
  }
}

void init()
{
  // initialize hvmagent
  ASSERT(SUCCEEDED(HVMAgent_InitWithMinRootMask(&cpuInfo, minRootMask)));
  std::cout << "HVMAgent initialized" << endl;

  ASSERT(SUCCEEDED(primary.init(mode, primaryNames, PRIMARY_SIZE, TRUE, disjointCpuGroups)));
  ASSERT(SUCCEEDED(hvm.init(mode, hvmNames, cpuInfo.NonMinRootCores - PRIMARY_SIZE, FALSE, disjointCpuGroups)));

  hvm.minCores = hvm.curCores;  // initial size of hvm
  hvm.maxCores = (INT)cpuInfo.NonMinRootCores > bufferSize ? cpuInfo.NonMinRootCores - bufferSize : hvm.minCores;
}

CycleCounter timer;
void computeFeature(Feature* f, int learningWindowUs, CpuLog* log)
{
  high_resolution_clock::time_point tStart, tStop;
  microseconds usElapsed;
  UINT64 systemBusyMask;
  INT32 primaryBusyCores, primaryCores, hvmBusyCores, hvmCores;
  INT32 newPrimaryCores = primary.maxCores;
  vector<int64_t> cpuBusy;  // num of busy cpu cores of primary vm --> reading every <1us
  int sum = 0;
  int max = 0;
  int min = primary.maxCores;

  // collect cpu data for vw learning window
  tStart = high_resolution_clock::now();
  while (true)
  {
    HVMAgent_SpinUS(read_cpu_sleep_us);
    // read cpu usage
    systemBusyMask = HVMAgent_BusyMaskCoresNonSMTVMs();
    hvmBusyCores = hvm.busyCores(systemBusyMask);
    hvmCores = hvm.curCores;
    primaryBusyCores = primary.busyCores(systemBusyMask);
    primaryCores = primary.curCores;
    if (DEBUG_PEAK)
    {
      recordsCPU[numLogEntriesCPU++] = {timer.ElapsedUS() / 1000000.0, primaryBusyCores, primaryCores};
      ASSERT(numLogEntries < MAX_RECORDS);
    }
    // record cpu reading
    cpuBusy.push_back(primaryBusyCores);
    sum += primaryBusyCores;
    if (primaryBusyCores < min)
      min = primaryBusyCores;
    if (primaryBusyCores > max)
      max = primaryBusyCores;
    // check time
    tStop = high_resolution_clock::now();
    usElapsed = duration_cast<microseconds>(tStop - tStart);
    if (usElapsed.count() >= learningWindowUs)
      break;  // a learning window past
  }

  // compute features
  f->min = min;
  f->max = max;
  int size = cpuBusy.size();
  f->avg = 1.0 * sum / size;
  double tmp = 0;
  for (int i = 0; i < size; i++)
  {
    tmp += (cpuBusy[i] - f->avg) * (cpuBusy[i] - f->avg);
  }
  tmp = 1.0 * tmp / size;
  f->stddev = sqrt(tmp);
  std::sort(cpuBusy.begin(), cpuBusy.end());
  if (size % 2 != 0)
    f->med = cpuBusy[size / 2];
  else
    f->med = (cpuBusy[(size - 1) / 2] + cpuBusy[size / 2]) / 2.0;

  log->hvmBusy = hvmBusyCores;
  log->hvmCores = hvmCores;
  log->primaryBusy = primaryBusyCores;
  log->primaryCores = primaryCores;
  log->primaryCoresMask = bitset<64>(primary.masks[primaryCores]).to_string();
  log->systemBusyMask = bitset<64>(systemBusyMask).to_string();
}

string getVWFeature(Feature* f) { return f->vwString(); }

string getVWLabel(int cpuPeakObserved)
{
  int cost;
  string vwLabel;
  int correctClass = cpuPeakObserved;

  switch (LEARNING_ALGO)
  {
    case CSOAA:
      if (COST_FUNCTION == 0)
      {
        for (int k = 1; k < (INT32)primary.maxCores + 1; k++)
        {
          if (k < correctClass)
            cost = correctClass - k + primary.maxCores;  // underprediction (worse --> higher cost)
          else
            cost = k - correctClass;  // prefect- & over-prediction
          vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
        }
      }
      if (COST_FUNCTION == 1)  // symmetric cost
      {
        for (int k = 1; k < (INT32)primary.maxCores + 1; k++)
        {
          if (k < correctClass)
            cost = correctClass - k;  // underprediction (worse --> higher cost)
          else
            cost = k - correctClass;  // prefect- & over-prediction
          vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
        }
      }
      if (COST_FUNCTION == 2)  // hinged cost
      {
        for (int k = 1; k < (INT32)primary.maxCores + 1; k++)
        {
          if (k < correctClass)
            cost = correctClass - k;  // underprediction (worse --> higher cost)
          else
            cost = 0;  // prefect- & over-prediction
          vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
        }
      }
      if (COST_FUNCTION == 3)  // max + 1
      {
        correctClass = std::min(correctClass + 1, (INT32)primary.maxCores);
        for (int k = 1; k < (INT32)primary.maxCores + 1; k++)
        {
          if (k < correctClass)
            cost = correctClass - k + primary.maxCores;  // underprediction (worse --> higher cost)
          else
            cost = k - correctClass;  // prefect- & over-prediction
          vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
        }
      }
      break;
    case REG:
      vwLabel = std::to_string(correctClass) + " ";
      break;
    default:
      std::cout << "EXIT: No learning algorithm specified." << endl;
      std::exit(1);
  }

  return vwLabel;
}

/************************/
// main function
/************************/
int __cdecl wmain(int argc, __in_ecount(argc) WCHAR* argv[])
{
  verbose = FALSE;
  process_args(argc, argv);

  vector<int64_t> feature_compute_us, model_update_us, model_inference_us;
  high_resolution_clock::time_point start, stop, learnStart, learnStop;
  microseconds duration;

  UINT64 systemBusyMask;
  INT32 primaryBusyCores, primaryCores, hvmBusyCores, hvmCores;
  INT32 newPrimaryCores = primary.maxCores;
  struct CpuLog log;
  struct CpuLog* l = &log;

  int cpuPeakObserved, predPeak, overPredicted, invokeLearning, invokeSafeguard;
  string vwLabel, vwFeature, vwMsg;
  Feature feature;
  Feature* f = &feature;

  int delayUs = (int)(DELAY_MS * 1000);
  int sleepUs = (int)SLEEP_MS * 1000;
  int learningWindowUs = LEARNING_MS * 1000;
  int firstWindow = 1;
  int newPrimaryCoresPrev = 0;
  int count = 0;
  int debugCount = 0;

  // initialize variables for global safeguard
  high_resolution_clock::time_point checkDispatchStart, checkDispatchStop;
  high_resolution_clock::time_point reenableHarvestStart, reenableHarvestStop;
  microseconds usElapsed;
  int stopHarvest = 0;
  int reset = 0;
  int checkDispatchUs = CHECK_DISPATCH_MS * 1000;
  int reenableHarvestUs = REENABLE_HARVEST_SEC * 1000 * 1000;
  BucketId bucketId = BucketX7;
  BucketId bucketIdPrev = BucketX7;
  if (DISABLE_HARVEST)
    std::cout << "harvesting can be disabled (if p" << PERC
              << " vcpu dispatch wait time lies in bucket >= " << BucketIdMapA[bucketIdThresh] << ")" << endl;
  else
    std::cout << "harvesting cannot be disabled" << endl;


  // initialize HVM agent
  init();
  time_t now = time(0);
  char* dt = ctime(&now);
  cout << "HVM agent initialized: " << dt << endl;


  // initialize vw learning
  ASSERT(SUCCEEDED(modelInit(LEARNING_ALGO, primary.maxCores, LEARNING_RATE)));


  // main loop
  now = time(0);
  dt = ctime(&now);
  std::cout << "HVM agent starting: " << dt << endl;
  timer.Start();
  while (timer.ElapsedSeconds() < RUN_DURATION_SEC)
  {
    debugCount++;
    if (DEBUG && debugCount > 6)
      break;

    // reenable harvesting (if it was disabled) every reenableHarvestUs
    if (stopHarvest)
    {
      reenableHarvestStop = high_resolution_clock::now();
      usElapsed = duration_cast<microseconds>(reenableHarvestStop - reenableHarvestStart);
      if (usElapsed.count() >= reenableHarvestUs)
      {
        stopHarvest = 0;
        reset = 0;
        std::cout << "ON" << endl;
        if (REENABLE_HARVEST_PERIODIC)
          reenableHarvestStart = high_resolution_clock::now();
      }
    }

    // check for vcpu wait time every checkDispatchUs
    checkDispatchStop = high_resolution_clock::now();
    usElapsed = duration_cast<microseconds>(checkDispatchStop - checkDispatchStart);
    if (usElapsed.count() >= checkDispatchUs)
    {
      std::cout << timer.ElapsedUS() / 1000000.0 << " sec elapsed;\t";
      bucketId = primary.GetCpuWaitTimePercentileBucketId(PERC);
      if (DISABLE_HARVEST && !firstWindow)  // harvesting allowed to be disabled
      {
        if (bucketId >= bucketIdThresh && bucketIdPrev >= bucketIdThresh)
        {
          // wait time too long--> disable harvesting
          stopHarvest = 1;
          std::cout << "OFF" << endl;
          if (!REENABLE_HARVEST_PERIODIC)
            checkDispatchStart = high_resolution_clock::now();
        }
        bucketIdPrev = bucketId;
      }
      checkDispatchStart = high_resolution_clock::now();
    }

    // policy 1: no harvesting
    if (NO_HARVESTING)
    {
      start = high_resolution_clock::now();
      cpuPeakObserved = 0;  // reset max

      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        systemBusyMask = HVMAgent_BusyMaskCoresNonSMTVMs();
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (primaryBusyCores > cpuPeakObserved)
          cpuPeakObserved = primaryBusyCores;

        if (DEBUG_PEAK)
        {
          recordsCPU[numLogEntriesCPU++] = {timer.ElapsedUS() / 1000000.0, primaryBusyCores, primaryCores};
          ASSERT(numLogEntries < MAX_RECORDS);
        }

        stop = high_resolution_clock::now();
        usElapsed = duration_cast<microseconds>(stop - start);
        if (usElapsed.count() >= learningWindowUs)
          break;  // log max from every 2ms
      }

      if (LOGGING)
      {
        // bucketId = primary.GetCpuWaitTimePercentileBucketId(99);
        records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
            primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMask).to_string(),
            0, 0, 0, 0, 0, 0, newPrimaryCores, cpuPeakObserved, 0, 0, 0, 0, bucketId};
        ASSERT(numLogEntries < MAX_RECORDS);
      }
    }

    // policy 2: fixed buffer
    if (FIXED_BUFFER)
    {
      start = high_resolution_clock::now();
      cpuPeakObserved = 0;  // reset max

      if (DEBUG)
        cout << "fixed buffer mode" << endl;

      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        systemBusyMask = HVMAgent_BusyMaskCoresNonSMTVMs();
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (primaryBusyCores > cpuPeakObserved)
          cpuPeakObserved = primaryBusyCores;

        stop = high_resolution_clock::now();
        usElapsed = duration_cast<microseconds>(stop - start);
        if (usElapsed.count() >= delayUs)
          break;  // log max from delayUs
      }

      newPrimaryCores = std::min(primaryBusyCores + bufferSize, (INT32)primary.maxCores);  // re-compute primary size

      // bucketId = primary.GetCpuWaitTimePercentileBucketId(99);
      records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
          primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMask).to_string(), 0,
          0, 0, 0, 0, 0, newPrimaryCores, cpuPeakObserved, 0, 0, 0, 0, bucketId};
      ASSERT(numLogEntries < MAX_RECORDS);

      if (newPrimaryCores != primary.curCores)
      {
        updateCores(newPrimaryCores, systemBusyMask);
        HVMAgent_SpinUS(sleepUs);  // sleep for 1ms for cpu affinity call (if issued) to take effects
        count++;
      }

      if (DEBUG)
      {
        cout << "hvmBusyCores " << hvmBusyCores << "hvmCores " << hvmCores << "primaryBusyCores " << primaryBusyCores
             << "primaryCores " << primaryCores << endl;
      }
    }

    // policy 3: heuristic based on peak from previous window
    if (USE_PREV_PEAK)
    {
      start = high_resolution_clock::now();
      cpuPeakObserved = 0;  // reset max

      while (true)
      {
        HVMAgent_SpinUS(read_cpu_sleep_us);

        systemBusyMask = HVMAgent_BusyMaskCoresNonSMTVMs();
        hvmBusyCores = hvm.busyCores(systemBusyMask);
        hvmCores = hvm.curCores;
        primaryBusyCores = primary.busyCores(systemBusyMask);
        primaryCores = primary.curCores;

        if (primaryBusyCores > cpuPeakObserved)
          cpuPeakObserved = primaryBusyCores;

        if (DEBUG_PEAK)
        {
          recordsCPU[numLogEntriesCPU++] = {timer.ElapsedUS() / 1000000.0, primaryBusyCores, primaryCores};
          ASSERT(numLogEntries < MAX_RECORDS);
        }

        stop = high_resolution_clock::now();
        usElapsed = duration_cast<microseconds>(stop - start);
        if (usElapsed.count() >= learningWindowUs)
          break;  // log max from every 2ms
      }

      if (cpuPeakObserved == newPrimaryCores)
        newPrimaryCores = std::min(cpuPeakObserved + 1, (int)primary.maxCores);  // increase vm size by one
      else
        newPrimaryCores = cpuPeakObserved;

      if (LOGGING)
      {
        records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, hvmBusyCores, hvmCores, primaryBusyCores,
            primaryCores, bitset<64>(primary.masks[primaryCores]).to_string(), bitset<64>(systemBusyMask).to_string(),
            0, 0, 0, 0, 0, 0, newPrimaryCores, cpuPeakObserved, 0, 0, 0, 0, bucketId};
        ASSERT(numLogEntries < MAX_RECORDS);
      }
      if (stopHarvest)
      {
        if (!reset)
        {
          updateCores(PRIMARY_SIZE, systemBusyMask);
          HVMAgent_SpinUS(sleepUs);
          reset = 1;
        }
      }
      else
      {
        // update hvm size
        if (newPrimaryCores != primary.curCores)
        {
          updateCores(newPrimaryCores, systemBusyMask);
          HVMAgent_SpinUS(sleepUs);
          count++;
        }
      }
    }

    // policy 4: learning
    if (LEARNING)
    {
      if (DEBUG)
        std::cout << "Using vw learning" << endl;

      // resetting parameters
      overPredicted = 1;
      invokeLearning = 1;
      invokeSafeguard = 0;
      learnStart = high_resolution_clock::now();

      // compute features
      start = high_resolution_clock::now();
      computeFeature(f, learningWindowUs, l);
      cpuPeakObserved = f->max;
      if (DEBUG)
        std::cout << "retured from computefeature" << endl;

      if (TIMING)
      {
        stop = high_resolution_clock::now();
        duration = duration_cast<microseconds>(stop - start);
        feature_compute_us.push_back(duration.count());
      }

      // set CPU affinity
      if (firstWindow)
      {
        // first iteration gives all cores to primary VM
        newPrimaryCores = primary.maxCores;
        newPrimaryCoresPrev = newPrimaryCores;
        vwFeature = getVWFeature(f);
      }
      else
      {
        start = high_resolution_clock::now();

        if (LEARNING_MODE == 1 || LEARNING_MODE == 2 || LEARNING_MODE == 5)
        {  // decide if overpredicted
          if (cpuPeakObserved < (INT32)primary.curCores || primary.curCores == primary.maxCores)
            overPredicted = 1;
          else
            overPredicted = 0;

          if (NO_PRED)
          {
            if (cpuPeakObserved < newPrimaryCores || newPrimaryCores == primary.maxCores)
              overPredicted = 1;
            else
              overPredicted = 0;
          }
        }

        if (LEARNING_MODE == 2 || LEARNING_MODE == 5)
        {
          // mode 2&5 can trigger safeguard
          if (!overPredicted)
          {
            invokeLearning = 0;
            invokeSafeguard = 1;
          }
        }

        if (invokeLearning)
        {
          // construct vw label
          vwLabel.clear();
          vwLabel = getVWLabel(cpuPeakObserved);
          // update model
          if (DEBUG)
          {
            std::cout << "vwLabel to update model: " << vwLabel.c_str() << endl;
            std::cout << "vwFeature to update model: " << vwFeature.c_str() << endl;
          }
          ASSERT(SUCCEEDED(modelUpdate(vwLabel, vwFeature)));

          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            model_update_us.push_back(duration.count());
            start = high_resolution_clock::now();
          }

          // predict cpu peak for next learning window
          vwFeature = getVWFeature(f);
          predPeak = (int)ceil(modelPredict(LEARNING_ALGO, vwFeature));
          // compute new core assignment (keep at least 1 core in addition to current # cores used by primary VM)
          newPrimaryCores = std::min(std::max(predPeak, primaryBusyCores + 1), (INT32)primary.maxCores);

          if (PRED_PLUS_ONE)
            newPrimaryCores =
                std::min(std::max(predPeak + PRED_PLUS_OFFSET, primaryBusyCores + 1), (INT32)primary.maxCores);
          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            model_inference_us.push_back(duration.count());
            start = high_resolution_clock::now();
          }
        }

        if (invokeSafeguard)
        {
          predPeak = 0;
          newPrimaryCores = primary.maxCores;  // primary.maxCores - hvm.curCores;  // max # cores given to primary
          if (LEARNING_MODE == 5)              // mode 5 uses less aggressive safeguard
            newPrimaryCores = std::min(primary.maxCores, primary.curCores * 2);
          if (NO_PRED && LEARNING_MODE == 5)
            newPrimaryCores = std::min((INT32)primary.maxCores, newPrimaryCoresPrev * 2);
        }
      }

      records[numLogEntries++] = {count, timer.ElapsedUS() / 1000000.0, l->hvmBusy, l->hvmCores, l->primaryBusy,
          l->primaryCores, l->primaryCoresMask, l->systemBusyMask, f->min, f->max, f->avg, f->stddev, f->med, predPeak,
          newPrimaryCores, cpuPeakObserved, overPredicted, invokeSafeguard, cpuPeakObserved, invokeLearning, bucketId};

      // update core assignment
      if (NO_PRED)
      {
        if (LEARNING_MODE == 5)
        {
          if (newPrimaryCores != newPrimaryCoresPrev)
            HVMAgent_SpinUS(sleepUs);
          newPrimaryCoresPrev = newPrimaryCores;
        }
      }
      else
      {
        if (stopHarvest)
        {
          if (!reset)
          {
            if (PRIMARY_SIZE != primary.curCores)
            {
              updateCores(PRIMARY_SIZE, systemBusyMask);
              HVMAgent_SpinUS(sleepUs);
            }
            reset = 1;
          }
        }
        else
        {
          if (newPrimaryCores != primary.curCores)
          {
            updateCores(newPrimaryCores, systemBusyMask);

            HVMAgent_SpinUS(sleepUs);
            count++;
          }
          else
          {
            if (FIXED_DELAY)
              HVMAgent_SpinUS(sleepUs);
          }
        }
      }

      firstWindow = 0;
      ASSERT(numLogEntries < MAX_RECORDS);
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
  }

  printf("Exiting\n");
  fflush(stdout);
  exit(0);

  return 0;
}
