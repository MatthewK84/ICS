// expect: AV 200:
// av200_for.cpp: a JSF AV seed with a for loop without an initialization.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 count_to(ics::int32 limit)
{
  ics::int32 value = 0;
  for (; value < limit; ++value)
  {
  }
  return value;
}

}  // namespace seed
