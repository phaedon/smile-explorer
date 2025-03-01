#ifndef MARKETS_TIME_H_
#define MARKETS_TIME_H_

namespace markets {

enum class Period { kSemi, kAnnual };

enum class YearStyle {
  k365,
  k365WithLeapYears,
  k360,
  kBusinessDays252,
  kBusinessDays256,
};

double numDaysInYear(YearStyle style) {
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
  void append(double val) { grid_.emplace_back(val); }

  // Returns the total time (in years) at time index `ti`.
  double time(int ti) const { return grid_[ti]; }

  // Returns the timestep at time index `ti`.
  double dt(int ti) const { return grid_[ti + 1] - grid_[ti]; }

  int size() const { return grid_.size(); }

  double accruedErrorEstimate() const {
    return std::numeric_limits<double>::epsilon() * grid_.size();
  }

  int getTimeIndexForExpiry(double expiry_years) const;

 private:
  std::vector<double> grid_;
};

int Timegrid::getTimeIndexForExpiry(double expiry_years) const {
  // for example if expiry=0.5 and timestep=1/12, then we should return 6.
  // if expiry=1/12 and timestep=1/365 then we should return 30 or 31
  // (depending on rounding convention)
  if (grid_.empty()) {
    // TODO log a fatal error? This should never happen.
  }

  for (int t = 0; t < grid_.size(); ++t) {
    if (t == grid_.size() - 1) {
      return t;
    }

    const double diff_curr = std::abs(grid_[t] - expiry_years);
    const double diff_next = std::abs(grid_[t + 1] - expiry_years);
    if (t == 0) {
      if (diff_curr < diff_next) {
        return t;
      }
    } else {
      if (diff_curr <= std::abs(grid_[t - 1] - expiry_years) &&
          diff_curr <= diff_next)
        return t;
    }
  }
}

}  // namespace markets

#endif  // MARKETS_TIME_H_