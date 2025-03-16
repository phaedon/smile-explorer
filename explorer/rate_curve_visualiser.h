
#ifndef SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_
#define SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_

#include <algorithm>

#include "absl/log/log.h"
#include "derivatives/interest_rate_derivative.h"
#include "explorer_params.h"
#include "gui_widgets.h"
#include "imgui/imgui.h"
#include "implot.h"
#include "magic_enum.hpp"
#include "rates/short_rate_tree_curve.h"
#include "tree_render.h"
#include "trees/trinomial_tree.h"

namespace smileexplorer {

// How much extra room to allow above and beyond the display of a curve so that
// it doesn't hit the limits of the graph display.
constexpr int kBasisPointPadding = 25;
constexpr double kRatePadding = kBasisPointPadding * 0.0001;

inline void plotTrinomialTree(const char* label,
                              const TrinomialTree& tree,
                              TrinomialValueExtractionType extraction) {
  if (ImPlot::BeginPlot(label, ImVec2(-1, 0))) {
    const auto r = getTreeRenderData(tree, extraction);
    ImPlotStyle& style = ImPlot::GetStyle();
    style.MarkerSize = 1;

    if (!r.x_coords.empty()) {
      auto [min_it, max_it] =
          std::minmax_element(r.x_coords.begin(), r.x_coords.end());
      ImPlot::SetupAxisLimits(ImAxis_X1, *min_it, *max_it, ImPlotCond_Always);
    }

    if (!r.y_coords.empty()) {
      auto [min_it, max_it] =
          std::minmax_element(r.y_coords.begin(), r.y_coords.end());
      ImPlot::SetupAxisLimits(ImAxis_Y1,
                              *min_it - kRatePadding * 4,
                              *max_it + kRatePadding * 4,
                              ImPlotCond_Always);
    }

    ImPlot::PlotLine("##Edges",
                     r.edge_x_coords.data(),
                     r.edge_y_coords.data(),
                     r.edge_x_coords.size(),
                     ImPlotLineFlags_Segments);
    ImPlot::PlotScatter(
        "Nodes", r.x_coords.data(), r.y_coords.data(), r.x_coords.size());
    ImPlot::EndPlot();
  }
}

inline void yieldCurveShiftButton(ExplorerParams& params) {
  static int curve_shift_bps = 0;     // The integer variable
  static bool buttonPressed = false;  // To check if the button was pressed.

  ImGui::InputInt("Parallel curve shift (bps)", &curve_shift_bps);

  if (ImGui::Button("Apply shift")) {
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

inline void plotProbabilityDistribution(const char* label,
                                        const TrinomialTree& rate_tree,
                                        int time_index) {
  if (time_index >= std::ssize(rate_tree.tree_)) {
    time_index = rate_tree.tree_.size() - 1;
  }

  if (ImPlot::BeginPlot(label, ImVec2(-1, 0))) {
    const auto& nodes = rate_tree.tree_[time_index];

    std::vector<double> rates;
    std::vector<double> probabilities;
    for (const auto& node : nodes) {
      rates.push_back(rate_tree.alphas_[time_index] + node.state_value);
      probabilities.push_back(node.arrow_debreu);
    }

    float bar_size = 1.;
    if (rates.size() > 2) {
      double min_spacing = std::numeric_limits<float>::infinity();
      for (size_t i = 1; i < rates.size(); ++i) {
        double distance = std::abs(rates[i] - rates[i - 1]);
        min_spacing = std::min(min_spacing, distance);
      }
      bar_size = min_spacing * 0.9;
    }

    auto [min_rate_iter, max_rate_iter] =
        std::minmax_element(rates.begin(), rates.end());
    ImPlot::SetupAxisLimits(
        ImAxis_X1, -0.01, *max_rate_iter, ImPlotCond_Always);

    ImPlot::SetupAxisLimits(
        ImAxis_Y1,
        0,
        *std::max_element(probabilities.begin(), probabilities.end()) * 1.1,
        ImPlotCond_Always);
    ImPlot::PlotBars("##Probabilities",
                     rates.data(),
                     probabilities.data(),
                     rates.size(),
                     bar_size);

    ImPlot::EndPlot();
  }
}

inline void plotForwardRateCurves(ExplorerParams& prop_params) {
  ImGui::Begin("Spot/Forward Rates");

  constexpr auto currency_names = magic_enum::enum_names<Currency>();
  static size_t current_item = 0;  // Index of the currently selected item

  displayCurrencyCombo(
      "Currency", current_item, prop_params, [&](Currency currency) {
        prop_params.currency = currency;
      });

  yieldCurveShiftButton(prop_params);

  ZeroSpotCurve* zero_curve = dynamic_cast<ZeroSpotCurve*>(
      prop_params.global_rates->curves[prop_params.currency].get());

  if (zero_curve == nullptr) {
    LOG(ERROR) << "Rates curve for currency:" << currency_names[current_item]
               << " not convertible to a ZeroSpotCurve.";
  } else {
    // Copy the values, because we have to provide these as floats (rather than
    // doubles).
    std::vector<int> fixed_maturities{1, 2, 3, 5, 7, 10};
    std::vector<float> float_rates(zero_curve->getInputRates().begin(),
                                   zero_curve->getInputRates().end());

    ImGui::PushID("Rate controllers");
    for (size_t i = 0; i < float_rates.size(); i++) {
      if (i > 0) ImGui::SameLine();
      ImGui::PushID(i);
      ImGui::PushStyleColor(ImGuiCol_FrameBg,
                            (ImVec4)ImColor::HSV(i / 7.0f, 0.5f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                            (ImVec4)ImColor::HSV(i / 7.0f, 0.6f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                            (ImVec4)ImColor::HSV(i / 7.0f, 0.7f, 0.5f));
      ImGui::PushStyleColor(ImGuiCol_SliderGrab,
                            (ImVec4)ImColor::HSV(i / 7.0f, 0.9f, 0.9f));
      ImGui::VSliderFloat("##v",
                          ImVec2(18, 300),
                          &float_rates[i],
                          0.0001f,
                          0.25f,
                          "",
                          ImGuiSliderFlags_Logarithmic);
      if (ImGui::IsItemActive() || ImGui::IsItemHovered())
        ImGui::SetTooltip("%dy:%.4f", fixed_maturities[i], float_rates[i]);
      ImGui::PopStyleColor(4);
      ImGui::PopID();
    }
    ImGui::PopID();

    for (int i = 0; i < std::ssize(float_rates); ++i) {
      zero_curve->updateRateAtMaturityIndex(i, float_rates[i]);
    }
  }

  TrinomialTree trinomial_tree(prop_params.short_rate_tree_duration,
                               prop_params.hullwhite_mean_reversion,
                               prop_params.short_rate_tree_timestep,
                               prop_params.hullwhite_sigma);
  trinomial_tree.forwardPropagate(*zero_curve);
  ShortRateTreeCurve tree_curve(std::move(trinomial_tree));

  std::vector<float> timestamps;
  std::vector<float> spot_rates;
  std::vector<float> fwd_rates;
  std::vector<float> tree_fwd_rates;

  for (double t = 0.0; t <= 10.0; t += 0.05) {
    timestamps.push_back(t);
    spot_rates.push_back(prop_params.curve()->forwardRate(0.0, t));
    fwd_rates.push_back(prop_params.curve()->forwardRate(t, t + 0.25));
    tree_fwd_rates.push_back(tree_curve.forwardRate(t, t + 0.25));
  }

  ImGui::SameLine();
  if (ImPlot::BeginPlot("Rates", ImVec2(-1, 0))) {
    float min_spot_rate =
        *std::min_element(spot_rates.begin(), spot_rates.end());
    float max_spot_rate =
        *std::max_element(spot_rates.begin(), spot_rates.end());
    float min_fwd_rate = *std::min_element(fwd_rates.begin(), fwd_rates.end());
    float max_fwd_rate = *std::max_element(fwd_rates.begin(), fwd_rates.end());
    float min_limit = std::min(min_spot_rate, min_fwd_rate) - kRatePadding;
    float max_limit = std::max(max_spot_rate, max_fwd_rate) + kRatePadding;
    if (max_limit - min_limit < kRatePadding * 2) {
      max_limit += kRatePadding;
      min_limit -= kRatePadding;
    }

    ImPlot::SetupAxisLimits(ImAxis_Y1, min_limit, max_limit, ImPlotCond_Always);

    ImPlot::PlotLine(
        "Spot", timestamps.data(), spot_rates.data(), spot_rates.size());
    ImPlot::PlotLine(
        "Forward", timestamps.data(), fwd_rates.data(), fwd_rates.size());
    ImPlot::PlotLine("Hull-White Forward",
                     timestamps.data(),
                     tree_fwd_rates.data(),
                     tree_fwd_rates.size());
    ImPlot::EndPlot();
  }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Hull-White tree")) {
    ImGui::SliderFloat("Tree duration",
                       &prop_params.short_rate_tree_duration,
                       0.1f,
                       20.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    ImGui::SliderFloat("Timestep (years)",
                       &prop_params.short_rate_tree_timestep,
                       1. / 252,
                       1.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    ImGui::SliderFloat("Hull-White mean-reversion",
                       &prop_params.hullwhite_mean_reversion,
                       0.01f,
                       1.0f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    ImGui::SliderFloat("Hull-White normal vol",
                       &prop_params.hullwhite_sigma,
                       0.001f,
                       0.25f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    plotTrinomialTree("Hull-White tree",
                      tree_curve.trinomialTree(),
                      TrinomialValueExtractionType::kShortRate);

    ImGui::TreePop();
    ImGui::Spacing();
  }

  InterestRateDerivative trinomial_option(&tree_curve);
  VanillaOption atm_caplet(tree_curve.forwardRate(1., 2.), OptionPayoff::Call);
  trinomial_option.price(atm_caplet, 1.0);

  // Visualising backward inductino for the interest-rate option might be useful
  // for debugging but otherwise may not be that interesting. If we restore
  // this, change the y-scaling in plotTrinomialTree which is currently a fixed
  // constant. ImGui::SetNextItemOpen(true, ImGuiCond_Once); if
  // (ImGui::TreeNode("Option tree")) {
  //   plotTrinomialTree("Option tree",
  //                     trinomial_option.tree(),
  //                     TrinomialValueExtractionType::kDerivValue);
  //   ImGui::TreePop();
  //   ImGui::Spacing();
  // }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Risk-neutral probabilities")) {
    ImGui::SliderFloat("Time (years)",
                       &prop_params.time_for_displaying_probability,
                       0.1f,
                       20.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);
    int time_index =
        tree_curve.trinomialTree()
            .getTimegrid()
            .getTimeIndexForExpiry(prop_params.time_for_displaying_probability)
            .value_or(tree_curve.trinomialTree().tree_.size() - 1);
    plotProbabilityDistribution(
        "Rate distribution", tree_curve.trinomialTree(), time_index);
    ImGui::TreePop();
    ImGui::Spacing();
  }

  ImGui::End();
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_RATE_CURVE_VISUALISER_