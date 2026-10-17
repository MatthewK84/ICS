// expect: AV 58:
// av58_parameters.cpp: a JSF AV seed with three parameters on one line.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 sum(ics::int32 first, ics::int32 second, ics::int32 third)
{
  return first + second + third;
}

}  // namespace seed
