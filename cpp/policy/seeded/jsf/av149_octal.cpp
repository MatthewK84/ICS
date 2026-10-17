// expect: AV 149:
// av149_octal.cpp: a JSF AV seed with an octal constant.
#include <ics/common/universal_types.h>

namespace seed
{

constexpr ics::int32 permissions = 0755;

}  // namespace seed
