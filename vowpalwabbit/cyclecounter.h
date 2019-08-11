#pragma once

#include <profileapi.h>
#include <winnt.h>

// freq                 cycles --> 1 second
// freq / 1,000,000,000 cycles --> 1 nanosecond
// n cycles                    --> n / freq / 1,000,000,000
#define CYCLES_TO_NS(cycles, freq) ((cycles) / ((freq) / 1000000000.0))
#define CYCLES_TO_US(cycles, freq) ((cycles) / ((freq) / 1000000.0))
#define CYCLES_TO_MS(cycles, freq) ((cycles) / ((freq) / 1000.0))
#define NS_TO_CYCLES(ns, freq) ((ns) * ((freq) / 1000000000.0))

using namespace std;

class CycleCounter
{
    LARGE_INTEGER start;
    LARGE_INTEGER end;
    LARGE_INTEGER frequency;
    LARGE_INTEGER sum;
    UINT64 count;

public:
    CycleCounter()
    {
        count = 0;
        sum.QuadPart = 0;
        QueryPerformanceFrequency(&frequency);
    }

    void Start()
    {
        QueryPerformanceCounter(&start);
    }

    LARGE_INTEGER Now()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return now;
    }

    UINT64 NowNS()
    {
        return (UINT64) CYCLES_TO_NS(Now().QuadPart, frequency.QuadPart);
    }

    UINT64 NowUS()
    {
        return (UINT64) CYCLES_TO_US(Now().QuadPart, frequency.QuadPart);
    }

    void Add(LARGE_INTEGER &a, UINT64 nanoseconds)
    {
        a.QuadPart += (UINT64) NS_TO_CYCLES(nanoseconds, frequency.QuadPart);
    }

    void SpinUntil(LARGE_INTEGER timeout)
    {
        while (Now().QuadPart < timeout.QuadPart);
    }

    void Stop()
    {
        QueryPerformanceCounter(&end);
        sum.QuadPart += DiffInCycles().QuadPart;
        count++;
    }

    LARGE_INTEGER Total()
    {
        return sum;
    }

    UINT64 TotalMS()
    {
        return (UINT64) CYCLES_TO_MS(sum.QuadPart, frequency.QuadPart);
    }

    LARGE_INTEGER DiffInCycles()
    {
        LARGE_INTEGER diff;
        diff.QuadPart = end.QuadPart - start.QuadPart;
        return diff;
    }

    UINT64 DiffInNanoseconds()
    {
        return (UINT64) CYCLES_TO_NS(end.QuadPart - start.QuadPart, frequency.QuadPart);
    }

    UINT64 DiffInUS()
    {
        return (UINT64) CYCLES_TO_US(end.QuadPart - start.QuadPart, frequency.QuadPart);
    }

    UINT64 GetCount()
    {
        return count;
    }

    UINT64 ElapsedUS()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return (UINT64) CYCLES_TO_US(now.QuadPart - start.QuadPart, frequency.QuadPart);
    }

    double ElapsedMS()
    {
        return ElapsedUS() / 1000.0;
    }

    double ElapsedSeconds()
    {
        return ElapsedMS() / 1000.0;
    }
};