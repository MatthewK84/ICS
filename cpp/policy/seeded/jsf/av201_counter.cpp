// expect: AV 201:
// av201_counter.cpp: a JSF AV seed with a loop counter changed in the body.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 skip(ics::int32 limit)
{
  ics::int32 count = 0;
  for (ics::int32 value = 0; value < limit; ++value)
  {
    value += 1;
    ++count;
  }
  return count;
}

}  // namespace seed
