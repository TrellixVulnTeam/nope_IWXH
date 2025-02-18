// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COCOA_CARD_UNMASK_PROMPT_VIEW_BRIDGE_H_
#define CHROME_BROWSER_UI_COCOA_CARD_UNMASK_PROMPT_VIEW_BRIDGE_H_

#include "base/mac/scoped_nsobject.h"
#include "base/memory/scoped_ptr.h"
#include "chrome/browser/ui/autofill/card_unmask_prompt_view.h"
#include "chrome/browser/ui/cocoa/constrained_window/constrained_window_mac.h"

namespace content {
class NavigationController;
}

@class CardUnmaskPromptViewCocoa;

namespace autofill {

class CardUnmaskPromptViewBridge : public CardUnmaskPromptView,
                                   public ConstrainedWindowMacDelegate {
 public:
  explicit CardUnmaskPromptViewBridge(CardUnmaskPromptController* controller);
  ~CardUnmaskPromptViewBridge() override;

  // CardUnmaskPromptView implementation:
  void ControllerGone() override;
  void DisableAndWaitForVerification() override;
  void GotVerificationResult(const base::string16& error_message,
                             bool allow_retry) override;

  // ConstrainedWindowMacDelegate implementation:
  void OnConstrainedWindowClosed(ConstrainedWindowMac* window) override;

  CardUnmaskPromptController* GetController();
  void PerformClose();

 private:
  scoped_ptr<ConstrainedWindowMac> constrained_window_;
  base::scoped_nsobject<CardUnmaskPromptViewCocoa> view_controller_;

  // The controller |this| queries for logic and state.
  CardUnmaskPromptController* controller_;
};

}  // autofill

@interface CardUnmaskPromptViewCocoa
    : NSViewController<NSWindowDelegate, NSTextFieldDelegate>

// Designated initializer. |bridge| must not be NULL.
- (id)initWithBridge:(autofill::CardUnmaskPromptViewBridge*)bridge;

- (void)setInputsEnabled:(BOOL)enabled;
- (void)updateVerifyButtonEnabled;

@end

#endif  // CHROME_BROWSER_UI_COCOA_CARD_UNMASK_PROMPT_VIEW_BRIDGE_H_
