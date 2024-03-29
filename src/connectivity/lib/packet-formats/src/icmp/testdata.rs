// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Data for testing parsing/serialization of ICMP.
//!
//! This data was obtained by capturing live network traffic.

pub(crate) mod ndp_neighbor {
    pub(crate) const SOLICITATION_IP_PACKET_BYTES: &[u8] = &[
        0x68, 0x00, 0x00, 0x00, 0x00, 0x20, 0x3a, 0xff, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x56, 0xe0, 0x32, 0x09, 0xc4, 0x74, 0x77, 0xf0, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x4f, 0xe7, 0x1a, 0x69, 0x86, 0x4b, 0x85, 0xc2, 0x87, 0x00, 0xca, 0xd0, 0x00,
        0x00, 0x00, 0x00, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xe7, 0x1a, 0x69,
        0x86, 0x4b, 0x85, 0xc2, 0x01, 0x01, 0x54, 0xe0, 0x32, 0x74, 0x77, 0xf0,
    ];

    pub(crate) const SOURCE_LINK_LAYER_ADDRESS: &[u8] = &[0x54, 0xe0, 0x32, 0x74, 0x77, 0xf0];

    pub(crate) const TARGET_ADDRESS: &[u8] = &[
        0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xe7, 0x1a, 0x69, 0x86, 0x4b, 0x85,
        0xc2,
    ];

    pub(crate) const ADVERTISEMENT_IP_PACKET_BYTES: &[u8] = &[
        0x60, 0x00, 0x00, 0x00, 0x00, 0x18, 0x3a, 0xff, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x4f, 0xe7, 0x1a, 0x69, 0x86, 0x4b, 0x85, 0xc2, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x56, 0xe0, 0x32, 0x09, 0xc4, 0x74, 0x77, 0xf0, 0x88, 0x00, 0x8a, 0x1e, 0x40,
        0x00, 0x00, 0x00, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xe7, 0x1a, 0x69,
        0x86, 0x4b, 0x85, 0xc2,
    ];
}

pub(crate) mod ndp_router {
    use core::num::NonZeroU8;

    use net_types::ip::{Ipv6Addr, Subnet};
    use nonzero_ext::nonzero;

    use crate::utils::NonZeroDuration;

    pub(crate) const ADVERTISEMENT_IP_PACKET_BYTES: &[u8] = &[
        0x68, 0x00, 0x00, 0x00, 0x00, 0x38, 0x3a, 0xff, 0xfe, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x02, 0x00, 0x5e, 0xff, 0xfe, 0x00, 0x02, 0x65, 0xff, 0x02, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x86, 0x00, 0xd9, 0x96, 0x40,
        0x00, 0x0e, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00,
        0x5e, 0x00, 0x02, 0x65, 0x03, 0x04, 0x40, 0xc0, 0x00, 0x27, 0x8d, 0x00, 0x00, 0x09, 0x3a,
        0x80, 0x00, 0x00, 0x00, 0x00, 0x26, 0x20, 0x00, 0x00, 0x10, 0x00, 0x50, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    ];

    /// Options in the Advertisement packet.
    // We know this is safe because we provide a non-zero `u8` value.
    pub(crate) const HOP_LIMIT: Option<NonZeroU8> = Some(nonzero!(64u8));
    pub(crate) const LIFETIME: Option<NonZeroDuration> =
        Some(NonZeroDuration::from_nonzero_secs(nonzero!(3600u64)));
    pub(crate) const REACHABLE_TIME: Option<NonZeroDuration> = None;
    pub(crate) const RETRANS_TIMER: Option<NonZeroDuration> = None;

    /// Data from the SourceLinkLayerAddress option.
    pub(crate) const SOURCE_LINK_LAYER_ADDRESS: &[u8] = &[0x00, 0x00, 0x5e, 0x00, 0x02, 0x65];

    /// Data from the Prefix Info option.
    pub(crate) const PREFIX_INFO_ON_LINK_FLAG: bool = true;
    pub(crate) const PREFIX_INFO_AUTONOMOUS_ADDRESS_CONFIGURATION_FLAG: bool = true;
    pub(crate) const PREFIX_INFO_VALID_LIFETIME_SECONDS: u32 = 2_592_000;
    pub(crate) const PREFIX_INFO_PREFERRED_LIFETIME_SECONDS: u32 = 604_800;
    // We know this is safe as the none of the host-bits are set and the prefix
    // length is valid for IPv6.
    pub(crate) const PREFIX_INFO_PREFIX: Subnet<Ipv6Addr> = unsafe {
        Subnet::new_unchecked(
            Ipv6Addr::new([0x2620, 0x00, 0x1000, 0x5000, 0x00, 0x00, 0x00, 0x00]),
            64,
        )
    };
}
