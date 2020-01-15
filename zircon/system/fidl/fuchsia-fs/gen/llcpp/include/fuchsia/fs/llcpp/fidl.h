// WARNING: This file is machine generated by fidlgen.

#pragma once

#include <lib/fidl/internal.h>
#include <lib/fidl/txn_header.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/connect_service.h>
#include <lib/fidl/llcpp/service_handler_interface.h>
#include <lib/fidl/llcpp/string_view.h>
#include <lib/fidl/llcpp/sync_call.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fidl/llcpp/vector_view.h>
#include <lib/fit/function.h>
#include <lib/zx/channel.h>
#include <lib/zx/event.h>
#include <zircon/fidl.h>

#include <fuchsia/io2/llcpp/fidl.h>

namespace llcpp {

namespace fuchsia {
namespace fs {

enum class FsType : uint32_t {
  BLOBFS = 2657701153u,
  MINFS = 1852394785u,
  MEMFS = 1047088417u,
};


struct FilesystemInfo;
struct Query_GetInfo_Response;
struct Query_GetInfo_Result;
class FilesystemInfoQuery final {
public:
  constexpr FilesystemInfoQuery() : value_(0u) {}
  explicit constexpr FilesystemInfoQuery(uint64_t value) : value_(value) {}
  const static FilesystemInfoQuery TOTAL_BYTES;
  const static FilesystemInfoQuery USED_BYTES;
  const static FilesystemInfoQuery TOTAL_NODES;
  const static FilesystemInfoQuery USED_NODES;
  const static FilesystemInfoQuery FREE_SHARED_POOL_BYTES;
  const static FilesystemInfoQuery FS_ID;
  const static FilesystemInfoQuery BLOCK_SIZE;
  const static FilesystemInfoQuery MAX_NODE_NAME_SIZE;
  const static FilesystemInfoQuery FS_TYPE;
  const static FilesystemInfoQuery NAME;
  const static FilesystemInfoQuery DEVICE_PATH;
  const static FilesystemInfoQuery mask;

  explicit constexpr inline operator uint64_t() const { return value_; }
  explicit constexpr inline operator bool() const { return static_cast<bool>(value_); }
  constexpr inline bool operator==(const FilesystemInfoQuery& other) const { return value_ == other.value_; }
  constexpr inline bool operator!=(const FilesystemInfoQuery& other) const { return value_ != other.value_; }
  constexpr inline FilesystemInfoQuery operator~() const;
  constexpr inline FilesystemInfoQuery operator|(const FilesystemInfoQuery& other) const;
  constexpr inline FilesystemInfoQuery operator&(const FilesystemInfoQuery& other) const;
  constexpr inline FilesystemInfoQuery operator^(const FilesystemInfoQuery& other) const;
  constexpr inline void operator|=(const FilesystemInfoQuery& other);
  constexpr inline void operator&=(const FilesystemInfoQuery& other);
  constexpr inline void operator^=(const FilesystemInfoQuery& other);

private:
  uint64_t value_;
};
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::TOTAL_BYTES = ::llcpp::fuchsia::fs::FilesystemInfoQuery(1u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::USED_BYTES = ::llcpp::fuchsia::fs::FilesystemInfoQuery(2u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::TOTAL_NODES = ::llcpp::fuchsia::fs::FilesystemInfoQuery(4u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::USED_NODES = ::llcpp::fuchsia::fs::FilesystemInfoQuery(8u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::FREE_SHARED_POOL_BYTES = ::llcpp::fuchsia::fs::FilesystemInfoQuery(16u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::FS_ID = ::llcpp::fuchsia::fs::FilesystemInfoQuery(32u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::BLOCK_SIZE = ::llcpp::fuchsia::fs::FilesystemInfoQuery(64u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::MAX_NODE_NAME_SIZE = ::llcpp::fuchsia::fs::FilesystemInfoQuery(128u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::FS_TYPE = ::llcpp::fuchsia::fs::FilesystemInfoQuery(256u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::NAME = ::llcpp::fuchsia::fs::FilesystemInfoQuery(512u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::DEVICE_PATH = ::llcpp::fuchsia::fs::FilesystemInfoQuery(1024u);
constexpr const ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::mask = ::llcpp::fuchsia::fs::FilesystemInfoQuery(2047u);

constexpr inline ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::operator~() const {
  return ::llcpp::fuchsia::fs::FilesystemInfoQuery(static_cast<uint64_t>(~this->value_ & mask.value_));
}

constexpr inline ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::operator|(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) const {
  return ::llcpp::fuchsia::fs::FilesystemInfoQuery(static_cast<uint64_t>(this->value_ | other.value_));
}

constexpr inline ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::operator&(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) const {
  return ::llcpp::fuchsia::fs::FilesystemInfoQuery(static_cast<uint64_t>(this->value_ & other.value_));
}

constexpr inline ::llcpp::fuchsia::fs::FilesystemInfoQuery FilesystemInfoQuery::operator^(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) const {
  return ::llcpp::fuchsia::fs::FilesystemInfoQuery(static_cast<uint64_t>(this->value_ ^ other.value_));
}

constexpr inline void FilesystemInfoQuery::operator|=(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) {
  this->value_ |= other.value_;
}

constexpr inline void FilesystemInfoQuery::operator&=(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) {
  this->value_ &= other.value_;
}

constexpr inline void FilesystemInfoQuery::operator^=(
    const ::llcpp::fuchsia::fs::FilesystemInfoQuery& other) {
  this->value_ ^= other.value_;
}

class Query;

extern "C" const fidl_type_t v1_fuchsia_fs_FilesystemInfoTable;

// Information about a filesystem.
//
// If a particular field is not applicable or not supported, implementations
// should leave it absent.
struct FilesystemInfo final : private ::fidl::VectorView<fidl_envelope_t> {
  using EnvelopesView = ::fidl::VectorView<fidl_envelope_t>;
 public:
  // Returns whether no field is set.
  bool IsEmpty() const { return EnvelopesView::empty(); }

  // The number of data bytes which may be stored in the filesystem.
  const uint64_t& total_bytes() const {
    ZX_ASSERT(has_total_bytes());
    return *reinterpret_cast<const uint64_t*>(EnvelopesView::at(1 - 1).data);
  }
  uint64_t& total_bytes() {
    ZX_ASSERT(has_total_bytes());
    return *reinterpret_cast<uint64_t*>(EnvelopesView::at(1 - 1).data);
  }
  bool has_total_bytes() const {
    return EnvelopesView::count() >= 1 && EnvelopesView::at(1 - 1).data != nullptr;
  }

  // The number of data bytes which are in use by the filesystem.
  // Note that this value may change in the mean time.
  const uint64_t& used_bytes() const {
    ZX_ASSERT(has_used_bytes());
    return *reinterpret_cast<const uint64_t*>(EnvelopesView::at(2 - 1).data);
  }
  uint64_t& used_bytes() {
    ZX_ASSERT(has_used_bytes());
    return *reinterpret_cast<uint64_t*>(EnvelopesView::at(2 - 1).data);
  }
  bool has_used_bytes() const {
    return EnvelopesView::count() >= 2 && EnvelopesView::at(2 - 1).data != nullptr;
  }

  // The number of nodes which may be stored in the filesystem.
  const uint64_t& total_nodes() const {
    ZX_ASSERT(has_total_nodes());
    return *reinterpret_cast<const uint64_t*>(EnvelopesView::at(3 - 1).data);
  }
  uint64_t& total_nodes() {
    ZX_ASSERT(has_total_nodes());
    return *reinterpret_cast<uint64_t*>(EnvelopesView::at(3 - 1).data);
  }
  bool has_total_nodes() const {
    return EnvelopesView::count() >= 3 && EnvelopesView::at(3 - 1).data != nullptr;
  }

  // The number of nodes used by the filesystem.
  // Note that this value may change in the mean time.
  const uint64_t& used_nodes() const {
    ZX_ASSERT(has_used_nodes());
    return *reinterpret_cast<const uint64_t*>(EnvelopesView::at(4 - 1).data);
  }
  uint64_t& used_nodes() {
    ZX_ASSERT(has_used_nodes());
    return *reinterpret_cast<uint64_t*>(EnvelopesView::at(4 - 1).data);
  }
  bool has_used_nodes() const {
    return EnvelopesView::count() >= 4 && EnvelopesView::at(4 - 1).data != nullptr;
  }

  // The amount of space which may be allocated from the underlying
  // volume manager. Note that this value may change in the mean time.
  const uint64_t& free_shared_pool_bytes() const {
    ZX_ASSERT(has_free_shared_pool_bytes());
    return *reinterpret_cast<const uint64_t*>(EnvelopesView::at(5 - 1).data);
  }
  uint64_t& free_shared_pool_bytes() {
    ZX_ASSERT(has_free_shared_pool_bytes());
    return *reinterpret_cast<uint64_t*>(EnvelopesView::at(5 - 1).data);
  }
  bool has_free_shared_pool_bytes() const {
    return EnvelopesView::count() >= 5 && EnvelopesView::at(5 - 1).data != nullptr;
  }

  // A globally unique identifier for this filesystem instance.
  const ::zx::event& fs_id() const {
    ZX_ASSERT(has_fs_id());
    return *reinterpret_cast<const ::zx::event*>(EnvelopesView::at(6 - 1).data);
  }
  ::zx::event& fs_id() {
    ZX_ASSERT(has_fs_id());
    return *reinterpret_cast<::zx::event*>(EnvelopesView::at(6 - 1).data);
  }
  bool has_fs_id() const {
    return EnvelopesView::count() >= 6 && EnvelopesView::at(6 - 1).data != nullptr;
  }

  // The size of a single filesystem block.
  const uint32_t& block_size() const {
    ZX_ASSERT(has_block_size());
    return *reinterpret_cast<const uint32_t*>(EnvelopesView::at(7 - 1).data);
  }
  uint32_t& block_size() {
    ZX_ASSERT(has_block_size());
    return *reinterpret_cast<uint32_t*>(EnvelopesView::at(7 - 1).data);
  }
  bool has_block_size() const {
    return EnvelopesView::count() >= 7 && EnvelopesView::at(7 - 1).data != nullptr;
  }

  // The maximum length of a filesystem name.
  const uint32_t& max_node_name_size() const {
    ZX_ASSERT(has_max_node_name_size());
    return *reinterpret_cast<const uint32_t*>(EnvelopesView::at(8 - 1).data);
  }
  uint32_t& max_node_name_size() {
    ZX_ASSERT(has_max_node_name_size());
    return *reinterpret_cast<uint32_t*>(EnvelopesView::at(8 - 1).data);
  }
  bool has_max_node_name_size() const {
    return EnvelopesView::count() >= 8 && EnvelopesView::at(8 - 1).data != nullptr;
  }

  // A unique identifier for the type of the underlying filesystem.
  const ::llcpp::fuchsia::fs::FsType& fs_type() const {
    ZX_ASSERT(has_fs_type());
    return *reinterpret_cast<const ::llcpp::fuchsia::fs::FsType*>(EnvelopesView::at(9 - 1).data);
  }
  ::llcpp::fuchsia::fs::FsType& fs_type() {
    ZX_ASSERT(has_fs_type());
    return *reinterpret_cast<::llcpp::fuchsia::fs::FsType*>(EnvelopesView::at(9 - 1).data);
  }
  bool has_fs_type() const {
    return EnvelopesView::count() >= 9 && EnvelopesView::at(9 - 1).data != nullptr;
  }

  // The name of the filesystem.
  const ::fidl::StringView& name() const {
    ZX_ASSERT(has_name());
    return *reinterpret_cast<const ::fidl::StringView*>(EnvelopesView::at(10 - 1).data);
  }
  ::fidl::StringView& name() {
    ZX_ASSERT(has_name());
    return *reinterpret_cast<::fidl::StringView*>(EnvelopesView::at(10 - 1).data);
  }
  bool has_name() const {
    return EnvelopesView::count() >= 10 && EnvelopesView::at(10 - 1).data != nullptr;
  }

  // Path to the device backing this filesystem.
  const ::fidl::StringView& device_path() const {
    ZX_ASSERT(has_device_path());
    return *reinterpret_cast<const ::fidl::StringView*>(EnvelopesView::at(11 - 1).data);
  }
  ::fidl::StringView& device_path() {
    ZX_ASSERT(has_device_path());
    return *reinterpret_cast<::fidl::StringView*>(EnvelopesView::at(11 - 1).data);
  }
  bool has_device_path() const {
    return EnvelopesView::count() >= 11 && EnvelopesView::at(11 - 1).data != nullptr;
  }

  FilesystemInfo() = default;
  ~FilesystemInfo() = default;
  FilesystemInfo(FilesystemInfo&& other) noexcept = default;
  FilesystemInfo& operator=(FilesystemInfo&& other) noexcept = default;

  class Builder;
  friend class Builder;
  static Builder Build();
  static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_FilesystemInfoTable;
  static constexpr uint32_t MaxNumHandles = 1;
  static constexpr uint32_t PrimarySize = 16;
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = 4408;
  static constexpr bool HasPointer = true;

 private:
  FilesystemInfo(uint64_t max_ordinal, fidl_envelope_t* data) : EnvelopesView(data, max_ordinal) {}
};

class FilesystemInfo::Builder {
 public:
  FilesystemInfo view() { return FilesystemInfo(max_ordinal_, envelopes_.data_); }
  ~Builder() = default;
  Builder(Builder&& other) noexcept = default;
  Builder& operator=(Builder&& other) noexcept = default;

  // The number of data bytes which may be stored in the filesystem.
  Builder&& set_total_bytes(uint64_t* elem);

  // The number of data bytes which are in use by the filesystem.
  // Note that this value may change in the mean time.
  Builder&& set_used_bytes(uint64_t* elem);

  // The number of nodes which may be stored in the filesystem.
  Builder&& set_total_nodes(uint64_t* elem);

  // The number of nodes used by the filesystem.
  // Note that this value may change in the mean time.
  Builder&& set_used_nodes(uint64_t* elem);

  // The amount of space which may be allocated from the underlying
  // volume manager. Note that this value may change in the mean time.
  Builder&& set_free_shared_pool_bytes(uint64_t* elem);

  // A globally unique identifier for this filesystem instance.
  Builder&& set_fs_id(::zx::event* elem);

  // The size of a single filesystem block.
  Builder&& set_block_size(uint32_t* elem);

  // The maximum length of a filesystem name.
  Builder&& set_max_node_name_size(uint32_t* elem);

  // A unique identifier for the type of the underlying filesystem.
  Builder&& set_fs_type(::llcpp::fuchsia::fs::FsType* elem);

  // The name of the filesystem.
  Builder&& set_name(::fidl::StringView* elem);

  // Path to the device backing this filesystem.
  Builder&& set_device_path(::fidl::StringView* elem);

 private:
  Builder() = default;
  friend Builder FilesystemInfo::Build();

  uint64_t max_ordinal_ = 0;
  ::fidl::Array<fidl_envelope_t, 11> envelopes_ = {};
};

extern "C" const fidl_type_t v1_fuchsia_fs_Query_GetInfo_ResultTable;

struct Query_GetInfo_Result {
  Query_GetInfo_Result() : ordinal_(Ordinal::Invalid), envelope_{} {}

  enum class Tag : fidl_xunion_tag_t {
    kResponse = 1,  // 0x1
    kErr = 2,  // 0x2
  };

  bool has_invalid_tag() const { return ordinal_ == Ordinal::Invalid; }

  bool is_response() const { return ordinal() == Ordinal::kResponse; }

  static Query_GetInfo_Result WithResponse(::llcpp::fuchsia::fs::Query_GetInfo_Response* val) {
    Query_GetInfo_Result result;
    result.set_response(val);
    return result;
  }

  void set_response(::llcpp::fuchsia::fs::Query_GetInfo_Response* elem) {
    ordinal_ = Ordinal::kResponse;
    envelope_.data = static_cast<void*>(elem);
  }

  ::llcpp::fuchsia::fs::Query_GetInfo_Response& mutable_response() {
    ZX_ASSERT(ordinal() == Ordinal::kResponse);
    return *static_cast<::llcpp::fuchsia::fs::Query_GetInfo_Response*>(envelope_.data);
  }
  const ::llcpp::fuchsia::fs::Query_GetInfo_Response& response() const {
    ZX_ASSERT(ordinal() == Ordinal::kResponse);
    return *static_cast<::llcpp::fuchsia::fs::Query_GetInfo_Response*>(envelope_.data);
  }

  bool is_err() const { return ordinal() == Ordinal::kErr; }

  static Query_GetInfo_Result WithErr(int32_t* val) {
    Query_GetInfo_Result result;
    result.set_err(val);
    return result;
  }

  void set_err(int32_t* elem) {
    ordinal_ = Ordinal::kErr;
    envelope_.data = static_cast<void*>(elem);
  }

  int32_t& mutable_err() {
    ZX_ASSERT(ordinal() == Ordinal::kErr);
    return *static_cast<int32_t*>(envelope_.data);
  }
  const int32_t& err() const {
    ZX_ASSERT(ordinal() == Ordinal::kErr);
    return *static_cast<int32_t*>(envelope_.data);
  }
  Tag which() const {
    ZX_ASSERT(!has_invalid_tag());
    return static_cast<Tag>(ordinal());
  }

  static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_Query_GetInfo_ResultTable;
  static constexpr uint32_t MaxNumHandles = 1;
  static constexpr uint32_t PrimarySize = 24;
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = 4424;
  static constexpr bool HasPointer = true;

 private:
  enum class Ordinal : fidl_xunion_tag_t {
    Invalid = 0,
    kResponse = 1,  // 0x1
    kErr = 2,  // 0x2
  };

  Ordinal ordinal() const {
    return ordinal_;
  }

  static void SizeAndOffsetAssertionHelper();
  Ordinal ordinal_;
  FIDL_ALIGNDECL
  fidl_envelope_t envelope_;
};

// The maximum length of the name of a filesystem.
constexpr uint64_t MAX_FS_NAME_LENGTH = 32u;

extern "C" const fidl_type_t v1_fuchsia_fs_Query_GetInfo_ResponseTable;

struct Query_GetInfo_Response {
  static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_Query_GetInfo_ResponseTable;
  static constexpr uint32_t MaxNumHandles = 1;
  static constexpr uint32_t PrimarySize = 16;
  [[maybe_unused]]
  static constexpr uint32_t MaxOutOfLine = 4408;
  static constexpr bool HasPointer = true;

  ::llcpp::fuchsia::fs::FilesystemInfo info = {};
};

extern "C" const fidl_type_t v1_fuchsia_fs_QueryGetInfoRequestTable;
extern "C" const fidl_type_t v1_fuchsia_fs_QueryGetInfoResponseTable;
extern "C" const fidl_type_t v1_fuchsia_fs_QueryIsNodeInFilesystemRequestTable;
extern "C" const fidl_type_t v1_fuchsia_fs_QueryIsNodeInFilesystemResponseTable;

// `Query` exposes objective filesystem information independent of specific
// files and directories.
class Query final {
  Query() = delete;
 public:
  static constexpr char Name[] = "fuchsia.fs.Query";

  struct GetInfoResponse final {
    FIDL_ALIGNDECL
    fidl_message_header_t _hdr;
    ::llcpp::fuchsia::fs::Query_GetInfo_Result result;

    static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_QueryGetInfoResponseTable;
    static constexpr uint32_t MaxNumHandles = 1;
    static constexpr uint32_t PrimarySize = 40;
    static constexpr uint32_t MaxOutOfLine = 4424;
    static constexpr bool HasFlexibleEnvelope = true;
    static constexpr bool HasPointer = true;
    static constexpr bool ContainsUnion = true;
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kResponse;
  };
  struct GetInfoRequest final {
    FIDL_ALIGNDECL
    fidl_message_header_t _hdr;
    ::llcpp::fuchsia::fs::FilesystemInfoQuery query;

    static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_QueryGetInfoRequestTable;
    static constexpr uint32_t MaxNumHandles = 0;
    static constexpr uint32_t PrimarySize = 24;
    static constexpr uint32_t MaxOutOfLine = 0;
    static constexpr uint32_t AltPrimarySize = 24;
    static constexpr uint32_t AltMaxOutOfLine = 0;
    static constexpr bool HasFlexibleEnvelope = false;
    static constexpr bool HasPointer = false;
    static constexpr bool ContainsUnion = false;
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kRequest;
    using ResponseType = GetInfoResponse;
  };

  struct IsNodeInFilesystemResponse final {
    FIDL_ALIGNDECL
    fidl_message_header_t _hdr;
    bool is_in_filesystem;

    static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_QueryIsNodeInFilesystemResponseTable;
    static constexpr uint32_t MaxNumHandles = 0;
    static constexpr uint32_t PrimarySize = 24;
    static constexpr uint32_t MaxOutOfLine = 0;
    static constexpr bool HasFlexibleEnvelope = false;
    static constexpr bool HasPointer = false;
    static constexpr bool ContainsUnion = false;
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kResponse;
  };
  struct IsNodeInFilesystemRequest final {
    FIDL_ALIGNDECL
    fidl_message_header_t _hdr;
    ::zx::event token;

    static constexpr const fidl_type_t* Type = &v1_fuchsia_fs_QueryIsNodeInFilesystemRequestTable;
    static constexpr uint32_t MaxNumHandles = 1;
    static constexpr uint32_t PrimarySize = 24;
    static constexpr uint32_t MaxOutOfLine = 0;
    static constexpr uint32_t AltPrimarySize = 24;
    static constexpr uint32_t AltMaxOutOfLine = 0;
    static constexpr bool HasFlexibleEnvelope = false;
    static constexpr bool HasPointer = false;
    static constexpr bool ContainsUnion = false;
    static constexpr ::fidl::internal::TransactionalMessageKind MessageKind =
        ::fidl::internal::TransactionalMessageKind::kRequest;
    using ResponseType = IsNodeInFilesystemResponse;
  };


  // Collection of return types of FIDL calls in this interface.
  class ResultOf final {
    ResultOf() = delete;
   private:
    template <typename ResponseType>
    class GetInfo_Impl final : private ::fidl::internal::OwnedSyncCallBase<ResponseType> {
      using Super = ::fidl::internal::OwnedSyncCallBase<ResponseType>;
     public:
      GetInfo_Impl(::zx::unowned_channel _client_end, ::llcpp::fuchsia::fs::FilesystemInfoQuery query);
      ~GetInfo_Impl() = default;
      GetInfo_Impl(GetInfo_Impl&& other) = default;
      GetInfo_Impl& operator=(GetInfo_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
    };
    template <typename ResponseType>
    class IsNodeInFilesystem_Impl final : private ::fidl::internal::OwnedSyncCallBase<ResponseType> {
      using Super = ::fidl::internal::OwnedSyncCallBase<ResponseType>;
     public:
      IsNodeInFilesystem_Impl(::zx::unowned_channel _client_end, ::zx::event token);
      ~IsNodeInFilesystem_Impl() = default;
      IsNodeInFilesystem_Impl(IsNodeInFilesystem_Impl&& other) = default;
      IsNodeInFilesystem_Impl& operator=(IsNodeInFilesystem_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
    };

   public:
    using GetInfo = GetInfo_Impl<GetInfoResponse>;
    using IsNodeInFilesystem = IsNodeInFilesystem_Impl<IsNodeInFilesystemResponse>;
  };

  // Collection of return types of FIDL calls in this interface,
  // when the caller-allocate flavor or in-place call is used.
  class UnownedResultOf final {
    UnownedResultOf() = delete;
   private:
    template <typename ResponseType>
    class GetInfo_Impl final : private ::fidl::internal::UnownedSyncCallBase<ResponseType> {
      using Super = ::fidl::internal::UnownedSyncCallBase<ResponseType>;
     public:
      GetInfo_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::llcpp::fuchsia::fs::FilesystemInfoQuery query, ::fidl::BytePart _response_buffer);
      ~GetInfo_Impl() = default;
      GetInfo_Impl(GetInfo_Impl&& other) = default;
      GetInfo_Impl& operator=(GetInfo_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
    };
    template <typename ResponseType>
    class IsNodeInFilesystem_Impl final : private ::fidl::internal::UnownedSyncCallBase<ResponseType> {
      using Super = ::fidl::internal::UnownedSyncCallBase<ResponseType>;
     public:
      IsNodeInFilesystem_Impl(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::zx::event token, ::fidl::BytePart _response_buffer);
      ~IsNodeInFilesystem_Impl() = default;
      IsNodeInFilesystem_Impl(IsNodeInFilesystem_Impl&& other) = default;
      IsNodeInFilesystem_Impl& operator=(IsNodeInFilesystem_Impl&& other) = default;
      using Super::status;
      using Super::error;
      using Super::ok;
      using Super::Unwrap;
      using Super::value;
      using Super::operator->;
      using Super::operator*;
    };

   public:
    using GetInfo = GetInfo_Impl<GetInfoResponse>;
    using IsNodeInFilesystem = IsNodeInFilesystem_Impl<IsNodeInFilesystemResponse>;
  };

  class SyncClient final {
   public:
    explicit SyncClient(::zx::channel channel) : channel_(std::move(channel)) {}
    ~SyncClient() = default;
    SyncClient(SyncClient&&) = default;
    SyncClient& operator=(SyncClient&&) = default;

    const ::zx::channel& channel() const { return channel_; }

    ::zx::channel* mutable_channel() { return &channel_; }

    // Queries the filesystem.
    //
    // + `query` specifies the fields in `FilesystemInfo` that the caller is
    //   interested in.
    // - `info` see [`fuchsia.fs/FilesystemInfo`] for details on the fields.
    //
    // Allocates 24 bytes of request buffer on the stack. Response is heap-allocated.
    ResultOf::GetInfo GetInfo(::llcpp::fuchsia::fs::FilesystemInfoQuery query);

    // Queries the filesystem.
    //
    // + `query` specifies the fields in `FilesystemInfo` that the caller is
    //   interested in.
    // - `info` see [`fuchsia.fs/FilesystemInfo`] for details on the fields.
    //
    // Caller provides the backing storage for FIDL message via request and response buffers.
    UnownedResultOf::GetInfo GetInfo(::fidl::BytePart _request_buffer, ::llcpp::fuchsia::fs::FilesystemInfoQuery query, ::fidl::BytePart _response_buffer);

    // Checks if a node is associated with this filesystem, given some token
    // representing a connection to that node.
    // Allocates 48 bytes of message buffer on the stack. No heap allocation necessary.
    ResultOf::IsNodeInFilesystem IsNodeInFilesystem(::zx::event token);

    // Checks if a node is associated with this filesystem, given some token
    // representing a connection to that node.
    // Caller provides the backing storage for FIDL message via request and response buffers.
    UnownedResultOf::IsNodeInFilesystem IsNodeInFilesystem(::fidl::BytePart _request_buffer, ::zx::event token, ::fidl::BytePart _response_buffer);

   private:
    ::zx::channel channel_;
  };

  // Methods to make a sync FIDL call directly on an unowned channel, avoiding setting up a client.
  class Call final {
    Call() = delete;
   public:

    // Queries the filesystem.
    //
    // + `query` specifies the fields in `FilesystemInfo` that the caller is
    //   interested in.
    // - `info` see [`fuchsia.fs/FilesystemInfo`] for details on the fields.
    //
    // Allocates 24 bytes of request buffer on the stack. Response is heap-allocated.
    static ResultOf::GetInfo GetInfo(::zx::unowned_channel _client_end, ::llcpp::fuchsia::fs::FilesystemInfoQuery query);

    // Queries the filesystem.
    //
    // + `query` specifies the fields in `FilesystemInfo` that the caller is
    //   interested in.
    // - `info` see [`fuchsia.fs/FilesystemInfo`] for details on the fields.
    //
    // Caller provides the backing storage for FIDL message via request and response buffers.
    static UnownedResultOf::GetInfo GetInfo(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::llcpp::fuchsia::fs::FilesystemInfoQuery query, ::fidl::BytePart _response_buffer);

    // Checks if a node is associated with this filesystem, given some token
    // representing a connection to that node.
    // Allocates 48 bytes of message buffer on the stack. No heap allocation necessary.
    static ResultOf::IsNodeInFilesystem IsNodeInFilesystem(::zx::unowned_channel _client_end, ::zx::event token);

    // Checks if a node is associated with this filesystem, given some token
    // representing a connection to that node.
    // Caller provides the backing storage for FIDL message via request and response buffers.
    static UnownedResultOf::IsNodeInFilesystem IsNodeInFilesystem(::zx::unowned_channel _client_end, ::fidl::BytePart _request_buffer, ::zx::event token, ::fidl::BytePart _response_buffer);

  };

  // Messages are encoded and decoded in-place when these methods are used.
  // Additionally, requests must be already laid-out according to the FIDL wire-format.
  class InPlace final {
    InPlace() = delete;
   public:

    // Queries the filesystem.
    //
    // + `query` specifies the fields in `FilesystemInfo` that the caller is
    //   interested in.
    // - `info` see [`fuchsia.fs/FilesystemInfo`] for details on the fields.
    //
    static ::fidl::DecodeResult<GetInfoResponse> GetInfo(::zx::unowned_channel _client_end, ::fidl::DecodedMessage<GetInfoRequest> params, ::fidl::BytePart response_buffer);

    // Checks if a node is associated with this filesystem, given some token
    // representing a connection to that node.
    static ::fidl::DecodeResult<IsNodeInFilesystemResponse> IsNodeInFilesystem(::zx::unowned_channel _client_end, ::fidl::DecodedMessage<IsNodeInFilesystemRequest> params, ::fidl::BytePart response_buffer);

  };

  // Pure-virtual interface to be implemented by a server.
  class Interface {
   public:
    Interface() = default;
    virtual ~Interface() = default;
    using _Outer = Query;
    using _Base = ::fidl::CompleterBase;

    class GetInfoCompleterBase : public _Base {
     public:
      void Reply(::llcpp::fuchsia::fs::Query_GetInfo_Result result);
      void ReplySuccess(::llcpp::fuchsia::fs::FilesystemInfo info);
      void ReplyError(int32_t error);
      void Reply(::fidl::BytePart _buffer, ::llcpp::fuchsia::fs::Query_GetInfo_Result result);
      void ReplySuccess(::fidl::BytePart _buffer, ::llcpp::fuchsia::fs::FilesystemInfo info);
      void Reply(::fidl::DecodedMessage<GetInfoResponse> params);

     protected:
      using ::fidl::CompleterBase::CompleterBase;
    };

    using GetInfoCompleter = ::fidl::Completer<GetInfoCompleterBase>;

    virtual void GetInfo(::llcpp::fuchsia::fs::FilesystemInfoQuery query, GetInfoCompleter::Sync _completer) = 0;

    class IsNodeInFilesystemCompleterBase : public _Base {
     public:
      void Reply(bool is_in_filesystem);
      void Reply(::fidl::BytePart _buffer, bool is_in_filesystem);
      void Reply(::fidl::DecodedMessage<IsNodeInFilesystemResponse> params);

     protected:
      using ::fidl::CompleterBase::CompleterBase;
    };

    using IsNodeInFilesystemCompleter = ::fidl::Completer<IsNodeInFilesystemCompleterBase>;

    virtual void IsNodeInFilesystem(::zx::event token, IsNodeInFilesystemCompleter::Sync _completer) = 0;

  };

  // Attempts to dispatch the incoming message to a handler function in the server implementation.
  // If there is no matching handler, it returns false, leaving the message and transaction intact.
  // In all other cases, it consumes the message and returns true.
  // It is possible to chain multiple TryDispatch functions in this manner.
  static bool TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn);

  // Dispatches the incoming message to one of the handlers functions in the interface.
  // If there is no matching handler, it closes all the handles in |msg| and closes the channel with
  // a |ZX_ERR_NOT_SUPPORTED| epitaph, before returning false. The message should then be discarded.
  static bool Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn);

  // Same as |Dispatch|, but takes a |void*| instead of |Interface*|. Only used with |fidl::Bind|
  // to reduce template expansion.
  // Do not call this method manually. Use |Dispatch| instead.
  static bool TypeErasedDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
    return Dispatch(static_cast<Interface*>(impl), msg, txn);
  }


  // Helper functions to fill in the transaction header in a |DecodedMessage<TransactionalMessage>|.
  class SetTransactionHeaderFor final {
    SetTransactionHeaderFor() = delete;
   public:
    static void GetInfoRequest(const ::fidl::DecodedMessage<Query::GetInfoRequest>& _msg);
    static void GetInfoResponse(const ::fidl::DecodedMessage<Query::GetInfoResponse>& _msg);
    static void IsNodeInFilesystemRequest(const ::fidl::DecodedMessage<Query::IsNodeInFilesystemRequest>& _msg);
    static void IsNodeInFilesystemResponse(const ::fidl::DecodedMessage<Query::IsNodeInFilesystemResponse>& _msg);
  };
};

}  // namespace fs
}  // namespace fuchsia
}  // namespace llcpp

namespace fidl {

template <>
struct IsFidlType<::llcpp::fuchsia::fs::FilesystemInfo> : public std::true_type {};
static_assert(std::is_standard_layout_v<::llcpp::fuchsia::fs::FilesystemInfo>);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query_GetInfo_Response> : public std::true_type {};
static_assert(std::is_standard_layout_v<::llcpp::fuchsia::fs::Query_GetInfo_Response>);
static_assert(offsetof(::llcpp::fuchsia::fs::Query_GetInfo_Response, info) == 0);
static_assert(sizeof(::llcpp::fuchsia::fs::Query_GetInfo_Response) == ::llcpp::fuchsia::fs::Query_GetInfo_Response::PrimarySize);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query_GetInfo_Result> : public std::true_type {};
static_assert(std::is_standard_layout_v<::llcpp::fuchsia::fs::Query_GetInfo_Result>);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::FilesystemInfoQuery> : public std::true_type {};
static_assert(std::is_standard_layout_v<::llcpp::fuchsia::fs::FilesystemInfoQuery>);
static_assert(sizeof(::llcpp::fuchsia::fs::FilesystemInfoQuery) == sizeof(uint64_t));

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query::GetInfoRequest> : public std::true_type {};
template <>
struct IsFidlMessage<::llcpp::fuchsia::fs::Query::GetInfoRequest> : public std::true_type {};
static_assert(sizeof(::llcpp::fuchsia::fs::Query::GetInfoRequest)
    == ::llcpp::fuchsia::fs::Query::GetInfoRequest::PrimarySize);
static_assert(offsetof(::llcpp::fuchsia::fs::Query::GetInfoRequest, query) == 16);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query::GetInfoResponse> : public std::true_type {};
template <>
struct IsFidlMessage<::llcpp::fuchsia::fs::Query::GetInfoResponse> : public std::true_type {};
static_assert(sizeof(::llcpp::fuchsia::fs::Query::GetInfoResponse)
    == ::llcpp::fuchsia::fs::Query::GetInfoResponse::PrimarySize);
static_assert(offsetof(::llcpp::fuchsia::fs::Query::GetInfoResponse, result) == 16);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query::IsNodeInFilesystemRequest> : public std::true_type {};
template <>
struct IsFidlMessage<::llcpp::fuchsia::fs::Query::IsNodeInFilesystemRequest> : public std::true_type {};
static_assert(sizeof(::llcpp::fuchsia::fs::Query::IsNodeInFilesystemRequest)
    == ::llcpp::fuchsia::fs::Query::IsNodeInFilesystemRequest::PrimarySize);
static_assert(offsetof(::llcpp::fuchsia::fs::Query::IsNodeInFilesystemRequest, token) == 16);

template <>
struct IsFidlType<::llcpp::fuchsia::fs::Query::IsNodeInFilesystemResponse> : public std::true_type {};
template <>
struct IsFidlMessage<::llcpp::fuchsia::fs::Query::IsNodeInFilesystemResponse> : public std::true_type {};
static_assert(sizeof(::llcpp::fuchsia::fs::Query::IsNodeInFilesystemResponse)
    == ::llcpp::fuchsia::fs::Query::IsNodeInFilesystemResponse::PrimarySize);
static_assert(offsetof(::llcpp::fuchsia::fs::Query::IsNodeInFilesystemResponse, is_in_filesystem) == 16);

}  // namespace fidl
