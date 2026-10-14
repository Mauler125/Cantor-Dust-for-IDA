#include "ida/ida_universe.h"
#include "cantordust/scalar_types.h"

// IDA SDK declarations require ida.hpp before funcs.hpp and xref.hpp.
#include <ida.hpp>
#include <funcs.hpp>
#include <xref.hpp>
#include <bytes.hpp>
#include <name.hpp>
#include <segment.hpp>
#include <unordered_set>

#include <vector>
#include <algorithm>
#include <cstdint>
#include <sstream>
#include <iomanip>

namespace cantordust
{
namespace
{
// All references into the same data item are represented by a single
// global node keyed by the item's head. Resolve that node's display name
// from the same head, not from the first referenced member/array element.
std::string Ida_GlobalDisplayName( const ea_t item, const ea_t target )
{
    qstring name;
    get_ea_name( &name, item );
    // Some referenced sub-items have their own IDA name even if the
    // containing item does not. Retain that useful name as a fallback.
    if ( name.empty() && target != item )
        get_ea_name( &name, target );

    if ( name.empty() )
    {
        // IDA can display an address even without a database name.
        // Ask IDA to format it rather than inventing a decimal global_N.
        get_nice_colored_name( &name, item, GNCN_NOCOLOR | GNCN_NOSEG );
    }

    if ( !name.empty() )
        return name.c_str();

    // Last resort for unmapped/broken names: unambiguous hexadecimal EA.
    std::ostringstream fallback;
    fallback << "0x" << std::hex << std::uppercase
             << static_cast< u64 >( item );

    return fallback.str();
}
} // namespace

UniverseData_s Ida_ReadUniverseData( const bool includeGlobals )
{
    std::vector< UniverseFunction_s > functions;
    std::vector< UniverseReference_s > references;

    const usize count = std::min< usize >(
        get_func_qty(), CANTORDUST_UNIVERSE_MAX_CANDIDATES );

    functions.reserve( count );

    for ( usize i = 0; i < count; ++i )
    {
        const func_t* const function = getn_func( i );

        if ( function == nullptr )
            continue;

        qstring name;
        get_func_name( &name, function->start_ea );

        const segment_t* const segment = getseg( function->start_ea );
        u8 flags = 0;

        if ( ( function->flags & FUNC_LIB ) != 0 )
            flags |= UFF_LIBRARY;

        if ( ( function->flags & FUNC_THUNK ) != 0 )
            flags |= UFF_THUNK;

        if ( segment != nullptr && segment->type == SEG_XTRN )
            flags |= UFF_IMPORT;

        functions.push_back({
            static_cast< u64 >( function->start_ea ),
            name.c_str(), UNK_FUNCTION, flags });
    }

    bool callTruncated = false;

    const usize callReferenceLimit = includeGlobals
        ? CANTORDUST_UNIVERSE_MAX_REFERENCES - 80000u
        : CANTORDUST_UNIVERSE_MAX_REFERENCES;

    references.reserve( std::min< usize >(
        CANTORDUST_UNIVERSE_MAX_REFERENCES, count * 8 ) );

    for ( const UniverseFunction_s& function : functions )
    {
        xrefblk_t xref;

        for ( bool ok = xref.first_to(
                  static_cast< ea_t >( function.address ), XREF_NOFLOW | XREF_CODE );

              ok; ok = xref.next_to() )
        {
            if ( references.size() >= callReferenceLimit )
            {
                callTruncated = true;
                break;
            }

            if ( !xref.iscode ||
                 ( xref.type != fl_CN && xref.type != fl_CF ) )
            {
                continue;
            }

            const func_t* const caller = get_func( xref.from );

            if ( caller == nullptr )
                continue;

            references.push_back({
                static_cast< u64 >( caller->start_ea ),
                function.address,
                static_cast< u64 >( xref.from ) });
        }

        if ( references.size() >= callReferenceLimit )
        {
            callTruncated = true;
            break;
        }
    }

    bool addedGlobalsTruncated = false;

    if ( includeGlobals )
    {
        // Scan function item heads, not raw bytes; bound work for large IDBs.
        std::unordered_set< u64 > seen;

        const usize scanLimit = 800000u;
        const usize globalLimit = 8192u;
        usize scanned = 0;
        usize addedGlobals = 0;

        for ( usize i = 0; i < count && scanned < scanLimit; ++i )
        {
            const func_t* const function = getn_func( i );

            if ( function == nullptr )
                continue;

            for ( ea_t ea = function->start_ea;
                  ea < function->end_ea && scanned < scanLimit; )
            {
                ++scanned;

                for ( ea_t target = get_first_dref_from( ea );
                      target != BADADDR;
                      target = get_next_dref_from( ea, target ) )
                {
                    const ea_t item = get_item_head( target );

                    if ( item == BADADDR || !is_data( get_flags( item ) ) ||
                         get_func( item ) != nullptr )
                    {
                        continue;
                    }

                    const u64 key =
                        static_cast< u64 >( item );

                    if ( seen.insert( key ).second )
                    {
                        if ( addedGlobals >= globalLimit )
                        {
                            addedGlobalsTruncated = true;
                            continue;
                        }

                        functions.push_back({ key,
                            Ida_GlobalDisplayName( item, target ), UNK_GLOBAL });
                        ++addedGlobals;
                    }

                    if ( references.size() < CANTORDUST_UNIVERSE_MAX_REFERENCES )
                    {
                        references.push_back({
                            static_cast< u64 >( function->start_ea ),
                            key, static_cast< u64 >( ea ),
                            static_cast< u64 >( target ) });
                    }
                }

                const ea_t next = next_head( ea, function->end_ea );

                if ( next == BADADDR || next <= ea )
                    break;

                ea = next;
            }
        }
    }

    UniverseData_s result;
    result.functions = std::move( functions );
    result.references = std::move( references );
    result.truncated = get_func_qty() > count ||
        callTruncated ||
        result.references.size() >= CANTORDUST_UNIVERSE_MAX_REFERENCES ||
        ( includeGlobals && addedGlobalsTruncated );

    return result;
}

UniverseGraph_s Ida_ReadUniverseGraph( const bool includeGlobals )
{
    const UniverseData_s data = Ida_ReadUniverseData( includeGlobals );
    UniverseGraph_s graph = Universe_BuildGraph(
        data.functions, data.references );

    graph.truncated |= data.truncated;
    return graph;
}

} // namespace cantordust
