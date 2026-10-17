// expect: AV 151:
// av151_magic_number.cpp: a JSF AV seed with a magic number.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 scaled(ics::int32 value)
{
  return value * 37;
}

}  // namespace seed
