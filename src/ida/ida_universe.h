#ifndef IDA_IDA_UNIVERSE_H
#define IDA_IDA_UNIVERSE_H

#include "cantordust/universe.h"

namespace cantordust
{

UniverseData_s Ida_ReadUniverseData( bool includeGlobals = false );
UniverseGraph_s Ida_ReadUniverseGraph( bool includeGlobals = false );

} // namespace cantordust

#endif // IDA_IDA_UNIVERSE_H
