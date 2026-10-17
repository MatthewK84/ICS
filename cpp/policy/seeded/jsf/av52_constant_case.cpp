// expect: AV 52:
// av52_constant_case.cpp: a JSF AV seed with a constant named in the k-prefix style.
#include <ics/common/universal_types.h>

namespace seed
{

constexpr ics::int32 kLimit = 3;

}  // namespace seed
