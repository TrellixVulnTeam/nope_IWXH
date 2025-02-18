// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_BUBBLE_DELEGATE_H_
#define CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_BUBBLE_DELEGATE_H_

// A delegate for the toolbar actions bar bubble, which briefly tells users
// about the toolbar redesign.
class ToolbarActionsBarBubbleDelegate {
 public:
  enum CloseAction {
    ACKNOWLEDGED,
    DISMISSED,
  };

  virtual void OnToolbarActionsBarBubbleClosed(CloseAction action) = 0;
};

#endif  // CHROME_BROWSER_UI_TOOLBAR_TOOLBAR_ACTIONS_BAR_BUBBLE_DELEGATE_H_
