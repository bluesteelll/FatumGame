// FlecsBarrage Components Registration

#include "FlecsBarrageComponents.h"
#include "Properties/FlecsComponentProperties.h"

// Bridge components
REGISTER_FLECS_COMPONENT(FBarrageBody);
REGISTER_FLECS_COMPONENT(FISMRender);

// Constraint components
REGISTER_FLECS_COMPONENT(FConstraintLink);
REGISTER_FLECS_COMPONENT(FFlecsConstraintData);
