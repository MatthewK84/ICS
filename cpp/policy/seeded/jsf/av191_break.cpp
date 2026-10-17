// expect: AV 191:
// av191_break.cpp: a JSF AV seed with break out of a loop.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 first_over(ics::int32 limit)
{
  ics::int32 found = 0;
  for (ics::int32 value = 0; value < limit; ++value)
  {
    found = value;
    if (value > 3)
    {
      break;
    }
  }
  return found;
}

}  // namespace seed
