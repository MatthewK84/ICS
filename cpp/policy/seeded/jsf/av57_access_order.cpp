// expect: AV 57:
// av57_access_order.cpp: a JSF AV seed with a private section before a public one.
#include <ics/common/universal_types.h>

namespace seed
{

class Counter
{
private:
  ics::int32 count_ = 0;

public:
  [[nodiscard]] ics::int32 count() const
  {
    return count_;
  }
};

}  // namespace seed
