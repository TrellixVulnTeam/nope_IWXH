// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/output/direct_renderer.h"

#include <utility>
#include <vector>

#include "base/containers/hash_tables.h"
#include "base/containers/scoped_ptr_hash_map.h"
#include "base/metrics/histogram.h"
#include "base/trace_event/trace_event.h"
#include "cc/base/math_util.h"
#include "cc/output/bsp_tree.h"
#include "cc/output/bsp_walk_action.h"
#include "cc/output/copy_output_request.h"
#include "cc/quads/draw_quad.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/transform.h"

static gfx::Transform OrthoProjectionMatrix(float left,
                                            float right,
                                            float bottom,
                                            float top) {
  // Use the standard formula to map the clipping frustum to the cube from
  // [-1, -1, -1] to [1, 1, 1].
  float delta_x = right - left;
  float delta_y = top - bottom;
  gfx::Transform proj;
  if (!delta_x || !delta_y)
    return proj;
  proj.matrix().set(0, 0, 2.0f / delta_x);
  proj.matrix().set(0, 3, -(right + left) / delta_x);
  proj.matrix().set(1, 1, 2.0f / delta_y);
  proj.matrix().set(1, 3, -(top + bottom) / delta_y);

  // Z component of vertices is always set to zero as we don't use the depth
  // buffer while drawing.
  proj.matrix().set(2, 2, 0);

  return proj;
}

static gfx::Transform window_matrix(int x, int y, int width, int height) {
  gfx::Transform canvas;

  // Map to window position and scale up to pixel coordinates.
  canvas.Translate3d(x, y, 0);
  canvas.Scale3d(width, height, 0);

  // Map from ([-1, -1] to [1, 1]) -> ([0, 0] to [1, 1])
  canvas.Translate3d(0.5, 0.5, 0.5);
  canvas.Scale3d(0.5, 0.5, 0.5);

  return canvas;
}

namespace cc {

DirectRenderer::DrawingFrame::DrawingFrame()
    : root_render_pass(NULL), current_render_pass(NULL), current_texture(NULL) {
}

DirectRenderer::DrawingFrame::~DrawingFrame() {}

//
// static
gfx::RectF DirectRenderer::QuadVertexRect() {
  return gfx::RectF(-0.5f, -0.5f, 1.f, 1.f);
}

// static
void DirectRenderer::QuadRectTransform(gfx::Transform* quad_rect_transform,
                                       const gfx::Transform& quad_transform,
                                       const gfx::RectF& quad_rect) {
  *quad_rect_transform = quad_transform;
  quad_rect_transform->Translate(0.5 * quad_rect.width() + quad_rect.x(),
                                 0.5 * quad_rect.height() + quad_rect.y());
  quad_rect_transform->Scale(quad_rect.width(), quad_rect.height());
}

void DirectRenderer::InitializeViewport(DrawingFrame* frame,
                                        const gfx::Rect& draw_rect,
                                        const gfx::Rect& viewport_rect,
                                        const gfx::Size& surface_size) {
  DCHECK_GE(viewport_rect.x(), 0);
  DCHECK_GE(viewport_rect.y(), 0);
  DCHECK_LE(viewport_rect.right(), surface_size.width());
  DCHECK_LE(viewport_rect.bottom(), surface_size.height());
  bool flip_y = FlippedFramebuffer(frame);
  if (flip_y) {
    frame->projection_matrix = OrthoProjectionMatrix(draw_rect.x(),
                                                     draw_rect.right(),
                                                     draw_rect.bottom(),
                                                     draw_rect.y());
  } else {
    frame->projection_matrix = OrthoProjectionMatrix(draw_rect.x(),
                                                     draw_rect.right(),
                                                     draw_rect.y(),
                                                     draw_rect.bottom());
  }

  gfx::Rect window_rect = viewport_rect;
  if (flip_y)
    window_rect.set_y(surface_size.height() - viewport_rect.bottom());
  frame->window_matrix = window_matrix(window_rect.x(),
                                       window_rect.y(),
                                       window_rect.width(),
                                       window_rect.height());
  SetDrawViewport(window_rect);

  current_draw_rect_ = draw_rect;
  current_viewport_rect_ = viewport_rect;
  current_surface_size_ = surface_size;
}

gfx::Rect DirectRenderer::MoveFromDrawToWindowSpace(
    const DrawingFrame* frame,
    const gfx::Rect& draw_rect) const {
  gfx::Rect window_rect = draw_rect;
  window_rect -= current_draw_rect_.OffsetFromOrigin();
  window_rect += current_viewport_rect_.OffsetFromOrigin();
  if (FlippedFramebuffer(frame))
    window_rect.set_y(current_surface_size_.height() - window_rect.bottom());
  return window_rect;
}

DirectRenderer::DirectRenderer(RendererClient* client,
                               const RendererSettings* settings,
                               OutputSurface* output_surface,
                               ResourceProvider* resource_provider)
    : Renderer(client, settings),
      output_surface_(output_surface),
      resource_provider_(resource_provider),
      overlay_processor_(
          new OverlayProcessor(output_surface, resource_provider)) {
  overlay_processor_->Initialize();
}

DirectRenderer::~DirectRenderer() {}

void DirectRenderer::SetEnlargePassTextureAmountForTesting(
    const gfx::Vector2d& amount) {
  enlarge_pass_texture_amount_ = amount;
}

void DirectRenderer::DecideRenderPassAllocationsForFrame(
    const RenderPassList& render_passes_in_draw_order) {
  if (!resource_provider_)
    return;

  base::hash_map<RenderPassId, gfx::Size> render_passes_in_frame;
  for (size_t i = 0; i < render_passes_in_draw_order.size(); ++i)
    render_passes_in_frame.insert(std::pair<RenderPassId, gfx::Size>(
        render_passes_in_draw_order[i]->id,
        RenderPassTextureSize(render_passes_in_draw_order[i])));

  std::vector<RenderPassId> passes_to_delete;
  base::ScopedPtrHashMap<RenderPassId, ScopedResource>::const_iterator
      pass_iter;
  for (pass_iter = render_pass_textures_.begin();
       pass_iter != render_pass_textures_.end();
       ++pass_iter) {
    base::hash_map<RenderPassId, gfx::Size>::const_iterator it =
        render_passes_in_frame.find(pass_iter->first);
    if (it == render_passes_in_frame.end()) {
      passes_to_delete.push_back(pass_iter->first);
      continue;
    }

    gfx::Size required_size = it->second;
    ScopedResource* texture = pass_iter->second;
    DCHECK(texture);

    bool size_appropriate = texture->size().width() >= required_size.width() &&
                            texture->size().height() >= required_size.height();
    if (texture->id() && !size_appropriate)
      texture->Free();
  }

  // Delete RenderPass textures from the previous frame that will not be used
  // again.
  for (size_t i = 0; i < passes_to_delete.size(); ++i)
    render_pass_textures_.erase(passes_to_delete[i]);

  for (size_t i = 0; i < render_passes_in_draw_order.size(); ++i) {
    if (!render_pass_textures_.contains(render_passes_in_draw_order[i]->id)) {
      scoped_ptr<ScopedResource> texture =
          ScopedResource::Create(resource_provider_);
      render_pass_textures_.set(render_passes_in_draw_order[i]->id,
                              texture.Pass());
    }
  }
}

void DirectRenderer::DrawFrame(RenderPassList* render_passes_in_draw_order,
                               float device_scale_factor,
                               const gfx::Rect& device_viewport_rect,
                               const gfx::Rect& device_clip_rect,
                               bool disable_picture_quad_image_filtering) {
  TRACE_EVENT0("cc", "DirectRenderer::DrawFrame");
  UMA_HISTOGRAM_COUNTS("Renderer4.renderPassCount",
                       render_passes_in_draw_order->size());

  const RenderPass* root_render_pass = render_passes_in_draw_order->back();
  DCHECK(root_render_pass);

  DrawingFrame frame;
  frame.render_passes_in_draw_order = render_passes_in_draw_order;
  frame.root_render_pass = root_render_pass;
  frame.root_damage_rect = Capabilities().using_partial_swap
                               ? root_render_pass->damage_rect
                               : root_render_pass->output_rect;
  frame.root_damage_rect.Intersect(gfx::Rect(device_viewport_rect.size()));
  frame.device_viewport_rect = device_viewport_rect;
  frame.device_clip_rect = device_clip_rect;
  frame.disable_picture_quad_image_filtering =
      disable_picture_quad_image_filtering;

  overlay_processor_->ProcessForOverlays(render_passes_in_draw_order,
                                         &frame.overlay_list);

  EnsureBackbuffer();

  // Only reshape when we know we are going to draw. Otherwise, the reshape
  // can leave the window at the wrong size if we never draw and the proper
  // viewport size is never set.
  output_surface_->Reshape(device_viewport_rect.size(), device_scale_factor);

  BeginDrawingFrame(&frame);
  for (size_t i = 0; i < render_passes_in_draw_order->size(); ++i) {
    RenderPass* pass = render_passes_in_draw_order->at(i);
    DrawRenderPass(&frame, pass);

    for (ScopedPtrVector<CopyOutputRequest>::iterator it =
             pass->copy_requests.begin();
         it != pass->copy_requests.end();
         ++it) {
      if (it != pass->copy_requests.begin()) {
        // Doing a readback is destructive of our state on Mac, so make sure
        // we restore the state between readbacks. http://crbug.com/99393.
        UseRenderPass(&frame, pass);
      }
      CopyCurrentRenderPassToBitmap(&frame, pass->copy_requests.take(it));
    }
  }
  FinishDrawingFrame(&frame);

  render_passes_in_draw_order->clear();
}

gfx::Rect DirectRenderer::ComputeScissorRectForRenderPass(
    const DrawingFrame* frame) {
  gfx::Rect render_pass_scissor = frame->current_render_pass->output_rect;

  if (frame->root_damage_rect == frame->root_render_pass->output_rect ||
      !frame->current_render_pass->copy_requests.empty())
    return render_pass_scissor;

  gfx::Transform inverse_transform(gfx::Transform::kSkipInitialization);
  if (frame->current_render_pass->transform_to_root_target.GetInverse(
          &inverse_transform)) {
    // Only intersect inverse-projected damage if the transform is invertible.
    gfx::Rect damage_rect_in_render_pass_space =
        MathUtil::ProjectEnclosingClippedRect(inverse_transform,
                                              frame->root_damage_rect);
    render_pass_scissor.Intersect(damage_rect_in_render_pass_space);
  }

  return render_pass_scissor;
}

bool DirectRenderer::NeedDeviceClip(const DrawingFrame* frame) const {
  if (frame->current_render_pass != frame->root_render_pass)
    return false;

  return !frame->device_clip_rect.Contains(frame->device_viewport_rect);
}

gfx::Rect DirectRenderer::DeviceClipRectInWindowSpace(const DrawingFrame* frame)
    const {
  gfx::Rect device_clip_rect = frame->device_clip_rect;
  if (FlippedFramebuffer(frame))
    device_clip_rect.set_y(current_surface_size_.height() -
                           device_clip_rect.bottom());
  return device_clip_rect;
}

void DirectRenderer::SetScissorStateForQuad(const DrawingFrame* frame,
                                            const DrawQuad& quad) {
  if (quad.isClipped()) {
    SetScissorTestRectInDrawSpace(frame, quad.clipRect());
    return;
  }
  if (NeedDeviceClip(frame)) {
    SetScissorTestRect(DeviceClipRectInWindowSpace(frame));
    return;
  }

  EnsureScissorTestDisabled();
}

bool DirectRenderer::ShouldSkipQuad(const DrawQuad& quad,
                                    const gfx::Rect& render_pass_scissor) {
  if (render_pass_scissor.IsEmpty())
    return true;

  if (quad.isClipped()) {
    gfx::Rect r = quad.clipRect();
    r.Intersect(render_pass_scissor);
    return r.IsEmpty();
  }

  return false;
}

void DirectRenderer::SetScissorStateForQuadWithRenderPassScissor(
    const DrawingFrame* frame,
    const DrawQuad& quad,
    const gfx::Rect& render_pass_scissor) {
  gfx::Rect quad_scissor_rect = render_pass_scissor;
  if (quad.isClipped())
    quad_scissor_rect.Intersect(quad.clipRect());
  SetScissorTestRectInDrawSpace(frame, quad_scissor_rect);
}

void DirectRenderer::SetScissorTestRectInDrawSpace(
    const DrawingFrame* frame,
    const gfx::Rect& draw_space_rect) {
  gfx::Rect window_space_rect =
      MoveFromDrawToWindowSpace(frame, draw_space_rect);
  if (NeedDeviceClip(frame))
    window_space_rect.Intersect(DeviceClipRectInWindowSpace(frame));
  SetScissorTestRect(window_space_rect);
}

void DirectRenderer::FinishDrawingQuadList() {}

void DirectRenderer::DoDrawPolygon(const DrawPolygon& poly,
                                   DrawingFrame* frame,
                                   const gfx::Rect& render_pass_scissor,
                                   bool using_scissor_as_optimization) {
  if (using_scissor_as_optimization) {
    SetScissorStateForQuadWithRenderPassScissor(frame, *poly.original_ref(),
                                                render_pass_scissor);
  } else {
    SetScissorStateForQuad(frame, *poly.original_ref());
  }

  // If the poly has not been split, then it is just a normal DrawQuad,
  // and we should save any extra processing that would have to be done.
  if (!poly.is_split()) {
    DoDrawQuad(frame, poly.original_ref(), NULL);
    return;
  }

  std::vector<gfx::QuadF> quads;
  poly.ToQuads2D(&quads);
  for (size_t i = 0; i < quads.size(); ++i) {
    DoDrawQuad(frame, poly.original_ref(), &quads[i]);
  }
}

void DirectRenderer::FlushPolygons(ScopedPtrDeque<DrawPolygon>* poly_list,
                                   DrawingFrame* frame,
                                   const gfx::Rect& render_pass_scissor,
                                   bool using_scissor_as_optimization) {
  if (poly_list->empty()) {
    return;
  }

  BspTree bsp_tree(poly_list);
  BspWalkActionDrawPolygon action_handler(this, frame, render_pass_scissor,
                                          using_scissor_as_optimization);
  bsp_tree.TraverseWithActionHandler(&action_handler);
  DCHECK(poly_list->empty());
}

void DirectRenderer::DrawRenderPass(DrawingFrame* frame,
                                    const RenderPass* render_pass) {
  TRACE_EVENT0("cc", "DirectRenderer::DrawRenderPass");
  if (!UseRenderPass(frame, render_pass))
    return;

  bool using_scissor_as_optimization = Capabilities().using_partial_swap;
  gfx::Rect render_pass_scissor;
  bool draw_rect_covers_full_surface = true;
  if (frame->current_render_pass == frame->root_render_pass &&
      !frame->device_viewport_rect.Contains(
           gfx::Rect(output_surface_->SurfaceSize())))
    draw_rect_covers_full_surface = false;

  if (using_scissor_as_optimization) {
    render_pass_scissor = ComputeScissorRectForRenderPass(frame);
    SetScissorTestRectInDrawSpace(frame, render_pass_scissor);
    if (!render_pass_scissor.Contains(frame->current_render_pass->output_rect))
      draw_rect_covers_full_surface = false;
  }

  if (frame->current_render_pass != frame->root_render_pass ||
      settings_->should_clear_root_render_pass) {
    if (NeedDeviceClip(frame)) {
      SetScissorTestRect(DeviceClipRectInWindowSpace(frame));
      draw_rect_covers_full_surface = false;
    } else if (!using_scissor_as_optimization) {
      EnsureScissorTestDisabled();
    }

    bool has_external_stencil_test =
        output_surface_->HasExternalStencilTest() &&
        frame->current_render_pass == frame->root_render_pass;

    DiscardPixels(has_external_stencil_test, draw_rect_covers_full_surface);
    ClearFramebuffer(frame, has_external_stencil_test);
  }

  const QuadList& quad_list = render_pass->quad_list;
  ScopedPtrDeque<DrawPolygon> poly_list;

  int next_polygon_id = 0;
  int last_sorting_context_id = 0;
  for (auto it = quad_list.BackToFrontBegin(); it != quad_list.BackToFrontEnd();
       ++it) {
    const DrawQuad& quad = **it;
    gfx::QuadF send_quad(quad.visible_rect);

    if (using_scissor_as_optimization &&
        ShouldSkipQuad(quad, render_pass_scissor)) {
      continue;
    }

    if (last_sorting_context_id != quad.shared_quad_state->sorting_context_id) {
      last_sorting_context_id = quad.shared_quad_state->sorting_context_id;
      FlushPolygons(&poly_list, frame, render_pass_scissor,
                    using_scissor_as_optimization);
    }

    // This layer is in a 3D sorting context so we add it to the list of
    // polygons to go into the BSP tree.
    if (quad.shared_quad_state->sorting_context_id != 0) {
      scoped_ptr<DrawPolygon> new_polygon(new DrawPolygon(
          *it, quad.visible_rect, quad.quadTransform(), next_polygon_id++));
      if (new_polygon->points().size() > 2u) {
        poly_list.push_back(new_polygon.Pass());
      }
      continue;
    }

    // We are not in a 3d sorting context, so we should draw the quad normally.
    if (using_scissor_as_optimization) {
      SetScissorStateForQuadWithRenderPassScissor(frame, quad,
                                                  render_pass_scissor);
    } else {
      SetScissorStateForQuad(frame, quad);
    }

    DoDrawQuad(frame, &quad, nullptr);
  }
  FlushPolygons(&poly_list, frame, render_pass_scissor,
                using_scissor_as_optimization);
  FinishDrawingQuadList();
}

bool DirectRenderer::UseRenderPass(DrawingFrame* frame,
                                   const RenderPass* render_pass) {
  frame->current_render_pass = render_pass;
  frame->current_texture = NULL;

  if (render_pass == frame->root_render_pass) {
    BindFramebufferToOutputSurface(frame);
    InitializeViewport(frame,
                       render_pass->output_rect,
                       frame->device_viewport_rect,
                       output_surface_->SurfaceSize());
    return true;
  }

  ScopedResource* texture = render_pass_textures_.get(render_pass->id);
  DCHECK(texture);

  gfx::Size size = RenderPassTextureSize(render_pass);
  size.Enlarge(enlarge_pass_texture_amount_.x(),
               enlarge_pass_texture_amount_.y());
  if (!texture->id())
    texture->Allocate(
        size, ResourceProvider::TEXTURE_HINT_IMMUTABLE_FRAMEBUFFER, RGBA_8888);
  DCHECK(texture->id());

  return BindFramebufferToTexture(frame, texture, render_pass->output_rect);
}

bool DirectRenderer::HasAllocatedResourcesForTesting(RenderPassId id) const {
  ScopedResource* texture = render_pass_textures_.get(id);
  return texture && texture->id();
}

// static
gfx::Size DirectRenderer::RenderPassTextureSize(const RenderPass* render_pass) {
  return render_pass->output_rect.size();
}

}  // namespace cc
