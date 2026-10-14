#ifndef IDA_NAVIGATION_GATE_H
#define IDA_NAVIGATION_GATE_H


#include "cantordust/scalar_types.h"
#include <cstddef>
#include <cstdint>

#define CANTORDUST_NAVIGATION_INTERVAL_MS 24u

namespace cantordust
{

class NavigationGate final
{
public:
    void Reset()
    {
        m_hasLast = false;
    }

    bool ShouldNavigate(
        const usize index,
        const u64 elapsedMs,
        const bool force )
    {
        if ( m_hasLast )
        {
            if ( index == m_lastIndex )
            {
                return false;
            }

            if ( !force && elapsedMs >= m_lastTimeMs &&
                 elapsedMs - m_lastTimeMs < CANTORDUST_NAVIGATION_INTERVAL_MS )
            {
                return false;
            }
        }

        m_hasLast = true;
        m_lastIndex = index;
        m_lastTimeMs = elapsedMs;
        return true;
    }

private:
    usize m_lastIndex = 0;
    u64 m_lastTimeMs = 0;
    bool m_hasLast = false;
};

} // namespace cantordust

#endif // IDA_NAVIGATION_GATE_H
