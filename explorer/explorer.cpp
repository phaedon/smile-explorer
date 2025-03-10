#include <stdio.h>

#include "asset_visualiser.h"
#include "explorer_params.h"
#include "global_rates.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "implot.h"
#include "implot3d.h"
#include "rate_curve_visualiser.h"
#include "time.h"
#include "trees/propagators.h"
#include "volatility/volatility.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

namespace smileexplorer {

void PlotVolSurface(const ExplorerParams& params) {
  TermStructureVolSurface ts_vol(params);
  Volatility volsurface(ts_vol);
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

}  // namespace smileexplorer

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

  GLFWwindow* window =
      glfwCreateWindow(1280, 720, "SmileExplorer", nullptr, nullptr);
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

  smileexplorer::GlobalRates global_rates;
  smileexplorer::ExplorerParams crr_prop_params(&global_rates);
  smileexplorer::ExplorerParams term_structure_params(&global_rates);
  smileexplorer::ExplorerParams jr_prop_params(&global_rates);
  smileexplorer::ExplorerParams localvol_prop_params(&global_rates);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImVec2 display_size = io.DisplaySize;

    const double window_spacing = 20;

    ImGui::SetNextWindowPos(ImVec2(display_size.x * 0.5, window_spacing));
    ImGui::SetNextWindowSize(
        ImVec2(display_size.x * 0.45, display_size.y * 0.45));
    smileexplorer::plotForwardRateCurves(crr_prop_params);

    ImGui::SetNextWindowPos(ImVec2(10, window_spacing));
    smileexplorer::displayPairedAssetDerivativePanel<
        smileexplorer::CRRPropagator,
        smileexplorer::ConstantVolSurface,
        smileexplorer::Derivative>("Cox-Ross-Rubinstein convention",
                                   crr_prop_params);

    ImGui::SetNextWindowPos(ImVec2(10, window_spacing * 2));
    smileexplorer::displayPairedAssetDerivativePanel<
        smileexplorer::JarrowRuddPropagator,
        smileexplorer::ConstantVolSurface,
        smileexplorer::Derivative>("Jarrow-Rudd convention", jr_prop_params);

    ImGui::SetNextWindowPos(ImVec2(10, window_spacing * 3));
    smileexplorer::displayPairedAssetDerivativePanel<
        smileexplorer::CRRPropagator,
        smileexplorer::TermStructureVolSurface,
        smileexplorer::Derivative>("Deterministic term-structure vol",
                                   term_structure_params);

    ImGui::SetNextWindowPos(ImVec2(10, window_spacing * 4));
    smileexplorer::displayPairedAssetDerivativePanel<
        smileexplorer::LocalVolatilityPropagator,
        smileexplorer::SigmoidSmile,
        smileexplorer::Derivative>("Smile with a negative sigmoid function",
                                   localvol_prop_params);

    ImGui::SetNextWindowPos(ImVec2(10, window_spacing * 5));
    smileexplorer::displayPairedAssetDerivativePanel<
        smileexplorer::JarrowRuddPropagator,
        smileexplorer::ConstantVolSurface,
        smileexplorer::CurrencyDerivative>("FX options", jr_prop_params);

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