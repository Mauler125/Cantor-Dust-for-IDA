#ifndef CANTORDUST_UNIVERSE_H
#define CANTORDUST_UNIVERSE_H


#include "cantordust/scalar_types.h"
#include "cantordust/cube.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#define CANTORDUST_UNIVERSE_MAX_FUNCTIONS 4096u
#define CANTORDUST_UNIVERSE_MAX_LINKS 20000u
#define CANTORDUST_UNIVERSE_MAX_CANDIDATES 20000u
#define CANTORDUST_UNIVERSE_MAX_REFERENCES 250000u

// Multi-hop expansion has separate budgets from the ordinary universe tiers.
#define CANTORDUST_UNIVERSE_HOP_MAX_FUNCTIONS 16384u
#define CANTORDUST_UNIVERSE_HOP_MAX_LINKS 65536u
#define CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS 8192u
#define CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS 32768u
#define CANTORDUST_UNIVERSE_HOP_MAX_FANOUT 1024u

namespace cantordust
{

// Shared graph-layout and Follow IDA halo policy.
inline constexpr f32 CANTORDUST_UNIVERSE_PATH_LAYER_SPACING = 0.43f;
inline constexpr u8 CANTORDUST_UNIVERSE_SYNC_ALPHA_MIN = 48u;
inline constexpr u8 CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX = 235u;

// Semantic route colors are stable across palettes and output formats.
// These mark different routes, not byte classifications or selections.
inline constexpr u32 CANTORDUST_UNIVERSE_ROUTE_COLORS[ 5 ] = {
    0xFF36EACDu, 0xFFFFC857u, 0xFFFD75D5u,
    0xFF7FA8FFu, 0xFFFF8660u };

inline constexpr u32 CANTORDUST_UNIVERSE_PATH_START_COLOR = 0xFF35DCEBu;
inline constexpr u32 CANTORDUST_UNIVERSE_PATH_END_COLOR = 0xFFFF5DCDu;
inline constexpr u32 CANTORDUST_UNIVERSE_PATH_MIDDLE_COLOR = 0xFFFFCB62u;

// Neutral outer-hop shading is a rendering effect, not a palette selection.
inline constexpr u32 CANTORDUST_UNIVERSE_OUTER_HOP_DIM = 0xFF090D14u;

enum UniverseNodeKind_e
{
    UNK_FUNCTION = 0,
    UNK_GLOBAL,
    UNK_COMMUNITY
};

enum UniverseFunctionFlag_e : u8
{
    UFF_IMPORT = 1u,
    UFF_THUNK = 2u,
    UFF_LIBRARY = 4u
};

struct UniverseFunction_s
{
    u64 address = 0;
    std::string name;
    UniverseNodeKind_e kind = UNK_FUNCTION;
    u8 flags = 0; // IDA classification; 0 for unclassified functions/globals
};

struct UniverseReference_s
{
    u64 caller = 0;
    u64 callee = 0;
    u64 callSite = 0;
    // Actual addressed field; 0 falls back to callee for old call refs.
    u64 targetAddress = 0;
};

struct UniverseData_s
{
    std::vector< UniverseFunction_s > functions;
    std::vector< UniverseReference_s > references;
    bool truncated = false;
};

// Indexed full-dataset metadata for hover/selection. The index stores only
// reference offsets so no IDA SDK queries or rescans are needed per hover.
struct UniverseInspectorNode_s
{
    u64 address = 0;
    std::string name;
    UniverseNodeKind_e kind = UNK_FUNCTION;
    std::vector< usize > incomingReferences;
    std::vector< usize > outgoingReferences;
};

struct UniverseInspectorIndex_s
{
    std::unordered_map< u64, UniverseInspectorNode_s > nodes;
};

struct UniverseInspectorConnection_s
{
    u64 peerAddress = 0;
    std::string peerName;
    UniverseNodeKind_e peerKind = UNK_FUNCTION;
    bool incoming = false;
    std::vector< u64 > callSites;
};

struct UniverseInspectorResult_s
{
    bool found = false;
    u64 address = 0;
    std::string name;
    UniverseNodeKind_e kind = UNK_FUNCTION;
    usize incomingReferences = 0;
    usize outgoingReferences = 0;
    std::vector< UniverseInspectorConnection_s > connections;
};

UniverseInspectorIndex_s Universe_BuildInspectorIndex( const UniverseData_s& data );
UniverseInspectorResult_s Universe_InspectFunction(
    const UniverseData_s& data, const UniverseInspectorIndex_s& index,
    u64 address );

struct UniverseNode_s
{
    u64 address = 0;
    std::string name;
    u32 incoming = 0;
    u32 outgoing = 0;
    u32 focusCalls = 0;
    f32 x = 0;
    f32 y = 0;
    f32 z = 0;
    UniverseNodeKind_e kind = UNK_FUNCTION;
    // Directed focus: 1 = caller, 2 = callee, 3 = both.
    u8 focusDirection = 0;
    // 0 = center/overview, 1..10 = distance from the focused center.
    u8 focusDepth = 0;
    // Parent used to discover this node (for multi-hop callsite navigation).
    usize focusParent = CANTORDUST_NO_SOURCE;
    u8 pathMask = 0;
    u32 communitySize = 1;
};

struct UniverseLink_s
{
    u32 caller = 0;
    u32 callee = 0;
    u32 count = 0;
    std::vector< u64 > callSites;
    std::vector< u64 > targetAddresses;
    u32 fieldCount = 0;
    u32 renderWeight = 0;
    u8 pathMask = 0;
};

struct UniverseGraph_s
{
    std::vector< UniverseNode_s > nodes;
    std::vector< UniverseLink_s > links;
    usize candidateCount = 0;
    usize referenceCount = 0;
    usize focusIndex = CANTORDUST_NO_SOURCE;
    bool truncated = false;
    bool distinctCountMode = false;
    bool globalFieldMode = false;
    bool adaptiveLayout = false;
    bool pathMode = false;
    bool multiPathMode = false;
    bool communityMode = false;
    u8 activePathRoute = 0;
    u32 absoluteHeatMaximum = 0;
    // Focus exploration diagnostics (not used by overview/path graphs).
    u8 focusDepthRequested = 0;
    u8 focusDepthReached = 0;
    bool focusNodeLimitReached = false;
    bool focusLinkLimitReached = false;
    bool focusFanoutLimitReached = false;
};

// Collapse strongly interconnected functions in a bounded visible tier.
// Member addresses are retained for the UI's drill-down menu.
struct UniverseCommunityResult_s
{
    UniverseGraph_s graph;
    std::unordered_map< u64, std::vector< u64 > > members;
};
UniverseCommunityResult_s Universe_BuildCommunities( const UniverseGraph_s& graph );

// If an index was invalidated but a selected node is still visible, recover
// the retained callsites already stored on its rendered links. Unlike the
// full-dataset inspector this reflects *only* visible graph connections.
UniverseInspectorResult_s Universe_InspectGraphFunction(
    const UniverseGraph_s& graph, u64 address );

struct UniverseFilterOptions_s
{
    bool hideImports = false;
    bool hideThunks = false;
    bool hideLibraries = false;
    bool hideGlobals = false;
    u32 minimumIncoming = 0;
    u32 minimumOutgoing = 0;
};

// Hide nodes and their incident links using the full extracted dataset.
// Focus centers are always retained; no graph geometry is recalculated.
UniverseGraph_s Universe_FilterGraphAdvanced( const UniverseGraph_s& graph,
    const UniverseData_s& data, UniverseFilterOptions_s options = {} );

struct UniverseAddressRange_s
{
    u64 start = 0;
    u64 end = 0;
};

struct UniverseRenderStyle_s
{
    bool relativeNodeHeat = true;
    bool relativeNodeSize = true;
    bool antiAliasing = true;
    f32 universeSpacing = 1.0f;
    // Auto-fit outer focus shells; always 1 for legacy single-hop views.
    f32 focusScale = 1.0f;
    u32 absoluteHeatMaximum = 0;
};

struct UniverseView_s
{
    f32 panX = 0;
    f32 panY = 0;
    f32 panZ = 0;
};

UniverseGraph_s Universe_FilterGraph(
    const UniverseGraph_s& graph,
    const std::vector< UniverseAddressRange_s >& ranges );

UniverseGraph_s Universe_FilterGraphRangeHeat(
    const UniverseGraph_s& graph,
    const std::vector< UniverseAddressRange_s >& ranges,
    bool distinctFunctions = false, bool globalFields = false );

UniverseGraph_s Universe_FocusCallers(
    const UniverseGraph_s& graph, u64 address );

UniverseGraph_s Universe_FocusCallersInRanges(
    const UniverseGraph_s& graph, u64 address,
    const std::vector< UniverseAddressRange_s >& ranges );

// Focus against extracted references, not the tier's bounded display links.
UniverseGraph_s Universe_FocusCallersFromData(
    const UniverseData_s& data, u64 address,
    bool distinctCenter, bool globalFields = false );

UniverseGraph_s Universe_FocusCallersInRangesFromData(
    const UniverseData_s& data, u64 address,
    const std::vector< UniverseAddressRange_s >& ranges,
    bool distinctCenter, bool globalFields = false );

enum UniverseFocusMode_e
{
    UFM_CALLERS = 0,
    UFM_BOTH = 1,
    UFM_CALLEES = 2
};

// These use the complete extracted callsite table, not bounded tier links.
UniverseGraph_s Universe_FocusConnectionsFromData(
    const UniverseData_s& data, u64 address,
    UniverseFocusMode_e mode,
    const std::vector< UniverseAddressRange_s >* ranges = nullptr,
    bool distinctCenter = true, bool globalFields = false );

struct UniverseExploreLimits_s
{
    // Budget the *additional* exploration without dropping the original
    // first-hop graph, which may already be larger than either budget.
    usize maxNodes = CANTORDUST_UNIVERSE_HOP_DEFAULT_FUNCTIONS;
    usize maxLinks = CANTORDUST_UNIVERSE_HOP_DEFAULT_LINKS;
};

// Extend the exact one-hop focus layout with bounded outward BFS branches.
// A collapsed address keeps the node visible but suppresses its descendants.
// Each expanded parent admits up to 1024 new neighbors. The configurable
// expansion budgets are capped separately from the ordinary tier budgets.
// Depth 1 matches the legacy one-hop view.
UniverseGraph_s Universe_FocusMultiHopFromData(
    const UniverseData_s& data, u64 address,
    UniverseFocusMode_e mode, u8 maxDepth,
    const std::vector< UniverseAddressRange_s >* ranges = nullptr,
    bool distinctCenter = true, bool globalFields = false,
    const std::vector< u64 >* collapsed = nullptr,
    UniverseExploreLimits_s limits = {} );

// Portable exports: DOT preserves graph topology and callsite metadata;
// SVG is a vector snapshot of the current 3D camera and visible connections.
// The SVG pulse is frozen at a bright, readable phase.
std::string Universe_ExportDot( const UniverseGraph_s& graph );
std::string Universe_ExportSvg( const UniverseGraph_s& graph,
    const CubeCamera_s& camera, const UniverseView_s& view,
    const UniverseRenderStyle_s& style, Palette_e palette,
    bool allLinks, usize selectedIndex,
    unsigned int width, unsigned int height );

struct UniverseCallPath_s
{
    std::vector< u64 > addresses;
    // One exact call instruction address per step, in caller-to-callee order.
    std::vector< u64 > callSites;
};

// Choose a call-path endpoint from the live IDA cursor or a deliberate
// search-result selection. A missing preferred source falls back to the other.
struct UniversePathEndpoint_s
{
    u64 address = 0;
    bool fromSearch = false;
};
std::optional< UniversePathEndpoint_s > Universe_ChoosePathEndpoint(
    std::optional< u64 > idaFunction,
    std::optional< u64 > searchResult,
    bool preferSearch );

// Directed shortest path over known function-to-function call references.
UniverseCallPath_s Universe_FindCallPath(
    const UniverseData_s& data, u64 from, u64 to );

UniverseGraph_s Universe_BuildPathGraph(
    const UniverseData_s& data, const UniverseCallPath_s& path );
// Up to five distinct directed, loop-free paths, shortest first.
std::vector< UniverseCallPath_s > Universe_FindCallPaths(
    const UniverseData_s& data, u64 from, u64 to,
    usize maxPaths = 5 );

UniverseGraph_s Universe_BuildMultiPathGraph(
    const UniverseData_s& data,
    const std::vector< UniverseCallPath_s >& paths,
    usize selectedPath = 0 );

// Case-insensitive name/hex-address search, ranked by incoming calls.
struct UniverseSearchHit_s
{
    u64 address = 0;
    std::string name;
    u32 incoming = 0;
};
std::vector< UniverseSearchHit_s > Universe_SearchFunctions(
    const UniverseData_s& data, const std::string& query,
    u32 minimumIncoming = 0, usize limit = 200 );

UniverseGraph_s Universe_FilterGraphSearch(
    const UniverseGraph_s& graph, const std::string& query,
    u32 minimumIncoming = 0 );

u64 Universe_CallSiteAt(
    const UniverseGraph_s& graph, u64 caller,
    u64 callee, usize ordinal );

void Universe_SetCountMode( UniverseGraph_s& graph, bool distinctFunctions,
    bool globalFields = false );

// Recompute global/overview positions without rebuilding references.
// Focused caller spheres keep their separate address-proximity layout.
void Universe_RelayoutGraph( UniverseGraph_s& graph, bool organic );

UniverseGraph_s Universe_BuildGraph(
    const std::vector< UniverseFunction_s >& functions,
    const std::vector< UniverseReference_s >& references,
    usize maxFunctions = CANTORDUST_UNIVERSE_MAX_FUNCTIONS,
    usize maxLinks = CANTORDUST_UNIVERSE_MAX_LINKS,
    usize rankStart = 0, bool distinctRanking = true,
    bool globalFields = false );

usize Universe_RankForAddress(
    const UniverseData_s& data, u64 address,
    bool distinctRanking = true, bool globalFields = false );

u32 Universe_HeatColor(
    const UniverseGraph_s& graph, usize index, Palette_e palette,
    const UniverseRenderStyle_s& style = {} );

f32 Universe_NodeRadius( const UniverseGraph_s& graph,
    usize index, const UniverseRenderStyle_s& style = {} );

// Compute many radii with one normalization pass (for large SVG exports).
std::vector< f32 > Universe_NodeRadii( const UniverseGraph_s& graph,
    const UniverseRenderStyle_s& style = {} );

// Default camera pivot favors the most-referenced nodes in a global tier.
// Focused caller spheres keep their centered origin.
UniverseView_s Universe_DefaultView( const UniverseGraph_s& graph );

// Framing scale for the prominent nodes (1 for focused/manual graphs).
f32 Universe_FrameScale( const UniverseGraph_s& graph,
    f32 spacing = 1.0f );

u32 Universe_SelectionColor( Palette_e palette );

void Universe_RenderInto(
    Image_s& image,
    const UniverseGraph_s& graph,
    const CubeCamera_s& camera,
    usize selectedIndex,
    bool allLinks,
    usize width,
    usize height,
    Palette_e palette, const UniverseView_s& view = {},
    const UniverseRenderStyle_s& style = {},
    usize idaSyncIndex = CANTORDUST_NO_SOURCE,
    u8 idaSyncAlpha = CANTORDUST_UNIVERSE_SYNC_ALPHA_MAX,
    f32 selectedPathPulse = 0.0f );

} // namespace cantordust

#endif // CANTORDUST_UNIVERSE_H
