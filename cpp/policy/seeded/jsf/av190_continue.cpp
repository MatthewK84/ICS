// expect: AV 190:
// av190_continue.cpp: a JSF AV seed with continue.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 odd_count(ics::int32 limit)
{
  ics::int32 count = 0;
  for (ics::int32 value = 0; value < limit; ++value)
  {
    if ((value % 2) == 0)
    {
      continue;
    }
    ++count;
  }
  return count;
}

}  // namespace seed
