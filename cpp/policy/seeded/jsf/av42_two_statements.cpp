// expect: AV 42:
// av42_two_statements.cpp: a JSF AV seed with two statements on one line.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 twice(ics::int32 value)
{
  ics::int32 twice = value; twice += value;
  return twice;
}

}  // namespace seed
