#pragma once

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

#include "vw.h"
#include "vw_exception.h"

using namespace VW::config;

enum LearningAlgo
{
  CSOAA,
  REG,
  CBANDIT
};

typedef struct Feature
{
  int max;
  int min;
  double med;
  double stddev;
  double avg;
  string vwString()
  {
    return " |busy_cores_prev_interval min:" + std::to_string(min) + " max:" + std::to_string(max) +
        " avg:" + std::to_string(avg) + " stddev:" + std::to_string(stddev) + " med:" + std::to_string(med);
  }
} Feature;

vw* vwPtr;

HRESULT modelInit(LearningAlgo algo, int maxCores, float learningRate)
{
  // initialize vw
  switch (algo)
  {
    case CSOAA:
      vwPtr = VW::initialize("--csoaa " + to_string(maxCores) + " --power_t 0 -l " + to_string(learningRate));
      std::cout << "csoaa: vw initialized with " << maxCores << " classes." << endl;
      break;
    case REG:
      vwPtr = VW::initialize(" --power_t 0 -l " + to_string(learningRate));
      std::cout << "linear regression: vw initialized" << endl;
      break;
    default:
      cout << "EXIT: No learning algorithm specified." << endl;
      return E_FAIL;
  }
  return S_OK;
}


HRESULT modelUpdate(string vwLabel, string vwFeature)
{
  string vwMsg;
  example* exPtr;

  // create vw training data
  // vwLabel = "1:3 2:0 3:-1";
  // vwFeature = " |busy_cores_prev_interval min:0 max:0 avg:0 stddev:0 med:0"
  vwMsg = vwLabel + vwFeature;                   // vwLabel from current window, features generated from previous window
  exPtr = VW::read_example(*vwPtr, vwMsg.c_str());  // update vw model with features from previous window

  // udpate vw model
  vwPtr->learn(*exPtr);
  VW::finish_example(*vwPtr, *exPtr);

  return S_OK;
}


float modelPredict(LearningAlgo algo, string vwFeature)
{
  float pred;
  example* exPtr;
  exPtr = VW::read_example(*vwPtr, vwFeature.c_str());
  vwPtr->predict(*exPtr);

  switch (algo)
  {
    case CSOAA:
      pred = VW::get_cost_sensitive_prediction(exPtr);
      break;
    case REG:
      pred = VW::get_prediction(exPtr);
      break;
    default:
      std::cout << "EXIT: No learning algorithm specified." << endl;
      std::exit(1);
  }

  VW::finish_example(*vwPtr, *exPtr);

  return pred;
}
