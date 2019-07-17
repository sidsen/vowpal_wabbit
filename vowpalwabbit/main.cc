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
  int BUFFER_MODE = 0;
  delay = 100;
  HVMAgent_Init(bufferSize, totalPrimaryCores, delay);
  std::cout << "" << endl;
  std::cout << "HVMAgent initialized" << endl;
  std::cout << "" << endl;


  // read server CPU info from HVMagent
  int totalCores = HVMAgent_GetPhysicalCoreCount();    // # physical cores on the whole server (min root included) (e.g. 28)
  UINT32 minRootCores = HVMAgent_GetMinRootLPs() / 2;  // # physical core for min root (e.g. 4)
  UINT64 busyMask, hvmMask;
  int busyMaskCount;
  int numCoresBusyPrimary;

  totalPrimaryCores = totalCores - minRootCores;  // max #cores used by primary = 28-4=24
  std::cout << "totalCores " << totalCores << endl;
  std::cout << "minRootCores " << minRootCores << endl;
  std::cout << "totalPrimaryCores " << totalPrimaryCores << endl;

  bufferSize = 0;
  if (argc == 2)
  {
    BUFFER_MODE = 1;
    bufferSize = std::stoi(argv[1]);
    std::cout << "bufferSize=" << bufferSize << endl;
  }


  // create CPU group for HVM
  GUID hvmGuid;
  // CLSIDFromString(L"9123CF97-AD5D-46F2-BFA3-F36950100051", (LPCLSID)&hvmGuid);
  int numPrimaryCores = totalPrimaryCores;
  int minHvmCores = 1;                                             // min guaranteed # cores for HVM
  int numHvmCores = minHvmCores;                                   
  ASSERT(HVMAgent_CreateCpuGroup(&hvmGuid, numHvmCores) == S_OK);  // create a cpu group for HVM
  ASSERT(SUCCEEDED(HVMAgent_AssignCpuGroup(hvmGuid)));
  std::cout << "" << endl;
  std::cout << "CPUgroup for HVM created" << endl;
  std::cout << "" << endl;

  
  //while (1)
  for (int j = 0; j < 5; j++)
  {
    std::cout << "" << endl;
    std::cout << "iteration " << j << endl;

    // sleep for 1ms (change cpu group every 1ms)
    Sleep(1);

    hvmMask = HVMAgent_GenerateMaskFromBack(numHvmCores);
    busyMask = HVMAgent_BusyMaskPrimary() & (~hvmMask);  // read number of busy logical cores of all cores (excluding hvm cores) on the server
    std::bitset<64> cpuBusyBit(busyMask);              // 64 bit
    numCoresBusyPrimary = (cpuBusyBit.count() - minRootCores) / 2;  // # busy physical cores used by primary (excluding minRootCores)

    numHvmCores = std::max(minHvmCores, totalPrimaryCores - (numCoresBusyPrimary + bufferSize));  // # phsyical cores to give to HVM

    std::cout << "numCoresBusyPrimary" << numCoresBusyPrimary << endl;
    std::cout << "numHvmCores" << numHvmCores << endl;
    ASSERT(SUCCEEDED(HVMAgent_UpdateHVMCores(hvmGuid, numHvmCores)));
  }

  return 0;
}
