#ifndef TESTS_STUBS_SEGMENT_H
#define TESTS_STUBS_SEGMENT_H
#include <ida.hpp>
struct segment_t { int type = 0; };

constexpr int SEG_XTRN = 1;
const segment_t* getseg( ea_t address );
#endif
