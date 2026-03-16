///////////////////////////////////////////////////////////////////////////////
//         Gigi Rapid Graphics Prototyping and Code Generation Suite         //
//        Copyright (c) 2024 Electronic Arts Inc. All rights reserved.       //
///////////////////////////////////////////////////////////////////////////////

#pragma once

#include <string>
#include "external/IconFontCppHeaders/IconsFontAwesome7.h"

struct ImVec4;

// ImGui helper to disable a UI section in the scope of the instance of this class
// RAII: Resource Acquisition Is Initialization meaning it used scope / destructor
class ImGuiEnable
{
public:
    // @param enable true:normal UI, false: disabled UI in local scope of this class
    explicit ImGuiEnable(bool enable);
    ~ImGuiEnable();

    bool m_enabled = false;
};

//
// like ImGui::InputText() but work with std::string (currently limited size)
// and adds ".." button with file open dialog.
// Known issues:
// * Tooltip only shows up when hovering over the label
// * When the usable width get too small the button should disappear
//
// Usage:
//   if (ImGui_File("File", desc.texture.fileName))
//       setSomethingDirty();
//   ShowToolTip("My tooltip");
//
// @param label see ImGui::InputText()
// @param e.g. filterList e.g. "*;jpeg;jpg;png;bmp;hdr;psd;tga;gif;pic;pgm;ppm"
// @return see ImGui::InputText()
bool ImGui_File(const char* label, std::string& inOutName, const char* filterList);

// like ImGui::Checkbox(); but right aligned like other properties
bool ImGui_Checkbox(const char* label, bool *value);

// replacement for ImGui::Checkbox(label, value) but more compact UI and with color
bool ImGui_CheckboxButton(const char* label, bool* value, ImVec4 color);

/*
// like MenuItem but prints the path differently from the filename for better user experience
// first path then file
// @param fileNameWithPath skipped if ""
// @return -1:delete, 0:nothing, 1:activate
int ImGui_PathFileMenuItem(const char* fileNameWithPath, int index);
*/

// like MenuItem but prints the path differently from the filename for better user experience
// first file, then path
// @return -1:delete, 0:nothing, 1:activate
int ImGui_FilePathMenuItem(const char* fileNameWithPath, int index);

bool ImGuiIconButton(const char* label, const char* icon);

// @param icon can be 0 e.g. "\xef\x80\x84" for Heart
// @param p_checked not 0 for checkbox
// @param shortcut only visually working, like ImGui
bool ImGuiMenuItem(const char* label, const char* icon, const char* shortcut, bool* p_checked = NULL, bool enabled = true);
inline bool ImGuiMenuItem(const char* label) { return ImGuiMenuItem(label, 0, 0, 0, true); }

bool ImGuiBeginMenu(const char* label, bool enabled = true);

void ImGuiRightAlign(const char* text);

// todo: use table? copy to clipboard
void ImGuiKeyValueString(const char* key, const char* value);

//
// Searchable combo box: a combo/dropdown that shows a text filter at the top
// of the popup so users can type-search large lists.
//
//Usage pattern:
//  if (BeginSearchableCombo("Format", currentLabel))
//  {
//      for (auto& item : items)
//      {
//          if (!SearchableComboFilter(item.name))
//              continue;
//          if (ImGui::Selectable(item.name, item == selected))
//              selected = item;
//      }
//      EndSearchableCombo();
//  }
//

struct SearchableComboState
{
    char filter[256] = {};
    bool justOpened = true;
};

// Replaces ImGui::BeginCombo(). Opens the combo popup with a search input at the top.
// Returns true if the popup is open (caller should then emit selectables and call EndSearchableCombo).
bool BeginSearchableCombo(const char* label, const char* preview_value, int flags = 0);

// Returns true if the given item text passes the current search filter (case-insensitive substring match).
bool SearchableComboFilter(const char* itemText);

// Replaces ImGui::EndCombo().
void EndSearchableCombo();