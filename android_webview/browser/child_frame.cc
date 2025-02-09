// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "android_webview/browser/child_frame.h"

#include <utility>

#include "cc/output/compositor_frame.h"

namespace android_webview {

ChildFrame::ChildFrame(uint32_t output_surface_id,
                       std::unique_ptr<cc::CompositorFrame> frame,
                       uint32_t compositor_id,
                       bool viewport_rect_for_tile_priority_empty,
                       const gfx::Transform& transform_for_tile_priority,
                       bool offscreen_pre_raster,
                       bool is_layer)
    : output_surface_id(output_surface_id),
      frame(std::move(frame)),
      compositor_id(compositor_id),
      viewport_rect_for_tile_priority_empty(
          viewport_rect_for_tile_priority_empty),
      transform_for_tile_priority(transform_for_tile_priority),
      offscreen_pre_raster(offscreen_pre_raster),
      is_layer(is_layer) {}

ChildFrame::~ChildFrame() {
}

}  // namespace webview
