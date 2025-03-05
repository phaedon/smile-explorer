#include <stdio.h>

#include <Eigen/Dense>
#include <chrono>

#include "binomial_tree.h"
#include "derivative.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "implot.h"
#include "implot3d.h"
#include "markets/binomial_tree.h"
#include "markets/propagators.h"
// #include "markets/rates/arrow_debreu.h"
// #include "markets/rates/bdt.h"
#include "markets/rates/rates_curve.h"
// #include "markets/rates/swaps.h"
#include "markets/volatility.h"
#include "markets/yield_curve.h"
#include "stochastic_tree_model.h"
#include "time.h"
#include "volatility.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace markets {

double call_payoff(double strike, double val) {
  return std::max(0.0, val - strike);
}

double put_payoff(double strike, double val) {
  return std::max(0.0, strike - val);
}

struct DermanExampleVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  double operator()(double t) const {
    if (t <= 1) return 0.2;
    if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
    // return forwardVol(0, 2, 3, 0.255, 0.311);
    return forwardVol(0, 2, 3, 0.255, 0.22);
  }
};

struct SmoothLocalVol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeVaryingSkewSmile;
  double operator()(double t, double s) const {
    double base_vol = 0.20;
    double term_factor = 1.0 + 0.3 * exp(-t / 0.5);
    //    double skew_factor = 1.0 - 0.1 * (s - 100) / 100;
    double skew_factor = 1.0 + 0.05 * (std::pow((s - 100) / 100, 2)) *
                                   (1.0 + 0.1 * std::exp(-t));
    return base_vol * term_factor * skew_factor;
  }
};

void PlotVolSurface() {
  DermanExampleVol dermanvol;
  Volatility volsurface(dermanvol);
  const auto timegrid = volsurface.generateTimegrid(5.0, 0.1);

  double init_price = 80;
  int num_price_gradations = 50;

  const int xy = timegrid.size() * num_price_gradations;
  std::vector<double> timestamps(xy);
  std::vector<double> prices(xy);
  std::vector<double> vols(xy);

  for (int i = 0; i < timegrid.size(); ++i) {
    for (int j = 0; j < num_price_gradations; ++j) {
      int idx = i * num_price_gradations + j;
      timestamps[idx] = timegrid.time(i);
      prices[idx] = init_price + j * 1;
      vols[idx] = volsurface.get(timegrid.time(i));
    }
  }

  ImGui::Begin("Vol Surface");

  ImPlot3D::CreateContext();
  if (ImPlot3D::BeginPlot("Vol Surface")) {
    ImPlot3D::PlotSurface("hello",
                          timestamps.data(),
                          prices.data(),
                          vols.data(),
                          num_price_gradations,
                          timegrid.size());
    ImPlot3D::EndPlot();
  }
  ImGui::End();
}

void PlotForwardRateCurves() {
  ZeroSpotCurve curve({1, 2, 3, 5, 10, 15},
                      {0.03, 0.035, 0.038, 0.04, 0.045, 0.05},
                      CompoundingPeriod::kAnnual);
  // SimpleUncalibratedShortRatesCurve curve(10, 0.1);

  std::vector<float> timestamps;
  std::vector<float> spot_rates;
  std::vector<float> fwd_rates;

  for (double t = 1.0; t <= 10.0; t += 0.5) {
    timestamps.push_back(t);
    spot_rates.push_back(curve.getForwardRate(0.0, t));
    fwd_rates.push_back(curve.getForwardRate(t, t + 0.5));
  }

  ImGui::Begin("Spot/Forward Rates");
  if (ImPlot::BeginPlot("Rates")) {
    ImPlot::PlotLine(
        "Spot", timestamps.data(), spot_rates.data(), spot_rates.size());
    ImPlot::PlotLine(
        "Forward", timestamps.data(), fwd_rates.data(), fwd_rates.size());
    ImPlot::EndPlot();
  }
  ImGui::End();
}

struct DermanChapter14Vol {
  static constexpr VolSurfaceFnType type =
      VolSurfaceFnType::kTimeInvariantSkewSmile;
  DermanChapter14Vol(double spot_price) : spot_price_(spot_price) {}

  double operator()(double s) const {
    // .5 / ( (1 + exp(.1(x - 80)))) + 0.1
    double vol_range = 0.4;
    double vol_floor = 0.12;
    double stretchy = 0.1;
    return vol_floor + vol_range / (1 + std::exp(stretchy * (s - spot_price_)));
    // double v = std::max(0.15875 - 0.4 * (s - spot_price_) / spot_price_,
    // 0.04); return std::min(v, 0.5);
  }

 private:
  double spot_price_;
};

}  // namespace markets

int main(int, char**) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  // GL 3.2 + GLSL 150 (Modern OpenGL on macOS/Linux)
  const char* glsl_version =
      "#version 150";  // Or #version 130 for older systems
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE,
                 GLFW_OPENGL_CORE_PROFILE);             // macOS requires this
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);  // macOS requires this

  GLFWwindow* window = glfwCreateWindow(
      1280, 720, "Binomial options visualizer!", nullptr, nullptr);
  if (window == nullptr) return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // Enable vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  // see for example
  // https://sebgroup.com/our-offering/reports-and-publications/rates-and-iban/swap-rates
  // YieldCurve spot_curve({1, 2, 5, 7, 10},
  //                       {0.045, 0.0423, 0.0401, 0.0398, 0.0397});

  /*
double spot_rate = 0.05;
const auto timestep = std::chrono::weeks(4);
const auto tree_duration = std::chrono::years(5);
auto tree = markets::BinomialTree::create(tree_duration, timestep);
tree.setInitValue(spot_rate);

markets::BdtPropagator bdt(tree.numTimesteps(), vol, spot_rate);
tree.forwardPropagate(bdt);

markets::ArrowDebreauPropagator arrowdeb(tree, tree.numTimesteps());
markets::BinomialTree adtree(tree_duration, timestep);
adtree.setInitValue(1.0);
adtree.forwardPropagate(arrowdeb);

std::vector<double> yield_curve(tree.numTimesteps());
for (int t = 0; t < tree.numTimesteps(); ++t) {
double yrs = t * tree.exactTimestepInYears();
yield_curve[t] = spot_curve.getRate(yrs);
std::cout << "t:" << t << "   yrs:" << yrs << "  rate:" << yield_curve[t]
<< std::endl;
}

calibrate(tree, bdt, adtree, arrowdeb, yield_curve);


  for (int i = 1; i < 5; ++i) {
    std::cout << "Swap rate at time:" << i << " = "
              << markets::swapRate<markets::Period::kAnnual>(
                     adtree, std::chrono::years(i))
              << std::endl;
  }
              */

  float vol = 0.15875;  // Initial value

  const double expected_drift = 0.0;

  markets::DermanExampleVol dermanvol;
  markets::Volatility volsurface(dermanvol);

  auto asset_tree = markets::BinomialTree::create(
      std::chrono::months(38), std::chrono::days(10), markets::YearStyle::k360);

  markets::DermanChapter14Vol volsmile_example(100);
  markets::Volatility volsmilesurface(volsmile_example);
  auto localvol_asset_tree = markets::BinomialTree::create(
      std::chrono::months(36), std::chrono::days(10), markets::YearStyle::k360);
  markets::ZeroSpotCurve curve(
      {0.01, 1.0}, {0.04, 0.04}, markets::CompoundingPeriod::kContinuous);
  markets::LocalVolatilityPropagator lv_prop_with_rates(curve, 100.0);
  markets::StochasticTreeModel localvol_asset(std::move(localvol_asset_tree),
                                              lv_prop_with_rates);
  localvol_asset.forwardPropagate(volsmilesurface);

  float spot_price = 100;
  markets::CRRPropagator crr_prop(spot_price);
  markets::StochasticTreeModel asset(std::move(asset_tree), crr_prop);
  asset.forwardPropagate(volsurface);
  markets::NoDiscountingCurve none_curve;
  markets::Derivative deriv(asset.binomialTree(), &none_curve);
  asset.registerForUpdates(&deriv);

  markets::JarrowRuddPropagator jr_prop(expected_drift, spot_price);

  float deriv_expiry = 1.0;
  float strike = 100;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    markets::PlotVolSurface();
    markets::PlotForwardRateCurves();

    ImGui::Begin("Binomial Tree");

    static int current_item = 0;  // Index of the currently selected item
    const char* items[] = {"CRR",
                           "Jarrow-Rudd",
                           "Derman term structure example",
                           "Local vol example"};  // The options in the dropdown
    if (ImGui::BeginCombo("Select an option", items[current_item])) {
      for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
        bool is_selected = (current_item == n);  // Is this item selected?
        if (ImGui::Selectable(items[n],
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

    ImGui::SliderFloat("Volatility", &vol, 0.0f, 0.40f, "%.3f");

    ImGui::DragFloat("Spot", &spot_price, 0.1f, 0.0f, 200.0f, "%.2f");
    asset.updateSpot(spot_price);

    if (current_item == 0) {
      asset.forwardPropagate(markets::Volatility(markets::FlatVol(vol)));
    } else if (current_item == 1) {
      // asset.forwardPropagate(jr_prop);
    } else if (current_item == 2) {
      asset.forwardPropagate(volsurface);
    } else if (current_item == 3) {
      localvol_asset.forwardPropagate(volsmilesurface);
    }

    ImGui::BeginChild("Plot1", ImVec2(0, 0), true);  // true for border

    if (ImPlot::BeginPlot("Asset Tree Plot", ImVec2(-1, -1))) {
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
    ImGui::EndChild();

    ImGui::BeginChild("Plot2", ImVec2(0, 0), true);

    if (ImPlot::BeginPlot("Asset With Local Vol Tree Plot")) {
      const auto r = getTreeRenderData(localvol_asset.binomialTree());

      ImPlotStyle& style = ImPlot::GetStyle();
      style.MarkerSize = 1;

      if (!r.x_coords.empty()) {
        ImPlot::SetupAxisLimits(
            ImAxis_X1,
            0,
            localvol_asset.binomialTree().totalTimeAtIndex(
                localvol_asset.binomialTree().numTimesteps() - 1),
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
    ImGui::EndChild();

    ImGui::End();

    ImGui::Begin("Option Tree");
    ImGui::SliderFloat("Strike", &strike, 1.0f, 200.f, "%.2f");
    ImGui::SliderFloat("Expiry",
                       &deriv_expiry,
                       0.0f,
                       asset.binomialTree().treeDurationYears(),
                       "%.2f");

    if (current_item == 0) {
    } else if (current_item == 1) {
    }

    double computed_value = deriv.price(
        asset, std::bind_front(&markets::call_payoff, strike), deriv_expiry);
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

    if (ImPlot::BeginPlot("Option Tree Plot", ImVec2(-1, -1))) {
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
    ImGui::End();

    ImGui::Render();
    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(clear_color.x * clear_color.w,
                 clear_color.y * clear_color.w,
                 clear_color.z * clear_color.w,
                 clear_color.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();

  glfwDestroyWindow(window);
  glfwTerminate();

  return 0;
}