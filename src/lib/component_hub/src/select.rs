// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::Directory,
    anyhow::Result,
    fuchsia_async::TimeoutExt,
    futures::future::{join_all, BoxFuture},
    futures::FutureExt,
};

static CAPABILITY_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);

// Given a v2 hub directory, collect components that expose |capability|.
// |name| and |moniker| correspond to the name and moniker of the current component
// respectively. This function is recursive and will find matching CMX and CML components.
pub fn find_components(
    capability: String,
    name: String,
    moniker: String,
    hub_dir: Directory,
) -> BoxFuture<'static, Result<Vec<String>>> {
    async move {
        let mut futures = vec![];
        let children_dir = hub_dir.open_dir("children")?;

        for child_name in children_dir.entries().await? {
            let child_moniker = format!("{}/{}", moniker, child_name);
            let child_hub_dir = children_dir.open_dir(&child_name)?;
            let child_future =
                find_components(capability.clone(), child_name, child_moniker, child_hub_dir);
            futures.push(child_future);
        }

        if name == "appmgr" {
            let realm_dir = hub_dir.open_dir("exec/out/hub")?;
            let appmgr_future = find_cmx_realms(capability.clone(), moniker.clone(), realm_dir);
            futures.push(appmgr_future);
        }

        let results = join_all(futures).await;
        let mut matching_components = vec![];
        for result in results {
            let mut result = result?;
            matching_components.append(&mut result);
        }

        if exposed_capability_exists_v2(hub_dir, capability).await? {
            matching_components.push(moniker);
        }

        Ok(matching_components)
    }
    .boxed()
}

// Given a v1 realm directory, collect components that expose |capability|.
// |moniker| corresponds to the moniker of the current realm.
fn find_cmx_realms(
    capability: String,
    moniker: String,
    realm_dir: Directory,
) -> BoxFuture<'static, Result<Vec<String>>> {
    async move {
        let mut futures = vec![];

        let c_dir = realm_dir.open_dir("c")?;

        for child_name in c_dir.entries().await? {
            let child_moniker = format!("{}/{}", moniker, child_name);
            let job_id_dir = c_dir.open_dir(&child_name)?;
            let child_future =
                find_cmx_components_skip_process_ids(capability.clone(), child_moniker, job_id_dir);
            futures.push(child_future);
        }

        let r_dir = realm_dir.open_dir("r")?;

        for realm_name in r_dir.entries().await? {
            let realm_moniker = format!("{}/{}", moniker, realm_name);
            let job_id_dir = r_dir.open_dir(&realm_name)?;
            let realm_future =
                find_cmx_realms_skip_job_ids(capability.clone(), realm_moniker, job_id_dir);
            futures.push(realm_future);
        }

        let results = join_all(futures).await;
        let mut matching_components = vec![];
        for result in results {
            let mut result = result?;
            matching_components.append(&mut result);
        }

        Ok(matching_components)
    }
    .boxed()
}

// Given a directory containing job IDs of a realm, collect components that expose |capability|.
// |moniker| corresponds to the moniker of the component.
fn find_cmx_realms_skip_job_ids(
    capability: String,
    moniker: String,
    job_id_dir: Directory,
) -> BoxFuture<'static, Result<Vec<String>>> {
    async move {
        let mut futures = vec![];
        for job_id in job_id_dir.entries().await? {
            let realm_dir = job_id_dir.open_dir(&job_id)?;
            let future = find_cmx_realms(capability.clone(), moniker.clone(), realm_dir);
            futures.push(future);
        }

        let results = join_all(futures).await;
        let mut matching_components = vec![];
        for result in results {
            let mut result = result?;
            matching_components.append(&mut result);
        }

        Ok(matching_components)
    }
    .boxed()
}

// Given a directory containing process IDs of a component, collect components that expose
// |capability|. |name| and |moniker| corresponds to the name and moniker of the component.
fn find_cmx_components_skip_process_ids(
    capability: String,
    moniker: String,
    job_id_dir: Directory,
) -> BoxFuture<'static, Result<Vec<String>>> {
    async move {
        let mut futures = vec![];
        for job_id in job_id_dir.entries().await? {
            let hub_dir = job_id_dir.open_dir(&job_id)?;
            let future = exposed_capability_exists_v1(hub_dir, capability.clone());
            futures.push(future);
        }

        let results = join_all(futures).await;
        let mut matching_components = vec![];
        for result in results {
            if result? {
                matching_components.push(moniker.clone());
            }
        }

        Ok(matching_components)
    }
    .boxed()
}

// Returns true if |capability| is exposed by this v2 component. False otherwise.
async fn exposed_capability_exists_v2(hub_dir: Directory, capability: String) -> Result<bool> {
    if !hub_dir.exists("resolved").await? {
        // We have no information about an unresolved component
        return Ok(false);
    }

    let exec_dir = hub_dir.open_dir("resolved/expose")?;
    let capabilities = get_capabilities(exec_dir).await?;
    Ok(capabilities.iter().any(|c| c.as_str() == capability))
}

// Returns true if |capability| is exposed by this v1 component. False otherwise.
async fn exposed_capability_exists_v1(hub_dir: Directory, capability: String) -> Result<bool> {
    if !hub_dir.exists("out").await? {
        // No `out` directory implies no exposed capabilities
        return Ok(false);
    }

    let out_dir = hub_dir.open_dir("out")?;
    let capabilities =
        get_capabilities(out_dir).on_timeout(CAPABILITY_TIMEOUT, || Ok(vec![])).await?;
    Ok(capabilities.iter().any(|c| c.as_str() == capability))
}

// Get all entries in a capabilities directory. If there is a "svc" directory, traverse it and
// collect all protocol names as well.
async fn get_capabilities(capability_dir: Directory) -> Result<Vec<String>> {
    let mut entries = capability_dir.entries().await?;

    for (index, name) in entries.iter().enumerate() {
        if name == "svc" {
            entries.remove(index);
            let svc_dir = capability_dir.open_dir("svc")?;
            let mut svc_entries = svc_dir.entries().await?;
            entries.append(&mut svc_entries);
            break;
        }
    }

    Ok(entries)
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        std::fs::{self, File},
        tempfile::TempDir,
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn unresolved_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        fs::create_dir(root.join("children")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components = find_components(
            "fuchsia.logger.LogSink".to_string(),
            ".".to_string(),
            ".".to_string(),
            hub_dir,
        )
        .await
        .unwrap();

        assert!(components.is_empty());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cml_protocol_found() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- resolved
        //    |- expose
        //       |- svc
        //          |- fuchsia.logger.LogSink
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/svc")).unwrap();
        File::create(root.join("resolved/expose/svc/fuchsia.logger.LogSink")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components = find_components(
            "fuchsia.logger.LogSink".to_string(),
            ".".to_string(),
            ".".to_string(),
            hub_dir,
        )
        .await
        .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.as_str(), ".");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cml_dir_found() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        // |- resolved
        //    |- expose
        //       |- hub
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose/hub")).unwrap();

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("hub".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.as_str(), ".");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn nested_cml() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //    |- core
        //       |- children
        //       |- resolved
        //          |- expose
        //             |- minfs
        // |- resolved
        //    |- expose
        fs::create_dir(root.join("children")).unwrap();
        fs::create_dir_all(root.join("resolved/expose")).unwrap();

        {
            let core = root.join("children/core");
            fs::create_dir_all(core.join("children")).unwrap();
            fs::create_dir_all(core.join("resolved/expose/minfs")).unwrap();
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("minfs".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.as_str(), "./core");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cmx() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- exec
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        //                            |- out
        //                               |- dev
        fs::create_dir(root.join("children")).unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
                fs::create_dir_all(sshd.join("out/dev")).unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("dev".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert_eq!(components.len(), 1);
        let component = &components[0];
        assert_eq!(component.as_str(), "./appmgr/sshd.cmx");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn cmx_no_out() {
        let test_dir = TempDir::new_in("/tmp").unwrap();
        let root = test_dir.path();

        // Create the following structure
        // .
        // |- children
        //       |- appmgr
        //          |- children
        //          |- exec
        //             |- out
        //                |- hub
        //                   |- r
        //                   |- c
        //                      |- sshd.cmx
        //                         |- 9898
        fs::create_dir(root.join("children")).unwrap();

        {
            let appmgr = root.join("children/appmgr");
            fs::create_dir(&appmgr).unwrap();
            fs::create_dir(appmgr.join("children")).unwrap();
            fs::create_dir_all(appmgr.join("exec/out/hub/r")).unwrap();

            {
                let sshd = appmgr.join("exec/out/hub/c/sshd.cmx/9898");
                fs::create_dir_all(&sshd).unwrap();
            }
        }

        let hub_dir = Directory::from_namespace(root.to_path_buf()).unwrap();
        let components =
            find_components("dev".to_string(), ".".to_string(), ".".to_string(), hub_dir)
                .await
                .unwrap();

        assert!(components.is_empty());
    }
}