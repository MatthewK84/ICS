// expect: AV 61:
// av61_brace.cpp: a JSF AV seed with an opening brace on the line of its statement.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 sign(ics::int32 value)
{
  ics::int32 sign = 0;
  if (value > 0) {
    sign = 1;
  }
  return sign;
}

}  // namespace seed
