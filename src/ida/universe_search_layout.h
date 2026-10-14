#ifndef IDA_UNIVERSE_SEARCH_LAYOUT_H
#define IDA_UNIVERSE_SEARCH_LAYOUT_H

#include <algorithm>

namespace cantordust
{

// A small, Qt-independent width policy for the mode-specific Universe controls.
// Layout directions and widget moves are applied by CantorPanel.
struct UniverseSearchLayout_s
{
    bool wrap;
    bool stackOptions;
    bool stackLabels;
};

inline UniverseSearchLayout_s Universe_SearchLayoutForWidth(
    const int availableWidth,
    const int findWidth,
    const int neighborsWidth,
    const int tierWidth,
    const int firstRowSpacing,
    const int optionsSpacing )
{
    // Reserve breathing room for Qt style/frame differences at breakpoints.
    constexpr int widthSlack = 12;
    const int optionsWidth = neighborsWidth + tierWidth + optionsSpacing;

    return {
        availableWidth < findWidth + optionsWidth + firstRowSpacing + widthSlack,
        availableWidth < optionsWidth + widthSlack,
        availableWidth < std::max( neighborsWidth, tierWidth ) + widthSlack
    };
}

} // namespace cantordust

#endif // IDA_UNIVERSE_SEARCH_LAYOUT_H
