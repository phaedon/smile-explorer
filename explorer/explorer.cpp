#include <stdio.h>

#include <Eigen/Dense>
#include <chrono>

#include "asset_visualiser.h"
#include "explorer_params.h"
#include "global_rates.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "implot.h"
#include "implot3d.h"
#include "rate_curve_visualiser.h"
#include "rates/rates_curve.h"
#include "time.h"
#include "trees/binomial_tree.h"
#include "trees/propagators.h"
#include "trees/stochastic_tree_model.h"
#include "volatility/volatility.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace markets {

struct DermanExampleVol {
  static constexpr VolSurfaceFnType type = VolSurfaceFnType::kTermStructure;
  double operator()(double t) const {
    if (t <= 1) return 0.2;
    if (t <= 2) return forwardVol(0, 1, 2, 0.2, 0.255);
    // return forwardVol(0, 2, 3, 0.255, 0.311);
    return forwardVol(0, 2, 3, 0.255, 0.22);
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
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

  ImGui::StyleColorsDark();

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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

  markets::GlobalRates global_rates;
  markets::ExplorerParams crr_prop_params(&global_rates);
  markets::ExplorerParams jr_prop_params(&global_rates);
  markets::ExplorerParams localvol_prop_params(&global_rates);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    markets::PlotVolSurface();
    markets::PlotForwardRateCurves(crr_prop_params);

    markets::displayPairedAssetDerivativePanel<markets::CRRPropagator,
                                               markets::ConstantVolSurface>(
        "Cox-Ross-Rubinstein convention", crr_prop_params);
    markets::displayPairedAssetDerivativePanel<markets::JarrowRuddPropagator,
                                               markets::ConstantVolSurface>(
        "Jarrow-Rudd convention", jr_prop_params);
    markets::displayPairedAssetDerivativePanel<
        markets::LocalVolatilityPropagator,
        markets::SigmoidSmile>("Smile with a negative sigmoid function",
                               localvol_prop_params);

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