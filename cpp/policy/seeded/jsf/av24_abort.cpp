// expect: AV 24:
// av24_abort.cpp: a JSF AV seed with std::abort.
#include <cstdlib>

namespace seed
{

void stop()
{
  std::abort();
}

}  // namespace seed
