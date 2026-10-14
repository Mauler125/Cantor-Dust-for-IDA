#include "tier1/universe_history.h"
#include "cantordust/scalar_types.h"

namespace cantordust
{

UniverseHistory::UniverseHistory()
{
    Reset();
}

void UniverseHistory::Reset()
{
    m_entries.assign( 1, std::nullopt );
    m_position = 0;
}

void UniverseHistory::Push( const std::optional< u64 > address )
{
    if ( m_entries[ m_position ] == address )
    {
        return;
    }

    m_entries.resize( m_position + 1 );
    m_entries.push_back( address );
    ++m_position;
}

bool UniverseHistory::GoBack( std::optional< u64 >& address )
{
    if ( !CanGoBack() )
    {
        return false;
    }

    address = m_entries[ --m_position ];
    return true;
}

bool UniverseHistory::GoForward( std::optional< u64 >& address )
{
    if ( !CanGoForward() )
    {
        return false;
    }

    address = m_entries[ ++m_position ];
    return true;
}

std::optional< u64 > UniverseHistory::Current() const
{
    return m_entries[ m_position ];
}

bool UniverseHistory::CanGoBack() const
{
    return m_position > 0;
}

bool UniverseHistory::CanGoForward() const
{
    return m_position + 1 < m_entries.size();
}

} // namespace cantordust
