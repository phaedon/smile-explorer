#include <stdio.h>

#include <cmath>    // For exp, sqrt
#include <numeric>  // For std::iota (to generate time points easily)
#include <vector>   // To store path data for ImPlot

#include "imgui/imgui.h"
#include "imgui/imgui_impl_glfw.h"
#include "imgui/imgui_impl_opengl3.h"
#include "implot.h"     // For plotting functions
#include "prototype.h"  // Your header with generateUniformRandomPath
#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

// --- NEW INCLUDES FOR ABSL::RANDOM (if not already in prototype.h) ---
#include "absl/random/gaussian_distribution.h"  // For generating standard
// normal variates
// --- END NEW INCLUDES ---

static void glfw_error_callback(int error, const char* description) {
  fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

// Helper function to convert Eigen::VectorXd to std::vector<double> for ImPlot
std::vector<double> eigenVectorToStdVector(const Eigen::VectorXd& eigen_vec) {
  std::vector<double> std_vec(eigen_vec.size());
  Eigen::VectorXd::Map(&std_vec[0], eigen_vec.size()) = eigen_vec;
  return std_vec;
}

// Previous GBM simulation function (retained for comparison/future use),
// modified to pass absl::BitGen by reference as best practice.
void SimulateGBMPath(std::vector<double>& out_times,
                     std::vector<double>& out_prices,
                     double S0,
                     double r,
                     double sigma,
                     double T,
                     int num_steps,
                     absl::BitGen& bitgen) {  // Pass bitgen by reference

  out_times.clear();
  out_prices.clear();
  out_times.reserve(num_steps + 1);
  out_prices.reserve(num_steps + 1);

  double dt = T / num_steps;
  double current_S = S0;

  out_times.push_back(0.0);
  out_prices.push_back(S0);

  for (int i = 0; i < num_steps; ++i) {
    // Generate a standard normal random variable using the shared bitgen
    double Z = absl::Gaussian<double>(bitgen, 0.0, 1.0);  // Mean 0, Std Dev 1

    // Apply the discrete-time GBM formula
    current_S *=
        std::exp((r - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * Z);

    out_times.push_back((i + 1) * dt);
    out_prices.push_back(current_S);
  }
}

void SimulateMultipleGBMPaths(
    std::vector<double>&
        out_times,  // Single vector for time points (shared across paths)
    std::vector<std::vector<double>>&
        out_prices_paths,  // 2D vector for prices (each inner vector is a path)
    int num_paths,
    double S0,
    double r,
    double sigma,
    double T,
    int num_steps,
    absl::BitGen& bitgen) {  // Pass bitgen by reference

  // Clear previous data
  out_times.clear();
  out_prices_paths.clear();

  // Prepare time points (only once, as they are the same for all paths)
  out_times.reserve(num_steps + 1);
  double dt = T / num_steps;
  for (int i = 0; i <= num_steps; ++i) {
    out_times.push_back(i * dt);
  }

  // Simulate each path
  out_prices_paths.reserve(num_paths);  // Pre-allocate for outer vector
  for (int p = 0; p < num_paths; ++p) {
    std::vector<double> current_path_prices;
    current_path_prices.reserve(num_steps + 1);

    double current_S = S0;
    current_path_prices.push_back(S0);  // Price at t=0

    for (int i = 0; i < num_steps; ++i) {
      // CORRECTED: Use absl::Gaussian for standard normal random variable
      double Z = absl::Gaussian<double>(bitgen, 0.0, 1.0);  // Mean 0, Std Dev 1

      // Apply the discrete-time GBM formula
      current_S *=
          std::exp((r - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * Z);

      current_path_prices.push_back(current_S);
    }
    out_prices_paths.push_back(current_path_prices);
  }
}

int main(int, char**) {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  // GL 3.2 + GLSL 150 (Modern OpenGL on macOS/Linux)
  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

  GLFWwindow* window =
      glfwCreateWindow(1280, 720, "SmileExplorer", nullptr, nullptr);
  if (window == nullptr) return 1;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

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

  // --- DATA FOR PLOTTING generateUniformRandomPath ---
  int uniform_path_size = 100;
  Eigen::VectorXd uniform_random_values;
  std::vector<double> uniform_x_data;
  std::vector<double> uniform_y_data;

  // Function to update the uniform random path data
  auto updateUniformPathData = [&]() {
    uniform_random_values =
        smileexplorer::generateUniformRandomPath(uniform_path_size);
    uniform_y_data = eigenVectorToStdVector(uniform_random_values);
    uniform_x_data.resize(uniform_path_size);
    std::iota(uniform_x_data.begin(),
              uniform_x_data.end(),
              0);  // Fill with 0, 1, 2, ...
  };

  // Initial generation
  updateUniformPathData();

  // --- GBM PATH SIMULATION SETUP (INITIALIZATION) ---
  absl::BitGen
      shared_bitgen;  // One shared random generator for all robust simulations

  // GBM parameters
  double S0 = 100.0;
  double r = 0.05;
  double sigma = 0.20;
  double T = 1.0;
  int num_steps = 252;
  int num_gbm_paths = 10;  // Default number of paths

  std::vector<double> gbm_time_points;
  std::vector<std::vector<double>> gbm_simulated_prices_multi_paths;

  auto updateGbmPathsData = [&]() {
    SimulateMultipleGBMPaths(gbm_time_points,
                             gbm_simulated_prices_multi_paths,
                             num_gbm_paths,
                             S0,
                             r,
                             sigma,
                             T,
                             num_steps,
                             shared_bitgen);
  };

  updateGbmPathsData();
  // --- END GBM PATH SIMULATION SETUP ---

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // --- IMGUI WINDOW FOR UNIFORM RANDOM PATH ---
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_Once);

    if (ImGui::Begin("Uniform Random Path (generateUniformRandomPath)")) {
      if (ImGui::Button("Generate New Uniform Path")) {
        updateUniformPathData();
      }
      ImGui::SameLine();
      ImGui::Text("Size: %d", uniform_path_size);

      if (ImPlot::BeginPlot("Uniform Random Values",
                            "Index",
                            "Value",
                            ImVec2(-1, 0))) {  // -1 uses full width
        ImPlot::PlotLine("Uniform RVs",
                         uniform_x_data.data(),
                         uniform_y_data.data(),
                         uniform_y_data.size());
        ImPlot::EndPlot();
      }
      ImGui::End();
    }
    // --- END IMGUI WINDOW FOR UNIFORM RANDOM PATH ---

    // --- IMGUI WINDOW FOR GBM PATH ---
    ImGui::SetNextWindowPos(ImVec2(620, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(600, 350), ImGuiCond_Once);

    if (ImGui::Begin("Simulated Stock Paths (GBM)")) {
      // Slider for number of paths
      if (ImGui::SliderInt("Number of Paths", &num_gbm_paths, 1, 500)) {
        // Re-simulate paths immediately if slider value changes
        updateGbmPathsData();
      }

      if (ImGui::Button("Simulate New GBM Paths")) {
        updateGbmPathsData();
      }
      ImGui::SameLine();
      ImGui::Text("S0: %.2f, r: %.2f, sigma: %.2f, T: %.2f, steps: %d",
                  S0,
                  r,
                  sigma,
                  T,
                  num_steps);

      if (ImPlot::BeginPlot(
              "GBM Stock Price Simulation", "Time", "Price", ImVec2(-1, 0))) {
        for (size_t i = 0; i < gbm_simulated_prices_multi_paths.size(); ++i) {
          ImPlot::PlotLine((std::string("Path ") + std::to_string(i)).c_str(),
                           gbm_time_points.data(),
                           gbm_simulated_prices_multi_paths[i].data(),
                           gbm_time_points.size());
        }
        ImPlot::EndPlot();
      }
      ImGui::End();
    }
    // --- END IMGUI WINDOW FOR GBM PATH ---

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