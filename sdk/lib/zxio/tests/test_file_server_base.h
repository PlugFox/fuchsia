// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_
#define LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_

#include <fuchsia/io/llcpp/fidl_test_base.h>

#include <zxtest/zxtest.h>

namespace zxio_tests {

// This is a test friendly implementation of a fuchsia_io::File server
// that simply returns ZX_ERR_NOT_SUPPORTED for every operation other than
// fuchsia.io.File/Close.
class TestFileServerBase : public fuchsia_io::testing::File_TestBase {
 public:
  TestFileServerBase() = default;
  virtual ~TestFileServerBase() = default;

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) final {
    ADD_FAILURE("unexpected message received: %s", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  // Exercised by |zxio_close|.
  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
    // After the reply, we should close the connection.
    completer.Close(ZX_OK);
  }
};

}  // namespace zxio_tests

#endif  // LIB_ZXIO_TESTS_TEST_FILE_SERVER_BASE_H_
