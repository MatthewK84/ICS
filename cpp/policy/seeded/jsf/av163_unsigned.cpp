// expect: AV 163:
// av163_unsigned.cpp: a JSF AV seed with unsigned subtraction.
#include <ics/common/universal_types.h>

namespace seed
{

ics::uint32 less(ics::uint32 value, ics::uint32 step)
{
  return value - step;
}

}  // namespace seed
