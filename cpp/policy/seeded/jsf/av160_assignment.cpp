// expect: AV 160:
// av160_assignment.cpp: a JSF AV seed with an assignment inside an expression.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 twice(ics::int32 value)
{
  ics::int32 copy = 0;
  const ics::int32 result = (copy = value) + value;
  return result;
}

}  // namespace seed
