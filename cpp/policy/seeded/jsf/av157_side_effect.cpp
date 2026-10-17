// expect: AV 157:
// av157_side_effect.cpp: a JSF AV seed with an increment in the right-hand operand of &&.
#include <ics/common/universal_types.h>

namespace seed
{

bool counted(bool ready, ics::int32& count)
{
  return ready && (++count > 0);
}

}  // namespace seed
