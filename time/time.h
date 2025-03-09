#ifndef SMILEEXPLORER_TIME_H_
#define SMILEEXPLORER_TIME_H_

namespace smileexplorer {

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
    case YearStyle::k360:
      return 360;
    case YearStyle::kBusinessDays252:
      // TODO: This is an estimated constant, but should really be
      // region-dependent.
      return 252.0;
    case YearStyle::kBusinessDays256:
      return 256.0;
  }
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_TIME_H_