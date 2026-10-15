#include "pch/core_pch.h"
#include "cantordust/universe.h"
#include "cantordust/scalar_types.h"
#include "cantordust/constants.h"

#include "math/cube.h"
#include "math/color.h"

#include <iomanip>
#include <sstream>

namespace cantordust
{

using std::sqrt;
namespace
{
std::string HexAddress( const u64 address )
{
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << address;
    return stream.str();
}

std::string EscapeXml( const std::string& input )
{
    std::string escaped;
    escaped.reserve( input.size() );

    for ( const unsigned char c : input )
    {
        switch ( c )
        {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default:
            if ( c >= 32u || c == '\t' || c == '\n' || c == '\r' )
                escaped.push_back( static_cast< char >( c ) );

            break;
        }
    }

    return escaped;
}

std::string EscapeDot( const std::string& input )
{
    std::string escaped;
    escaped.reserve( input.size() );

    for ( const unsigned char c : input )
    {
        if ( c == '"' || c == '\\' )
            escaped += '\\';

        if ( c == '\n' || c == '\r' )
            escaped += "\\n";

        else if ( c >= 32u || c == '\t' )
            escaped.push_back( static_cast< char >( c ) );
    }

    return escaped;
}

std::string Rgb( const u32 color )
{
    std::ostringstream stream;
    stream << '#' << std::uppercase << std::hex << std::setfill( '0' )
        << std::setw( 6 ) << ( color & 0xFFFFFFu );

    return stream.str();
}

u32 RouteColor( const UniverseGraph_s& graph,
    const u8 mask, const u32 defaultColor )
{
    if ( !graph.multiPathMode || mask == 0u )
        return defaultColor;

    if ( graph.activePathRoute < 5u &&
         ( mask & ( 1u << graph.activePathRoute ) ) != 0u )
    {
        return CANTORDUST_UNIVERSE_ROUTE_COLORS[ graph.activePathRoute ];
    }

    for ( unsigned int i = 0; i < 5u; ++i )
        if ( ( mask & ( 1u << i ) ) != 0u )
            return CANTORDUST_UNIVERSE_ROUTE_COLORS[ i ];

    return defaultColor;
}

struct Projected_s
{
    f64 x = 0;
    f64 y = 0;
    f32 depth = 0;
    f64 radius = 0;
    bool valid = false;
};

} // namespace

std::string Universe_ExportDot( const UniverseGraph_s& graph )
{
    std::ostringstream out;
    out << "digraph \"Cantor Dust\" {\n"
        << "  graph [label=\"Cantor Dust Function Universe\", labelloc=t, "
        << "overlap=false, splines=true];\n"
        << "  node [shape=ellipse, style=filled, fontname=\"Consolas\"];\n";

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];
        const bool active = graph.multiPathMode &&
            graph.activePathRoute < 5u &&
            ( node.pathMask & ( 1u << graph.activePathRoute ) ) != 0u;

        const std::string name = node.name.empty() ? HexAddress( node.address )
            : node.name;

        out << "  n" << i << " [label=\"" << EscapeDot( name )
            << "\\n" << HexAddress( node.address ) << "\", address=\""
            << HexAddress( node.address ) << "\", incoming=" << node.incoming
            << ", outgoing=" << node.outgoing << ", kind=\""
            << ( node.kind == UNK_COMMUNITY ? "community" :
                 node.kind == UNK_GLOBAL ? "global" : "function" )
            << "\", community_size=" << node.communitySize
            << ", path_mask=" << static_cast< unsigned int >( node.pathMask )
            << ", fillcolor=\"" << Rgb( RouteColor( graph, node.pathMask,
                0xFF728BA9u ) ) << "\", fontcolor=\"#101820\"";

        if ( active )
            out << ", penwidth=3";

        out << "];\n";
    }

    for ( const UniverseLink_s& link : graph.links )
    {
        if ( link.caller >= graph.nodes.size() ||
             link.callee >= graph.nodes.size() )
        {
            continue;
        }

        const bool active = graph.multiPathMode &&
            graph.activePathRoute < 5u &&
            ( link.pathMask & ( 1u << graph.activePathRoute ) ) != 0u;

        out << "  n" << link.caller << " -> n" << link.callee
            << " [label=\"" << link.count << "\", count=" << link.count
            << ", path_mask=" << static_cast< unsigned int >( link.pathMask )
            << ", color=\"" << Rgb( RouteColor( graph, link.pathMask,
                0xFF879BB6u ) ) << "\", penwidth=" << ( active ? 3 : 1 )
            << ", callsites=\"";

        for ( usize i = 0; i < link.callSites.size(); ++i )
        {
            if ( i != 0 )
                out << ',';

            out << HexAddress( link.callSites[ i ] );
        }

        out << "\"];\n";
    }

    out << "}\n";
    return out.str();
}

std::string Universe_ExportSvg( const UniverseGraph_s& graph,
    const CubeCamera_s& camera, const UniverseView_s& view,
    const UniverseRenderStyle_s& style, const Palette_e palette,
    const bool allLinks, const usize selectedIndex,
    const unsigned int width, const unsigned int height )
{
    if ( width < 128u || height < 128u || width > 8192u || height > 8192u )
        return {};

    const Palette_s& colors = Palette_Get( palette );
    const CubeTransform_s transform = Math_CubeTransform( camera );
    const f32 frame = Universe_FrameScale( graph, style.universeSpacing ) *
        style.focusScale;

    const f32 panX = std::clamp( view.panX, -32.0f, 32.0f );
    const f32 panY = std::clamp( view.panY, -32.0f, 32.0f );
    const f32 panZ = std::clamp( view.panZ, -32.0f, 32.0f );
    const f32 scale = static_cast< f32 >( std::min( width, height ) ) * 0.94f;
    // Same projection/framing as the native CPU renderer. Freeze the pulse
    // at its vivid color for a readable, deterministic vector snapshot.
    const std::vector< f32 > nodeRadii = Universe_NodeRadii( graph, style );
    std::vector< Projected_s > points( graph.nodes.size() );

    for ( usize i = 0; i < graph.nodes.size(); ++i )
    {
        const UniverseNode_s& node = graph.nodes[ i ];
        const CubeProjection_s projection = Math_CubeProject(
            ( node.x - panX ) * frame, ( node.y - panY ) * frame,
            ( node.z - panZ ) * frame, transform );

        const f64 x = width * 0.5 + static_cast< f64 >( projection.x ) * scale;
        const f64 y = height * 0.5 - static_cast< f64 >( projection.y ) * scale;
        const f32 denom = CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE -
            CANTORDUST_CAMERA_PERSPECTIVE_DEPTH_WEIGHT * projection.depth;

        if ( !std::isfinite( x ) || !std::isfinite( y ) ||
             std::abs( x ) > 1000000.0 || std::abs( y ) > 1000000.0 ||
             !std::isfinite( denom ) || denom <= 0.0f )
        {
            continue;
        }

        const f32 depthScale = std::clamp(
            CANTORDUST_CAMERA_PERSPECTIVE_DISTANCE / denom, 0.38f, 1.85f );
        const f64 radius = nodeRadii[ i ] *
            std::clamp( sqrt( std::max( camera.zoom, 0.0f ) ), 0.70f, 1.65f ) *
            depthScale;

        points[ i ] = { x, y, projection.depth, radius, true };
    }

    std::ostringstream out;
    out << std::fixed << std::setprecision( 2 );
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << width
        << "\" height=\"" << height << "\" viewBox=\"0 0 " << width
        << ' ' << height << "\">\n"
        << "  <title>Cantor Dust Function Universe</title>\n"
        << "  <rect width=\"100%\" height=\"100%\" fill=\""
        << Rgb( colors.background3d ) << "\"/>\n"
        << "  <!-- True vector shapes; all directions are retained. -->\n";

    std::vector< usize > links;

    for ( usize i = 0; i < graph.links.size(); ++i )
    {
        const UniverseLink_s& link = graph.links[ i ];

        if ( link.caller >= points.size() || link.callee >= points.size() ||
             !points[ link.caller ].valid || !points[ link.callee ].valid )
        {
            continue;
        }

        if ( allLinks || graph.pathMode || graph.focusIndex != CANTORDUST_NO_SOURCE ||
             ( selectedIndex < graph.nodes.size() &&
               ( link.caller == selectedIndex || link.callee == selectedIndex ) ) )
        {
            links.push_back( i );
        }
    }

    std::sort( links.begin(), links.end(), [&]( usize a, usize b ) {
        const UniverseLink_s& lhs = graph.links[ a ];
        const UniverseLink_s& rhs = graph.links[ b ];
        return points[ lhs.caller ].depth + points[ lhs.callee ].depth <
            points[ rhs.caller ].depth + points[ rhs.callee ].depth;
    } );
    out << "  <g fill=\"none\" stroke-linecap=\"round\">\n";

    for ( const usize index : links )
    {
        const UniverseLink_s& link = graph.links[ index ];
        const Projected_s& from = points[ link.caller ];
        const Projected_s& to = points[ link.callee ];

        if ( from.x == to.x && from.y == to.y )
            continue;
        // Shorten the arrow to the sphere edge so links remain directional.
        const f64 dx = to.x - from.x;
        const f64 dy = to.y - from.y;
        const f64 length = std::hypot( dx, dy );

        if ( length < from.radius + to.radius + 2.0 )
            continue;

        const f64 start = ( from.radius + 1.0 ) / length;
        const f64 end = ( to.radius + 1.0 ) / length;
        const bool active = graph.multiPathMode && graph.activePathRoute < 5u &&
            ( link.pathMask & ( 1u << graph.activePathRoute ) ) != 0u;

        const std::string color = Rgb( RouteColor( graph, link.pathMask,
            colors.histogram ) );

        const f64 opacity = graph.multiPathMode && !active ? 0.52 : 0.87;
        const f64 endX = to.x - dx * end;
        const f64 endY = to.y - dy * end;
        out << "    <path d=\"M" << from.x + dx * start << ' ' << from.y + dy * start
            << " L" << endX << ' ' << endY
            << "\" stroke=\"" << color << "\" stroke-width=\""
            << ( graph.pathMode ? ( active ? 3.5 : 1.6 ) : 1.6 )
            << "\" opacity=\"" << opacity << "\"><title>"
            << EscapeXml( graph.nodes[ link.caller ].name ) << " → "
            << EscapeXml( graph.nodes[ link.callee ].name ) << " ("
            << link.count << " calls)</title></path>\n";
        // SVG 1.1 compatible arrowhead, without context-stroke markers.
        const f64 ux = dx / length;
        const f64 uy = dy / length;
        const f64 arrowSize = graph.pathMode && active ? 7.0 : 5.0;
        const f64 baseX = endX - arrowSize * ux;
        const f64 baseY = endY - arrowSize * uy;
        out << "    <path d=\"M" << endX << ' ' << endY << " L"
            << baseX - arrowSize * 0.44 * uy << ' '
            << baseY + arrowSize * 0.44 * ux << " L"
            << baseX + arrowSize * 0.44 * uy << ' '
            << baseY - arrowSize * 0.44 * ux
            << " Z\" fill=\"" << color << "\" fill-opacity=\""
            << opacity << "\"/>\n";
    }

    out << "  </g>\n";
    std::vector< usize > order( points.size() );
    std::iota( order.begin(), order.end(), 0 );
    std::sort( order.begin(), order.end(), [&]( usize a, usize b ) {
        return points[ a ].depth < points[ b ].depth;
    } );

    const bool labels = graph.nodes.size() <= 96u;

    for ( const usize i : order )
    {
        const Projected_s& p = points[ i ];

        if ( !p.valid || p.x + p.radius < 0 || p.y + p.radius < 0 ||
             p.x - p.radius > width || p.y - p.radius > height )
        {
            continue;
        }

        const UniverseNode_s& node = graph.nodes[ i ];
        const bool active = graph.multiPathMode && graph.activePathRoute < 5u &&
            ( node.pathMask & ( 1u << graph.activePathRoute ) ) != 0u;

        u32 defaultColor = Universe_HeatColor( graph, i,
            palette, style );

        if ( graph.communityMode && node.kind == UNK_COMMUNITY )
        {
            const u32 bands[] = {
                colors.high, colors.printable, colors.full, colors.low };

            defaultColor = Math_BlendColor( defaultColor,
                bands[ node.address % 4u ], 0.48 );
        }

        if ( graph.pathMode && !graph.multiPathMode )
            defaultColor = i == 0 ? CANTORDUST_UNIVERSE_PATH_START_COLOR :
                i + 1 == graph.nodes.size() ? CANTORDUST_UNIVERSE_PATH_END_COLOR
                    : CANTORDUST_UNIVERSE_PATH_MIDDLE_COLOR;

        const std::string color = Rgb( RouteColor( graph, node.pathMask,
            defaultColor ) );

        out << "  <g><title>" << EscapeXml( node.name ) << " ("
            << HexAddress( node.address ) << ")</title>\n";

        if ( active || i == selectedIndex || i == graph.focusIndex )
        {
            const std::string outline = i == selectedIndex
                ? Rgb( colors.selection ) : color;

            out << "    <circle cx=\"" << p.x << "\" cy=\"" << p.y
                << "\" r=\"" << p.radius + 3.5
                << "\" fill=\"none\" stroke=\"" << outline
                << "\" stroke-opacity=\"0.60\" stroke-width=\"2\"/>\n";
        }

        out << "    <circle cx=\"" << p.x << "\" cy=\"" << p.y
            << "\" r=\"" << std::max( 2.0, p.radius )
            << "\" fill=\"" << color
            << "\" fill-opacity=\"0.9\" stroke=\"" << color
            << "\" stroke-width=\"1\"/>\n";

        if ( labels || active || i == graph.focusIndex )
            out << "    <text x=\"" << p.x << "\" y=\""
                << p.y + p.radius + 14.0 << "\" fill=\""
                << Rgb( colors.high )
                << "\" font-family=\"sans-serif\" font-size=\"11\" "
                << "text-anchor=\"middle\">" << EscapeXml( node.name.empty()
                    ? HexAddress( node.address ) : node.name ) << "</text>\n";

        out << "  </g>\n";
    }

    out << "</svg>\n";
    return out.str();
}

} // namespace cantordust
