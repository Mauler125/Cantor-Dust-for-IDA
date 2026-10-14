#ifndef MATH_HILBERT_H
#define MATH_HILBERT_H


#include "cantordust/scalar_types.h"
#include "cantordust/types.h"

namespace cantordust
{

Point_s Math_HilbertPosition( usize index, usize side );
Point_s Math_HilbertPosition( usize index );
usize Math_HilbertIndex( Point_s point, usize side );

} // namespace cantordust

#endif // MATH_HILBERT_H
