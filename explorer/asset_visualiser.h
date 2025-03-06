
#ifndef MARKETS_EXPLORER_ASSET_VISUALISER_
#define MARKETS_EXPLORER_ASSET_VISUALISER_

#include "explorer_params.h"
#include "imgui/imgui.h"
#include "implot.h"
#include "markets/derivative.h"
#include "markets/stochastic_tree_model.h"
#include "markets/volatility.h"
#include "propagator_factories.h"

namespace markets {

inline double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

inline double put_payoff(double strike, double val) {
  return std::max(0.0, strike - val);
}

template <typename FwdPropT>
void displayPairedAssetDerivativePanel(std::string_view window_name,
                                       ExplorerParams& prop_params) {
  ImGui::Begin(window_name.data());

  BinomialTree binomial_tree(prop_params.asset_tree_duration,
                             prop_params.asset_tree_timestep);
  StochasticTreeModel<FwdPropT> asset(
      std::move(binomial_tree), createDefaultPropagator<FwdPropT>(prop_params));

  // TODO remove this hard-coding so that different panels can use different
  // volatility types.
  FlatVol flat_vol(prop_params.flat_vol);
  asset.forwardPropagate(Volatility(flat_vol));

  Derivative deriv(&asset, prop_params.curve.get());

  ImGui::SetNextItemOpen(true, ImGuiCond_Once);
  if (ImGui::TreeNode("Asset")) {
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

    ImGui::SliderFloat("Volatility",
                       //             ImVec2(30, 100),
                       &prop_params.flat_vol,
                       0.0f,
                       0.80f,
                       "%.3f",
                       ImGuiSliderFlags_Logarithmic);
    asset.forwardPropagate(Volatility(flat_vol));

    if (ImPlot::BeginPlot("Asset Tree Plot", ImVec2(-1, 0))) {
      const auto r = getTreeRenderData(asset.binomialTree());

      ImPlotStyle& style = ImPlot::GetStyle();
      style.MarkerSize = 1;

      if (!r.x_coords.empty()) {
        ImPlot::SetupAxisLimits(ImAxis_X1,
                                0,
                                asset.binomialTree().totalTimeAtIndex(
                                    asset.binomialTree().numTimesteps() - 1),
                                ImPlotCond_Always);
      }

      if (!r.y_coords.empty()) {
        auto [min_it, max_it] =
            std::minmax_element(r.y_coords.begin(), r.y_coords.end());
        double min_y = *min_it;
        double max_y = *max_it;
        ImPlot::SetupAxisLimits(ImAxis_Y1, min_y, max_y, ImPlotCond_Always);
      }

      // Plot the edges as line segments
      ImPlot::PlotLine("##Edges",
                       r.edge_x_coords.data(),
                       r.edge_y_coords.data(),
                       r.edge_x_coords.size(),
                       ImPlotLineFlags_Segments);

      ImPlot::PlotScatter(
          "Nodes", r.x_coords.data(), r.y_coords.data(), r.x_coords.size());

      ImPlot::EndPlot();
    }

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

    double computed_value = deriv.price(
        std::bind_front(&markets::call_payoff, 100), prop_params.option_expiry);
    std::string value_str = std::to_string(computed_value);
    char buffer[64];  // A buffer to hold the string (adjust
                      // size as needed)
    strncpy(buffer, value_str.c_str(),
            sizeof(buffer) - 1);        // Copy to buffer
    buffer[sizeof(buffer) - 1] = '\0';  // Ensure null termination

    ImGui::InputText("European call",
                     buffer,
                     IM_ARRAYSIZE(buffer),
                     ImGuiInputTextFlags_ReadOnly);

    if (ImPlot::BeginPlot("Option Tree Plot", ImVec2(0, 0))) {
      const auto r = getTreeRenderData(deriv.binomialTree());

      ImPlotStyle& style = ImPlot::GetStyle();
      style.MarkerSize = 1;

      if (!r.x_coords.empty()) {
        ImPlot::SetupAxisLimits(ImAxis_X1,
                                0,
                                deriv.binomialTree().totalTimeAtIndex(
                                    deriv.binomialTree().numTimesteps() - 1),
                                ImPlotCond_Always);
      }

      if (!r.y_coords.empty()) {
        auto [min_it, max_it] =
            std::minmax_element(r.y_coords.begin(), r.y_coords.end());
        double min_y = *min_it;
        double max_y = *max_it;
        ImPlot::SetupAxisLimits(ImAxis_Y1, min_y, max_y, ImPlotCond_Always);
      }

      // Plot the edges as line segments
      ImPlot::PlotLine("##Edges",
                       r.edge_x_coords.data(),
                       r.edge_y_coords.data(),
                       r.edge_x_coords.size(),
                       ImPlotLineFlags_Segments);

      ImPlot::PlotScatter(
          "Nodes", r.x_coords.data(), r.y_coords.data(), r.x_coords.size());

      ImPlot::EndPlot();
    }

    ImGui::TreePop();
    ImGui::Spacing();
  }

  ImGui::End();
}

}  // namespace markets

#endif  // MARKETS_EXPLORER_ASSET_VISUALISER_