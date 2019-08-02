#pragma once

#include <profileapi.h>
#include <winnt.h>

// freq                 cycles --> 1 second
// freq / 1,000,000,000 cycles --> 1 nanosecond
// n cycles                    --> n / freq / 1,000,000,000
#define CYCLES_TO_NS(cycles, freq) (((cycles) * 1000000000) / (freq))
#define CYCLES_TO_US(cycles, freq) (((cycles) * 1000000) / (freq))
#define CYCLES_TO_MS(cycles, freq) (((cycles) * 1000) / (freq))
#define NS_TO_CYCLES(ns, freq) (((ns) * (freq)) / 1000000000)

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

    void Add(LARGE_INTEGER &a, UINT64 nanoseconds)
    {
        a.QuadPart += NS_TO_CYCLES(nanoseconds, frequency.QuadPart);
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
        return CYCLES_TO_MS(sum.QuadPart, frequency.QuadPart);
    }

    LARGE_INTEGER DiffInCycles()
    {
        LARGE_INTEGER diff;
        diff.QuadPart = end.QuadPart - start.QuadPart;
        return diff;
    }

    UINT64 DiffInNanoseconds()
    {
        return CYCLES_TO_NS(end.QuadPart - start.QuadPart, frequency.QuadPart);
    }

    UINT64 GetCount()
    {
        return count;
    }

    UINT64 ElapsedMicroseconds()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return CYCLES_TO_NS(now.QuadPart - start.QuadPart, frequency.QuadPart) / 1000;
    }
};