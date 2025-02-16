#include <stdio.h>

#include <Eigen/Dense>
#include <chrono>

#include "binomial_tree.h"
#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "implot.h"
#include "markets/binomial_tree.h"
#include "markets/rates/arrow_debreu.h"
#include "markets/rates/bdt.h"
#include "markets/rates/swaps.h"
#include "markets/yield_curve.h"
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

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
  YieldCurve spot_curve({1, 2, 5, 7, 10},
                        {0.045, 0.0423, 0.0401, 0.0398, 0.0397});

  float vol = 0.2;  // Initial value
  double spot_rate = 0.05;
  const auto timestep = std::chrono::weeks(4);
  const auto tree_duration = std::chrono::years(5);
  markets::BinomialTree tree(tree_duration, timestep);
  tree.setInitValue(spot_rate);

  markets::BdtPropagator bdt(tree.numTimesteps(), vol, spot_rate);
  tree.forwardPropagate(bdt);

  markets::ArrowDebreauPropagator arrowdeb(tree, tree.numTimesteps());
  markets::BinomialTree adtree(tree_duration, timestep);
  adtree.setInitValue(1.0);
  adtree.forwardPropagate(arrowdeb);

  std::vector<double> yield_curve(tree.numTimesteps());
  for (int t = 0; t < tree.numTimesteps(); ++t) {
    double yrs = t * tree.timestep().count() / 365.;
    yield_curve[t] = spot_curve.getRate(yrs);
    std::cout << "t:" << t << "   yrs:" << yrs << "  rate:" << yield_curve[t]
              << std::endl;
  }

  // calibrate(tree, bdt, adtree, arrowdeb, yield_curve);

  for (int i = 1; i < 5; ++i) {
    std::cout << "Swap rate at time:" << i << " = "
              << markets::swapRate<markets::Period::kAnnual>(
                     adtree, std::chrono::years(i))
              << std::endl;
  }

  markets::CRRPropagator crr_prop(0.0, 0.2, 100);
  markets::JarrowRuddPropagator jr_prop(0.0, 0.2, 100);

  markets::BinomialTree asset(
      std::chrono::months(12), std::chrono::days(30), markets::YearStyle::k360);
  markets::BinomialTree deriv(
      std::chrono::months(12), std::chrono::days(30), markets::YearStyle::k360);
  double deriv_expiry = 1.0;

  asset.forwardPropagate(crr_prop);
  deriv.backPropagate(asset,
                      crr_prop,
                      std::bind_front(&markets::call_payoff, 100.0),
                      deriv_expiry);

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Binomial Tree");

    static int current_item = 0;  // Index of the currently selected item
    const char* items[] = {"CRR",
                           "Jarrow-Rudd"};  // The options in the dropdown
    if (ImGui::BeginCombo("Select an option", items[current_item])) {
      for (int n = 0; n < IM_ARRAYSIZE(items); n++) {
        bool is_selected = (current_item == n);        // Is this item selected?
        if (ImGui::Selectable(items[n], is_selected))  // If the item is clicked
        {
          current_item = n;  // Update the selection
        }
        if (is_selected)
          ImGui::SetItemDefaultFocus();  // Set the initial focus when opening
                                         // the combo
      }
      ImGui::EndCombo();
    }

    ImGui::SliderFloat("Volatility", &vol, 0.0f, 0.40f, "%.3f");
    crr_prop.updateVol(vol);
    jr_prop.updateVol(vol);
    // tree.forwardPropagate(bdt);
    // adtree.forwardPropagate(arrowdeb);
    if (current_item == 0) {
      asset.forwardPropagate(crr_prop);
      deriv.backPropagate(asset,
                          crr_prop,
                          std::bind_front(&markets::call_payoff, 100.0),
                          deriv_expiry);
    } else if (current_item == 1) {
      asset.forwardPropagate(jr_prop);
      deriv.backPropagate(asset,
                          jr_prop,
                          std::bind_front(&markets::call_payoff, 100.0),
                          deriv_expiry);
    }

    if (ImPlot::BeginPlot("Asset Tree Plot", ImVec2(-1, -1))) {
      const auto r = getTreeRenderData(asset);

      ImPlotStyle& style = ImPlot::GetStyle();
      style.MarkerSize = 1;

      if (!r.x_coords.empty()) {
        ImPlot::SetupAxisLimits(
            ImAxis_X1, 0, asset.numTimesteps(), ImPlotCond_Always);
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

    ImGui::Begin("Option Tree");
    if (ImPlot::BeginPlot("Option Tree Plot", ImVec2(-1, -1))) {
      const auto r = getTreeRenderData(deriv);

      ImPlotStyle& style = ImPlot::GetStyle();
      style.MarkerSize = 1;

      if (!r.x_coords.empty()) {
        ImPlot::SetupAxisLimits(
            ImAxis_X1, 0, asset.numTimesteps(), ImPlotCond_Always);
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