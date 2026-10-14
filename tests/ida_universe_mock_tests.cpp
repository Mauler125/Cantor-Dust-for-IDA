#include "ida/ida_universe.h"

#include <ida.hpp>
#include <funcs.hpp>
#include <xref.hpp>
#include <name.hpp>
#include <segment.hpp>

#ifdef NDEBUG
    #undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <string>
#include <vector>

using namespace cantordust;

static std::vector< func_t > s_functions;

const segment_t* getseg( const ea_t address )
{
    static segment_t code{}, imports{ SEG_XTRN };
    return address == 0x5000u ? &imports : &code;
}

static std::vector< std::pair< ea_t, ea_t > > s_drefs;
struct TestXref_s
{
    ea_t from = 0;
    ea_t to = 0;
    bool code = true;
    unsigned char type = fl_CN;
};
static std::vector< TestXref_s > s_xrefs;

size_t get_func_qty() { return s_functions.size(); }
func_t* getn_func( const std::size_t index )
{
    return index < s_functions.size() ? &s_functions[ index ] : nullptr;
}

func_t* get_func( const ea_t address )
{
    for ( func_t& func : s_functions )
    {
        if ( address >= func.start_ea && address < func.start_ea + 0x100 )
        {
            return &func;
        }
    }

    return nullptr;
}

ea_t get_first_dref_from( const ea_t address )
{
    for ( const auto& entry : s_drefs )
    {
        if ( entry.first == address )
            return entry.second;
    }

    return BADADDR;
}

ea_t get_next_dref_from( const ea_t address, const ea_t current )
{
    bool found = false;

    for ( const auto& entry : s_drefs )
    {
        if ( found && entry.first == address )
            return entry.second;

        if ( entry.first == address && entry.second == current )
            found = true;
    }

    return BADADDR;
}

unsigned int get_flags( const ea_t address )
{
    return address >= 0xA000 ? 1u : 0u;
}

bool is_data( const unsigned int flags ) { return flags == 1u; }
ea_t get_item_head( const ea_t address )
{
    return address >= 0xA000u && address < 0xA080u
        ? 0xA000u : address;
}

ea_t next_head( const ea_t address, const ea_t end )
{
    return address + 1 < end ? address + 1 : BADADDR;
}

static std::string s_globalName = "renamed_global_state";
std::size_t get_ea_name( qstring* const name, const ea_t address )
{
    // The first dref targets +8, whereas the node is keyed by the head.
    // IDA knows the name at the head, not at every referenced field.
    if ( address == 0xA000u )
        *name = s_globalName;

    else
        name->clear();
    return name->size();
}

std::size_t get_nice_colored_name( qstring* const name,
    const ea_t address, const int flags )
{
    assert( ( flags & GNCN_NOCOLOR ) != 0 );
    assert( ( flags & GNCN_NOSEG ) != 0 );
    *name = address == 0xA100u ? "qword_A100" : "";
    return name->size();
}

std::size_t get_func_name( qstring* const name, const ea_t address )
{
    *name = "fn_" + std::to_string( address );
    return name->size();
}

bool xrefblk_t::first_to( const ea_t target, const int )
{
    to = target;
    cursor = 0;
    return next_to();
}

bool xrefblk_t::next_to()
{
    while ( cursor < s_xrefs.size() )
    {
        const TestXref_s& next = s_xrefs[ cursor++ ];

        if ( next.to == to )
        {
            from = next.from;
            iscode = next.code;
            type = next.type;
            return true;
        }
    }

    return false;
}

int main()
{
    s_functions = { { 0x1000, 0x1100, FUNC_THUNK },
        { 0x3000, 0x3100, FUNC_LIB }, { 0x5000, 0x5100 } };

    s_xrefs = {
        { 0x1010, 0x3000, true, fl_CN },
        { 0x1011, 0x3000, true, fl_CF },
        { 0x1012, 0x3000, true, 22 },
        { 0x1013, 0x3000, false, fl_CN },
        { 0x5010, 0x3000, true, fl_CN },
        { 0x9999, 0x3000, true, fl_CN }
    };
    s_drefs = { { 0x1020, 0xA008 }, { 0x1010, 0xA000 },
        { 0x5010, 0xA000 }, { 0x1030, 0xA100 } };

    const UniverseData_s classified = Ida_ReadUniverseData();
    assert( ( classified.functions[ 0 ].flags & UFF_THUNK ) != 0 );
    assert( ( classified.functions[ 1 ].flags & UFF_LIBRARY ) != 0 );
    assert( ( classified.functions[ 2 ].flags & UFF_IMPORT ) != 0 );

    const UniverseGraph_s graph = Ida_ReadUniverseGraph();
    assert( graph.candidateCount == 3 );
    assert( graph.referenceCount == 3 );
    assert( graph.nodes.size() == 3 );
    assert( graph.nodes[ 0 ].address == 0x3000 );
    assert( graph.nodes[ 0 ].incoming == 3 );
    assert( graph.links.size() == 2 );

    const UniverseGraph_s focus = Universe_FocusCallers( graph, 0x3000u );
    assert( Universe_CallSiteAt( focus, 0x1000u, 0x3000u, 0 ) ==
        0x1010u );

    assert( Universe_CallSiteAt( focus, 0x1000u, 0x3000u, 1 ) ==
        0x1011u );

    assert( Universe_CallSiteAt( focus, 0x1000u, 0x3000u, 2 ) ==
        0x1010u );

    assert( Universe_CallSiteAt( focus, 0x5000u, 0x3000u, 0 ) ==
        0x5010u );

    const UniverseGraph_s withGlobals = Ida_ReadUniverseGraph( true );
    const auto global = std::find_if( withGlobals.nodes.begin(),
        withGlobals.nodes.end(), []( const UniverseNode_s& node ) {
            return node.address == 0xA000u && node.kind == UNK_GLOBAL;
        } );

    assert( global != withGlobals.nodes.end() );
    assert( global->incoming == 3u );
    assert( global->name == "renamed_global_state" );

    const auto unnamedGlobal = std::find_if( withGlobals.nodes.begin(),
        withGlobals.nodes.end(), []( const UniverseNode_s& node ) {
            return node.address == 0xA100u && node.kind == UNK_GLOBAL;
        } );

    assert( unnamedGlobal != withGlobals.nodes.end() );
    assert( unnamedGlobal->name == "qword_A100" );

    const UniverseData_s fields = Ida_ReadUniverseData( true );
    const UniverseGraph_s fieldGraph = Universe_BuildGraph(
        fields.functions, fields.references, 10u, 100u, 0u, true, true );

    const auto fieldNode = std::find_if( fieldGraph.nodes.begin(),
        fieldGraph.nodes.end(), []( const UniverseNode_s& node ) {
            return node.address == 0xA000u;
        } );

    assert( fieldNode != fieldGraph.nodes.end() );

    const std::size_t fieldIndex = static_cast< std::size_t >(
        std::distance( fieldGraph.nodes.begin(), fieldNode ) );

    UniverseGraph_s fieldsMode = fieldGraph;
    Universe_SetCountMode( fieldsMode, true, true );
    assert( fieldsMode.nodes[ fieldIndex ].incoming == 3u );
    Universe_SetCountMode( fieldsMode, true, false );
    assert( fieldsMode.nodes[ fieldIndex ].incoming == 2u );

    const UniverseData_s data = Ida_ReadUniverseData( true );
    // Exercise the actual IDA extraction -> inspector index path, not just
    // synthetic UniverseData. The same function should be inspectable from
    // either overview or focus views, including its precise callsites.
    const UniverseInspectorIndex_s index = Universe_BuildInspectorIndex( data );
    const UniverseInspectorResult_s inspected = Universe_InspectFunction( data, index, 0x3000u );
    assert( inspected.found );
    assert( inspected.incomingReferences == 3u );
    assert( inspected.outgoingReferences == 0u );
    assert( inspected.connections.size() == 2u );
    assert( inspected.connections[ 0 ].peerAddress == 0x1000u );
    assert( inspected.connections[ 0 ].callSites.size() == 2u );
    assert( inspected.connections[ 0 ].callSites[ 0 ] == 0x1010u );
    assert( inspected.connections[ 0 ].callSites[ 1 ] == 0x1011u );

    const UniverseInspectorResult_s inspectedGlobal = Universe_InspectFunction( data, index, 0xA000u );
    assert( inspectedGlobal.found );
    assert( inspectedGlobal.kind == UNK_GLOBAL );
    assert( inspectedGlobal.name == "renamed_global_state" );
    assert( Universe_SearchFunctions( data, "renamed_global_state", 0 ).size() == 1u );

    const std::string dot = Universe_ExportDot( withGlobals );
    assert( dot.find( "renamed_global_state" ) != std::string::npos );
    assert( dot.find( "qword_A100" ) != std::string::npos );
    assert( dot.find( "global_40960" ) == std::string::npos );
    // Renaming a global in IDA is reflected by the next extraction (Refresh).
    s_globalName = "newly_renamed_global";

    const UniverseData_s renamedData = Ida_ReadUniverseData( true );
    const auto renamed = std::find_if( renamedData.functions.begin(),
        renamedData.functions.end(), []( const UniverseFunction_s& node ) {
            return node.address == 0xA000u;
        } );

    assert( renamed != renamedData.functions.end() );
    assert( renamed->name == "newly_renamed_global" );
    assert( inspectedGlobal.incomingReferences == 3u );
    assert( inspectedGlobal.connections.size() == 2u );
    assert( inspectedGlobal.connections[ 0 ].callSites.size() == 2u );

    const UniverseGraph_s focusedGlobal = Universe_FocusCallersFromData(
        data, 0xA000u, true );

    assert( focusedGlobal.focusIndex == 0u );
    assert( focusedGlobal.nodes.front().kind == UNK_GLOBAL );
    assert( focusedGlobal.nodes.front().name == "renamed_global_state" );
    assert( focusedGlobal.nodes.front().incoming == 2u );
    assert( focusedGlobal.links.size() == 2u );
    assert( Universe_CallSiteAt( focusedGlobal, 0x1000u, 0xA000u, 0u ) ==
        0x1010u );

    assert( Universe_CallSiteAt( focusedGlobal, 0x1000u, 0xA000u, 1u ) ==
        0x1020u );

    return 0;
}
