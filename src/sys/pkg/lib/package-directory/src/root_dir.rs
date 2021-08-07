// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        meta_file::{MetaFile, MetaFileLocation},
        Error,
    },
    anyhow::{anyhow, Context as _},
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileProxy, NodeAttributes, NodeMarker, OPEN_FLAG_APPEND, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_ADMIN, OPEN_RIGHT_WRITABLE,
        VMO_FLAG_READ,
    },
    fuchsia_archive::AsyncReader,
    fuchsia_pkg::MetaContents,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon as zx,
    once_cell::sync::OnceCell,
    std::{
        collections::{HashMap, HashSet},
        convert::TryInto,
        sync::Arc,
    },
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            dirents_sink::AppendResult,
            entry::EntryInfo,
            entry_container::AsyncGetEntry,
            immutable::connection::io1::{ImmutableConnection, ImmutableConnectionClient},
            test_utils::reexport::Status as vfs_status,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        path::Path as VfsPath,
        test_utils::assertions::reexport::{DIRENT_TYPE_DIRECTORY, DIRENT_TYPE_FILE, INO_UNKNOWN},
    },
};

#[allow(dead_code)]
pub(crate) struct RootDir {
    blobfs: blobfs::Client,
    hash: fuchsia_hash::Hash,
    pub(crate) meta_far: FileProxy,
    pub(crate) meta_files: HashMap<String, MetaFileLocation>,
    pub(crate) non_meta_files: HashMap<String, fuchsia_hash::Hash>,
    pub(crate) filesystem: Arc<dyn vfs::filesystem::Filesystem>,
    meta_far_vmo: OnceCell<zx::Vmo>,
}

impl RootDir {
    pub(crate) async fn new(
        blobfs: blobfs::Client,
        hash: fuchsia_hash::Hash,
        filesystem: Arc<dyn vfs::filesystem::Filesystem>,
    ) -> Result<Self, Error> {
        let meta_far = blobfs.open_blob_for_read_no_describe(&hash).map_err(Error::OpenMetaFar)?;

        let reader = io_util::file::AsyncFile::from_proxy(Clone::clone(&meta_far));
        let mut async_reader = AsyncReader::new(reader).await.map_err(Error::ArchiveReader)?;
        let reader_list = async_reader.list();

        let mut meta_files = HashMap::with_capacity(reader_list.size_hint().0);

        for entry in reader_list {
            if entry.path().starts_with("meta/") {
                meta_files.insert(
                    String::from(entry.path()),
                    MetaFileLocation { offset: entry.offset(), length: entry.length() },
                );
            }
        }

        let meta_contents_bytes =
            async_reader.read_file("meta/contents").await.map_err(Error::ReadMetaContents)?;

        let non_meta_files: HashMap<_, _> = MetaContents::deserialize(&meta_contents_bytes[..])
            .map_err(Error::DeserializeMetaContents)?
            .into_contents()
            .into_iter()
            .collect();

        let meta_far_vmo = OnceCell::new();

        Ok(RootDir { blobfs, hash, meta_far, meta_files, non_meta_files, filesystem, meta_far_vmo })
    }

    fn get_entries(&self) -> Vec<(EntryInfo, String)> {
        let mut non_meta_keys = HashSet::new();
        let mut res = vec![];

        res.push((EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), "meta".to_string()));

        for key in self.non_meta_files.keys() {
            match key.split_once("/") {
                None => {
                    // TODO(fxbug.dev/81370) Replace .contains/.insert with .get_or_insert_owned when non-experimental.
                    if !non_meta_keys.contains(key) {
                        res.push((EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE), key.to_string()));
                        non_meta_keys.insert(key.to_string());
                    }
                }
                Some((first, _)) => {
                    if !non_meta_keys.contains(first) {
                        res.push((
                            EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY),
                            first.to_string(),
                        ));
                        non_meta_keys.insert(first.to_string());
                    }
                }
            }
        }

        res.sort_by(|a, b| a.1.cmp(&b.1));
        res
    }

    pub(crate) async fn meta_far_vmo(&self) -> Result<&zx::Vmo, anyhow::Error> {
        Ok(if let Some(vmo) = self.meta_far_vmo.get() {
            vmo
        } else {
            let (status, buffer) = self
                .meta_far
                .get_buffer(VMO_FLAG_READ)
                .await
                .context("meta.far .get_buffer() fidl error")?;
            let () = zx::Status::ok(status).context("meta.far .get_buffer protocol error")?;
            if let Some(buffer) = buffer {
                self.meta_far_vmo.get_or_init(|| buffer.vmo)
            } else {
                anyhow::bail!("meta.far get_buffer call succeeded but returned no VMO");
            }
        })
    }
}

impl vfs::directory::entry::DirectoryEntry for RootDir {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: VfsPath,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path.is_empty() {
            if flags
                & (OPEN_RIGHT_WRITABLE
                    | OPEN_RIGHT_ADMIN
                    | OPEN_FLAG_CREATE
                    | OPEN_FLAG_CREATE_IF_ABSENT
                    | OPEN_FLAG_TRUNCATE
                    | OPEN_FLAG_APPEND)
                != 0
            {
                let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_SUPPORTED);
                return;
            }

            let () = ImmutableConnection::create_connection(
                scope,
                OpenDirectory::new(self as Arc<dyn ImmutableConnectionClient>),
                flags,
                server_end,
            );
            return;
        }

        // vfs::path::Path::as_str() is an object relative path expression [1], except that it may:
        //   1. have a trailing "/"
        //   2. be exactly "."
        //   3. be longer than 4,095 bytes
        // The .is_empty() check above rules out "." and the following line removes the possible
        // trailing "/".
        // [1] https://fuchsia.dev/fuchsia-src/concepts/process/namespaces?hl=en#object_relative_path_expressions
        let canonical_path = path.as_ref().strip_suffix("/").unwrap_or_else(|| path.as_ref());

        if canonical_path == "meta" {
            let () = Arc::new(Meta::new(self)).open(scope, flags, mode, VfsPath::dot(), server_end);
            return;
        }

        if canonical_path.starts_with("meta/") {
            if let Some(meta_file) = self.meta_files.get(canonical_path).copied() {
                let () = Arc::new(MetaFile::new(self, meta_file)).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
                return;
            }

            let subdir_prefix = canonical_path.to_string() + "/";
            for k in self.meta_files.keys() {
                if k.starts_with(&subdir_prefix) {
                    let () = Arc::new(MetaSubdir::new(self, canonical_path.to_string())).open(
                        scope,
                        flags,
                        mode,
                        VfsPath::dot(),
                        server_end,
                    );
                    return;
                }
            }

            let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
            return;
        }

        if let Some(blob) = self.non_meta_files.get(canonical_path) {
            let () = self.blobfs.forward_open(blob, flags, mode, server_end).unwrap_or_else(|e| {
                fx_log_err!("Error forwarding content blob open to blobfs: {:#}", anyhow!(e))
            });
            return;
        }

        let subdir_prefix = canonical_path.to_string() + "/";
        for k in self.non_meta_files.keys() {
            if k.starts_with(&subdir_prefix) {
                let () = Arc::new(NonMetaSubdir::new(self, canonical_path.to_string())).open(
                    scope,
                    flags,
                    mode,
                    VfsPath::dot(),
                    server_end,
                );
                return;
            }
        }

        let () = send_on_open_with_error(flags, server_end, zx::Status::NOT_FOUND);
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl vfs::directory::entry_container::Directory for RootDir {
    // Used for linking which is not supported.
    fn get_entry<'a>(self: Arc<Self>, _: &'a str) -> AsyncGetEntry<'a> {
        AsyncGetEntry::from(vfs_status::NOT_SUPPORTED)
    }

    async fn read_dirents<'a>(
        &'a self,
        pos: &'a TraversalPosition,
        mut sink: Box<(dyn vfs::directory::dirents_sink::Sink + 'static)>,
    ) -> Result<
        (TraversalPosition, Box<(dyn vfs::directory::dirents_sink::Sealed + 'static)>),
        vfs_status,
    > {
        fn u64_to_usize_safe(u: u64) -> usize {
            let ret: usize = u.try_into().unwrap();
            static_assertions::assert_eq_size_val!(u, ret);
            ret
        }

        let starting_position = match pos {
            TraversalPosition::End => {
                return Ok((TraversalPosition::End, sink.seal()));
            }
            TraversalPosition::Name(_) => {
                // The VFS should never send this to us, since we never return it here.
                unreachable!();
            }
            TraversalPosition::Start => {
                match sink.append(&EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY), ".") {
                    AppendResult::Ok(new_sink) => sink = new_sink,
                    AppendResult::Sealed(sealed) => {
                        return Ok((TraversalPosition::Start, sealed));
                    }
                };
                0 as usize
            }
            TraversalPosition::Index(i) => u64_to_usize_safe(*i),
        };

        let entries = self.get_entries();

        for i in starting_position..entries.len() {
            let (info, name) = &entries[i];
            match sink.append(info, name) {
                AppendResult::Ok(new_sink) => sink = new_sink,
                AppendResult::Sealed(sealed) => {
                    return Ok((TraversalPosition::Index(crate::usize_to_u64_safe(i)), sealed));
                }
            }
        }
        Ok((TraversalPosition::End, sink.seal()))
    }

    fn register_watcher(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: fidl::AsyncChannel,
    ) -> Result<(), vfs_status> {
        Err(vfs_status::NOT_SUPPORTED)
    }

    // `register_watcher` is unsupported so no need to do anything here.
    fn unregister_watcher(self: Arc<Self>, _: usize) {}

    async fn get_attrs(&self) -> Result<NodeAttributes, vfs_status> {
        Ok(NodeAttributes {
            mode: 0, /* Populated by the VFS connection */
            id: 1,
            content_size: 0,
            storage_size: 0,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    fn close(&self) -> Result<(), vfs_status> {
        Ok(())
    }
}

#[allow(dead_code)]
struct Meta {
    root_dir: Arc<RootDir>,
}

impl Meta {
    pub fn new(root_dir: Arc<RootDir>) -> Self {
        Meta { root_dir }
    }
}

// TODO(fxbug.dev/75599)
impl vfs::directory::entry::DirectoryEntry for Meta {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[allow(dead_code)]
struct MetaSubdir {
    root_dir: Arc<RootDir>,
    path: String,
}

impl MetaSubdir {
    pub fn new(root_dir: Arc<RootDir>, path: String) -> Self {
        MetaSubdir { root_dir, path }
    }
}

// TODO(fxbug.dev/75600)
impl vfs::directory::entry::DirectoryEntry for MetaSubdir {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[allow(dead_code)]
struct NonMetaSubdir {
    root_dir: Arc<RootDir>,
    path: String,
}

impl NonMetaSubdir {
    pub fn new(root_dir: Arc<RootDir>, path: String) -> Self {
        NonMetaSubdir { root_dir, path }
    }
}

// TODO(fxbug.dev/75603)
impl vfs::directory::entry::DirectoryEntry for NonMetaSubdir {
    fn open(
        self: Arc<Self>,
        _: ExecutionScope,
        _: u32,
        _: u32,
        _: VfsPath,
        _: ServerEnd<NodeMarker>,
    ) {
        todo!()
    }
    fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
        EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)
    }
    fn can_hardlink(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::Filesystem,
        fidl::endpoints::{create_proxy, Proxy as _},
        fidl::{AsyncChannel, Channel},
        fidl_fuchsia_io::{DirectoryMarker, FileMarker, OPEN_RIGHT_READABLE},
        fuchsia_pkg_testing::{blobfs::Fake as FakeBlobfs, PackageBuilder},
        std::any::Any,
        vfs::directory::{
            dirents_sink::{self, Sealed, Sink},
            entry::DirectoryEntry,
            entry_container::Directory,
        },
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields_meta_far_only() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 0 }),
            (String::from("meta/package"), MetaFileLocation { offset: 4096, length: 38 }),
        ]
        .iter()
        .cloned()
        .collect();
        assert_eq!(root_dir.meta_files, meta_files);
        assert_eq!(root_dir.non_meta_files, HashMap::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn check_fields() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        let meta_files: HashMap<String, MetaFileLocation> = [
            (String::from("meta/contents"), MetaFileLocation { offset: 4096, length: 74 }),
            (String::from("meta/package"), MetaFileLocation { offset: 12288, length: 39 }),
            (String::from("meta/file"), MetaFileLocation { offset: 8192, length: 9 }),
        ]
        .iter()
        .cloned()
        .collect();
        assert_eq!(root_dir.meta_files, meta_files);
        let non_meta_files: HashMap<String, fuchsia_hash::Hash> = [(
            String::from("resource"),
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
                .parse::<fuchsia_hash::Hash>()
                .unwrap(),
        )]
        .iter()
        .cloned()
        .collect();
        assert_eq!(root_dir.non_meta_files, non_meta_files);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn hardlink() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        assert_eq!(root_dir.can_hardlink(), false);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_attrs() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        assert_eq!(
            root_dir.get_attrs().await.unwrap(),
            NodeAttributes {
                mode: 0,
                id: 1,
                content_size: 0,
                storage_size: 0,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            }
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn entry_info() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        assert_eq!(root_dir.entry_info(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn get_entry_not_supported() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir =
            Arc::new(RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap());

        match root_dir.get_entry("") {
            AsyncGetEntry::Future(_) => panic!("RootDir::get_entry should immediately fail"),
            AsyncGetEntry::Immediate(res) => {
                assert_eq!(res.err().unwrap(), vfs_status::NOT_SUPPORTED)
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn register_watcher_not_supported() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir =
            Arc::new(RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap());

        assert_eq!(
            root_dir.register_watcher(
                ExecutionScope::new(),
                0,
                AsyncChannel::from_channel(Channel::create().unwrap().0).unwrap()
            ),
            Err(vfs_status::NOT_SUPPORTED)
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn close() {
        let package = PackageBuilder::new("just-meta-far").build().await.expect("created pkg");
        let (metafar_blob, _) = package.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        assert_eq!(root_dir.close(), Ok(()));
    }

    #[derive(Clone)]
    struct DummySink {
        max_size: usize,
        entries: Vec<(String, EntryInfo)>,
        sealed: bool,
    }

    impl DummySink {
        pub fn new(max_size: usize) -> Self {
            DummySink { max_size, entries: Vec::with_capacity(max_size), sealed: false }
        }

        fn from_sealed(sealed: Box<dyn dirents_sink::Sealed>) -> Box<DummySink> {
            sealed.into()
        }
    }

    impl From<Box<dyn dirents_sink::Sealed>> for Box<DummySink> {
        fn from(sealed: Box<dyn dirents_sink::Sealed>) -> Self {
            sealed.open().downcast::<DummySink>().unwrap()
        }
    }

    impl Sink for DummySink {
        fn append(mut self: Box<Self>, entry: &EntryInfo, name: &str) -> AppendResult {
            assert!(!self.sealed);
            if self.entries.len() == self.max_size {
                AppendResult::Sealed(self.seal())
            } else {
                self.entries.push((name.to_owned(), entry.clone()));
                AppendResult::Ok(self)
            }
        }

        fn seal(mut self: Box<Self>) -> Box<dyn Sealed> {
            self.sealed = true;
            self
        }
    }

    impl Sealed for DummySink {
        fn open(self: Box<Self>) -> Box<dyn Any> {
            self
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_start() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        let (start_pos, sealed) = root_dir
            .read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(0)))
            .await
            .expect("read_dirents failed");
        assert_eq!(DummySink::from_sealed(sealed).entries, vec![]);
        assert_eq!(start_pos, TraversalPosition::Start);

        let (end_pos, sealed) = root_dir
            .read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(3)))
            .await
            .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))
            ]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_end() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();
        let (pos, sealed) = root_dir
            .read_dirents(&TraversalPosition::End, Box::new(DummySink::new(3)))
            .await
            .expect("read_dirents failed");
        assert_eq!(DummySink::from_sealed(sealed).entries, vec![]);
        assert_eq!(pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn read_dirents_index() {
        let pkg = PackageBuilder::new("base-package-0")
            .add_resource_at("resource", &[][..])
            .add_resource_at("meta/file", "meta/file".as_bytes())
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }
        let filesystem = Arc::new(Filesystem::new(8196));

        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();
        let (pos, sealed) = root_dir
            .read_dirents(&TraversalPosition::Start, Box::new(DummySink::new(2)))
            .await
            .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![
                (".".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
                ("meta".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_DIRECTORY)),
            ]
        );
        assert_eq!(pos, TraversalPosition::Index(1));

        let (end_pos, sealed) = root_dir
            .read_dirents(&pos, Box::new(DummySink::new(2)))
            .await
            .expect("read_dirents failed");
        assert_eq!(
            DummySink::from_sealed(sealed).entries,
            vec![("resource".to_string(), EntryInfo::new(INO_UNKNOWN, DIRENT_TYPE_FILE))]
        );
        assert_eq!(end_pos, TraversalPosition::End);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_self() {
        let pkg = PackageBuilder::new("pkg").build().await.unwrap();
        let (metafar_blob, _) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));
        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();
        let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();

        Arc::new(root_dir).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::dot(),
            server_end.into_channel().into(),
        );

        assert_eq!(
            files_async::readdir(&proxy).await.unwrap(),
            vec![files_async::DirEntry {
                name: "meta".to_string(),
                kind: files_async::DirentKind::Directory
            }]
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_content_blob() {
        let pkg = PackageBuilder::new("pkg")
            .add_resource_at("content-blob", &b"content-blob-contents"[..])
            .build()
            .await
            .unwrap();
        let (metafar_blob, content_blobs) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        for content in content_blobs {
            blobfs_fake.add_blob(content.merkle, content.contents);
        }
        let filesystem = Arc::new(Filesystem::new(8196));
        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();
        let (proxy, server_end) = create_proxy().unwrap();

        Arc::new(root_dir).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::validate_and_split("content-blob").unwrap(),
            server_end,
        );

        assert_eq!(
            io_util::file::read(&FileProxy::from_channel(proxy.into_channel().unwrap()))
                .await
                .unwrap(),
            b"content-blob-contents".to_vec()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_meta_file() {
        let pkg = PackageBuilder::new("pkg")
            .add_resource_at("meta/file", &b"meta-file-contents"[..])
            .build()
            .await
            .unwrap();
        let (metafar_blob, _) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(4 * 4096));
        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();
        let (proxy, server_end) = create_proxy::<FileMarker>().unwrap();

        Arc::new(root_dir).open(
            ExecutionScope::new(),
            OPEN_RIGHT_READABLE,
            0,
            VfsPath::validate_and_split("meta/file").unwrap(),
            server_end.into_channel().into(),
        );

        assert_eq!(io_util::file::read(&proxy).await.unwrap(), b"meta-file-contents".to_vec());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn meta_far_vmo() {
        let pkg = PackageBuilder::new("pkg").build().await.unwrap();
        let (metafar_blob, _) = pkg.contents();
        let (blobfs_fake, blobfs_client) = FakeBlobfs::new();
        blobfs_fake.add_blob(metafar_blob.merkle, metafar_blob.contents);
        let filesystem = Arc::new(Filesystem::new(8196));
        let root_dir = RootDir::new(blobfs_client, metafar_blob.merkle, filesystem).await.unwrap();

        // VMO is readable
        let vmo = root_dir.meta_far_vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, fuchsia_archive::MAGIC_INDEX_VALUE);

        // Accessing the VMO caches it
        assert!(root_dir.meta_far_vmo.get().is_some());

        // Accessing the VMO through the cached path works
        let vmo = root_dir.meta_far_vmo().await.unwrap();
        let mut buf = [0u8; 8];
        vmo.read(&mut buf, 0).unwrap();
        assert_eq!(buf, fuchsia_archive::MAGIC_INDEX_VALUE);
    }
}