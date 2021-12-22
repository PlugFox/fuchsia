// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod extend;
mod layer;
pub mod painter;
mod point;
pub mod rasterizer;
mod segment;
mod simd;
mod uninitialized;

pub use layer::Layer;
pub use point::Point;
pub use segment::{Lines, LinesBuilder, Segment};

const PIXEL_WIDTH: usize = 16;
const PIXEL_DOUBLE_WIDTH: usize = PIXEL_WIDTH * 2;
const PIXEL_SHIFT: usize = PIXEL_WIDTH.trailing_zeros() as usize;
const PIXEL_MASK: usize = PIXEL_WIDTH - 1;

pub const MAX_WIDTH: usize = 1 << 16;
pub const MAX_HEIGHT: usize = 1 << 15;

const MAX_WIDTH_SHIFT: usize = MAX_WIDTH.trailing_zeros() as usize;
const MAX_HEIGHT_SHIFT: usize = MAX_HEIGHT.trailing_zeros() as usize;

pub const TILE_SIZE: usize = 16;
const _ASSERT_TILE_SIZE_MULTIPLE_OF_16: usize = 0 - (TILE_SIZE % 16);
const _ASSERT_MAX_TILE_SIZE: usize = 128 - TILE_SIZE;
const TILE_SHIFT: usize = TILE_SIZE.trailing_zeros() as usize;
const TILE_MASK: usize = TILE_SIZE - 1;

pub const LAYER_LIMIT: usize = (1 << rasterizer::BIT_FIELD_LENS[3]) - 1;
