// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/process_update_action.h"

#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {

ProcessUpdateAction::ProcessUpdateAction(ActionContext* action_context,
                                         ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context),
      last_spoken_feedback_(zx::time::infinite_past()) {}

ProcessUpdateAction::~ProcessUpdateAction() = default;

void ProcessUpdateAction::Run(GestureContext gesture_context) {
  auto a11y_focus_manager = screen_reader_context_->GetA11yFocusManager();
  FX_DCHECK(a11y_focus_manager);

  auto a11y_focus = a11y_focus_manager->GetA11yFocus();
  if (!a11y_focus) {
    return;
  }

  // Before trying to describe potential changes of a semantic node, it can be the case that a
  // previous action registered itself to handle the updates and describe them.
  if (screen_reader_context_->has_on_node_update_callback()) {
    screen_reader_context_->run_and_clear_on_node_update_callback();
    screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged();
    return;
  }

  const zx::time now = async::Now(async_get_default_dispatcher());
  if (now - last_spoken_feedback_ <= zx::msec(1000)) {
    // Some nodes update too frequently. Avoid repeating them too often.
    return;
  }

  if (!screen_reader_context_->UpdateCacheIfDescribableA11yFocusedNodeContentChanged()) {
    // No changes to be spoken to user about the node in focus.
    return;
  }

  // Get the node in focus.
  const auto* focused_node = action_context_->semantics_source->GetSemanticNode(
      a11y_focus->view_ref_koid, a11y_focus->node_id);

  if (!focused_node) {
    return;
  }

  last_spoken_feedback_ = now;
  auto promise = BuildSpeechTaskFromNodePromise(a11y_focus->view_ref_koid, a11y_focus->node_id)
                     // Cancel any promises if this class goes out of scope.
                     .wrap_with(scope_);
  auto* executor = screen_reader_context_->executor();
  executor->schedule_task(std::move(promise));
}

}  // namespace a11y
