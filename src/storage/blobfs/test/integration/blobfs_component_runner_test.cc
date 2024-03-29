// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.process.lifecycle/cpp/wire.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/service/llcpp/service.h>
#include <lib/zx/resource.h>

#include <gtest/gtest.h>

#include "src/lib/storage/block_client/cpp/fake_block_device.h"
#include "src/storage/blobfs/component_runner.h"
#include "src/storage/blobfs/mkfs.h"

namespace blobfs {
namespace {

constexpr uint32_t kBlockSize = 512;
constexpr uint32_t kNumBlocks = 8192;

class FakeDriverManagerAdmin final
    : public fidl::WireServer<fuchsia_device_manager::Administrator> {
 public:
  void Suspend(SuspendRequestView request, SuspendCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
  }

  void UnregisterSystemStorageForShutdown(
      UnregisterSystemStorageForShutdownRequestView request,
      UnregisterSystemStorageForShutdownCompleter::Sync& completer) override {
    unregister_was_called_ = true;
    completer.Reply(ZX_OK);
  }

  bool UnregisterWasCalled() { return unregister_was_called_; }

 private:
  std::atomic<bool> unregister_was_called_ = false;
};

class BlobfsComponentRunnerTest : public testing::Test {
 public:
  BlobfsComponentRunnerTest() : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {}

  void SetUp() override {
    ASSERT_EQ(loop_.StartThread("blobfs test dispatcher"), ZX_OK);

    device_ = std::make_unique<block_client::FakeBlockDevice>(kNumBlocks, kBlockSize);
    ASSERT_EQ(FormatFilesystem(device_.get(), FilesystemOptions{}), ZX_OK);
  }
  void TearDown() override {}

  void StartServe(fidl::ClientEnd<fuchsia_device_manager::Administrator> device_admin_client) {
    runner_ = std::make_unique<ComponentRunner>(loop_);
    auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    ASSERT_EQ(endpoints.status_value(), ZX_OK);
    auto status = runner_->ServeRoot(std::move(endpoints->server),
                                     fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle>(),
                                     std::move(device_admin_client), zx::resource());
    ASSERT_EQ(status.status_value(), ZX_OK);
    root_ = fidl::BindSyncClient(std::move(endpoints->client));
  }

  fidl::ClientEnd<fuchsia_io::Directory> GetSvcDir() {
    auto svc_endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
    EXPECT_EQ(svc_endpoints.status_value(), ZX_OK);
    root_->Open(fuchsia_io::wire::kOpenRightReadable | fuchsia_io::wire::kOpenRightWritable,
                fuchsia_io::wire::kModeTypeDirectory, "svc",
                fidl::ServerEnd<fuchsia_io::Node>(svc_endpoints->server.TakeChannel()));
    return std::move(svc_endpoints->client);
  }

  async::Loop loop_;
  std::unique_ptr<block_client::FakeBlockDevice> device_;
  std::unique_ptr<ComponentRunner> runner_;
  fidl::WireSyncClient<fuchsia_io::Directory> root_;
};

TEST_F(BlobfsComponentRunnerTest, ServeAndConfigureStartsBlobfs) {
  FakeDriverManagerAdmin driver_admin;
  auto admin_endpoints = fidl::CreateEndpoints<fuchsia_device_manager::Administrator>();
  ASSERT_TRUE(admin_endpoints.is_ok());
  fidl::BindServer(loop_.dispatcher(), std::move(admin_endpoints->server), &driver_admin);

  ASSERT_NO_FATAL_FAILURE(StartServe(std::move(admin_endpoints->client)));

  auto svc_dir = GetSvcDir();
  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);

  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(svc_dir.borrow());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  auto query_client = fidl::BindSyncClient(std::move(*query_client_end));

  auto query_res = query_client->GetInfo();
  ASSERT_EQ(query_res.status(), ZX_OK);
  ASSERT_TRUE(!query_res->result.is_err());
  ASSERT_EQ(query_res->result.response().info.fs_type, VFS_TYPE_BLOBFS);

  sync_completion_t callback_called;
  runner_->Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(callback_called);
  });
  ASSERT_EQ(sync_completion_wait(&callback_called, ZX_TIME_INFINITE), ZX_OK);

  EXPECT_TRUE(driver_admin.UnregisterWasCalled());
}

TEST_F(BlobfsComponentRunnerTest, ServeAndConfigureStartsBlobfsWithoutDriverManager) {
  ASSERT_NO_FATAL_FAILURE(StartServe(fidl::ClientEnd<fuchsia_device_manager::Administrator>()));

  auto svc_dir = GetSvcDir();
  auto client_end = service::ConnectAt<fuchsia_fs_startup::Startup>(svc_dir.borrow());
  ASSERT_EQ(client_end.status_value(), ZX_OK);

  MountOptions options;
  auto status = runner_->Configure(std::move(device_), options);
  ASSERT_EQ(status.status_value(), ZX_OK);

  auto query_client_end = service::ConnectAt<fuchsia_fs::Query>(svc_dir.borrow());
  ASSERT_EQ(query_client_end.status_value(), ZX_OK);
  auto query_client = fidl::BindSyncClient(std::move(*query_client_end));

  auto query_res = query_client->GetInfo();
  ASSERT_EQ(query_res.status(), ZX_OK);
  ASSERT_TRUE(!query_res->result.is_err());
  ASSERT_EQ(query_res->result.response().info.fs_type, VFS_TYPE_BLOBFS);

  sync_completion_t callback_called;
  runner_->Shutdown([callback_called = &callback_called](zx_status_t status) {
    EXPECT_EQ(status, ZX_OK);
    sync_completion_signal(callback_called);
  });
  ASSERT_EQ(sync_completion_wait(&callback_called, ZX_TIME_INFINITE), ZX_OK);
}

}  // namespace
}  // namespace blobfs
