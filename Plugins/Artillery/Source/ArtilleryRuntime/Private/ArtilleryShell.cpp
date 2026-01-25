#include "ArtilleryShell.h"

#include "FCablePackedInput.h"

float FArtilleryShell::GetStickLeftX()
{
    return FCableInputPacker::UnpackStick(MyInputActions >> 53);
}
int32_t FArtilleryShell::GetStickLeftXAsACSN()
{
    return FCableInputPacker::DebiasStick(MyInputActions >> 53);
}

float FArtilleryShell::GetStickLeftY()
{
    return FCableInputPacker::UnpackStick((MyInputActions >> 42) & 0b11111111111);
}

int32_t FArtilleryShell::GetStickLeftYAsACSN()
{
    return FCableInputPacker::DebiasStick((MyInputActions >> 42) & 0b11111111111);
}

float FArtilleryShell::GetStickRightX()
{
    return FCableInputPacker::UnpackStick((MyInputActions >> 31) & 0b11111111111);
}
int32_t FArtilleryShell::GetStickRightXAsACSN()
{
    return FCableInputPacker::FCableInputPacker::DebiasStick((MyInputActions >> 31) & 0b11111111111);
}

float FArtilleryShell::GetStickRightY()
{
    return FCableInputPacker::UnpackStick((MyInputActions >> 20) & 0b11111111111);
}

int32_t FArtilleryShell::GetStickRightYAsACSN()
{
    return FCableInputPacker::FCableInputPacker::DebiasStick((MyInputActions >> 20) & 0b11111111111);
}

// index is 0 - 19
bool FArtilleryShell::GetInputAction(uint8 inputActionIndex)
{
    return (MyInputActions >> inputActionIndex) & 0b1;
}

uint32 FArtilleryShell::GetButtonsAndEventsFlat()
{                         //0b1111 1111 1111 1111 1111 is 20 bits.
    return MyInputActions & 0b11111111111111111111;
}
/**
* 	std::bitset<11> lx;
	std::bitset<11> ly;
	std::bitset<11> rx;
	std::bitset<11> ry;
	std::bitset<20> buttons;

    sticks: 1 - 44 (1 - 11, 12 - 22, 23 - 33, 34 - 44)
    buttons: 45 - 58
    events: 59 - 64
    sticks: (64 >> 53), (64 >> 42)
    buttons: (64 >> 19) 
*/