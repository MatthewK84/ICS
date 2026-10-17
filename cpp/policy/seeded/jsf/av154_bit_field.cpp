// expect: AV 154:
// av154_bit_field.cpp: a JSF AV seed with a bit-field.
#include <ics/common/universal_types.h>

namespace seed
{

struct Flags
{
  ics::uint8 ready : 1;
};

}  // namespace seed
