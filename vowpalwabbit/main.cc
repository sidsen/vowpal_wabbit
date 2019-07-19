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

/*
  optional 1st arg = buffer size
*/
int main(int argc, char* argv[])
{
  // initialize HVMagent
  int bufferSize, totalPrimaryCores, delay;
  int FIXED_BUFFER_MODE = 0;
  int fixed_buffer_sleep_ms = 1;
  if (argc == 2)
  {
    FIXED_BUFFER_MODE = 1;
    bufferSize = std::stoi(argv[1]);
    std::cout << "bufferSize=" << bufferSize << endl;
  }

  delay = 100;
  ASSERT(HVMAgent_Init(bufferSize, totalPrimaryCores, delay) == S_OK);
  std::cout << "" << endl;
  std::cout << "HVMAgent initialized" << endl;
  std::cout << "" << endl;

  // read server CPU info from HVMagent
  int totalCores = HVMAgent_GetPhysicalCoreCount();    // # physical cores on the whole server (min root included)
  UINT32 minRootCores = HVMAgent_GetMinRootLPs() / 2;  // # physical core for min root
  UINT64 busyMask, hvmMask;
  int busyMaskCount;
  int numCoresBusyPrimary;

  totalPrimaryCores = totalCores - minRootCores;  // max #cores used by primary =28-4=24 (including minHvmCores)
  std::cout << "totalCores " << totalCores << endl;
  std::cout << "minRootCores " << minRootCores << endl;
  std::cout << "totalPrimaryCores " << totalPrimaryCores << endl;

  // create CPU group for HVM
  GUID hvmGuid;
  // CLSIDFromString(L"9123CF97-AD5D-46F2-BFA3-F36950100051", (LPCLSID)&hvmGuid);
  int minHvmCores = 1;
  int numHvmCores = minHvmCores;                          // min guaranteed # cores for HVM
  int numPrimaryCores = totalPrimaryCores - numHvmCores;  // max # cores given to primary
  int busyPrimaryCores;
  ASSERT(HVMAgent_CreateCpuGroup(&hvmGuid, numHvmCores) == S_OK);  // create a cpu group for HVM
  ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroup(hvmGuid)));
  std::cout << "" << endl;
  std::cout << "CPUgroup for HVM created" << endl;
  std::cout << "" << endl;

  ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, 4)));
  std::cout << "CPUgroup for HVM successfully updated" << endl;

  // initilize vw features
  int size = 200;       // 10 * 1000 / 50 = 200 readings per 10ms
  int cpu_busy_a[200];  // num of busy cpu cores of primary vm --> read every 50 us
  int sum = 0;
  int max = 0;
  int min = totalCores;
  int med = 0;
  float stddev;
  float avg;
  string feature;
  string feature_old;
  int pred;
  int cost;
  int first_window = 1;

  // initialize vw learning agent
  auto vw = VW::initialize("--csoaa " + to_string(totalPrimaryCores) + " --power_t 0 -l 0.1");
  string vwLabel, vwFeature, vwMsg;
  example* ex;
  example* ex_pred;
  // int sleep_ms = 0.05;  // 0.05ms = 50us

  //for (int j = 0; j < 5; j++)
  //{
  //  std::cout << "" << endl;
  //  std::cout << "iteration" << j << endl;

  while(1)
  {
    if (FIXED_BUFFER_MODE)
    {
      Sleep(fixed_buffer_sleep_ms);

      hvmMask = HVMAgent_GenerateMaskFromBack(numHvmCores);
      busyMask = HVMAgent_BusyMaskPrimary() &
          (~hvmMask);  // read number of busy logical cores of all cores (excluding hvm cores) on the server
      std::bitset<64> cpuBusyBit(busyMask);  // 64 bit
      numCoresBusyPrimary = (cpuBusyBit.count() - minRootCores) / 2;  // # busy physical cores used by primary (excluding minRootCores)

      numHvmCores = std::max(minHvmCores, totalPrimaryCores - (numCoresBusyPrimary + bufferSize));  // # phsyical cores to give to HVM

      std::cout << "numCoresBusyPrimary" << numCoresBusyPrimary << endl;
      std::cout << "numHvmCores" << numHvmCores << endl;
      ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
    }
    else
    {
      sum = 0;
      /****** collect cpu data for 10ms ******/
      for (int i = 0; i < size; i++)
      {
        // TODO: sleep for 50us --> time how long the loop takes
        hvmMask = HVMAgent_GenerateMaskFromBack(numHvmCores);
        busyMask = HVMAgent_BusyMaskPrimary() & (~hvmMask);                        // i % total_cpu; // read number of busy cores of all cores on the server
        std::bitset<64> cpuBusyBit(busyMask);  // 64 bit
        numCoresBusyPrimary = (cpuBusyBit.count() - minRootCores) / 2;  // # busy physical cores used by primary (with minRootCores subtracted)

        /*
        if (i == 1)
        {
          std::bitset<64> hvm_busy_bit(hvmMask);  // 64 bit
          std::cout << "hvmMask (LP) " << hvm_busy_bit << endl;
          std::cout << "busyMask (LP) " << cpuBusyBit << endl;
          std::cout << numCoresBusyPrimary << " busy physical cores " << endl;
        }
        */

        cpu_busy_a[i] = numCoresBusyPrimary;  // record cpu reading
        sum += numCoresBusyPrimary;

        if (numCoresBusyPrimary < min)
        {
          min = numCoresBusyPrimary;
        }
        if (numCoresBusyPrimary > max)
        {
          max = numCoresBusyPrimary;
        }
      }

      // compute avg
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
      std::sort(cpu_busy_a, cpu_busy_a + sizeof(cpu_busy_a) / sizeof(cpu_busy_a[0]));
      if (size % 2 != 0)
        med = cpu_busy_a[size / 2];
      else
        med = (cpu_busy_a[(size - 1) / 2] + cpu_busy_a[size / 2]) / 2.0;

      /*
      std::cout << "min=" << min << endl;
      std::cout << "max=" << max << endl;
      std::cout << "avg=" << avg << endl;
      std::cout << "stddev=" << stddev << endl;
      std::cout << "med=" << med << endl;
      */

      /****** construct example to train vw ******/
      if (first_window)
      {
        // first iteration gives all cores to primary VM
        first_window = 0;
        numHvmCores = minHvmCores;  // min guaranteed # cores for HVM
        numPrimaryCores = totalPrimaryCores - numHvmCores;
      }
      else
      {
        if (max < numPrimaryCores)
        {
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
          std::cout << "vwMsg to update model:" << endl;
          std::cout << vwMsg.c_str() << endl;
          // example* ex = VW::read_example(*vw, "1:3 2:0 3:-1 |busy_cores_prev_interval min:0 max:0 avg:0 stddev:0
          // med:0");
          vw->learn(*ex);
          VW::finish_example(*vw, *ex);

          // construct features for prediction
          vwFeature = "|busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +
              " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
          std::cout << "vwFeature:" << endl;
          std::cout << vwFeature.c_str() << endl;
          ex_pred = VW::read_example(*vw, vwFeature.c_str());

          // get prediction --> # cores to primary VM
          vw->predict(*ex_pred);
          pred = VW::get_cost_sensitive_prediction(ex_pred);
          std::cout << "pred = " << pred << endl;
          VW::finish_example(*vw, *ex_pred);
          numPrimaryCores = std::min(std::max(pred, max + 1), totalPrimaryCores);
          // std::cout << "numPrimaryCores = " << numPrimaryCores << endl;

          // update cpu group using prediction
          numHvmCores = std::max(minHvmCores, totalPrimaryCores - numPrimaryCores);  // #cores to give to HVM
          // std::cout << "numHvmCores = " << numHvmCores << endl;
        }
        else
        {
          // under-prediction
          numHvmCores = minHvmCores;                              // min guaranteed # cores for HVM
          int numPrimaryCores = totalPrimaryCores - numHvmCores;  // max # cores given to primary
        }
      }
      //std::cout << "numPrimaryCores = " << numPrimaryCores << endl;
      //std::cout << "numHvmCores = " << numHvmCores << endl;
      ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
    }
  }

  return 0;
}
