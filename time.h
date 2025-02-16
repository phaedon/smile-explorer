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

}  // namespace markets

#endif  // MARKETS_TIME_H_