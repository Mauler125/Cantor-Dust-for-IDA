#ifndef TESTS_STUBS_NAME_H
#define TESTS_STUBS_NAME_H
#include <funcs.hpp>
std::size_t get_ea_name( qstring* name, ea_t address );

constexpr int GNCN_NOCOLOR = 1;
constexpr int GNCN_NOSEG = 2;
std::size_t get_nice_colored_name( qstring* name, ea_t address, int flags );
#endif
