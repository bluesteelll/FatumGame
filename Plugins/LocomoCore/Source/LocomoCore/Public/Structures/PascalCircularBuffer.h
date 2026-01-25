//Hey jake, why isn't this templated?
//Well, if you need it for anything that would need a template, use array, varray, vector, or a circular buffer class. This is VERY SPECIFICALLY a single sort of thing
//which is to say a pascal-style array used as a circular buffer where the first element is a counter, we do not initialize, and we do not maintain an allocator or grow
//because we cannot spare the data member for it. Instead, we just throw your shit on the floor angrily. You get one alert about this, if it can be called an alert.

//this exists specifically for use in sketches and lsh applications, as well as cases where we allocate a very large number of very small arrays and the target size of
//each small array can be known with a high degree of accuracy. This is actually quite a lot of cases. 


#pragma once

// ReSharper disable once CppClassNeedsConstructorBecauseOfUninitializedMember
//it's intentional.
constexpr static char FTOPAYRESPECTS = 0x07;
class FPascally_15
{
public:
	uint8_t count = 0;
	uint8_t cycle = 0;
	// ReSharper disable once CppUninitializedNonStaticDataMember
	uint16_t block[15]; // we don't need to init. we trade two bytes for this. it's a bad deal but hey.
	// ReSharper disable once CppPossiblyUninitializedMember
	FPascally_15()
		: count(0),
		  cycle(0)
	{
	}

	bool push_back(uint16_t val)
	{
		if (count < 16u)
		{
			block[count++] = val;
			return true;
		}
		else
		{
			count=0; // this DOES EXACTLY WHAT YOU THINK!
			++cycle;
			return false;
		}
	}
};

// ReSharper disable once CppClassNeedsConstructorBecauseOfUninitializedMember
class FPascally_31
{
public:
	uint8_t count = 0;
	uint8_t cycle = 0;
	// ReSharper disable once CppUninitializedNonStaticDataMember
	uint16_t block[31]; // we don't need to init. we trade two bytes for this. it's a bad deal but hey.

	bool push_back(uint16_t val)
	{
		if (count < 32u)
		{
			block[count++] = val;
			return true;
		}
		else
		{
			++cycle;
			return false;
		}
	}

	FPascally_31()
	: count(0),
	  cycle(0)
	{
	}
};

// ReSharper disable once CppClassNeedsConstructorBecauseOfUninitializedMember
class FPascally_63
{
public:
	uint8_t count = 0;
	uint8_t cycle = 0;
	// ReSharper disable once CppUninitializedNonStaticDataMember
	uint16_t block[63]; // we don't need to init. we trade two bytes for this. it's a bad deal but hey.

	bool push_back(uint16_t val)
	{
		if (count < 64u)
		{
			block[count++] = val;
			return true;
		}
		else
		{
			count=0; // this DOES EXACTLY WHAT YOU THINK!
			++cycle;
			return false;
		}
	}
};