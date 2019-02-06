// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/coroutine/coroutine_manager.h"

#include <memory>

#include <lib/callback/set_when_called.h>

#include "gtest/gtest.h"
#include "peridot/bin/ledger/coroutine/coroutine_impl.h"

namespace coroutine {
namespace {

TEST(CoroutineManager, CallbackIsCalled) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service);

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine(callback::SetWhenCalled(&called),
                         [&handler](CoroutineHandler* current_handler,
                                    fit::function<void()> callback) {
                           handler = current_handler;
                           EXPECT_EQ(ContinuationStatus::OK, handler->Yield());
                           callback();
                         });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  handler->Resume(ContinuationStatus::OK);
  EXPECT_TRUE(called);
}

TEST(CoroutineManager, InterruptCoroutineOnDestruction) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service);

  bool called = false;
  CoroutineHandler* handler = nullptr;
  manager->StartCoroutine(callback::SetWhenCalled(&called),
                          [&handler](CoroutineHandler* current_handler,
                                     fit::function<void()> callback) {
                            handler = current_handler;
                            EXPECT_EQ(ContinuationStatus::INTERRUPTED,
                                      handler->Yield());
                            callback();
                          });

  ASSERT_TRUE(handler);
  EXPECT_FALSE(called);
  manager.reset();
  EXPECT_TRUE(called);
}

TEST(CoroutineManager, NoCallback) {
  CoroutineServiceImpl coroutine_service;
  CoroutineManager manager(&coroutine_service);

  CoroutineHandler* handler = nullptr;
  manager.StartCoroutine([&handler](CoroutineHandler* current_handler) {
    handler = current_handler;
    EXPECT_EQ(ContinuationStatus::OK, handler->Yield());
  });

  ASSERT_TRUE(handler);
  handler->Resume(ContinuationStatus::OK);
}

TEST(CoroutineManager, DeleteInCallback) {
  CoroutineServiceImpl coroutine_service;
  std::unique_ptr<CoroutineManager> manager =
      std::make_unique<CoroutineManager>(&coroutine_service);

  manager->StartCoroutine([&manager] { manager.reset(); },
                          [](CoroutineHandler* current_handler,
                                     fit::function<void()> callback) {
                            callback();
                          });

}

}  // namespace
}  // namespace coroutine
