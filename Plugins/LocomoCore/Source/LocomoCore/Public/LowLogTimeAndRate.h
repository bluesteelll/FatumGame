#pragma once

#include <chrono>

#include "CompileTimeStrings.h"


//Simple call-cost averaging logger for use in application cases where you can still hit the time function.
//To extend for RT operations, simply call your shadow now, frame count, or comparable instead of the system timer.
//This will generate one timer per thread. Currently, we don't disambiguate which thread is calling, we just make sure you don't crash
// so you'll need to figure out a way to make which thread is using it on your own. Many options exist.
//usage example:
/**
	while (...)
	{
		CustomTimer<"Example Loop Average"> Time;
		if (...)
		{
			CustomTimer<"Example Sub Case Average"> Time;
		}
	}
 */

/**
 * Helper classes that can be used to accumulate the average runtime of a function
 */
template<CompTimeStr id>
class FunctionAverageTimerContainer
{
public:
	static FunctionAverageTimerContainer& GetInstance()
	{
		thread_local FunctionAverageTimerContainer instance;//templating this will effectively cause a new class per id.
		return instance;
	}

	void AddTime(long long MSAdded)
	{
		TotalTime += MSAdded;
		CallCounter++;

		if (CallCounter % 1000 == 0)
		{
			auto swap = std::chrono::high_resolution_clock::now();
			auto avgrate = duration_cast<std::chrono::nanoseconds>(swap - LastRollOver).count()/1000.0;
			LastRollOver = swap;
			UE_LOG(LogTemp, Warning, TEXT("Average time for function id.%hs is nanoseconds = '%lld' at roughly one call per '%f'."), id.data, TotalTime / CallCounter, avgrate);
			TotalTime =0;
			CallCounter = 0;
		}
	}

private:
	FunctionAverageTimerContainer()
	{
		CallCounter = 0;
		LastRollOver = std::chrono::high_resolution_clock::now();
		TotalTime = 0;
	}
	long long CallCounter;
	std::chrono::time_point<std::chrono::high_resolution_clock> LastRollOver;
	long long TotalTime;

public:
	FunctionAverageTimerContainer(FunctionAverageTimerContainer const&) = delete;
	void operator=(FunctionAverageTimerContainer const&) = delete;
};

/**
 * Class used in function we want to log for. Simply declare the class on the stack at the start of the function
 * 
 */
template<CompTimeStr id>
class CustomTimer
{
public:
	CustomTimer()
	{
		start = std::chrono::high_resolution_clock::now();
	}

	~CustomTimer()
	{
		using std::chrono::high_resolution_clock;
		using std::chrono::duration_cast;
		using std::chrono::nanoseconds;
		FunctionAverageTimerContainer<id>::GetInstance().AddTime(duration_cast<nanoseconds>(high_resolution_clock::now() - start).count());
	}

private:
	std::chrono::time_point<std::chrono::steady_clock> start;
};