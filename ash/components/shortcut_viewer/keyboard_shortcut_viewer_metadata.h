// Copyright 2018 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_COMPONENTS_SHORTCUT_VIEWER_KEYBOARD_SHORTCUT_VIEWER_METADATA_H_
#define ASH_COMPONENTS_SHORTCUT_VIEWER_KEYBOARD_SHORTCUT_VIEWER_METADATA_H_

#include <vector>

#include "ash/components/shortcut_viewer/ksv_export.h"
#include "base/containers/span.h"
#include "base/optional.h"
#include "base/strings/string16.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/vector_icon_types.h"

namespace keyboard_shortcut_viewer {

struct KeyboardShortcutItem;
enum class ShortcutCategory;

// Returns a list of Ash and Chrome keyboard shortcuts metadata.
KSV_EXPORT const std::vector<KeyboardShortcutItem>&
GetKeyboardShortcutItemList();

base::string16 GetStringForCategory(ShortcutCategory category);

// Returns the string of a DomeKey for a given VKEY. VKEY needs to be mapped to
// a physical key |dom_code| and then the |dom_code| needs to be mapped to a
// meaning or character of |dom_key| based on the corresponding keyboard layout.
// TODO(https://crbug.com/803502): Get strings for non US keyboard layout.
base::string16 GetStringForKeyboardCode(ui::KeyboardCode key_code);

// Returns the VectorIcon if |key_code| need to be represented as an icon.
// Returns nullptr if |key_code| should not be represented as an icon.
const gfx::VectorIcon* GetVectorIconForKeyboardCode(ui::KeyboardCode key_code);

}  // namespace keyboard_shortcut_viewer

#endif  // ASH_COMPONENTS_SHORTCUT_VIEWER_KEYBOARD_SHORTCUT_VIEWER_METADATA_H_
