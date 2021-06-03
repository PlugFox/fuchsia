// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context,
    bytes::Bytes,
    fidl_fuchsia_pkg as pkg,
    futures::{future::ready, stream::once, AsyncReadExt, Stream},
    parking_lot::Mutex,
    serde::{Deserialize, Serialize},
    std::{
        convert::TryFrom,
        io,
        path::PathBuf,
        pin::Pin,
        sync::atomic::{AtomicUsize, Ordering},
    },
    tuf::{
        client::{Client, Config},
        crypto::KeyType,
        interchange::Json,
        metadata::{MetadataPath, MetadataVersion, RawSignedMetadata},
        repository::{EphemeralRepository, RepositoryProvider},
    },
};

mod file_system;
pub mod http_repository;
mod manager;
mod server;

pub use file_system::FileSystemRepository;
pub use http_repository::package_download;
pub use manager::{RepositoryManager, RepositorySpec};
pub use server::{RepositoryServer, RepositoryServerBuilder};

/// A unique ID which is given to every repository.
#[derive(Copy, Clone, Debug, PartialEq, Eq, Hash)]
pub struct RepositoryId(usize);

impl RepositoryId {
    fn new() -> Self {
        static NEXT_ID: AtomicUsize = AtomicUsize::new(0);
        RepositoryId(NEXT_ID.fetch_add(1, Ordering::Relaxed))
    }
}

/// The below types exist to provide definitions with Serialize.
/// TODO(fxbug.dev/76041) They should be removed in favor of the
/// corresponding fidl-fuchsia-pkg-ext types.
#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct RepositoryConfig {
    pub repo_url: Option<String>,
    pub root_keys: Option<Vec<RepositoryKeyConfig>>,
    pub root_version: Option<u32>,
    pub mirrors: Option<Vec<MirrorConfig>>,
}

impl Into<pkg::RepositoryConfig> for RepositoryConfig {
    fn into(self) -> pkg::RepositoryConfig {
        pkg::RepositoryConfig {
            repo_url: self.repo_url,
            root_keys: self.root_keys.map(|v| v.into_iter().map(|r| r.into()).collect()),
            root_version: self.root_version,
            mirrors: self.mirrors.map(|v| v.into_iter().map(|m| m.into()).collect()),
            ..pkg::RepositoryConfig::EMPTY
        }
    }
}

#[derive(Debug, Clone, Deserialize, PartialEq, Serialize)]
pub enum RepositoryKeyConfig {
    /// The raw ed25519 public key as binary data.
    Ed25519Key(Vec<u8>),
}

impl Into<pkg::RepositoryKeyConfig> for RepositoryKeyConfig {
    fn into(self) -> pkg::RepositoryKeyConfig {
        match self {
            Self::Ed25519Key(keys) => pkg::RepositoryKeyConfig::Ed25519Key(keys),
        }
    }
}

#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct MirrorConfig {
    /// The base URL of the TUF metadata on this mirror. Required.
    pub mirror_url: Option<String>,
    /// Whether or not to automatically monitor the mirror for updates. Required.
    pub subscribe: Option<bool>,
}

impl Into<pkg::MirrorConfig> for MirrorConfig {
    fn into(self) -> pkg::MirrorConfig {
        pkg::MirrorConfig {
            mirror_url: self.mirror_url,
            subscribe: self.subscribe,
            ..pkg::MirrorConfig::EMPTY
        }
    }
}
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("not found")]
    NotFound,
    #[error("invalid path '{0}'")]
    InvalidPath(PathBuf),
    #[error("I/O error")]
    Io(#[source] io::Error),
    #[error(transparent)]
    Other(#[from] anyhow::Error),
}

impl From<std::io::Error> for Error {
    fn from(err: std::io::Error) -> Self {
        if err.kind() == std::io::ErrorKind::NotFound {
            Error::NotFound
        } else {
            Error::Io(err)
        }
    }
}

/// [Resource] represents some resource as a stream of [Bytes] as provided from
/// a repository server.
pub struct Resource {
    /// The length of the file in bytes.
    pub len: u64,

    /// A stream of bytes representing the resource.
    pub stream: Pin<Box<dyn Stream<Item = io::Result<Bytes>> + Send + Unpin + 'static>>,
}

impl std::fmt::Debug for Resource {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("resource").field("len", &self.len).field("stream", &"..").finish()
    }
}

impl TryFrom<RepositoryConfig> for Resource {
    type Error = Error;
    fn try_from(config: RepositoryConfig) -> Result<Resource, Error> {
        let json = Bytes::from(serde_json::to_vec(&config).map_err(|e| anyhow::anyhow!(e))?);
        Ok(Resource { len: json.len() as u64, stream: Box::pin(once(ready(Ok(json)))) })
    }
}

#[derive(Clone, Debug, Default)]
pub struct RepositoryMetadata {
    root_keys: Vec<RepositoryKeyConfig>,
    root_version: u32,
}

impl RepositoryMetadata {
    pub fn new(root_keys: Vec<RepositoryKeyConfig>, root_version: u32) -> Self {
        Self { root_keys, root_version }
    }
}

pub struct Repository {
    /// The name of the repository.
    name: String,

    /// A unique ID for the repository, scoped to this instance of the daemon.
    id: RepositoryId,

    /// The TUF metadata for this repository
    metadata: RepositoryMetadata,

    /// Backend for this repository
    backend: Box<dyn RepositoryBackend + Send + Sync>,

    drop_handlers: Mutex<Vec<Box<dyn FnOnce() + Send + Sync>>>,
}

impl Repository {
    /// This should only be used in tests.
    pub fn new_with_metadata(
        name: &str,
        backend: Box<dyn RepositoryBackend + Sync + Send>,
        metadata: RepositoryMetadata,
    ) -> Self {
        Self {
            name: name.to_string(),
            id: RepositoryId::new(),
            backend,
            metadata,
            drop_handlers: Mutex::new(Vec::new()),
        }
    }

    pub fn id(&self) -> RepositoryId {
        self.id
    }

    /// Stores the given function to be run when the repository is dropped.
    pub fn on_drop<F: FnOnce() + Send + Sync + 'static>(&self, f: F) {
        self.drop_handlers.lock().push(Box::new(f));
    }

    pub async fn new(
        name: &str,
        backend: Box<dyn RepositoryBackend + Send + Sync>,
    ) -> Result<Self, Error> {
        let metadata = Self::get_metadata(backend.get_tuf_repo()?).await?;
        Ok(Self {
            name: name.to_string(),
            id: RepositoryId::new(),
            backend,
            metadata,
            drop_handlers: Mutex::new(Vec::new()),
        })
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    /// Get a [RepositorySpec] for this [Repository]
    pub fn spec(&self) -> RepositorySpec {
        self.backend.spec()
    }

    pub async fn fetch(&self, path: &str) -> Result<Resource, Error> {
        self.backend.fetch(path).await
    }

    pub async fn get_config(&self, mirror_url: &str) -> Result<RepositoryConfig, Error> {
        Ok(RepositoryConfig {
            repo_url: Some(format!("fuchsia-pkg://{}", self.name)),
            root_keys: Some(self.metadata.root_keys.clone()),
            root_version: Some(self.metadata.root_version),
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://{}", mirror_url)),
                subscribe: Some(false),
            }]),
        })
    }

    async fn get_metadata(
        tuf_repo: Box<dyn RepositoryProvider<Json>>,
    ) -> Result<RepositoryMetadata, Error> {
        let metadata_repo = EphemeralRepository::<Json>::new();

        let mut md = tuf_repo
            .fetch_metadata(
                &MetadataPath::new("root").context("fetching root metadata")?,
                &MetadataVersion::None,
                None,
                None,
            )
            .await
            .context("fetching metadata")?;
        let mut buf = Vec::new();
        md.read_to_end(&mut buf).await.context("reading metadata")?;

        let raw_signed_meta = RawSignedMetadata::<Json, _>::new(buf);

        let client =
            Client::with_trusted_root(Config::default(), &raw_signed_meta, metadata_repo, tuf_repo)
                .await
                .context("initializing client")?;

        let root_keys = client
            .trusted_root()
            .root_keys()
            .filter(|k| *k.typ() == KeyType::Ed25519)
            .map(|key| RepositoryKeyConfig::Ed25519Key(key.as_bytes().to_vec()))
            .collect::<Vec<_>>();
        let root_version = client.root_version();

        Ok(RepositoryMetadata::new(root_keys, root_version))
    }
}

impl Drop for Repository {
    fn drop(&mut self) {
        for handler in std::mem::take(&mut *self.drop_handlers.lock()) {
            (handler)()
        }
    }
}

#[async_trait::async_trait]
pub trait RepositoryBackend: std::fmt::Debug {
    /// Get a [RepositorySpec] for this [Repository]
    fn spec(&self) -> RepositorySpec;

    /// Fetch a [Resource] from this repository.
    async fn fetch(&self, path: &str) -> Result<Resource, Error>;

    /// Produces the backing TUF [RepositoryProvider] for this repository.
    fn get_tuf_repo(&self) -> Result<Box<dyn RepositoryProvider<Json>>, Error>;
}

#[cfg(test)]
mod test {
    use super::*;
    const ROOT_VERSION: u32 = 1;

    fn fake_root_key() -> Vec<RepositoryKeyConfig> {
        vec![RepositoryKeyConfig::Ed25519Key(vec![1, 2, 3, 4])]
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_config() {
        let d = tempfile::tempdir().unwrap();
        let repo = Repository::new_with_metadata(
            "devhost".into(),
            Box::new(FileSystemRepository::new(d.path().to_path_buf())),
            RepositoryMetadata::new(fake_root_key(), ROOT_VERSION),
        );

        let server_url = "some-url:1234";
        let expected = RepositoryConfig {
            repo_url: Some("fuchsia-pkg://devhost".to_string()),
            root_keys: Some(fake_root_key()),
            root_version: Some(ROOT_VERSION),
            mirrors: Some(vec![MirrorConfig {
                mirror_url: Some(format!("http://{}", server_url)),
                subscribe: Some(false),
            }]),
        };

        assert_eq!(repo.get_config(server_url).await.unwrap(), expected);
    }
}
