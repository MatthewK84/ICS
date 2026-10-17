// expect: AV 192:
// av192_final_else.cpp: a JSF AV seed with an else if without a final else.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 sign(ics::int32 value)
{
  ics::int32 sign = 0;
  if (value > 0)
  {
    sign = 1;
  }
  else if (value < 0)
  {
    sign = -1;
  }
  return sign;
}

}  // namespace seed
