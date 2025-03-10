
#ifndef SMILEEXPLORER_EXPLORER_ASSET_VISUALISER_
#define SMILEEXPLORER_EXPLORER_ASSET_VISUALISER_

#include <algorithm>

#include "derivatives/derivative.h"
#include "explorer_params.h"
#include "imgui/imgui.h"
#include "implot.h"
#include "propagator_factories.h"
#include "trees/stochastic_tree_model.h"
#include "vol_surface_factories.h"
#include "volatility/volatility.h"

namespace smileexplorer {

inline void displayValueAsReadOnlyText(const char* label, double value) {
  std::string value_str = std::to_string(value);
  char buffer[64];
  strncpy(buffer, value_str.c_str(), sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';
  ImGui::InputText(
      label, buffer, IM_ARRAYSIZE(buffer), ImGuiInputTextFlags_ReadOnly);
}

inline void plotBinomialTree(const char* label, const BinomialTree& tree) {
  if (ImPlot::BeginPlot(label, ImVec2(-1, 0))) {
    const auto r = getTreeRenderData(tree);
    ImPlotStyle& style = ImPlot::GetStyle();
    style.MarkerSize = 1;

    if (!r.x_coords.empty()) {
      ImPlot::SetupAxisLimits(ImAxis_X1,
                              0,
                              tree.totalTimeAtIndex(tree.numTimesteps() - 1),
                              ImPlotCond_Always);
    }

    if (!r.y_coords.empty()) {
      auto [min_it, max_it] =
          std::minmax_element(r.y_coords.begin(), r.y_coords.end());
      ImPlot::SetupAxisLimits(ImAxis_Y1, *min_it, *max_it, ImPlotCond_Always);
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

inline void plotProbabilityDistribution(const char* label,
                                        const BinomialTree& state_tree,
                                        const BinomialTree& probability_tree,
                                        int time_index) {
  if (ImPlot::BeginPlot(label, ImVec2(-1, 0))) {
    const auto prices = state_tree.statesAtTimeIndex(time_index);
    const auto probabilities = probability_tree.statesAtTimeIndex(time_index);

    float bar_size = 1.;
    if (prices.size() > 2) {
      double min_spacing = std::numeric_limits<float>::infinity();
      for (size_t i = 1; i < prices.size(); ++i) {
        double distance = std::abs(prices[i] - prices[i - 1]);
        min_spacing = std::min(min_spacing, distance);
      }
      bar_size = min_spacing * 0.9;
    }

    ImPlot::SetupAxisLimits(
        ImAxis_Y1,
        0,
        *std::max_element(probabilities.begin(), probabilities.end()) * 1.1,
        ImPlotCond_Always);
    ImPlot::PlotBars("##Probabilities",
                     prices.data(),
                     probabilities.data(),
                     prices.size(),
                     bar_size);

    ImPlot::EndPlot();
  }
}

template <typename VolFunctorT>
inline void displayAdditionalVolControls(ExplorerParams& prop_params) {}

template <>
inline void displayAdditionalVolControls<SigmoidSmile>(
    ExplorerParams& prop_params) {
  ImGui::SliderFloat("Vol range (local vol only)",
                     &prop_params.sigmoid_vol_range,
                     0.0f,
                     0.5f,
                     "%.3f",
                     ImGuiSliderFlags_Logarithmic);

  ImGui::SliderFloat("Vol stretch factor (local vol only)",
                     &prop_params.sigmoid_vol_stretch,
                     0.0f,
                     1.0f,
                     "%.3f",
                     ImGuiSliderFlags_Logarithmic);
}

template <typename DerivativeT>
inline void displayAdditionalCurrencyControls(ExplorerParams& prop_params) {}

// TODO: Refactor this (from the PlotForwardRateCurves impl) into a common
// utility.
template <>
inline void displayAdditionalCurrencyControls<CurrencyDerivative>(
    ExplorerParams& prop_params) {
  constexpr auto currency_names = magic_enum::enum_names<Currency>();

  static int foreign_currency_index = 0;
  if (ImGui::BeginCombo("Foreign (base)",
                        currency_names[foreign_currency_index].data())) {
    for (int n = 0; n < currency_names.size(); n++) {
      bool is_selected = (foreign_currency_index == n);
      if (ImGui::Selectable(currency_names[n].data(), is_selected)) {
        foreign_currency_index = n;  // Update the selection
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  prop_params.foreign_currency =
      magic_enum::enum_cast<Currency>(currency_names[foreign_currency_index])
          .value();

  static int domestic_currency_index = 0;
  if (ImGui::BeginCombo("Domestic (numeraire)",
                        currency_names[domestic_currency_index].data())) {
    for (int n = 0; n < currency_names.size(); n++) {
      bool is_selected = (domestic_currency_index == n);
      if (ImGui::Selectable(currency_names[n].data(), is_selected)) {
        domestic_currency_index = n;  // Update the selection
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
  prop_params.currency =
      magic_enum::enum_cast<Currency>(currency_names[domestic_currency_index])
          .value();
}

template <typename DerivativeT>
DerivativeT createDerivative(const BinomialTree* asset_tree,
                             const ExplorerParams& params);

template <>
inline Derivative createDerivative<Derivative>(const BinomialTree* asset_tree,
                                               const ExplorerParams& params) {
  return Derivative(asset_tree, params.curve());
}

template <>
inline CurrencyDerivative createDerivative<CurrencyDerivative>(
    const BinomialTree* asset_tree, const ExplorerParams& params) {
  return CurrencyDerivative(asset_tree, params.curve(), params.foreign_curve());
}

template <typename FwdPropT, typename VolFunctorT, typename DerivativeT>
void displayPairedAssetDerivativePanel(std::string_view window_name,
                                       ExplorerParams& prop_params) {
  ImGui::Begin(window_name.data());

  BinomialTree binomial_tree(prop_params.asset_tree_duration,
                             prop_params.asset_tree_timestep);
  StochasticTreeModel<FwdPropT> asset(
      std::move(binomial_tree), createDefaultPropagator<FwdPropT>(prop_params));

  Volatility<VolFunctorT> vol_surface(prop_params);
  asset.forwardPropagate(vol_surface);

  auto deriv =
      createDerivative<DerivativeT>(&asset.binomialTree(), prop_params);

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Asset")) {
    displayAdditionalCurrencyControls<DerivativeT>(prop_params);

    ImGui::SliderFloat("Tree duration",
                       &prop_params.asset_tree_duration,
                       0.1f,
                       20.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    ImGui::SliderFloat("Timestep (years)",
                       &prop_params.asset_tree_timestep,
                       1. / 252,
                       1.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    ImGui::DragFloat("Spot",
                     &prop_params.spot_price,
                     0.1f,
                     0.0f,
                     200.0f,
                     "%.2f",
                     ImGuiSliderFlags_Logarithmic);
    asset.updateSpot(prop_params.spot_price);

    ImGui::SliderFloat("Volatility",
                       //             ImVec2(30, 100),
                       &prop_params.flat_vol,
                       0.0f,
                       0.80f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);

    displayAdditionalVolControls<VolFunctorT>(prop_params);
    asset.forwardPropagate(vol_surface);

    plotBinomialTree("Asset tree", asset.binomialTree());

    ImGui::TreePop();
    ImGui::Spacing();
  }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Option")) {
    ImGui::SliderFloat("Expiry",
                       &prop_params.option_expiry,
                       0.0f,
                       asset.binomialTree().treeDurationYears(),
                       "%.2f");

    ImGui::SliderFloat("Strike",
                       &prop_params.option_strike,
                       prop_params.spot_price * 0.1,
                       prop_params.spot_price * 2.,
                       "%.2f",
                       ImGuiSliderFlags_Logarithmic);

    VanillaOption vanilla(prop_params.option_strike, OptionPayoff::Call);
    displayValueAsReadOnlyText("European call",
                               deriv.price(vanilla, prop_params.option_expiry));

    double df_end = prop_params.curve()->df(prop_params.option_expiry);
    // TODO: there is a bug here in the closed-form delta, because we need the
    // int-rate differential.
    double cont_comp_spot_rate = fwdRateByPeriod(
        1.0, df_end, prop_params.option_expiry, CompoundingPeriod::kContinuous);

    displayValueAsReadOnlyText(
        "BSM call delta",
        vanilla.blackScholesGreek(prop_params.spot_price,
                                  prop_params.flat_vol,
                                  prop_params.option_expiry,
                                  cont_comp_spot_rate,
                                  0.0,
                                  Greeks::Delta));

    plotBinomialTree("Option tree", deriv.binomialTree());

    ImGui::TreePop();
    ImGui::Spacing();
  }

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Risk-neutral probabilities")) {
    ImPlot::SetNextAxesToFit();

    ImGui::SliderFloat("Time (years)",
                       &prop_params.time_for_displaying_probability,
                       0.1f,
                       20.f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);
    int time_index =
        asset.binomialTree()
            .getTimegrid()
            .getTimeIndexForExpiry(prop_params.time_for_displaying_probability)
            .value_or(asset.binomialTree().numTimesteps());

    plotProbabilityDistribution("Arrow-Debreu prices",
                                asset.binomialTree(),
                                deriv.arrowDebreuTree(),
                                time_index);

    ImGui::TreePop();
    ImGui::Spacing();
  }

  ImGui::End();
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_ASSET_VISUALISER_