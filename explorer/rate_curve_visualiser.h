
#ifndef SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_
#define SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_

#include "absl/log/log.h"
#include "explorer_params.h"
#include "gui_widgets.h"
#include "imgui/imgui.h"
#include "implot.h"
#include "magic_enum.hpp"

namespace smileexplorer {

inline void yieldCurveShiftButton(ExplorerParams& params) {
  static int curve_shift_bps = 0;     // The integer variable
  static bool buttonPressed = false;  // To check if the button was pressed.

  ImGui::InputInt("Parallel curve shift (bps)", &curve_shift_bps);

  if (ImGui::Button("Update curve")) {  // Button
    buttonPressed = true;
  }

  if (buttonPressed) {
    ZeroSpotCurve* zero_curve = dynamic_cast<ZeroSpotCurve*>(
        params.global_rates->curves[params.currency].get());
    const auto& input_rates = zero_curve->getInputRates();
    for (int i = 0; i < std::ssize(input_rates); ++i) {
      zero_curve->updateRateAtMaturityIndex(
          i, input_rates[i] + curve_shift_bps * 0.0001);
    }

    buttonPressed = false;  // Reset the button press flag
  }
}

inline void plotForwardRateCurves(ExplorerParams& prop_params) {
  ImGui::Begin("Spot/Forward Rates");

  constexpr auto currency_names = magic_enum::enum_names<Currency>();
  static size_t current_item = 0;  // Index of the currently selected item

  displayCurrencyCombo(
      "Select an option", current_item, prop_params, [&](Currency currency) {
        prop_params.currency = currency;
      });

  ZeroSpotCurve* zero_curve = dynamic_cast<ZeroSpotCurve*>(
      prop_params.global_rates->curves[prop_params.currency].get());

  if (zero_curve == nullptr) {
    LOG(ERROR) << "Rates curve for currency:" << currency_names[current_item]
               << " not convertible to a ZeroSpotCurve.";
  } else {
    // Copy the values, because we have to provide these as floats (rather than
    // doubles).
    std::vector<float> float_rates(zero_curve->getInputRates().begin(),
                                   zero_curve->getInputRates().end());
    ImGui::DragFloat4("{1,2,5,10}",
                      float_rates.data(),
                      0.0005f,
                      0.0001f,  // 1 basis point
                      0.25f,
                      "%.4f",
                      ImGuiSliderFlags_Logarithmic);
    for (int i = 0; i < std::ssize(float_rates); ++i) {
      zero_curve->updateRateAtMaturityIndex(i, float_rates[i]);
    }
  }

  yieldCurveShiftButton(prop_params);

  std::vector<float> timestamps;
  std::vector<float> spot_rates;
  std::vector<float> fwd_rates;

  for (double t = 0.0; t <= 10.0; t += 0.05) {
    timestamps.push_back(t);
    spot_rates.push_back(prop_params.curve()->forwardRate(0.0, t));
    fwd_rates.push_back(prop_params.curve()->forwardRate(t, t + (1. / 12)));
  }

  if (ImPlot::BeginPlot("Rates", ImVec2(-1, -1))) {
    float min_spot_rate =
        *std::min_element(spot_rates.begin(), spot_rates.end());
    float max_spot_rate =
        *std::max_element(spot_rates.begin(), spot_rates.end());
    float min_fwd_rate = *std::min_element(fwd_rates.begin(), fwd_rates.end());
    float max_fwd_rate = *std::max_element(fwd_rates.begin(), fwd_rates.end());
    float min_limit = std::min(min_spot_rate, min_fwd_rate) - 0.0020;
    float max_limit = std::max(max_spot_rate, max_fwd_rate) + 0.0020;
    if (max_limit - min_limit < 0.0050) {  // TODO arbitrary threshold -- make
                                           // this a constant or a param
      max_limit += 0.0025;
      min_limit -= 0.0025;
    }

    ImPlot::SetupAxisLimits(ImAxis_Y1, min_limit, max_limit, ImPlotCond_Always);

    ImPlot::PlotLine(
        "Spot", timestamps.data(), spot_rates.data(), spot_rates.size());
    ImPlot::PlotLine(
        "Forward", timestamps.data(), fwd_rates.data(), fwd_rates.size());
    ImPlot::EndPlot();
  }
  ImGui::End();
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_