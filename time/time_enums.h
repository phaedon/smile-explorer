#ifndef SMILEEXPLORER_TIME_ENUMS_H_
#define SMILEEXPLORER_TIME_ENUMS_H_

namespace smileexplorer {

constexpr int kNumMonthsPerYear = 12.;

// Represents the tenor of a FRA (forward-rate agreement).
enum class ForwardRateTenor : int {
  k1Month = 1,
  k3Month = 3,
  k6Month = 6,
  k12Month = 12
};

enum class CompoundingPeriod {
  kContinuous = 0,
  kAnnual = 1,
  kSemi = 2,
  kQuarterly = 4,
  kMonthly = 12
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
    case YearStyle::k365:
      return 365.0;
    case YearStyle::kBusinessDays252:
      // TODO: This is an estimated constant, but should really be
      // region-dependent.
      return 252.0;
    case YearStyle::kBusinessDays256:
      return 256.0;
    case YearStyle::k360:
    default:
      // Arbitrary choice of default to satisfy GCC.
      return 360;
  }
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TIME_ENUMS_H_