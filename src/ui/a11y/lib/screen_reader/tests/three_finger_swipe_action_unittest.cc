// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/three_finger_swipe_action.h"

#include "fuchsia/accessibility/gesture/cpp/fidl.h"
#include "lib/sys/cpp/testing/component_context_provider.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/lib/gesture_manager/tests/mocks/mock_gesture_listener.h"
#include "src/ui/a11y/lib/screen_reader/focus/tests/mocks/mock_a11y_focus_manager.h"
#include "src/ui/a11y/lib/screen_reader/screen_reader_action.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/tests/mocks/mock_tts_engine.h"
#include "src/ui/a11y/lib/tts/tts_manager.h"

namespace accessibility_test {
namespace {

const std::string kListenerUtterance = "Gesture Performed";
using fuchsia::accessibility::gesture::Type;

class ThreeFingerSwipeActionTest : public gtest::TestLoopFixture {
 public:
  ThreeFingerSwipeActionTest() {
    // Setup screen_reader_context_.
    screen_reader_context_ = std::make_unique<MockScreenReaderContext>();
    a11y_focus_manager_ptr_ = screen_reader_context_->mock_a11y_focus_manager_ptr();
    mock_speaker_ptr_ = screen_reader_context_->mock_speaker_ptr();
  }

  ~ThreeFingerSwipeActionTest() override = default;

  a11y::ScreenReaderAction::ActionContext action_context_;
  std::unique_ptr<MockScreenReaderContext> screen_reader_context_;
  MockA11yFocusManager* a11y_focus_manager_ptr_;
  MockScreenReaderContext::MockSpeaker* mock_speaker_ptr_;
  a11y::GestureListenerRegistry gesture_listener_registry_;
  MockGestureListener mock_gesture_listener_;
};

// Tests the case when the listener is not registered.
TEST_F(ThreeFingerSwipeActionTest, ListenerNotRegistered) {
  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_UP);

  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  EXPECT_FALSE(mock_gesture_listener_.is_registered());
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the case when the listener returns false status when OnGesture() is called. In this case,
// there shouldn't be any call to TTS even if Utterance is present.
TEST_F(ThreeFingerSwipeActionTest, UpSwipeListenerReturnsFalseStatus) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_UP);

  mock_gesture_listener_.SetOnGestureCallbackStatus(false);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);
  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  EXPECT_TRUE(mock_gesture_listener_.is_registered());
  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_UP);
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the case when the listener returns true status along with an empty utterance. In this case,
// TTS should not be called.
TEST_F(ThreeFingerSwipeActionTest, UpSwipeListenerReturnsEmptyUtterance) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_UP);

  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);
  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_UP);
  EXPECT_FALSE(mock_speaker_ptr_->ReceivedSpeak());
}

// Tests the case when three finger Up Swipe is performed with an utterance.
TEST_F(ThreeFingerSwipeActionTest, UpSwipePerformed) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_UP);

  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than UP_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);

  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_UP);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

// Tests the case when three finger Down Swipe is performed with an utterance.
TEST_F(ThreeFingerSwipeActionTest, DownSwipePerformed) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_DOWN);

  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than DOWN_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_UP);

  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_DOWN);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

// Tests the case when three finger Left Swipe is performed with an utterance.
TEST_F(ThreeFingerSwipeActionTest, LeftSwipePerformed) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_LEFT);

  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than LEFT_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);

  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_LEFT);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

// Tests the case when three finger Right Swipe is performed with an utterance.
TEST_F(ThreeFingerSwipeActionTest, RightSwipePerformed) {
  gesture_listener_registry_.Register(mock_gesture_listener_.NewBinding(), []() {});

  a11y::ThreeFingerSwipeAction three_finger_swipe_action(
      &action_context_, screen_reader_context_.get(), &gesture_listener_registry_,
      Type::THREE_FINGER_SWIPE_RIGHT);

  mock_gesture_listener_.SetOnGestureCallbackStatus(true);
  mock_gesture_listener_.SetUtterance(kListenerUtterance);
  // Set GestureType in the mock to something other than RIGHT_SWIPE, so that when OnGesture() is
  // called, we can confirm it's called with the correct gesture type.
  mock_gesture_listener_.SetGestureType(Type::THREE_FINGER_SWIPE_DOWN);

  three_finger_swipe_action.Run({});
  RunLoopUntilIdle();

  ASSERT_EQ(mock_gesture_listener_.gesture_type(), Type::THREE_FINGER_SWIPE_RIGHT);
  EXPECT_TRUE(mock_speaker_ptr_->ReceivedSpeak());
  ASSERT_EQ(mock_speaker_ptr_->messages().size(), 1u);
  EXPECT_EQ(mock_speaker_ptr_->messages()[0], kListenerUtterance);
}

}  // namespace
}  // namespace accessibility_test
