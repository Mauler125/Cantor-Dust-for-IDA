#include "cantordust/universe.h"
#include "cantordust/cube.h"
#include "cantordust/types.h"

#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

using namespace cantordust;

// The hot graph-link array should fit in 72 bytes on the usual 64-bit
// std::vector layout. Other standard library debug layouts remain supported.
static_assert( sizeof( void* ) != 8u ||
    sizeof( std::vector< u64 > ) != 24u ||
    sizeof( UniverseLink_s ) == 72u,
    "Unexpected graph-link padding on the 64-bit release layout" );

static_assert( sizeof( void* ) != 8u ||
    sizeof( std::vector< u64 > ) != 24u ||
    offsetof( UniverseLink_s, pathMask ) == 12u,
    "The route mask must occupy the gap before the vectors" );

int main()
{
    // Preserve the default and aggregate-initialized link metadata.
    const UniverseLink_s link{ 2u, 3u, 5u, 4u, { 0x1000, 0x1010 } };
    assert( link.caller == 2u && link.callee == 3u && link.count == 5u );
    assert( link.pathMask == 4u );
    assert( link.callSites.size() == 2u );
    assert( link.callSites[ 0 ] == 0x1000 );
    assert( link.targetAddresses.empty() );
    assert( link.fieldCount == 0u && link.renderWeight == 0u );

    const UniverseLink_s empty;
    assert( empty.pathMask == 0u && empty.callSites.empty() );

    std::cout << "Graph link: " << sizeof( UniverseLink_s )
        << " bytes; graph node: " << sizeof( UniverseNode_s )
        << "; cube point: " << sizeof( CubePoint_s )
        << "; byte cell: " << sizeof( ByteCell_s ) << '\n';
}
