// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
#define PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_

#include <lib/async/dispatcher.h>

#include "lib/fxl/tasks/task_runner.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace ledger {

// Environment for the ledger application.
class Environment {
 public:
  explicit Environment(fxl::RefPtr<fxl::TaskRunner> main_runner,
                       async_t* async = nullptr);
  ~Environment();

  const fxl::RefPtr<fxl::TaskRunner> main_runner() { return main_runner_; }
  const async_t* async() { return async_; }

  coroutine::CoroutineService* coroutine_service() {
    return coroutine_service_.get();
  }

 private:
  fxl::RefPtr<fxl::TaskRunner> main_runner_;
  async_t* const async_;
  std::unique_ptr<coroutine::CoroutineService> coroutine_service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Environment);
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_ENVIRONMENT_ENVIRONMENT_H_
