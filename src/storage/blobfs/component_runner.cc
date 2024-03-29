// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/component_runner.h"

#include <fidl/fuchsia.fs.startup/cpp/wire.h>
#include <fidl/fuchsia.fs/cpp/wire.h>
#include <fidl/fuchsia.update.verify/cpp/wire.h>
#include <lib/inspect/service/cpp/service.h>
#include <lib/syslog/cpp/macros.h>

#include "src/storage/blobfs/admin_service.h"
#include "src/storage/blobfs/service/lifecycle.h"

namespace blobfs {

ComponentRunner::ComponentRunner(async::Loop& loop) : fs::PagedVfs(loop.dispatcher()), loop_(loop) {
  outgoing_ = fbl::MakeRefCounted<fs::PseudoDir>(this);
  svc_ = fbl::MakeRefCounted<fs::PseudoDir>(this);
  outgoing_->AddEntry("svc", svc_);

  FX_LOGS(INFO) << "setting up startup service";
  startup_svc_ = fbl::MakeRefCounted<StartupService>(
      loop_.dispatcher(), [this](std::unique_ptr<BlockDevice> device, const MountOptions& options) {
        FX_LOGS(INFO) << "configure callback is called";
        zx::status<> status = Configure(std::move(device), options);
        if (status.is_error()) {
          FX_LOGS(ERROR) << "Could not configure blobfs: " << status.status_string();
        }
        return status;
      });
  svc_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs_startup::Startup>, startup_svc_);
}

void ComponentRunner::RemoveSystemDrivers(fit::callback<void(zx_status_t)> callback) {
  // If we don't have a connection to Driver Manager, just return ZX_OK.
  if (!driver_admin_.is_valid()) {
    FX_LOGS(INFO) << "blobfs doesn't have driver manager connection; assuming test environment";
    callback(ZX_OK);
    return;
  }

  using Unregister = fuchsia_device_manager::Administrator::UnregisterSystemStorageForShutdown;
  driver_admin_->UnregisterSystemStorageForShutdown(
      [callback = std::move(callback)](fidl::WireUnownedResult<Unregister>& result) mutable {
        if (!result.ok()) {
          callback(result.status());
          return;
        }
        callback(result->status);
      });
}

void ComponentRunner::Shutdown(fs::FuchsiaVfs::ShutdownCallback cb) {
  TRACE_DURATION("blobfs", "ComponentRunner::Shutdown");
  // Before shutting down blobfs, we need to try to shut down any drivers that are running out of
  // it, because right now those drivers don't have an explicit dependency on blobfs in the
  // component hierarchy so they don't get shut down before us yet.
  RemoveSystemDrivers([this, cb = std::move(cb)](zx_status_t status) mutable {
    // If we failed to notify the driver stack about the impending shutdown, log a warning, but
    // continue the shutdown.
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "failed to send shutdown signal to driver manager: "
                       << zx_status_get_string(status);
    }
    // Shutdown all external connections to blobfs.
    ManagedVfs::Shutdown([this, cb = std::move(cb)](zx_status_t status) mutable {
      async::PostTask(dispatcher(), [this, status, cb = std::move(cb)]() mutable {
        // Manually destroy the filesystem. The promise of Shutdown is that no
        // connections are active, and destroying the Runner object
        // should terminate all background workers.
        blobfs_ = nullptr;

        // Tell the mounting thread that the filesystem has terminated.
        loop_.Quit();

        // Tell the unmounting channel that we've completed teardown. This *must* be the last thing
        // we do because after this, the caller can assume that it's safe to destroy the runner.
        cb(status);
      });
    });
  });
}

zx::status<fs::FilesystemInfo> ComponentRunner::GetFilesystemInfo() {
  return blobfs_->GetFilesystemInfo();
}

zx::status<> ComponentRunner::ServeRoot(
    fidl::ServerEnd<fuchsia_io::Directory> root,
    fidl::ServerEnd<fuchsia_process_lifecycle::Lifecycle> lifecycle,
    fidl::ClientEnd<fuchsia_device_manager::Administrator> driver_admin_client,
    zx::resource vmex_resource) {
  LifecycleServer::Create(
      loop_.dispatcher(),
      [this](fs::FuchsiaVfs::ShutdownCallback cb) { this->Shutdown(std::move(cb)); },
      std::move(lifecycle));

  fidl::WireSharedClient<fuchsia_device_manager::Administrator> driver_admin;
  if (driver_admin_client.is_valid()) {
    driver_admin = fidl::WireSharedClient<fuchsia_device_manager::Administrator>(
        std::move(driver_admin_client), loop_.dispatcher());
  }
  driver_admin_ = std::move(driver_admin);

  vmex_resource_ = std::move(vmex_resource);
  zx_status_t status = ServeDirectory(outgoing_, std::move(root));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "mount failed; could not serve root directory";
    return zx::error(status);
  }

  return zx::ok();
}

zx::status<> ComponentRunner::Configure(std::unique_ptr<BlockDevice> device,
                                        const MountOptions& options) {
  if (auto status = Init(); status.is_error()) {
    FX_LOGS(ERROR) << "configure failed; vfs init failed";
    return status.take_error();
  }

  auto blobfs_or = Blobfs::Create(loop_.dispatcher(), std::move(device), this, options,
                                  std::move(vmex_resource_));
  if (blobfs_or.is_error()) {
    FX_LOGS(ERROR) << "configure failed; could not create blobfs";
    return blobfs_or.take_error();
  }
  blobfs_ = std::move(blobfs_or.value());
  SetReadonly(blobfs_->writability() != Writability::Writable);

  fbl::RefPtr<fs::Vnode> root;
  zx_status_t status = blobfs_->OpenRootNode(&root);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "configure failed; could not get root blob";
    return zx::error(status);
  }

  // Specify to fall back to DeepCopy mode instead of Live mode (the default) on failures to send
  // a Frozen copy of the tree (e.g. if we could not create a child copy of the backing VMO).
  // This helps prevent any issues with querying the inspect tree while the filesystem is under
  // load, since snapshots at the receiving end must be consistent. See fxbug.dev/57330 for details.
  inspect::TreeHandlerSettings settings{.snapshot_behavior =
                                            inspect::TreeServerSendPreference::Frozen(
                                                inspect::TreeServerSendPreference::Type::DeepCopy)};

  auto inspect_tree = fbl::MakeRefCounted<fs::Service>(
      [connector = inspect::MakeTreeHandler(blobfs_->GetMetrics()->inspector(), loop_.dispatcher(),
                                            settings)](zx::channel chan) mutable {
        connector(fidl::InterfaceRequest<fuchsia::inspect::Tree>(std::move(chan)));
        return ZX_OK;
      });

  outgoing_->AddEntry(kOutgoingDataRoot, std::move(root));

  auto diagnostics_dir = fbl::MakeRefCounted<fs::PseudoDir>(this);
  outgoing_->AddEntry("diagnostics", diagnostics_dir);
  diagnostics_dir->AddEntry(fuchsia::inspect::Tree::Name_, inspect_tree);

  query_svc_ = fbl::MakeRefCounted<fs::QueryService>(this);
  svc_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Query>, query_svc_);

  health_check_svc_ = fbl::MakeRefCounted<HealthCheckService>(loop_.dispatcher(), *blobfs_);
  svc_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_update_verify::BlobfsVerifier>,
                 health_check_svc_);

  svc_->AddEntry(fidl::DiscoverableProtocolName<fuchsia_fs::Admin>,
                 fbl::MakeRefCounted<AdminService>(blobfs_->dispatcher(),
                                                   [this](fs::FuchsiaVfs::ShutdownCallback cb) {
                                                     this->Shutdown(std::move(cb));
                                                   }));

  return zx::ok();
}

}  // namespace blobfs
