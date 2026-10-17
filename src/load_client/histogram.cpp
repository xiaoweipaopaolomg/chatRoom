#include "load_client/histogram.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chatroom::load {
std::uint64_t Histogram::Percentile(double fraction) const {
  if (values_.empty()) return 0;
  if (!(fraction > 0.0 && fraction <= 1.0)) throw std::invalid_argument("percentile must be in (0,1]");
  auto sorted = values_; std::sort(sorted.begin(), sorted.end());
  const auto rank = static_cast<std::size_t>(std::ceil(fraction * static_cast<double>(sorted.size())));
  return sorted[rank - 1];
}
}  // namespace chatroom::load
