#ifndef TESTS_STUBS_IDA_H
#define TESTS_STUBS_IDA_H

#include <cstdint>

using ea_t = std::uint64_t;

#define BADADDR ( ~static_cast< ea_t >( 0 ) )

#endif // TESTS_STUBS_IDA_H
