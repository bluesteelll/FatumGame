#include "MashFunctions.h"



FString FMMM::WhyDoIExist()
{
	return "I exist because Typehash and pointer hash both have extremely undesirable behaviors for arbitrary 32 bit and 64 bit scalar types. Despite claiming to be \"Hash functions for common types\" these dastards simply return the value of scalar types 4 bytes or less.";
}
