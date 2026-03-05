#include "FlecsResourcePoolProfile.h"
#include "FlecsResourceTypes.h"

static_assert(static_cast<uint8>(EResourceType::MAX) == static_cast<uint8>(EResourceTypeId::MAX),
	"EResourceType and EResourceTypeId must stay in sync");
