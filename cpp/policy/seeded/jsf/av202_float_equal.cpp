// expect: float-equal
// av202_float_equal.cpp: a JSF AV seed with floating-point equality.
#include <ics/common/universal_types.h>

namespace seed
{

bool same(ics::float64 first, ics::float64 second)
{
  return first == second;
}

}  // namespace seed
