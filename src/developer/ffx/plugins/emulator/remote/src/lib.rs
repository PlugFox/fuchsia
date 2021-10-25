// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ffx_core::ffx_plugin;
use ffx_emulator_common::vdl_files::VDLFiles;
use ffx_emulator_remote_args::RemoteCommand;

#[ffx_plugin("emu.remote.experimental")]
pub async fn remote(cmd: RemoteCommand) -> Result<(), anyhow::Error> {
    VDLFiles::new(cmd.sdk, false, None).await?.remote_emulator(&cmd)
}
