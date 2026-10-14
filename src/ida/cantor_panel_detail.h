#ifndef CANTORDUST_IDA_CANTOR_PANEL_DETAIL_HPP
#define CANTORDUST_IDA_CANTOR_PANEL_DETAIL_HPP

#include <ida.hpp>
#include <QString>

namespace cantordust
{

inline QString Ida_FormatAddress( const ea_t address )
{
    return QStringLiteral( "0x%1" )
        .arg( static_cast< qulonglong >( address ), 0, 16 );
}

} // namespace cantordust

#endif // CANTORDUST_IDA_CANTOR_PANEL_DETAIL_HPP
