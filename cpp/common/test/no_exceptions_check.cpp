// Compiled with -fno-exceptions and never linked (ICS-015): real-time targets
// will build without exceptions, so every common header must. Each container
// is instantiated in full.
#include "ics/common/check.hpp"
#include "ics/common/error.hpp"
#include "ics/common/fixed_pool.hpp"
#include "ics/common/ring_buffer.hpp"
#include "ics/common/static_vector.hpp"
#include "ics/common/units.hpp"

template class ics::StaticVector<int, 4>;
template class ics::RingBuffer<int, 4>;
template class ics::FixedPool<int, 4>;
