// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_config::get_sdk,
    ffx_core::ffx_plugin,
    ffx_scrutiny_fvm_args::ScrutinyFvmCommand,
    scrutiny_config::{Config, LaunchConfig, ModelConfig, RuntimeConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
};

#[ffx_plugin()]
pub async fn scrutiny_fvm(cmd: ScrutinyFvmCommand) -> Result<(), Error> {
    let sdk = get_sdk().await?;
    let blobfs = sdk.get_host_tool("blobfs")?;
    let config = Config {
        launch: LaunchConfig {
            command: Some(
                CommandBuilder::new("tool.fvm.extract")
                    .param("input", cmd.input)
                    .param("output", cmd.output)
                    .build(),
            ),
            script_path: None,
        },
        runtime: RuntimeConfig {
            model: ModelConfig { blobfs_tool_path: blobfs, ..ModelConfig::minimal() },
            ..RuntimeConfig::minimal()
        },
    };
    launcher::launch_from_config(config)?;

    Ok(())
}
