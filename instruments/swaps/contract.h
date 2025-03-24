#ifndef SMILEEXPLORER_INSTRUMENTS_SWAPS_CONTRACT_H_
#define SMILEEXPLORER_INSTRUMENTS_SWAPS_CONTRACT_H_

#include "time/time_enums.h"

namespace smileexplorer {

enum class SwapDirection : int { kPayer = -1, kReceiver = 1 };

struct SwapContractDetails {
  SwapDirection direction;
  CompoundingPeriod fixed_rate_frequency;
  ForwardRateTenor floating_rate_frequency;
  double notional_principal;
  double start_date_years;
  double end_date_years;
  double fixed_rate;
};

}  // namespace smileexplorer

#endif  //  SMILEEXPLORER_INSTRUMENTS_SWAPS_CONTRACT_H_