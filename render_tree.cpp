#include <stdio.h>

#include <Eigen/Dense>
#include <chrono>

#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include "imgui/imgui.h"
#include "implot.h"
#include "markets/binomial_tree.h"
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
      1280, 720, "Dear ImGui GLFW+OpenGL3 example", nullptr, nullptr);
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

  bool show_demo_window = true;
  ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

  auto timestep = std::chrono::weeks(2);
  markets::BinomialTree tree(std::chrono::years(2), timestep);
  tree.setInitValue(5.0);
  double vol = 0.15;  // Initial value
  tree.populateTreeForward(markets::genUpFn(vol, timestep),
                           markets::genDownFn(vol, timestep));
  std::vector<Eigen::Vector2d> nodes = markets::getNodes(tree);
  std::vector<double> x_coords, y_coords;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (show_demo_window) ImGui::ShowDemoWindow(&show_demo_window);

    {
      ImGui::Begin("Binomial Tree Control");

      // Volatility slider (only need this for the slider itself)
      ImGui::SliderFloat("Volatility", (float*)&vol, 0.0f, 0.40f, "%.3f");

      ImGui::End();
    }

    {
      ImGui::Begin("Binomial Tree");

      if (ImPlot::BeginPlot("Binomial Tree Plot", ImVec2(-1, -1))) {
        ImPlotStyle& style = ImPlot::GetStyle();
        style.MarkerSize = 1;

        // *** UPDATE DATA HERE, EVERY FRAME ***
        tree.populateTreeForward(
            markets::genUpFn(vol, timestep),  // Use current vol
            markets::genDownFn(vol, timestep));

        std::vector<Eigen::Vector2d> nodes = markets::getNodes(tree);

        std::vector<double> x_coords, y_coords;  // These can be local now
        for (const auto& node : nodes) {
          x_coords.push_back(node.x());
          y_coords.push_back(node.y());
        }

        if (!nodes.empty()) {
          ImPlot::SetupAxisLimits(
              ImAxis_X1, 0, tree.numTimesteps(), ImPlotCond_Always);
        }

        if (!y_coords.empty()) {
          auto [min_it, max_it] =
              std::minmax_element(y_coords.begin(), y_coords.end());
          double min_y = *min_it;
          double max_y = *max_it;
          ImPlot::SetupAxisLimits(ImAxis_Y1, 0, max_y, ImPlotCond_Always);
        }

        ImPlot::PlotScatter(
            "Nodes", x_coords.data(), y_coords.data(), x_coords.size());

        ImPlot::EndPlot();
      }

      ImGui::End();
    }

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