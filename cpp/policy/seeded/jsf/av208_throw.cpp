// expect: AV 208:
// av208_throw.cpp: a JSF AV seed with throw.
#include <stdexcept>

namespace seed
{

void refuse()
{
  throw std::runtime_error("refused");
}

}  // namespace seed
