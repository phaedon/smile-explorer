
#ifndef SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_
#define SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_

#include "absl/log/log.h"
#include "explorer_params.h"
#include "imgui/imgui.h"
#include "implot.h"

namespace smileexplorer {

inline void PlotForwardRateCurves(ExplorerParams& prop_params) {
  ImGui::Begin("Spot/Forward Rates");

  constexpr auto currency_names = magic_enum::enum_names<Currency>();
  static int current_item = 0;  // Index of the currently selected item
  // const char* items[] = currency_names.data();  // The options in the
  // dropdown
  if (ImGui::BeginCombo("Select an option",
                        currency_names[current_item].data())) {
    for (int n = 0; n < currency_names.size(); n++) {
      bool is_selected = (current_item == n);  // Is this item selected?
      if (ImGui::Selectable(currency_names[n].data(),
                            is_selected))  // If the item is clicked
      {
        current_item = n;  // Update the selection
      }
      if (is_selected)
        ImGui::SetItemDefaultFocus();  // Set the initial
                                       // focus when
                                       // opening the
                                       // combo
    }
    ImGui::EndCombo();
  }
  prop_params.currency =
      magic_enum::enum_cast<Currency>(currency_names[current_item]).value();

  ZeroSpotCurve* zero_curve = dynamic_cast<ZeroSpotCurve*>(
      prop_params.global_rates->curves[prop_params.currency].get());

  if (zero_curve == nullptr) {
    LOG(ERROR) << "Rates curve for currency:" << currency_names[current_item]
               << " not convertible to a ZeroSpotCurve.";
  } else {
    std::vector<float> float_rates(zero_curve->getInputRates().begin(),
                                   zero_curve->getInputRates().end());
    ImGui::DragFloat4("{1,2,5,10}",
                      float_rates.data(),
                      0.0005f,
                      0.0001f,  // 1 basis point
                      0.25f,
                      "%.4f",
                      ImGuiSliderFlags_Logarithmic);
    for (int i = 0; i < float_rates.size(); ++i) {
      zero_curve->updateRateAtMaturityIndex(i, float_rates[i]);
    }
  }

  std::vector<float> timestamps;
  std::vector<float> spot_rates;
  std::vector<float> fwd_rates;

  for (double t = 1.0; t <= 10.0; t += 0.1) {
    timestamps.push_back(t);
    spot_rates.push_back(prop_params.curve()->forwardRate(0.0, t));
    fwd_rates.push_back(prop_params.curve()->forwardRate(t, t + 1.0));
  }

  if (ImPlot::BeginPlot("Rates", ImVec2(-1, -1))) {
    float min_spot_rate =
        *std::min_element(spot_rates.begin(), spot_rates.end());
    float max_spot_rate =
        *std::max_element(spot_rates.begin(), spot_rates.end());
    float min_fwd_rate = *std::min_element(fwd_rates.begin(), fwd_rates.end());
    float max_fwd_rate = *std::max_element(fwd_rates.begin(), fwd_rates.end());
    // LOG(WARNING) << "min fwd: " << min_fwd_rate << " max fwd:" <<
    // max_fwd_rate;
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