#ifndef SMILEEXPLORER_EXPLORER_TARF_VISUALISER_H_
#define SMILEEXPLORER_EXPLORER_TARF_VISUALISER_H_

#include <cmath>
#include <algorithm>
#include "derivatives/target_redemption_forward.h"
#include "explorer/explorer_params.h"
#include "explorer/gui_widgets.h"
#include "imgui.h"
#include "implot.h"

namespace smileexplorer {

inline void plotTarfVisualiser(ExplorerParams& params) {
  ImGui::Begin("TARF Calculator");

  static float notional = 1.0;
  static float target = 6.0;
  static float volatility = 0.10;
  static int num_paths = 5000;
  static float end_date_years = 4.0;
  static float settlement_frequency = 0.25;

  ImGui::SliderFloat("Notional (FOR)", &notional, 0.0f, 1e7f, "%.2f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderFloat("Target (DOM)", &target, 0.0f, 1e7f, "%.2f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderFloat("Volatility", &volatility, 0.0f, 1.0f, "%.2f");
  ImGui::SliderInt("MC Paths", &num_paths, 100, 50000);
  ImGui::SliderFloat("Maturity (years)", &end_date_years, 0.25f, 10.0f, "%.2f");
  end_date_years = std::max(0.25f, roundf(end_date_years / 0.25f) * 0.25f);
  ImGui::SliderFloat("Settlement Freq (years)", &settlement_frequency, 0.25f, 1.0f, "%.2f");
  settlement_frequency = std::max(0.25f, roundf(settlement_frequency / 0.25f) * 0.25f);

  static size_t foreign_idx = 0;
  static size_t domestic_idx = 1;
  static double avg_fwd = 0.0;
  static TarfPricingResult pricing_result;
  static double zero_npv_strike = 0.0;
  static double strike = 0.0;
  static bool initialized = false;

  displayCurrencyCombo(
      "Foreign Currency", foreign_idx, params,
      [&](Currency currency) { params.foreign_currency = currency; });
  displayCurrencyCombo(
      "Domestic Currency", domestic_idx, params,
      [&](Currency currency) { params.currency = currency; });

  if (ImGui::Button("Recalculate") || !initialized) {
    params.spot_price = (*params.global_currencies)(params.foreign_currency, params.currency).value_or(1.0);
    const auto& foreign_curve = *params.global_rates->curves[params.foreign_currency];
    const auto& domestic_curve = *params.global_rates->curves[params.currency];

    avg_fwd = weightedAvgForward(
        params.spot_price, end_date_years, settlement_frequency, foreign_curve, domestic_curve);

    zero_npv_strike = findZeroNPVStrike(notional, target, end_date_years, settlement_frequency, FxTradeDirection::kLong, params.spot_price, volatility, foreign_curve, domestic_curve, num_paths);

    if (!initialized) {
        strike = zero_npv_strike;
    }

    TargetRedemptionForward tarf(notional, target, strike, end_date_years,
                                 settlement_frequency,
                                 FxTradeDirection::kLong);

    pricing_result = tarf.price(params.spot_price, volatility,
                              settlement_frequency / 4, num_paths,
                              foreign_curve, domestic_curve);
    initialized = true;
  }

  displayValueAsReadOnlyText("Avg Fwd", avg_fwd);
  displayValueAsReadOnlyText("Zero NPV Strike", zero_npv_strike);
  ImGui::InputDouble("Strike", &strike);
  displayValueAsReadOnlyText("NPV (DOM)", pricing_result.mean_npv);

  if (ImPlot::BeginPlot("NPV Distribution")) {
    if (!pricing_result.path_npvs.empty()) {
        auto [min_it, max_it] = std::minmax_element(pricing_result.path_npvs.begin(), pricing_result.path_npvs.end());
        double min_val = *min_it;
        double max_val = *max_it;
        double padding = (max_val - min_val) * 0.1;
        ImPlot::SetupAxisLimits(ImAxis_X1, min_val - padding, max_val + padding, ImPlotCond_Always);
    }
    ImPlot::PushStyleColor(ImPlotCol_Line, ImVec4(0, 0, 0, 1));
    ImPlot::PlotHistogram("NPV", pricing_result.path_npvs.data(), pricing_result.path_npvs.size(), 50);
    ImPlot::PopStyleColor();
    ImPlot::EndPlot();
  }

  ImGui::End();
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_TARF_VISUALISER_H_