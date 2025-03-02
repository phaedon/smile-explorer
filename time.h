#ifndef MARKETS_TIME_H_
#define MARKETS_TIME_H_

#include <optional>
#include <vector>

namespace markets {

enum class CompoundingPeriod {
  kMonthly,
  kQuarterly,
  kSemi,
  kAnnual,
  kContinuous
};

enum class YearStyle {
  k365,
  k365WithLeapYears,
  k360,
  kBusinessDays252,
  kBusinessDays256,
};

inline double numDaysInYear(YearStyle style) {
  switch (style) {
    case YearStyle::k365WithLeapYears:
      return 365.25;
    case markets::YearStyle::k365:
      return 365.0;
    case markets::YearStyle::k360:
      return 360;
    case markets::YearStyle::kBusinessDays252:
      // TODO: This is an estimated constant, but should really be
      // region-dependent.
      return 252.0;
    case markets::YearStyle::kBusinessDays256:
      return 256.0;
  }
}

class Timegrid {
 public:
  // Default: size of 1 element (0.0)
  Timegrid() : grid_(1) {}

  // Estimated size.
  Timegrid(int grid_size) : grid_(grid_size) {}

  void set(int ti, double val) { grid_[ti] = val; }
  void append(double val) {
    if (grid_.back() == val) {
      return;
    }
    grid_.emplace_back(val);
  }

  // Returns the total time (in years) at time index `ti`.
  double time(int ti) const { return grid_[ti]; }

  // Returns the timestep at time index `ti`.
  double dt(int ti) const { return grid_[ti + 1] - grid_[ti]; }

  int size() const { return grid_.size(); }

  double accruedErrorEstimate() const {
    return std::numeric_limits<double>::epsilon() * grid_.size();
  }

  std::optional<int> getTimeIndexForExpiry(double expiry_years) const;

 private:
  std::vector<double> grid_;
};

inline std::optional<int> Timegrid::getTimeIndexForExpiry(
    double expiry_years) const {
  // for example if expiry=0.5 and timestep=1/12, then we should return 6.
  // if expiry=1/12 and timestep=1/365 then we should return 30 or 31
  // (depending on rounding convention)
  if (grid_.empty()) {
    // This scenario should never happen because of the design of the class.
    return std::nullopt;
  }

  if (expiry_years > grid_.back() || expiry_years < 0) {
    // The timegrid is not large enough to span the requested timestamp.
    return std::nullopt;
  }

  for (int t = 0; t < grid_.size(); ++t) {
    const double diff_curr = std::abs(grid_[t] - expiry_years);
    const double diff_next = std::abs(grid_[t + 1] - expiry_years);
    if (t == 0) {
      // Special case: there is no "previous" value to compare to. As we step
      // forward, the difference gets larger, so it must be closest to 0.
      if (diff_curr < diff_next) {
        return 0;
      }
    } else {
      const double diff_prev = std::abs(grid_[t - 1] - expiry_years);
      if (diff_curr <= diff_prev && diff_curr <= diff_next) {
        return t;
      }
    }
  }

  // Default return value. Should never reach this branch if timegrid is
  // well-formed.
  return std::nullopt;
}

}  // namespace markets

#endif  // MARKETS_TIME_H_