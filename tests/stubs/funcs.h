#ifndef TESTS_STUBS_FUNCS_H
#define TESTS_STUBS_FUNCS_H

#ifndef TESTS_STUBS_IDA_H
#error "Include <ida.hpp> before <funcs.hpp>"
#endif
#include <cstddef>
#include <string>

struct func_t
{
    ea_t start_ea = 0;
    ea_t end_ea = 0;
    unsigned int flags = 0;
};

constexpr unsigned int FUNC_LIB = 0x04u;
constexpr unsigned int FUNC_THUNK = 0x80u;
using qstring = std::string;
size_t get_func_qty();
func_t* getn_func( std::size_t index );
func_t* get_func( ea_t address );
std::size_t get_func_name( qstring* name, ea_t address );

#endif // TESTS_STUBS_FUNCS_H
