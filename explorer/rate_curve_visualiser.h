
#ifndef MARKETS_EXPLORER_RATE_CURVE_VISUALISER_
#define MARKETS_EXPLORER_RATE_CURVE_VISUALISER_

#include "explorer_params.h"
#include "imgui/imgui.h"
#include "implot.h"

namespace markets {

inline void PlotForwardRateCurves(ExplorerParams& prop_params) {
  ImGui::Begin("Spot/Forward Rates");
  ImGui::DragFloat4("{1,2,5,10}",
                    prop_params.rates.data(),
                    0.0005f,
                    0.0001f,  // 1 basis point
                    0.25f,
                    "%.4f",
                    ImGuiSliderFlags_Logarithmic);
  prop_params.curve = std::make_unique<ZeroSpotCurve>(ZeroSpotCurve(
      {1, 2, 5, 10},
      std::vector<double>(prop_params.rates.begin(), prop_params.rates.end()),
      CompoundingPeriod::kAnnual));

  std::vector<float> timestamps;
  std::vector<float> spot_rates;
  std::vector<float> fwd_rates;

  for (double t = 1.0; t <= 10.0; t += 0.1) {
    timestamps.push_back(t);
    spot_rates.push_back(prop_params.curve->getForwardRate(0.0, t));
    fwd_rates.push_back(prop_params.curve->getForwardRate(t, t + 1.0));
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

}  // namespace markets

#endif  // MARKETS_EXPLORER_RATE_CURVE_VISUALISER_