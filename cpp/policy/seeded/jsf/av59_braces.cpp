// expect: AV 59:
// av59_braces.cpp: a JSF AV seed with an if without braces.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 clamp(ics::int32 value)
{
  ics::int32 clamped = value;
  if (value < 0)
    clamped = 0;
  return clamped;
}

}  // namespace seed
