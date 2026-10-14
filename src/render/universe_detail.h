#ifndef CANTORDUST_RENDER_UNIVERSE_DETAIL_HPP
#define CANTORDUST_RENDER_UNIVERSE_DETAIL_HPP

#include "cantordust/universe.h"
#include "math/universe.h"

namespace cantordust
{

inline f32 Universe_NodeProminence( const UniverseNode_s& node )
{
    return Math_UniverseProminence( node.incoming, node.outgoing );
}

} // namespace cantordust

#endif // CANTORDUST_RENDER_UNIVERSE_DETAIL_HPP
