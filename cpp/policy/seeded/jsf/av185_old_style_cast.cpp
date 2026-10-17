// expect: old-style-cast
// av185_old_style_cast.cpp: a JSF AV seed with a C-style cast.
#include <ics/common/universal_types.h>

namespace seed
{

ics::int32 whole(ics::float64 value)
{
  return (ics::int32)value;
}

}  // namespace seed
