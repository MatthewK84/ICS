// expect: AV 158:
// av158_parentheses.cpp: a JSF AV seed with an unparenthesized comparison beside &&.
#include <ics/common/universal_types.h>

namespace seed
{

bool between(ics::int32 low, ics::int32 value)
{
  return low < value && value < (low + 2);
}

}  // namespace seed
