#ifndef SMILEEXPLORER_EXPLORER_GLOBAL_RATES_
#define SMILEEXPLORER_EXPLORER_GLOBAL_RATES_

#include <magic_enum/magic_enum.hpp>
#include <optional>
#include <unordered_map>
#include <utility>

#include "absl/log/log.h"
#include "csv.hpp"
#include "rates/rates_curve.h"
#include "rates/zero_curve.h"

namespace smileexplorer {

enum class Currency { USD, EUR, JPY, CHF, NOK, ISK };

// This is very hacked 0th-order approximation just to get something going,
// in the absence of API access for market closes or live feeds.
inline double getApproxRate(Currency currency) {
  switch (currency) {
    case Currency::USD:
      return 0.042;
    case Currency::EUR:
      return 0.025;
    case Currency::JPY:
      return 0.01;
    case Currency::NOK:
      return 0.04;
    case Currency::CHF:
      return 0.0030;
    case Currency::ISK:
      return 0.082;
    default:
      return 0.05;
  }
}

struct GlobalRates {
  GlobalRates() {
    // For now, just hard-code the initialisation into the constructor.
    // Later, fetch from a real source such as
    // https://sebgroup.com/our-offering/reports-and-publications/rates-and-iban/swap-rates
    //
    // Note: At the moment, the demo assumes that these are zero-coupon bond
    // yields, not swap rates. But the goal for now is just to have a working
    // demo, not to match exact market rates.
    constexpr auto currencies = magic_enum::enum_values<Currency>();
    for (const auto currency : currencies) {
      double flat_rate = getApproxRate(currency);
      curves.insert(std::make_pair(
          currency,
          std::make_unique<ZeroSpotCurve>(
              std::vector<double>{1, 2, 3, 5, 7, 10},
              std::vector<double>{flat_rate,
                                  flat_rate,
                                  flat_rate,
                                  flat_rate,
                                  flat_rate,
                                  flat_rate},
              CompoundingPeriod::kAnnual,
              CurveInterpolationStyle::kMonotonePiecewiseCubicZeros)));
    }
  }

  std::unordered_map<Currency, std::unique_ptr<RatesCurve>> curves;
};

struct GlobalCurrencies {
  // TODO: Move this into a config file.
  GlobalCurrencies() {
    csv::CSVReader reader("explorer/market_data/currency_pairs_20250907.csv");

    for (csv::CSVRow& row : reader) {
      // The first column contains the "foreign" currency in the FOR-DOM
      // convention.
      const auto foreign_name = row[0].get<std::string>();
      const auto foreign = magic_enum::enum_cast<Currency>(foreign_name);
      if (!foreign.has_value()) {
        LOG(INFO) << "Currency " << foreign_name
                  << " not found in enum definition. Skipping row.";
        continue;
      }

      for (const auto& domestic : magic_enum::enum_values<Currency>()) {
        if (domestic == foreign.value()) {
          continue;
        }
        const auto domestic_name = magic_enum::enum_name(domestic);
        const double for_dom = row[domestic_name.data()].get<double>();

        fx_rates_[foreign.value()][domestic] = for_dom;
        std::cout << foreign_name << "-" << domestic_name << " == " << for_dom
                  << std::endl;
      }
    }
  }

  std::optional<double> operator()(Currency foreign, Currency domestic) {
    if (!fx_rates_.contains(foreign)) {
      LOG(ERROR) << "No market data found for currency "
                 << magic_enum::enum_name(foreign);
      return std::nullopt;
    }
    if (!fx_rates_[foreign].contains(domestic)) {
      LOG(ERROR) << "No market data found for currency "
                 << magic_enum::enum_name(domestic);
      return std::nullopt;
    }
    return fx_rates_[foreign][domestic];
  }

 private:
  std::unordered_map<Currency, std::unordered_map<Currency, double>> fx_rates_;
};

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_GLOBAL_RATES_