#ifndef TIER1_UNIVERSE_HISTORY_H
#define TIER1_UNIVERSE_HISTORY_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cantordust
{

class UniverseHistory
{
public:
    UniverseHistory();

    void Reset();
    void Push( std::optional< u64 > address );
    bool GoBack( std::optional< u64 >& address );
    bool GoForward( std::optional< u64 >& address );

    std::optional< u64 > Current() const;
    bool CanGoBack() const;
    bool CanGoForward() const;

private:
    std::vector< std::optional< u64 > > m_entries;
    usize m_position = 0;
};

} // namespace cantordust

#endif // TIER1_UNIVERSE_HISTORY_H
