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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

const WCHAR ARG_CSV[] = L"--csv";
const WCHAR ARG_BUFFER[] = L"--buffer";
const WCHAR ARG_LEARNING_MODE[] = L"--learning_mode";
const WCHAR ARG_LEARNING_RATE[] = L"--learning_rate";
const WCHAR ARG_DEBUG[] = L"--debug";
const WCHAR ARG_NO_HARVESTING[] = L"--no_harvesting";

std::wstring output_csv = L"";
int bufferSize = -1;
int FIXED_BUFFER_MODE = 0;
int LEARNING_MODE = 0;
// 1: fixed rate learning without safeguard
// 2: fixed rate learning with safeguard
// 3: moving rate learning
int LEARNING_RATE = 0;  // prediction window in ms
int DEBUG = 0;          // print debug messages
int NO_HARVESTING = 0;

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
    else if (0 == ::_wcsnicmp(argv[0], ARG_BUFFER, ARRAY_SIZE(ARG_BUFFER)))
    {
      bufferSize = _wtoi(argv[1]);
      FIXED_BUFFER_MODE = 1;
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_MODE, ARRAY_SIZE(ARG_LEARNING_MODE)))
    {
      LEARNING_MODE = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_LEARNING_RATE, ARRAY_SIZE(ARG_LEARNING_RATE)))
    {
      LEARNING_RATE = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_DEBUG, ARRAY_SIZE(ARG_DEBUG)))
    {
      DEBUG = _wtoi(argv[1]);
    }
    else if (0 == ::_wcsnicmp(argv[0], ARG_NO_HARVESTING, ARRAY_SIZE(ARG_NO_HARVESTING)))
    {
      NO_HARVESTING = _wtoi(argv[1]);
    }
    else
    {
      wprintf(L"Unknown argument: %s\n", argv[0]);
      // print_help();
      exit(1);
    }

    shift;
    shift;
  }

  if (LEARNING_MODE != 0)
    FIXED_BUFFER_MODE = 0;

  wcout << L"Parameters: buffer size:" << bufferSize << L" fixed buffer:" << FIXED_BUFFER_MODE << L" csv:" << output_csv
        << L" learning_mode:" << LEARNING_MODE << L" learning_rate:" << LEARNING_RATE << L" debug:" << DEBUG
        << L" no_harvesting: " << NO_HARVESTING << endl;
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

/*
  optional 1st arg = buffer size
*/
// int main(int argc, char* argv[])
int __cdecl wmain(int argc, __in_ecount(argc) WCHAR* argv[])
{
  process_args(argc, argv);

  /************************/
  // set up logging
  /************************/
  // std::wstring output_csv = L"hvmagent.csv";
  FILE* output_fp = nullptr;
  if (!output_csv.empty())
  {
    output_fp = _wfopen(output_csv.c_str(), L"w+");
    ASSERT(output_fp != NULL);
    if (FIXED_BUFFER_MODE)
      fprintf(output_fp,
          "iteration,time_sec,idle_cores,hvm_cores,hvm_mask,hypercall_time_us,busy_mask,primary_busy_cores\n");
    else
      fprintf(output_fp,
          "iteration,time_sec,idle_cores,hvm_cores,hvm_mask,hypercall_time_us,busy_mask,primary_busy_cores,min,max,avg,"
          "stddev,med,pred_peak,upper_bound,cpu_max,overpredicted,safeguard\n");

    fflush(output_fp);
  }

  CycleCounter timer;
  timer.Start();

  /************************/
  // initialize HVMagent
  /************************/
  int totalPrimaryCores, delay;
  delay = 1000;  // 1ms = 1000us

  int fixed_buffer_sleep_ms = 1;

  int debug_count = 0;
  if (DEBUG)
    std::cout << ">>> debug enabled" << endl;

  int TIMING = 0;  // std::stoi(argv[2]);
  if (TIMING)
    std::cout << ">>> timing enabled" << endl;
  auto start = high_resolution_clock::now();
  auto stop = high_resolution_clock::now();
  auto duration = duration_cast<microseconds>(stop - start);
  auto us_elapsed = duration_cast<microseconds>(stop - start);
  // auto time_data_collection, time_feature_computation, time_model_udpate, time_model_inference, time_cpugroup_update;

  /*
    if (argc >= 4)
  {
    IXED_BUFFER_MODE = 1;
    bufferSize = std::stoi(argv[3]);
    std::cout << "bufferSize=" << bufferSize << endl;
  }
  */

  // 1: udpate cpu group every 2ms (make a predicition every 2ms)
  // 2: update cpu group every 2ms + safeguard (make a prediction every 2ms, if underprediction give all cores to
  // primary for next interval)
  // 3: whenever buffer runs out (made an under-prediction) & it has been 2ms since last cpu
  // affinity update --> make a new predicition to update cpu affinity
  // 4: whenever buffer runs out (made an under-prediction) --> give all cores to primary & wait for 2 ms then make a
  // new prediction
  // int LEARNING_MODE = std::stoi(argv[4]);

  std::cout << "initing agent" << endl;
  delay = 1000;
  totalPrimaryCores = 6;
  ASSERT(HVMAgent_Init(bufferSize, totalPrimaryCores, delay) == S_OK);
  std::cout << "" << endl;
  std::cout << "HVMAgent initialized" << endl;
  std::cout << "" << endl;

  std::wstring primaryName = L"LatSensitive";
  ASSERT(SUCCEEDED(HVMAgent_PinPrimary(primaryName, totalPrimaryCores)));
  std::cout << "Pinned primary vm to " << totalPrimaryCores << "cores" << endl;

  // read server CPU info from HVMagent
  int totalCores = HVMAgent_GetPhysicalCoreCount();  // # physical cores on the whole server (min root included)
  UINT32 minRootCores = HVMAgent_GetMinRootLPs();    // # cores for min root

  UINT64 rawBusyMask, busyMask, hvmMask, busyMaskPrimary, nonIdleMask;
  int busyMaskCount, busyMaskAllCount;
  int numCoresBusyPrimary;
  int idleCoreCount;
  int numCoresBusy;
  int numCoresBusyAll;
  int current_buffer_size;

  totalPrimaryCores = 6;  // totalCores - minRootCores;  // max #cores used by primary =28-4=24 (including minHvmCores)
  std::cout << "totalCores " << totalCores << endl;
  std::cout << "minRootCores " << minRootCores << endl;
  std::cout << "totalPrimaryCores " << totalPrimaryCores << endl;

  int maxHvmCores = 12;  // 6+6 (# of non-min-root cores)
  for (UINT32 i = 1; i <= maxHvmCores; i++)
  {
    hvmMasks[i] = HVMAgent_GenerateCoreAffinityFromBack(i);  // 1111110000000011000000000000 (8cores for hvm)
    // hvmMasks[i] = HVMAgent_GenerateMaskFromBack(i);
    printf("Created bully mask with %d cores: %x\n", i, hvmMasks[i]);
  }

  UINT64 minRootMask = HVMAgent_GetMinRootMask();  // 0000001111111100000011111111 (16 cores for minroot)

  // create CPU group for HVM
  GUID hvmGuid;
  // CLSIDFromString(L"9123CF97-AD5D-46F2-BFA3-F36950100051", (LPCLSID)&hvmGuid);
  int minHvmCores = 6;
  int numHvmCores = minHvmCores;  // min guaranteed # cores for HVM
  int numPrimaryCores = 6;        // totalPrimaryCores - numHvmCores;  // max # cores given to primary
  int busyPrimaryCores;
  ASSERT(HVMAgent_CreateCpuGroup(&hvmGuid, numHvmCores) == S_OK);  // create a cpu group for HVM
  ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroup(hvmGuid)));
  std::cout << "" << endl;
  std::cout << "CPUgroup for HVM created" << endl;
  std::cout << "" << endl;

  ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, minHvmCores)));
  std::cout << "CPUgroup for HVM successfully updated" << endl;

  // initilize vw features
  int size = 200;          // 10 * 1000 / 50 = 200 readings per 10ms
  vector<int> cpu_busy_a;  // num of busy cpu cores of primary vm --> reading every <1us
  vector<int> cpu_busy_b;
  vector<int> time_b;

  int count = 0;
  int sum = 0;
  int max = 0;
  int min = totalCores;
  int med = 0;
  float stddev;
  float avg;
  string feature;
  string feature_old;
  int pred = 0;
  int overpredicted = 0;
  int cost;
  int first_window = 1;

  int safeguard = 0;

  /************************/
  // initialize VW learning agent
  /************************/
  auto vw = VW::initialize("--csoaa " + to_string(totalPrimaryCores) + " --power_t 0 -l 0.1");
  string vwLabel, vwFeature, vwMsg;
  example* ex;
  example* ex_pred;
  // int sleep_ms = 0.05;  // 0.05ms = 50us

  auto start_1min = high_resolution_clock::now();
  int PRIMARY_ALONE = 0;

  while (1)
  {
    debug_count++;
    if (DEBUG & debug_count > 6)
      break;

    if (PRIMARY_ALONE)
    {
      auto stop_1min = high_resolution_clock::now();
      auto duration_us = (duration_cast<microseconds>(stop_1min - start_1min)).count();

      busyMaskPrimary = HVMAgent_BusyMaskRaw() & ~(minRootMask) & ~(hvmMasks[numHvmCores]);
      // std::bitset<28> busyMaskAllBit(busyMaskPrimary);
      numCoresBusyPrimary = bitcount(busyMaskPrimary);

      cpu_busy_b.push_back(numCoresBusyPrimary);  // record all cpu readings
      time_b.push_back(duration_us);

      if (duration_us > 60 * 1000 * 1000)  // 1min has past
      {
        cout << "Saving logs to cpu_log.csv"
             << "\n";

        ofstream myfile;
        myfile.open("cpu_log.csv");

        for (int b = 0; b < cpu_busy_b.size(); b++) myfile << time_b[b] << "," << cpu_busy_b[b] << "\n";

        myfile.close();
        break;
      }
    }
    else if (NO_HARVESTING)
    {
      rawBusyMask = HVMAgent_BusyMaskRaw();
      busyMask = rawBusyMask & ~(minRootMask);
      busyMaskPrimary = rawBusyMask & ~(minRootMask) & ~(hvmMasks[numHvmCores]);
      // std::bitset<28> busyMaskAllBit(busyMaskPrimary);
      numCoresBusyPrimary = bitcount(busyMaskPrimary);

      nonIdleMask = rawBusyMask | minRootMask | hvmMasks[numHvmCores];
      idleCoreCount = totalCores - bitcount(nonIdleMask);

      if (output_fp)
      {
        double time = timer.ElapsedMicroseconds() / 1000000.0;
        fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,0x%x,%d\n", count, time, idleCoreCount, numHvmCores,
            hvmMasks[numHvmCores], 0, busyMask, numCoresBusyPrimary);
      }

      HVMAgent_SpinUS(delay);
    }
    else if (FIXED_BUFFER_MODE)
    {
      // std::cout << "debug3 " << endl;
      rawBusyMask = HVMAgent_BusyMaskRaw();
      busyMask = rawBusyMask & ~(minRootMask);
      busyMaskPrimary = rawBusyMask & ~(minRootMask) & ~(hvmMasks[numHvmCores]);
      // std::bitset<28> busyMaskAllBit(busyMaskPrimary);
      numCoresBusyPrimary = bitcount(busyMaskPrimary);

      nonIdleMask = rawBusyMask | minRootMask | hvmMasks[numHvmCores];
      idleCoreCount = totalCores - bitcount(nonIdleMask);

      UINT64 prevHvmCores = numHvmCores;
      numHvmCores = numHvmCores + idleCoreCount - bufferSize;

      numHvmCores = std::min(numHvmCores, maxHvmCores);  // curHvmCores + 1);
      numHvmCores = std::max(numHvmCores, minHvmCores);

      if (DEBUG)
      {
        cout << "totalCores " << totalCores << endl;
        cout << "nonIdleMask.count " << bitcount(nonIdleMask) << endl;
        cout << "idleCoreCount " << idleCoreCount << endl;
      }

      if (prevHvmCores != numHvmCores)
      {
        ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
      }

      if (output_fp)
      {
        double time = timer.ElapsedMicroseconds() / 1000000.0;
        fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,0x%x,%d\n", count, time, idleCoreCount, numHvmCores,
            hvmMasks[numHvmCores], 0, busyMask, numCoresBusyPrimary);
      }

      HVMAgent_SpinUS(delay);

      /*
      if (DEBUG)
      {
        std::cout << "hvmBit " << hvmBit << endl;
        std::cout << "cpuBusyBit " << cpuBusyBit << endl;
        std::cout << "cpuBusyBitAll " << cpuBusyBitAll << endl;
        std::cout << "numCoresBusyAll " << numCoresBusyAll << endl;
      }

      if (DEBUG)
      {
        std::cout << "numCoresBusy " << numCoresBusy << endl;
        std::cout << "minRootCores " << minRootCores << endl;
        std::cout << "numPrimaryCores " << numPrimaryCores << endl;
        std::cout << "numCoresBusyPrimary " << numCoresBusyPrimary << endl;
        std::cout << "current buffer size " << current_buffer_size << endl;
        std::cout << "numHvmCores " << numHvmCores << endl;
      }
      */
    }
    else
    {
      UINT64 prevHvmCores = numHvmCores;

      overpredicted = 1;

      sum = 0;
      size = 0;

      start = high_resolution_clock::now();

      /****** collect cpu data for 10ms ******/

      while (true)
      {
        stop = high_resolution_clock::now();
        us_elapsed = duration_cast<microseconds>(stop - start);

        if (us_elapsed.count() >= LEARNING_RATE * 1000)
        {
          if (DEBUG)
          {
            std::cout << "us_elapsed " << us_elapsed.count() << endl;
            std::cout << "# cpu readings in 2ms: " << count << endl;
          }
          break;
        }

        size++;

        // std::cout << "debug3 " << endl;
        rawBusyMask = HVMAgent_BusyMaskRaw();
        busyMask = rawBusyMask & ~(minRootMask);
        busyMaskPrimary = rawBusyMask & ~(minRootMask) & ~(hvmMasks[numHvmCores]);
        // std::bitset<28> busyMaskAllBit(busyMaskPrimary);
        numCoresBusyPrimary = bitcount(busyMaskPrimary);

        if (LEARNING_MODE == 3)
        {
          // buffer runs out (underprediction)
          if (numCoresBusyPrimary == numPrimaryCores && numPrimaryCores != totalPrimaryCores)
          {
            numHvmCores = minHvmCores;            // min guaranteed # cores for HVM
            numPrimaryCores = totalPrimaryCores;  // totalPrimaryCores - numHvmCores;  // max # cores given to primary

            if (prevHvmCores != numHvmCores)
            {
              ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
            }

            overpredicted = 0;

            /*
            if (output_fp)
            {
              double time = timer.ElapsedMicroseconds() / 1000000.0;
              fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,0x%x,%d,%d,%d,%f,%f,%d,%d,%d,%d\n", count, time, idleCoreCount,
                  numHvmCores, hvmMasks[numHvmCores], 0, busyMask, numCoresBusyPrimary, min, max, avg, stddev, med,
                  pred, numPrimaryCores, max);
            }
            */
          }
        }

        nonIdleMask = rawBusyMask | minRootMask | hvmMasks[numHvmCores];
        idleCoreCount = totalCores - bitcount(nonIdleMask);

        if (HVMAgent_IsHyperThreaded())
          numCoresBusyPrimary /= 2;

        cpu_busy_a.push_back(numCoresBusyPrimary);  // record cpu reading

        sum += numCoresBusyPrimary;
        if (numCoresBusyPrimary < min)
          min = numCoresBusyPrimary;
        if (numCoresBusyPrimary > max)
          max = numCoresBusyPrimary;
      }

      if (TIMING)
      {
        stop = high_resolution_clock::now();
        duration = duration_cast<microseconds>(stop - start);
        std::cout << duration.count() << endl;
        // time_data_collection = duration_cast<microseconds>(stop - start);
        // std::cout << "time_data_collection (us) " << duration.count() << endl;
        start = high_resolution_clock::now();
      }

      if (DEBUG)
        std::cout << size << " cpu readings collected in 2ms" << endl;

      // compute avg
      avg = 1.0 * sum / size;
      // compute stddev
      float tmp_sum = 0;
      for (int i = 0; i < size; i++) tmp_sum += (cpu_busy_a[i] - avg) * (cpu_busy_a[i] - avg);
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
        std::cout << duration.count() << endl;
        // time_feature_computation = duration_cast<microseconds>(stop - start);
        // std::cout << "time_feature_computation (us) " << duration.count() << endl;
      }

      cpu_busy_a.clear();

      if (DEBUG)
      {
        std::cout << "min=" << min << endl;
        std::cout << "max=" << max << endl;
        std::cout << "avg=" << avg << endl;
        std::cout << "stddev=" << stddev << endl;
        std::cout << "med=" << med << endl;
      }

      /****** construct example to train vw ******/
      if (first_window)
      {
        // first iteration gives all cores to primary VM
        std::cout << "Using vw learning" << endl;
        first_window = 0;
        numHvmCores = minHvmCores;  // min guaranteed # cores for HVM
        // numPrimaryCores = numPrimaryCores;
      }
      else
      {
        start = high_resolution_clock::now();
        //if (!overpredicted | pred > max | max == totalPrimaryCores)
        if (LEARNING_MODE != 3)
        {
          if (safeguard | max < numPrimaryCores | numPrimaryCores == totalPrimaryCores)
            overpredicted = 1;
          else
            overpredicted = 0;
        }
        

        if (LEARNING_MODE == 1 || overpredicted)
        {
          safeguard = 0;
          // over-prediction
          // create cost label
          vwLabel.clear();
          for (int k = 1; k < totalPrimaryCores + 1; k++)
          {
            if (k < max)
              cost = max - k + totalPrimaryCores;  // underprediction (worse --> higher cost)
            else
              cost = k - max;  // overprediction
            vwLabel += std::to_string(k) + ":" + std::to_string(cost) + " ";
          }

          // vwLabel = "1:3 2:0 3:1";
          vwMsg = vwLabel + vwFeature;  // vwLabel from current window, features generated from previous window

          // update vw model with features from previous window
          ex = VW::read_example(*vw, vwMsg.c_str());

          if (DEBUG)
            std::cout << "vwMsg to update model: " << vwMsg.c_str() << endl;
          // example* ex = VW::read_example(*vw, "1:3 2:0 3:-1 |busy_cores_prev_interval min:0 max:0 avg:0 stddev:0
          // med:0");
          vw->learn(*ex);
          VW::finish_example(*vw, *ex);

          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            std::cout << duration.count() << endl;
            // time_model_udpate = duration_cast<microseconds>(stop - start);
            // std::cout << "time_model_udpate (us) " << duration.count() << endl;
            start = high_resolution_clock::now();
          }

          // construct features for prediction
          vwFeature = "|busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +
              " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
          if (DEBUG)
            std::cout << "vwFeature: " << vwFeature.c_str() << endl;
          ex_pred = VW::read_example(*vw, vwFeature.c_str());

          // get prediction --> # cores to primary VM
          vw->predict(*ex_pred);
          pred = VW::get_cost_sensitive_prediction(ex_pred);
          if (DEBUG)
            std::cout << "pred = " << pred << endl;
          VW::finish_example(*vw, *ex_pred);
          // numPrimaryCores = std::min(std::max(pred, max + 1), totalPrimaryCores);
          numPrimaryCores = std::min(pred, totalPrimaryCores);

          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            std::cout << duration.count() << endl;
            // time_model_inference = duration_cast<microseconds>(stop - start);
            // std::cout << "time_model_inference (us) " << duration.count() << endl;
            start = high_resolution_clock::now();
          }

          // update cpu group using prediction
          numHvmCores = std::max(minHvmCores, maxHvmCores - numPrimaryCores);  // #cores to give to HVM

          if (TIMING)
          {
            stop = high_resolution_clock::now();
            duration = duration_cast<microseconds>(stop - start);
            std::cout << duration.count() << endl;
            // time_cpugroup_update = duration_cast<microseconds>(stop - start);
            // std::cout << "time_cpugroup_update (us) " << duration.count() << endl;
          }
        }
        else
        {
          // under-prediction
          pred = 0;
          if (DEBUG)
            std::cout << "UNDER-PREDICTION" << endl;
          numHvmCores = minHvmCores;            // min guaranteed # cores for HVM
          numPrimaryCores = totalPrimaryCores;  // totalPrimaryCores - numHvmCores;  // max # cores given to primary
          safeguard = 1;
        }
      }

      if (DEBUG)
      {
        std::cout << "numPrimaryCores = " << numPrimaryCores << endl;
        std::cout << "numHvmCores = " << numHvmCores << endl;
      }

      if (prevHvmCores != numHvmCores)
      {
        ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
      }

      if (output_fp)
      {
        double time = timer.ElapsedMicroseconds() / 1000000.0;
        fprintf(output_fp, "%d,%.3lf,%d,%d,0x%x,%d,0x%x,%d,%d,%d,%f,%f,%d,%d,%d,%d,%d,%d\n", count, time, idleCoreCount,
            numHvmCores, hvmMasks[numHvmCores], 0, busyMask, numCoresBusyPrimary, min, max, avg, stddev, med, pred,
            numPrimaryCores, max, overpredicted, safeguard);
      }
    }
  }

  return 0;
}
