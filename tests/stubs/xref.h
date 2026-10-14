#ifndef TESTS_STUBS_XREF_H
#define TESTS_STUBS_XREF_H

#include <ida.hpp>
#include <cstddef>

#define XREF_NOFLOW 1
#define XREF_CODE 4
#define fl_CN 17
#define fl_CF 18

ea_t get_first_dref_from( ea_t address );
ea_t get_next_dref_from( ea_t address, ea_t current );

struct xrefblk_t
{
    ea_t from = 0;
    ea_t to = 0;
    bool iscode = false;
    unsigned char type = 0;
    std::size_t cursor = 0;
    bool first_to( ea_t target, int flags );
    bool next_to();
};

#endif // TESTS_STUBS_XREF_H
