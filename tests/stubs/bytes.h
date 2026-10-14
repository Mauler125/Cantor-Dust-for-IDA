#ifndef TESTS_STUBS_BYTES_H
#define TESTS_STUBS_BYTES_H

#include <ida.hpp>

bool is_loaded( ea_t address );
unsigned char get_db_byte( ea_t address );
int nbits( ea_t address );
ea_t next_inited( ea_t address, ea_t maxAddress );
ea_t prev_inited( ea_t address, ea_t minAddress );

using flags_t = unsigned int;
flags_t get_flags( ea_t address );
bool is_data( flags_t flags );
ea_t next_head( ea_t address, ea_t end );
ea_t get_item_head( ea_t address );

#endif // TESTS_STUBS_BYTES_H
