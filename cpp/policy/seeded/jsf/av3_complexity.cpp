// expect: AV 3:
// av3_complexity.cpp: a JSF AV seed with a function whose cyclomatic complexity is 21.
#include <ics/common/universal_types.h>

namespace seed
{

// Twenty-one codes.
enum class Code
{
  a,
  b,
  c,
  d,
  e,
  f,
  g,
  h,
  i,
  j,
  k,
  l,
  m,
  n,
  o,
  p,
  q,
  r,
  s,
  t,
  u
};

// Whether a code is in the first group: twenty case labels make 21 paths.
ics::int32 group(Code code)
{
  ics::int32 first = 0;
  switch (code)
  {
    case Code::a:
    case Code::b:
    case Code::c:
    case Code::d:
    case Code::e:
    case Code::f:
    case Code::g:
    case Code::h:
    case Code::i:
    case Code::j:
    case Code::k:
    case Code::l:
    case Code::m:
    case Code::n:
    case Code::o:
    case Code::p:
    case Code::q:
    case Code::r:
    case Code::s:
    case Code::t:
      first = 1;
      break;
    default:
      break;
  }
  return first;
}

}  // namespace seed
