#include "ida/universe_search_layout.h"

#include <cassert>

int main()
{
    using cantordust::Universe_SearchLayoutForWidth;

    // Find (165), Neighbors (160), Tier (120), gaps (10 + 5), slack (12).
    const auto layoutAt = []( const int width ) {
        return Universe_SearchLayoutForWidth( width, 165, 160, 120, 10, 5 );
    };

    const auto wide = layoutAt( 600 );
    assert( !wide.wrap && !wide.stackOptions && !wide.stackLabels );

    const auto medium = layoutAt( 340 );
    assert( medium.wrap && !medium.stackOptions && !medium.stackLabels );

    const auto narrow = layoutAt( 250 );
    assert( narrow.wrap && narrow.stackOptions && !narrow.stackLabels );

    const auto veryNarrow = layoutAt( 130 );
    assert( veryNarrow.wrap && veryNarrow.stackOptions && veryNarrow.stackLabels );

    // The thresholds are stable and restoring width restores the wide layout.
    assert( !layoutAt( 297 ).stackOptions );
    assert( layoutAt( 296 ).stackOptions );
    assert( !layoutAt( 172 ).stackLabels );
    assert( layoutAt( 171 ).stackLabels );
    assert( !layoutAt( 600 ).wrap );

    // A hidden Tier still participates in the width calculation; its width
    // is passed as an explicit requirement, not a transient Qt size hint.
    const auto hiddenTier = Universe_SearchLayoutForWidth(
        340, 165, 160, 120, 10, 5 );
    assert( hiddenTier.wrap );
}
