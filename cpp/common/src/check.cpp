#include "ics/common/check.hpp"

#include <cstdio>

namespace ics::detail {

void report_check_failure(const std::source_location where) noexcept {
  // stderr is unbuffered, so this writes without allocating.
  static_cast<void>(std::fprintf(stderr, "ICS check failed at %s:%u in %s\n", where.file_name(),
                                 static_cast<unsigned>(where.line()), where.function_name()));
}

}  // namespace ics::detail
