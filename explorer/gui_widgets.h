#ifndef SMILEEXPLORER_EXPLORER_GUI_WIDGETS_
#define SMILEEXPLORER_EXPLORER_GUI_WIDGETS_

#include <magic_enum/magic_enum.hpp>

#include "explorer_params.h"
#include "global_rates.h"
#include "imgui/imgui.h"

namespace smileexplorer {

inline void displayCurrencyCombo(const char* label,
                                 size_t& selectedIndex,
                                 ExplorerParams& prop_params,
                                 std::function<void(Currency)> setCurrency) {
  constexpr auto currency_names = magic_enum::enum_names<Currency>();

  if (ImGui::BeginCombo(label, currency_names[selectedIndex].data())) {
    for (size_t n = 0; n < currency_names.size(); n++) {
      bool is_selected = (selectedIndex == n);
      if (ImGui::Selectable(currency_names[n].data(), is_selected)) {
        selectedIndex = n;
        setCurrency(
            magic_enum::enum_cast<Currency>(currency_names[selectedIndex])
                .value());
      }
      if (is_selected) ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }
}

inline void displayValueAsReadOnlyText(const char* label, double value) {
  std::string value_str = std::to_string(value);
  char buffer[64];
  strncpy(buffer, value_str.c_str(), sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';
  ImGui::InputText(
      label, buffer, IM_ARRAYSIZE(buffer), ImGuiInputTextFlags_ReadOnly);
}

}  // namespace smileexplorer

#endif  // SMILEEXPLORER_EXPLORER_GUI_WIDGETS_