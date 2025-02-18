/*
 *  Copyright (c) 2014 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>

#include "./vp9_rtcd.h"

#include "vpx_mem/vpx_mem.h"

#include "vp9/common/vp9_blockd.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_mvref_common.h"
#include "vp9/common/vp9_reconinter.h"
#include "vp9/common/vp9_reconintra.h"

#include "vp9/encoder/vp9_encoder.h"
#include "vp9/encoder/vp9_pickmode.h"
#include "vp9/encoder/vp9_ratectrl.h"
#include "vp9/encoder/vp9_rd.h"

typedef struct {
  uint8_t *data;
  int stride;
  int in_use;
} PRED_BUFFER;

static int mv_refs_rt(const VP9_COMMON *cm, const MACROBLOCKD *xd,
                      const TileInfo *const tile,
                      MODE_INFO *mi, MV_REFERENCE_FRAME ref_frame,
                      int_mv *mv_ref_list,
                      int mi_row, int mi_col) {
  const int *ref_sign_bias = cm->ref_frame_sign_bias;
  int i, refmv_count = 0;

  const POSITION *const mv_ref_search = mv_ref_blocks[mi->mbmi.sb_type];

  int different_ref_found = 0;
  int context_counter = 0;
  int const_motion = 0;

  // Blank the reference vector list
  vpx_memset(mv_ref_list, 0, sizeof(*mv_ref_list) * MAX_MV_REF_CANDIDATES);

  // The nearest 2 blocks are treated differently
  // if the size < 8x8 we get the mv from the bmi substructure,
  // and we also need to keep a mode count.
  for (i = 0; i < 2; ++i) {
    const POSITION *const mv_ref = &mv_ref_search[i];
    if (is_inside(tile, mi_col, mi_row, cm->mi_rows, mv_ref)) {
      const MODE_INFO *const candidate_mi = xd->mi[mv_ref->col + mv_ref->row *
                                                   xd->mi_stride].src_mi;
      const MB_MODE_INFO *const candidate = &candidate_mi->mbmi;
      // Keep counts for entropy encoding.
      context_counter += mode_2_counter[candidate->mode];
      different_ref_found = 1;

      if (candidate->ref_frame[0] == ref_frame)
        ADD_MV_REF_LIST(get_sub_block_mv(candidate_mi, 0, mv_ref->col, -1),
                        refmv_count, mv_ref_list, Done);
    }
  }

  const_motion = 1;

  // Check the rest of the neighbors in much the same way
  // as before except we don't need to keep track of sub blocks or
  // mode counts.
  for (; i < MVREF_NEIGHBOURS && !refmv_count; ++i) {
    const POSITION *const mv_ref = &mv_ref_search[i];
    if (is_inside(tile, mi_col, mi_row, cm->mi_rows, mv_ref)) {
      const MB_MODE_INFO *const candidate = &xd->mi[mv_ref->col + mv_ref->row *
                                                    xd->mi_stride].src_mi->mbmi;
      different_ref_found = 1;

      if (candidate->ref_frame[0] == ref_frame)
        ADD_MV_REF_LIST(candidate->mv[0], refmv_count, mv_ref_list, Done);
    }
  }

  // Since we couldn't find 2 mvs from the same reference frame
  // go back through the neighbors and find motion vectors from
  // different reference frames.
  if (different_ref_found && !refmv_count) {
    for (i = 0; i < MVREF_NEIGHBOURS; ++i) {
      const POSITION *mv_ref = &mv_ref_search[i];
      if (is_inside(tile, mi_col, mi_row, cm->mi_rows, mv_ref)) {
        const MB_MODE_INFO *const candidate = &xd->mi[mv_ref->col + mv_ref->row
                                              * xd->mi_stride].src_mi->mbmi;

        // If the candidate is INTRA we don't want to consider its mv.
        IF_DIFF_REF_FRAME_ADD_MV(candidate, ref_frame, ref_sign_bias,
                                 refmv_count, mv_ref_list, Done);
      }
    }
  }

 Done:

  mi->mbmi.mode_context[ref_frame] = counter_to_context[context_counter];

  // Clamp vectors
  for (i = 0; i < MAX_MV_REF_CANDIDATES; ++i)
    clamp_mv_ref(&mv_ref_list[i].as_mv, xd);

  return const_motion;
}

static int combined_motion_search(VP9_COMP *cpi, MACROBLOCK *x,
                                  BLOCK_SIZE bsize, int mi_row, int mi_col,
                                  int_mv *tmp_mv, int *rate_mv,
                                  int64_t best_rd_sofar) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0].src_mi->mbmi;
  struct buf_2d backup_yv12[MAX_MB_PLANE] = {{0, 0}};
  const int step_param = cpi->sf.mv.fullpel_search_step_param;
  const int sadpb = x->sadperbit16;
  MV mvp_full;
  const int ref = mbmi->ref_frame[0];
  const MV ref_mv = mbmi->ref_mvs[ref][0].as_mv;
  int dis;
  int rate_mode;
  const int tmp_col_min = x->mv_col_min;
  const int tmp_col_max = x->mv_col_max;
  const int tmp_row_min = x->mv_row_min;
  const int tmp_row_max = x->mv_row_max;
  int rv = 0;
  int cost_list[5];
  const YV12_BUFFER_CONFIG *scaled_ref_frame = vp9_get_scaled_ref_frame(cpi,
                                                                        ref);
  if (cpi->common.show_frame &&
      (x->pred_mv_sad[ref] >> 3) > x->pred_mv_sad[LAST_FRAME])
    return rv;

  if (scaled_ref_frame) {
    int i;
    // Swap out the reference frame for a version that's been scaled to
    // match the resolution of the current frame, allowing the existing
    // motion search code to be used without additional modifications.
    for (i = 0; i < MAX_MB_PLANE; i++)
      backup_yv12[i] = xd->plane[i].pre[0];
    vp9_setup_pre_planes(xd, 0, scaled_ref_frame, mi_row, mi_col, NULL);
  }
  vp9_set_mv_search_range(x, &ref_mv);

  assert(x->mv_best_ref_index[ref] <= 2);
  if (x->mv_best_ref_index[ref] < 2)
    mvp_full = mbmi->ref_mvs[ref][x->mv_best_ref_index[ref]].as_mv;
  else
    mvp_full = x->pred_mv[ref];

  mvp_full.col >>= 3;
  mvp_full.row >>= 3;

  vp9_full_pixel_search(cpi, x, bsize, &mvp_full, step_param, sadpb,
                        cond_cost_list(cpi, cost_list),
                        &ref_mv, &tmp_mv->as_mv, INT_MAX, 0);

  x->mv_col_min = tmp_col_min;
  x->mv_col_max = tmp_col_max;
  x->mv_row_min = tmp_row_min;
  x->mv_row_max = tmp_row_max;

  // calculate the bit cost on motion vector
  mvp_full.row = tmp_mv->as_mv.row * 8;
  mvp_full.col = tmp_mv->as_mv.col * 8;

  *rate_mv = vp9_mv_bit_cost(&mvp_full, &ref_mv,
                             x->nmvjointcost, x->mvcost, MV_COST_WEIGHT);

  rate_mode = cpi->inter_mode_cost[mbmi->mode_context[ref]]
                                  [INTER_OFFSET(NEWMV)];
  rv = !(RDCOST(x->rdmult, x->rddiv, (*rate_mv + rate_mode), 0) >
         best_rd_sofar);

  if (rv) {
    cpi->find_fractional_mv_step(x, &tmp_mv->as_mv, &ref_mv,
                                 cpi->common.allow_high_precision_mv,
                                 x->errorperbit,
                                 &cpi->fn_ptr[bsize],
                                 cpi->sf.mv.subpel_force_stop,
                                 cpi->sf.mv.subpel_iters_per_step,
                                 cond_cost_list(cpi, cost_list),
                                 x->nmvjointcost, x->mvcost,
                                 &dis, &x->pred_sse[ref], NULL, 0, 0);
  }

  if (scaled_ref_frame) {
    int i;
    for (i = 0; i < MAX_MB_PLANE; i++)
      xd->plane[i].pre[0] = backup_yv12[i];
  }
  return rv;
}


static void model_rd_for_sb_y(VP9_COMP *cpi, BLOCK_SIZE bsize,
                              MACROBLOCK *x, MACROBLOCKD *xd,
                              int *out_rate_sum, int64_t *out_dist_sum,
                              unsigned int *var_y, unsigned int *sse_y) {
  // Note our transform coeffs are 8 times an orthogonal transform.
  // Hence quantizer step is also 8 times. To get effective quantizer
  // we need to divide by 8 before sending to modeling function.
  unsigned int sse;
  int rate;
  int64_t dist;
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &xd->plane[0];
  const int64_t dc_thr = p->quant_thred[0] >> 6;
  const int64_t ac_thr = p->quant_thred[1] >> 6;
  const uint32_t dc_quant = pd->dequant[0];
  const uint32_t ac_quant = pd->dequant[1];
  unsigned int var = cpi->fn_ptr[bsize].vf(p->src.buf, p->src.stride,
                                           pd->dst.buf, pd->dst.stride, &sse);
  *var_y = var;
  *sse_y = sse;

  if (cpi->common.tx_mode == TX_MODE_SELECT) {
    if (sse > (var << 2))
      xd->mi[0].src_mi->mbmi.tx_size =
          MIN(max_txsize_lookup[bsize],
              tx_mode_to_biggest_tx_size[cpi->common.tx_mode]);
    else
      xd->mi[0].src_mi->mbmi.tx_size = TX_8X8;

    if (cpi->sf.partition_search_type == VAR_BASED_PARTITION) {
      if (cpi->oxcf.aq_mode == CYCLIC_REFRESH_AQ &&
          xd->mi[0].src_mi->mbmi.segment_id != CR_SEGMENT_ID_BASE)
        xd->mi[0].src_mi->mbmi.tx_size = TX_8X8;
      else if (xd->mi[0].src_mi->mbmi.tx_size > TX_16X16)
        xd->mi[0].src_mi->mbmi.tx_size = TX_16X16;
    }
  } else {
    xd->mi[0].src_mi->mbmi.tx_size =
        MIN(max_txsize_lookup[bsize],
            tx_mode_to_biggest_tx_size[cpi->common.tx_mode]);
  }

  // Evaluate if the partition block is a skippable block in Y plane.
  {
    const BLOCK_SIZE unit_size =
        txsize_to_bsize[xd->mi[0].src_mi->mbmi.tx_size];
    const unsigned int num_blk_log2 =
        (b_width_log2_lookup[bsize] - b_width_log2_lookup[unit_size]) +
        (b_height_log2_lookup[bsize] - b_height_log2_lookup[unit_size]);
    const unsigned int sse_tx = sse >> num_blk_log2;
    const unsigned int var_tx = var >> num_blk_log2;

    x->skip_txfm[0] = 0;
    // Check if all ac coefficients can be quantized to zero.
    if (var_tx < ac_thr || var == 0) {
      x->skip_txfm[0] = 2;
      // Check if dc coefficient can be quantized to zero.
      if (sse_tx - var_tx < dc_thr || sse == var)
        x->skip_txfm[0] = 1;
    }
  }

  if (x->skip_txfm[0] == 1) {
    *out_rate_sum = 0;
    *out_dist_sum = sse << 4;
    return;
  }

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bsize],
                                 dc_quant >> (xd->bd - 5), &rate, &dist);
  } else {
    vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bsize],
                                 dc_quant >> 3, &rate, &dist);
  }
#else
  vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bsize],
                               dc_quant >> 3, &rate, &dist);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  *out_rate_sum = rate >> 1;
  *out_dist_sum = dist << 3;

#if CONFIG_VP9_HIGHBITDEPTH
  if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
    vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bsize],
                                 ac_quant >> (xd->bd - 5), &rate, &dist);
  } else {
    vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bsize],
                                 ac_quant >> 3, &rate, &dist);
  }
#else
  vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bsize],
                               ac_quant >> 3, &rate, &dist);
#endif  // CONFIG_VP9_HIGHBITDEPTH

  *out_rate_sum += rate;
  *out_dist_sum += dist << 4;
}

static void model_rd_for_sb_uv(VP9_COMP *cpi, BLOCK_SIZE bsize,
                               MACROBLOCK *x, MACROBLOCKD *xd,
                               int *out_rate_sum, int64_t *out_dist_sum,
                               unsigned int *var_y, unsigned int *sse_y) {
  // Note our transform coeffs are 8 times an orthogonal transform.
  // Hence quantizer step is also 8 times. To get effective quantizer
  // we need to divide by 8 before sending to modeling function.
  unsigned int sse;
  int rate;
  int64_t dist;
  int i;

  *out_rate_sum = 0;
  *out_dist_sum = 0;

  for (i = 1; i <= 2; ++i) {
    struct macroblock_plane *const p = &x->plane[i];
    struct macroblockd_plane *const pd = &xd->plane[i];
    const uint32_t dc_quant = pd->dequant[0];
    const uint32_t ac_quant = pd->dequant[1];
    const BLOCK_SIZE bs = get_plane_block_size(bsize, pd);
    unsigned int var;

    if (!x->color_sensitivity[i - 1])
      continue;

    var = cpi->fn_ptr[bs].vf(p->src.buf, p->src.stride,
                             pd->dst.buf, pd->dst.stride, &sse);
    *var_y += var;
    *sse_y += sse;

  #if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bs],
                                   dc_quant >> (xd->bd - 5), &rate, &dist);
    } else {
      vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bs],
                                   dc_quant >> 3, &rate, &dist);
    }
  #else
    vp9_model_rd_from_var_lapndz(sse - var, num_pels_log2_lookup[bs],
                                 dc_quant >> 3, &rate, &dist);
  #endif  // CONFIG_VP9_HIGHBITDEPTH

    *out_rate_sum += rate >> 1;
    *out_dist_sum += dist << 3;

  #if CONFIG_VP9_HIGHBITDEPTH
    if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
      vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bs],
                                   ac_quant >> (xd->bd - 5), &rate, &dist);
    } else {
      vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bs],
                                   ac_quant >> 3, &rate, &dist);
    }
  #else
    vp9_model_rd_from_var_lapndz(var, num_pels_log2_lookup[bs],
                                 ac_quant >> 3, &rate, &dist);
  #endif  // CONFIG_VP9_HIGHBITDEPTH

    *out_rate_sum += rate;
    *out_dist_sum += dist << 4;
  }
}

static int get_pred_buffer(PRED_BUFFER *p, int len) {
  int i;

  for (i = 0; i < len; i++) {
    if (!p[i].in_use) {
      p[i].in_use = 1;
      return i;
    }
  }
  return -1;
}

static void free_pred_buffer(PRED_BUFFER *p) {
  if (p != NULL)
    p->in_use = 0;
}

static void encode_breakout_test(VP9_COMP *cpi, MACROBLOCK *x,
                                 BLOCK_SIZE bsize, int mi_row, int mi_col,
                                 MV_REFERENCE_FRAME ref_frame,
                                 PREDICTION_MODE this_mode,
                                 unsigned int var_y, unsigned int sse_y,
                                 struct buf_2d yv12_mb[][MAX_MB_PLANE],
                                 int *rate, int64_t *dist) {
  MACROBLOCKD *xd = &x->e_mbd;
  MB_MODE_INFO *mbmi = &xd->mi[0].src_mi->mbmi;

  const BLOCK_SIZE uv_size = get_plane_block_size(bsize, &xd->plane[1]);
  unsigned int var = var_y, sse = sse_y;
  // Skipping threshold for ac.
  unsigned int thresh_ac;
  // Skipping threshold for dc.
  unsigned int thresh_dc;
  if (x->encode_breakout > 0) {
    // Set a maximum for threshold to avoid big PSNR loss in low bit rate
    // case. Use extreme low threshold for static frames to limit
    // skipping.
    const unsigned int max_thresh = 36000;
    // The encode_breakout input
    const unsigned int min_thresh =
        MIN(((unsigned int)x->encode_breakout << 4), max_thresh);
#if CONFIG_VP9_HIGHBITDEPTH
    const int shift = (xd->bd << 1) - 16;
#endif

    // Calculate threshold according to dequant value.
    thresh_ac = (xd->plane[0].dequant[1] * xd->plane[0].dequant[1]) >> 3;
#if CONFIG_VP9_HIGHBITDEPTH
    if ((xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) && shift > 0) {
      thresh_ac = ROUND_POWER_OF_TWO(thresh_ac, shift);
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
    thresh_ac = clamp(thresh_ac, min_thresh, max_thresh);

    // Adjust ac threshold according to partition size.
    thresh_ac >>=
        8 - (b_width_log2_lookup[bsize] + b_height_log2_lookup[bsize]);

    thresh_dc = (xd->plane[0].dequant[0] * xd->plane[0].dequant[0] >> 6);
#if CONFIG_VP9_HIGHBITDEPTH
    if ((xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) && shift > 0) {
      thresh_dc = ROUND_POWER_OF_TWO(thresh_dc, shift);
    }
#endif  // CONFIG_VP9_HIGHBITDEPTH
  } else {
    thresh_ac = 0;
    thresh_dc = 0;
  }

  // Y skipping condition checking for ac and dc.
  if (var <= thresh_ac && (sse - var) <= thresh_dc) {
    unsigned int sse_u, sse_v;
    unsigned int var_u, var_v;

    // Skip UV prediction unless breakout is zero (lossless) to save
    // computation with low impact on the result
    if (x->encode_breakout == 0) {
      xd->plane[1].pre[0] = yv12_mb[ref_frame][1];
      xd->plane[2].pre[0] = yv12_mb[ref_frame][2];
      vp9_build_inter_predictors_sbuv(xd, mi_row, mi_col, bsize);
    }

    var_u = cpi->fn_ptr[uv_size].vf(x->plane[1].src.buf,
                                    x->plane[1].src.stride,
                                    xd->plane[1].dst.buf,
                                    xd->plane[1].dst.stride, &sse_u);

    // U skipping condition checking
    if (((var_u << 2) <= thresh_ac) && (sse_u - var_u <= thresh_dc)) {
      var_v = cpi->fn_ptr[uv_size].vf(x->plane[2].src.buf,
                                      x->plane[2].src.stride,
                                      xd->plane[2].dst.buf,
                                      xd->plane[2].dst.stride, &sse_v);

      // V skipping condition checking
      if (((var_v << 2) <= thresh_ac) && (sse_v - var_v <= thresh_dc)) {
        x->skip = 1;

        // The cost of skip bit needs to be added.
        *rate = cpi->inter_mode_cost[mbmi->mode_context[ref_frame]]
                                    [INTER_OFFSET(this_mode)];

        // More on this part of rate
        // rate += vp9_cost_bit(vp9_get_skip_prob(cm, xd), 1);

        // Scaling factor for SSE from spatial domain to frequency
        // domain is 16. Adjust distortion accordingly.
        // TODO(yunqingwang): In this function, only y-plane dist is
        // calculated.
        *dist = (sse << 4);  // + ((sse_u + sse_v) << 4);

        // *disable_skip = 1;
      }
    }
  }
}

struct estimate_block_intra_args {
  VP9_COMP *cpi;
  MACROBLOCK *x;
  PREDICTION_MODE mode;
  int rate;
  int64_t dist;
};

static void estimate_block_intra(int plane, int block, BLOCK_SIZE plane_bsize,
                                 TX_SIZE tx_size, void *arg) {
  struct estimate_block_intra_args* const args = arg;
  VP9_COMP *const cpi = args->cpi;
  MACROBLOCK *const x = args->x;
  MACROBLOCKD *const xd = &x->e_mbd;
  struct macroblock_plane *const p = &x->plane[0];
  struct macroblockd_plane *const pd = &xd->plane[0];
  const BLOCK_SIZE bsize_tx = txsize_to_bsize[tx_size];
  uint8_t *const src_buf_base = p->src.buf;
  uint8_t *const dst_buf_base = pd->dst.buf;
  const int src_stride = p->src.stride;
  const int dst_stride = pd->dst.stride;
  int i, j;
  int rate;
  int64_t dist;
  unsigned int var_y, sse_y;
  txfrm_block_to_raster_xy(plane_bsize, tx_size, block, &i, &j);
  assert(plane == 0);
  (void) plane;

  p->src.buf = &src_buf_base[4 * (j * src_stride + i)];
  pd->dst.buf = &dst_buf_base[4 * (j * dst_stride + i)];
  // Use source buffer as an approximation for the fully reconstructed buffer.
  vp9_predict_intra_block(xd, block >> (2 * tx_size),
                          b_width_log2_lookup[plane_bsize],
                          tx_size, args->mode,
                          x->skip_encode ? p->src.buf : pd->dst.buf,
                          x->skip_encode ? src_stride : dst_stride,
                          pd->dst.buf, dst_stride,
                          i, j, 0);
  // This procedure assumes zero offset from p->src.buf and pd->dst.buf.
  model_rd_for_sb_y(cpi, bsize_tx, x, xd, &rate, &dist, &var_y, &sse_y);
  p->src.buf = src_buf_base;
  pd->dst.buf = dst_buf_base;
  args->rate += rate;
  args->dist += dist;
}

static const THR_MODES mode_idx[MAX_REF_FRAMES - 1][4] = {
  {THR_DC, THR_H_PRED, THR_V_PRED, THR_TM},
  {THR_NEARESTMV, THR_NEARMV, THR_ZEROMV, THR_NEWMV},
  {THR_NEARESTG, THR_NEARG, THR_ZEROG, THR_NEWG},
};

static const PREDICTION_MODE intra_mode_list[] = {
  DC_PRED, V_PRED, H_PRED, TM_PRED
};

void vp9_pick_intra_mode(VP9_COMP *cpi, MACROBLOCK *x, RD_COST *rd_cost,
                         BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  RD_COST this_rdc, best_rdc;
  PREDICTION_MODE this_mode;
  struct estimate_block_intra_args args = { cpi, x, DC_PRED, 0, 0 };
  const TX_SIZE intra_tx_size =
      MIN(max_txsize_lookup[bsize],
          tx_mode_to_biggest_tx_size[cpi->common.tx_mode]);
  MODE_INFO *const mic = xd->mi[0].src_mi;
  int *bmode_costs;
  const MODE_INFO *above_mi = xd->mi[-xd->mi_stride].src_mi;
  const MODE_INFO *left_mi = xd->left_available ? xd->mi[-1].src_mi : NULL;
  const PREDICTION_MODE A = vp9_above_block_mode(mic, above_mi, 0);
  const PREDICTION_MODE L = vp9_left_block_mode(mic, left_mi, 0);
  bmode_costs = cpi->y_mode_costs[A][L];

  (void) ctx;
  vp9_rd_cost_reset(&best_rdc);
  vp9_rd_cost_reset(&this_rdc);

  mbmi->ref_frame[0] = INTRA_FRAME;
  mbmi->mv[0].as_int = INVALID_MV;
  mbmi->uv_mode = DC_PRED;
  vpx_memset(x->skip_txfm, 0, sizeof(x->skip_txfm));

  // Change the limit of this loop to add other intra prediction
  // mode tests.
  for (this_mode = DC_PRED; this_mode <= H_PRED; ++this_mode) {
    args.mode = this_mode;
    args.rate = 0;
    args.dist = 0;
    mbmi->tx_size = intra_tx_size;
    vp9_foreach_transformed_block_in_plane(xd, bsize, 0,
                                           estimate_block_intra, &args);
    this_rdc.rate = args.rate;
    this_rdc.dist = args.dist;
    this_rdc.rate += bmode_costs[this_mode];
    this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                             this_rdc.rate, this_rdc.dist);

    if (this_rdc.rdcost < best_rdc.rdcost) {
      best_rdc = this_rdc;
      mbmi->mode = this_mode;
    }
  }

  *rd_cost = best_rdc;
}

static const PREDICTION_MODE inter_mode_set[INTER_MODES] = {
    ZEROMV, NEARESTMV, NEARMV, NEWMV,
};

static const int ref_frame_cost[MAX_REF_FRAMES] = {
    1235, 229, 530, 615,
};
// TODO(jingning) placeholder for inter-frame non-RD mode decision.
// this needs various further optimizations. to be continued..
void vp9_pick_inter_mode(VP9_COMP *cpi, MACROBLOCK *x,
                         TileDataEnc *tile_data,
                         int mi_row, int mi_col, RD_COST *rd_cost,
                         BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  struct macroblockd_plane *const pd = &xd->plane[0];
  PREDICTION_MODE best_mode = ZEROMV;
  MV_REFERENCE_FRAME ref_frame, best_ref_frame = LAST_FRAME;
  TX_SIZE best_tx_size = TX_SIZES;
  INTERP_FILTER best_pred_filter = EIGHTTAP;
  int_mv frame_mv[MB_MODE_COUNT][MAX_REF_FRAMES];
  struct buf_2d yv12_mb[4][MAX_MB_PLANE];
  static const int flag_list[4] = { 0, VP9_LAST_FLAG, VP9_GOLD_FLAG,
                                    VP9_ALT_FLAG };
  RD_COST this_rdc, best_rdc;
  uint8_t skip_txfm = 0, best_mode_skip_txfm = 0;
  // var_y and sse_y are saved to be used in skipping checking
  unsigned int var_y = UINT_MAX;
  unsigned int sse_y = UINT_MAX;
  // Reduce the intra cost penalty for small blocks (<=16x16).
  const int reduction_fac =
      (cpi->sf.partition_search_type == VAR_BASED_PARTITION &&
       bsize <= BLOCK_16X16) ? 2 : 0;
  const int intra_cost_penalty = vp9_get_intra_cost_penalty(
      cm->base_qindex, cm->y_dc_delta_q, cm->bit_depth) >> reduction_fac;
  const int64_t inter_mode_thresh = RDCOST(x->rdmult, x->rddiv,
                                           intra_cost_penalty, 0);
  const int *const rd_threshes = cpi->rd.threshes[mbmi->segment_id][bsize];
  const int *const rd_thresh_freq_fact = tile_data->thresh_freq_fact[bsize];
  INTERP_FILTER filter_ref;
  const int bsl = mi_width_log2_lookup[bsize];
  const int pred_filter_search = cm->interp_filter == SWITCHABLE ?
      (((mi_row + mi_col) >> bsl) +
       get_chessboard_index(cm->current_video_frame)) & 0x1 : 0;
  int const_motion[MAX_REF_FRAMES] = { 0 };
  const int bh = num_4x4_blocks_high_lookup[bsize] << 2;
  const int bw = num_4x4_blocks_wide_lookup[bsize] << 2;
  // For speed 6, the result of interp filter is reused later in actual encoding
  // process.
  // tmp[3] points to dst buffer, and the other 3 point to allocated buffers.
  PRED_BUFFER tmp[4];
  DECLARE_ALIGNED_ARRAY(16, uint8_t, pred_buf, 3 * 64 * 64);
#if CONFIG_VP9_HIGHBITDEPTH
  DECLARE_ALIGNED_ARRAY(16, uint16_t, pred_buf_16, 3 * 64 * 64);
#endif
  struct buf_2d orig_dst = pd->dst;
  PRED_BUFFER *best_pred = NULL;
  PRED_BUFFER *this_mode_pred = NULL;
  const int pixels_in_block = bh * bw;
  int reuse_inter_pred = cpi->sf.reuse_inter_pred_sby && ctx->pred_pixel_ready;
  int ref_frame_skip_mask = 0;

  if (reuse_inter_pred) {
    int i;
    for (i = 0; i < 3; i++) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (cm->use_highbitdepth)
        tmp[i].data = CONVERT_TO_BYTEPTR(&pred_buf_16[pixels_in_block * i]);
      else
        tmp[i].data = &pred_buf[pixels_in_block * i];
#else
      tmp[i].data = &pred_buf[pixels_in_block * i];
#endif  // CONFIG_VP9_HIGHBITDEPTH
      tmp[i].stride = bw;
      tmp[i].in_use = 0;
    }
    tmp[3].data = pd->dst.buf;
    tmp[3].stride = pd->dst.stride;
    tmp[3].in_use = 0;
  }

  x->skip_encode = cpi->sf.skip_encode_frame && x->q_index < QIDX_SKIP_THRESH;
  x->skip = 0;

  if (xd->up_available)
    filter_ref = xd->mi[-xd->mi_stride].src_mi->mbmi.interp_filter;
  else if (xd->left_available)
    filter_ref = xd->mi[-1].src_mi->mbmi.interp_filter;
  else
    filter_ref = cm->interp_filter;

  // initialize mode decisions
  vp9_rd_cost_reset(&best_rdc);
  vp9_rd_cost_reset(rd_cost);
  mbmi->sb_type = bsize;
  mbmi->ref_frame[0] = NONE;
  mbmi->ref_frame[1] = NONE;
  mbmi->tx_size = MIN(max_txsize_lookup[bsize],
                      tx_mode_to_biggest_tx_size[cm->tx_mode]);
  mbmi->interp_filter = cm->interp_filter == SWITCHABLE ?
                        EIGHTTAP : cm->interp_filter;

#if CONFIG_VP9_TEMPORAL_DENOISING
  vp9_denoiser_reset_frame_stats(ctx);
#endif

  for (ref_frame = LAST_FRAME; ref_frame <= GOLDEN_FRAME; ++ref_frame) {
    const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_buffer(cpi, ref_frame);

    x->pred_mv_sad[ref_frame] = INT_MAX;
    frame_mv[NEWMV][ref_frame].as_int = INVALID_MV;
    frame_mv[ZEROMV][ref_frame].as_int = 0;

    if ((cpi->ref_frame_flags & flag_list[ref_frame]) && (yv12 != NULL)) {
      int_mv *const candidates = mbmi->ref_mvs[ref_frame];
      const struct scale_factors *const sf = &cm->frame_refs[ref_frame - 1].sf;

      vp9_setup_pred_block(xd, yv12_mb[ref_frame], yv12, mi_row, mi_col,
                           sf, sf);

      if (cm->use_prev_frame_mvs)
        vp9_find_mv_refs(cm, xd, tile_info, xd->mi[0].src_mi, ref_frame,
                         candidates, mi_row, mi_col, NULL, NULL);
      else
        const_motion[ref_frame] = mv_refs_rt(cm, xd, tile_info,
                                             xd->mi[0].src_mi,
                                             ref_frame, candidates,
                                             mi_row, mi_col);

      vp9_find_best_ref_mvs(xd, cm->allow_high_precision_mv, candidates,
                            &frame_mv[NEARESTMV][ref_frame],
                            &frame_mv[NEARMV][ref_frame]);

      if (!vp9_is_scaled(sf) && bsize >= BLOCK_8X8)
        vp9_mv_pred(cpi, x, yv12_mb[ref_frame][0].buf, yv12->y_stride,
                    ref_frame, bsize);
    } else {
      ref_frame_skip_mask |= (1 << ref_frame);
    }
  }

  if (cpi->rc.frames_since_golden == 0)
    ref_frame_skip_mask |= (1 << GOLDEN_FRAME);

  for (ref_frame = LAST_FRAME; ref_frame <= GOLDEN_FRAME; ++ref_frame) {
    PREDICTION_MODE this_mode;
    int i = (ref_frame == LAST_FRAME) ? GOLDEN_FRAME : LAST_FRAME;

    if (!(cpi->ref_frame_flags & flag_list[ref_frame]))
      continue;

    if (cpi->ref_frame_flags & flag_list[i])
      if (x->pred_mv_sad[ref_frame] > (x->pred_mv_sad[i] << 1))
        ref_frame_skip_mask |= (1 << ref_frame);

    if (ref_frame_skip_mask & (1 << ref_frame))
      continue;

    // Select prediction reference frames.
    for (i = 0; i < MAX_MB_PLANE; i++)
      xd->plane[i].pre[0] = yv12_mb[ref_frame][i];

    clamp_mv2(&frame_mv[NEARESTMV][ref_frame].as_mv, xd);
    clamp_mv2(&frame_mv[NEARMV][ref_frame].as_mv, xd);

    mbmi->ref_frame[0] = ref_frame;
    set_ref_ptrs(cm, xd, ref_frame, NONE);

    for (i = 0; i < INTER_MODES; ++i) {
      int rate_mv = 0;
      int mode_rd_thresh;
      int mode_index;
      this_mode = inter_mode_set[i];
      mode_index = mode_idx[ref_frame][INTER_OFFSET(this_mode)];

      if (const_motion[ref_frame] && this_mode == NEARMV)
        continue;

      if (!(cpi->sf.inter_mode_mask[bsize] & (1 << this_mode)))
        continue;

      mode_rd_thresh = best_mode_skip_txfm ? rd_threshes[mode_index] << 1 :
                                             rd_threshes[mode_index];
      if (rd_less_than_thresh(best_rdc.rdcost, mode_rd_thresh,
                              rd_thresh_freq_fact[mode_index]))
        continue;

      if (this_mode == NEWMV) {
        if (ref_frame > LAST_FRAME)
          continue;
        if (cpi->sf.partition_search_type != VAR_BASED_PARTITION &&
            best_rdc.rdcost < (int64_t)(1 << num_pels_log2_lookup[bsize]))
          continue;
        if (!combined_motion_search(cpi, x, bsize, mi_row, mi_col,
                                    &frame_mv[NEWMV][ref_frame],
                                    &rate_mv, best_rdc.rdcost))
          continue;
      }

      if (this_mode != NEARESTMV &&
          frame_mv[this_mode][ref_frame].as_int ==
              frame_mv[NEARESTMV][ref_frame].as_int)
        continue;

      mbmi->mode = this_mode;
      mbmi->mv[0].as_int = frame_mv[this_mode][ref_frame].as_int;

      // Search for the best prediction filter type, when the resulting
      // motion vector is at sub-pixel accuracy level for luma component, i.e.,
      // the last three bits are all zeros.
      if (reuse_inter_pred) {
        if (!this_mode_pred) {
          this_mode_pred = &tmp[3];
        } else {
          this_mode_pred = &tmp[get_pred_buffer(tmp, 3)];
          pd->dst.buf = this_mode_pred->data;
          pd->dst.stride = bw;
        }
      }

      if ((this_mode == NEWMV || filter_ref == SWITCHABLE) &&
          pred_filter_search &&
          ((mbmi->mv[0].as_mv.row & 0x07) != 0 ||
           (mbmi->mv[0].as_mv.col & 0x07) != 0)) {
        int pf_rate[3];
        int64_t pf_dist[3];
        unsigned int pf_var[3];
        unsigned int pf_sse[3];
        TX_SIZE pf_tx_size[3];
        int64_t best_cost = INT64_MAX;
        INTERP_FILTER best_filter = SWITCHABLE, filter;
        PRED_BUFFER *current_pred = this_mode_pred;

        for (filter = EIGHTTAP; filter <= EIGHTTAP_SHARP; ++filter) {
          int64_t cost;
          mbmi->interp_filter = filter;
          vp9_build_inter_predictors_sby(xd, mi_row, mi_col, bsize);
          model_rd_for_sb_y(cpi, bsize, x, xd, &pf_rate[filter],
                            &pf_dist[filter], &pf_var[filter], &pf_sse[filter]);
          pf_rate[filter] += vp9_get_switchable_rate(cpi, xd);
          cost = RDCOST(x->rdmult, x->rddiv, pf_rate[filter], pf_dist[filter]);
          pf_tx_size[filter] = mbmi->tx_size;
          if (cost < best_cost) {
            best_filter = filter;
            best_cost = cost;
            skip_txfm = x->skip_txfm[0];

            if (reuse_inter_pred) {
              if (this_mode_pred != current_pred) {
                free_pred_buffer(this_mode_pred);
                this_mode_pred = current_pred;
              }

              if (filter < EIGHTTAP_SHARP) {
                current_pred = &tmp[get_pred_buffer(tmp, 3)];
                pd->dst.buf = current_pred->data;
                pd->dst.stride = bw;
              }
            }
          }
        }

        if (reuse_inter_pred && this_mode_pred != current_pred)
          free_pred_buffer(current_pred);

        mbmi->interp_filter = best_filter;
        mbmi->tx_size = pf_tx_size[mbmi->interp_filter];
        this_rdc.rate = pf_rate[mbmi->interp_filter];
        this_rdc.dist = pf_dist[mbmi->interp_filter];
        var_y = pf_var[mbmi->interp_filter];
        sse_y = pf_sse[mbmi->interp_filter];
        x->skip_txfm[0] = skip_txfm;
      } else {
        mbmi->interp_filter = (filter_ref == SWITCHABLE) ? EIGHTTAP: filter_ref;
        vp9_build_inter_predictors_sby(xd, mi_row, mi_col, bsize);
        model_rd_for_sb_y(cpi, bsize, x, xd, &this_rdc.rate, &this_rdc.dist,
                          &var_y, &sse_y);
        this_rdc.rate += cm->interp_filter == SWITCHABLE ?
            vp9_get_switchable_rate(cpi, xd) : 0;
      }

      // chroma component rate-distortion cost modeling
      if (x->color_sensitivity[0] || x->color_sensitivity[1]) {
        int uv_rate = 0;
        int64_t uv_dist = 0;
        if (x->color_sensitivity[0])
          vp9_build_inter_predictors_sbp(xd, mi_row, mi_col, bsize, 1);
        if (x->color_sensitivity[1])
          vp9_build_inter_predictors_sbp(xd, mi_row, mi_col, bsize, 2);
        model_rd_for_sb_uv(cpi, bsize, x, xd, &uv_rate, &uv_dist,
                           &var_y, &sse_y);
        this_rdc.rate += uv_rate;
        this_rdc.dist += uv_dist;
      }

      this_rdc.rate += rate_mv;
      this_rdc.rate += cpi->inter_mode_cost[mbmi->mode_context[ref_frame]]
                                  [INTER_OFFSET(this_mode)];
      this_rdc.rate += ref_frame_cost[ref_frame];
      this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                               this_rdc.rate, this_rdc.dist);

      // Skipping checking: test to see if this block can be reconstructed by
      // prediction only.
      if (cpi->allow_encode_breakout) {
        encode_breakout_test(cpi, x, bsize, mi_row, mi_col, ref_frame,
                             this_mode, var_y, sse_y, yv12_mb,
                             &this_rdc.rate, &this_rdc.dist);
        if (x->skip) {
          this_rdc.rate += rate_mv;
          this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                   this_rdc.rate, this_rdc.dist);
        }
      }

#if CONFIG_VP9_TEMPORAL_DENOISING
      if (cpi->oxcf.noise_sensitivity > 0)
        vp9_denoiser_update_frame_stats(mbmi, sse_y, this_mode, ctx);
#else
      (void)ctx;
#endif

      if (this_rdc.rdcost < best_rdc.rdcost || x->skip) {
        best_rdc = this_rdc;
        best_mode = this_mode;
        best_pred_filter = mbmi->interp_filter;
        best_tx_size = mbmi->tx_size;
        best_ref_frame = ref_frame;
        best_mode_skip_txfm = x->skip_txfm[0];

        if (reuse_inter_pred) {
          free_pred_buffer(best_pred);
          best_pred = this_mode_pred;
        }
      } else {
        if (reuse_inter_pred)
          free_pred_buffer(this_mode_pred);
      }

      if (x->skip)
        break;
    }

    // Check that a prediction mode has been selected.
    assert(best_rdc.rdcost < INT64_MAX);

    if (x->skip)
      break;
  }

  mbmi->mode          = best_mode;
  mbmi->interp_filter = best_pred_filter;
  mbmi->tx_size       = best_tx_size;
  mbmi->ref_frame[0]  = best_ref_frame;
  mbmi->mv[0].as_int  = frame_mv[best_mode][best_ref_frame].as_int;
  xd->mi[0].src_mi->bmi[0].as_mv[0].as_int = mbmi->mv[0].as_int;
  x->skip_txfm[0] = best_mode_skip_txfm;

  // Perform intra prediction search, if the best SAD is above a certain
  // threshold.
  if (best_rdc.rdcost == INT64_MAX ||
      (!x->skip && best_rdc.rdcost > inter_mode_thresh &&
       bsize <= cpi->sf.max_intra_bsize)) {
    struct estimate_block_intra_args args = { cpi, x, DC_PRED, 0, 0 };
    const TX_SIZE intra_tx_size =
        MIN(max_txsize_lookup[bsize],
            tx_mode_to_biggest_tx_size[cpi->common.tx_mode]);
    int i;
    TX_SIZE best_intra_tx_size = TX_SIZES;

    if (reuse_inter_pred && best_pred != NULL) {
      if (best_pred->data == orig_dst.buf) {
        this_mode_pred = &tmp[get_pred_buffer(tmp, 3)];
#if CONFIG_VP9_HIGHBITDEPTH
        if (cm->use_highbitdepth)
          vp9_highbd_convolve_copy(best_pred->data, best_pred->stride,
                                   this_mode_pred->data, this_mode_pred->stride,
                                   NULL, 0, NULL, 0, bw, bh, xd->bd);
        else
          vp9_convolve_copy(best_pred->data, best_pred->stride,
                          this_mode_pred->data, this_mode_pred->stride,
                          NULL, 0, NULL, 0, bw, bh);
#else
        vp9_convolve_copy(best_pred->data, best_pred->stride,
                          this_mode_pred->data, this_mode_pred->stride,
                          NULL, 0, NULL, 0, bw, bh);
#endif  // CONFIG_VP9_HIGHBITDEPTH
        best_pred = this_mode_pred;
      }
    }
    pd->dst = orig_dst;

    for (i = 0; i < 4; ++i) {
      const PREDICTION_MODE this_mode = intra_mode_list[i];
      if (!((1 << this_mode) & cpi->sf.intra_y_mode_mask[intra_tx_size]))
        continue;
      args.mode = this_mode;
      args.rate = 0;
      args.dist = 0;
      mbmi->tx_size = intra_tx_size;
      vp9_foreach_transformed_block_in_plane(xd, bsize, 0,
                                             estimate_block_intra, &args);
      this_rdc.rate = args.rate;
      this_rdc.dist = args.dist;
      this_rdc.rate += cpi->mbmode_cost[this_mode];
      this_rdc.rate += ref_frame_cost[INTRA_FRAME];
      this_rdc.rate += intra_cost_penalty;
      this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                               this_rdc.rate, this_rdc.dist);

      if (this_rdc.rdcost < best_rdc.rdcost) {
        best_rdc = this_rdc;
        mbmi->mode = this_mode;
        best_intra_tx_size = mbmi->tx_size;
        mbmi->ref_frame[0] = INTRA_FRAME;
        mbmi->uv_mode = this_mode;
        mbmi->mv[0].as_int = INVALID_MV;
      }
    }

    // Reset mb_mode_info to the best inter mode.
    if (mbmi->ref_frame[0] != INTRA_FRAME) {
      x->skip_txfm[0] = best_mode_skip_txfm;
      mbmi->tx_size = best_tx_size;
    } else {
      mbmi->tx_size = best_intra_tx_size;
    }
  }

  pd->dst = orig_dst;

  if (reuse_inter_pred && best_pred != NULL) {
    if (best_pred->data != orig_dst.buf && is_inter_mode(mbmi->mode)) {
#if CONFIG_VP9_HIGHBITDEPTH
      if (cm->use_highbitdepth)
        vp9_highbd_convolve_copy(best_pred->data, best_pred->stride,
                                 pd->dst.buf, pd->dst.stride, NULL, 0,
                                 NULL, 0, bw, bh, xd->bd);
      else
        vp9_convolve_copy(best_pred->data, best_pred->stride,
                          pd->dst.buf, pd->dst.stride, NULL, 0,
                          NULL, 0, bw, bh);
#else
      vp9_convolve_copy(best_pred->data, best_pred->stride,
                        pd->dst.buf, pd->dst.stride, NULL, 0,
                        NULL, 0, bw, bh);
#endif  // CONFIG_VP9_HIGHBITDEPTH
    }
  }

  if (cpi->sf.adaptive_rd_thresh) {
    THR_MODES best_mode_idx = is_inter_block(mbmi) ?
        mode_idx[best_ref_frame][INTER_OFFSET(mbmi->mode)] :
        mode_idx[INTRA_FRAME][mbmi->mode];
    PREDICTION_MODE this_mode;
    for (ref_frame = LAST_FRAME; ref_frame <= GOLDEN_FRAME; ++ref_frame) {
      for (this_mode = NEARESTMV; this_mode <= NEWMV; ++this_mode) {
        THR_MODES thr_mode_idx = mode_idx[ref_frame][INTER_OFFSET(this_mode)];
        int *freq_fact = &tile_data->thresh_freq_fact[bsize][thr_mode_idx];
        if (thr_mode_idx == best_mode_idx)
          *freq_fact -= (*freq_fact >> 4);
        else
          *freq_fact = MIN(*freq_fact + RD_THRESH_INC,
                           cpi->sf.adaptive_rd_thresh * RD_THRESH_MAX_FACT);
      }
    }
  }

  *rd_cost = best_rdc;
}

void vp9_pick_inter_mode_sub8x8(VP9_COMP *cpi, MACROBLOCK *x,
                                TileDataEnc *tile_data,
                                int mi_row, int mi_col, RD_COST *rd_cost,
                                BLOCK_SIZE bsize, PICK_MODE_CONTEXT *ctx) {
  VP9_COMMON *const cm = &cpi->common;
  TileInfo *const tile_info = &tile_data->tile_info;
  SPEED_FEATURES *const sf = &cpi->sf;
  MACROBLOCKD *const xd = &x->e_mbd;
  MB_MODE_INFO *const mbmi = &xd->mi[0].src_mi->mbmi;
  const struct segmentation *const seg = &cm->seg;
  MV_REFERENCE_FRAME ref_frame, second_ref_frame = NONE;
  MV_REFERENCE_FRAME best_ref_frame = NONE;
  unsigned char segment_id = mbmi->segment_id;
  struct buf_2d yv12_mb[4][MAX_MB_PLANE];
  static const int flag_list[4] = { 0, VP9_LAST_FLAG, VP9_GOLD_FLAG,
                                    VP9_ALT_FLAG };
  int64_t best_rd = INT64_MAX;
  b_mode_info bsi[MAX_REF_FRAMES][4];
  int ref_frame_skip_mask = 0;
  const int num_4x4_blocks_wide = num_4x4_blocks_wide_lookup[bsize];
  const int num_4x4_blocks_high = num_4x4_blocks_high_lookup[bsize];
  int idx, idy;

  x->skip_encode = sf->skip_encode_frame && x->q_index < QIDX_SKIP_THRESH;
  ctx->pred_pixel_ready = 0;

  for (ref_frame = LAST_FRAME; ref_frame <= GOLDEN_FRAME; ++ref_frame) {
    const YV12_BUFFER_CONFIG *yv12 = get_ref_frame_buffer(cpi, ref_frame);
    int_mv dummy_mv[2];
    x->pred_mv_sad[ref_frame] = INT_MAX;

    if ((cpi->ref_frame_flags & flag_list[ref_frame]) && (yv12 != NULL)) {
      int_mv *const candidates = mbmi->ref_mvs[ref_frame];
      const struct scale_factors *const sf =
                             &cm->frame_refs[ref_frame - 1].sf;
      vp9_setup_pred_block(xd, yv12_mb[ref_frame], yv12, mi_row, mi_col,
                           sf, sf);
      vp9_find_mv_refs(cm, xd, tile_info, xd->mi[0].src_mi, ref_frame,
                       candidates, mi_row, mi_col, NULL, NULL);

      vp9_find_best_ref_mvs(xd, cm->allow_high_precision_mv, candidates,
                            &dummy_mv[0], &dummy_mv[1]);
    } else {
      ref_frame_skip_mask |= (1 << ref_frame);
    }
  }

  mbmi->sb_type = bsize;
  mbmi->tx_size = TX_4X4;
  mbmi->uv_mode = DC_PRED;
  mbmi->ref_frame[0] = LAST_FRAME;
  mbmi->ref_frame[1] = NONE;
  mbmi->interp_filter = cm->interp_filter == SWITCHABLE ? EIGHTTAP
                                                        : cm->interp_filter;

  for (ref_frame = LAST_FRAME; ref_frame <= GOLDEN_FRAME; ++ref_frame) {
    int64_t this_rd = 0;
    int plane;

    if (ref_frame_skip_mask & (1 << ref_frame))
      continue;

    // TODO(jingning, agrange): Scaling reference frame not supported for
    // sub8x8 blocks. Is this supported now?
    if (ref_frame > INTRA_FRAME &&
        vp9_is_scaled(&cm->frame_refs[ref_frame - 1].sf))
      continue;

    // If the segment reference frame feature is enabled....
    // then do nothing if the current ref frame is not allowed..
    if (vp9_segfeature_active(seg, segment_id, SEG_LVL_REF_FRAME) &&
        vp9_get_segdata(seg, segment_id, SEG_LVL_REF_FRAME) != (int)ref_frame)
      continue;

    mbmi->ref_frame[0] = ref_frame;
    x->skip = 0;
    set_ref_ptrs(cm, xd, ref_frame, second_ref_frame);

    // Select prediction reference frames.
    for (plane = 0; plane < MAX_MB_PLANE; plane++)
      xd->plane[plane].pre[0] = yv12_mb[ref_frame][plane];

    for (idy = 0; idy < 2; idy += num_4x4_blocks_high) {
      for (idx = 0; idx < 2; idx += num_4x4_blocks_wide) {
        int_mv b_mv[MB_MODE_COUNT];
        int64_t b_best_rd = INT64_MAX;
        const int i = idy * 2 + idx;
        PREDICTION_MODE this_mode;
        RD_COST this_rdc;
        unsigned int var_y, sse_y;

        struct macroblock_plane *p = &x->plane[0];
        struct macroblockd_plane *pd = &xd->plane[0];

        const struct buf_2d orig_src = p->src;
        const struct buf_2d orig_dst = pd->dst;
        struct buf_2d orig_pre[2];
        vpx_memcpy(orig_pre, xd->plane[0].pre, sizeof(orig_pre));

        // set buffer pointers for sub8x8 motion search.
        p->src.buf =
            &p->src.buf[vp9_raster_block_offset(BLOCK_8X8, i, p->src.stride)];
        pd->dst.buf =
            &pd->dst.buf[vp9_raster_block_offset(BLOCK_8X8, i, pd->dst.stride)];
        pd->pre[0].buf =
            &pd->pre[0].buf[vp9_raster_block_offset(BLOCK_8X8,
                                                    i, pd->pre[0].stride)];

        b_mv[ZEROMV].as_int = 0;
        b_mv[NEWMV].as_int = INVALID_MV;
        vp9_append_sub8x8_mvs_for_idx(cm, xd, tile_info, i, 0, mi_row, mi_col,
                                      &b_mv[NEARESTMV],
                                      &b_mv[NEARMV]);

        for (this_mode = NEARESTMV; this_mode <= NEWMV; ++this_mode) {
          int b_rate = 0;
          xd->mi[0].bmi[i].as_mv[0].as_int = b_mv[this_mode].as_int;

          if (this_mode == NEWMV) {
            const int step_param = cpi->sf.mv.fullpel_search_step_param;
            MV mvp_full;
            MV tmp_mv;
            int cost_list[5];
            const int tmp_col_min = x->mv_col_min;
            const int tmp_col_max = x->mv_col_max;
            const int tmp_row_min = x->mv_row_min;
            const int tmp_row_max = x->mv_row_max;
            int dummy_dist;

            if (i == 0) {
              mvp_full.row = b_mv[NEARESTMV].as_mv.row >> 3;
              mvp_full.col = b_mv[NEARESTMV].as_mv.col >> 3;
            } else {
              mvp_full.row = xd->mi[0].bmi[0].as_mv[0].as_mv.row >> 3;
              mvp_full.col = xd->mi[0].bmi[0].as_mv[0].as_mv.col >> 3;
            }

            vp9_set_mv_search_range(x, &mbmi->ref_mvs[0]->as_mv);

            vp9_full_pixel_search(
                cpi, x, bsize, &mvp_full, step_param, x->sadperbit4,
                cond_cost_list(cpi, cost_list),
                &mbmi->ref_mvs[ref_frame][0].as_mv, &tmp_mv,
                INT_MAX, 0);

            x->mv_col_min = tmp_col_min;
            x->mv_col_max = tmp_col_max;
            x->mv_row_min = tmp_row_min;
            x->mv_row_max = tmp_row_max;

            // calculate the bit cost on motion vector
            mvp_full.row = tmp_mv.row * 8;
            mvp_full.col = tmp_mv.col * 8;

            b_rate += vp9_mv_bit_cost(&mvp_full,
                                      &mbmi->ref_mvs[ref_frame][0].as_mv,
                                      x->nmvjointcost, x->mvcost,
                                      MV_COST_WEIGHT);

            b_rate += cpi->inter_mode_cost[mbmi->mode_context[ref_frame]]
                                          [INTER_OFFSET(NEWMV)];
            if (RDCOST(x->rdmult, x->rddiv, b_rate, 0) > b_best_rd)
              continue;

            cpi->find_fractional_mv_step(x, &tmp_mv,
                                         &mbmi->ref_mvs[ref_frame][0].as_mv,
                                         cpi->common.allow_high_precision_mv,
                                         x->errorperbit,
                                         &cpi->fn_ptr[bsize],
                                         cpi->sf.mv.subpel_force_stop,
                                         cpi->sf.mv.subpel_iters_per_step,
                                         cond_cost_list(cpi, cost_list),
                                         x->nmvjointcost, x->mvcost,
                                         &dummy_dist,
                                         &x->pred_sse[ref_frame], NULL, 0, 0);

            xd->mi[0].bmi[i].as_mv[0].as_mv = tmp_mv;
          } else {
            b_rate += cpi->inter_mode_cost[mbmi->mode_context[ref_frame]]
                                          [INTER_OFFSET(this_mode)];
          }

#if CONFIG_VP9_HIGHBITDEPTH
          if (xd->cur_buf->flags & YV12_FLAG_HIGHBITDEPTH) {
            vp9_highbd_build_inter_predictor(pd->pre[0].buf, pd->pre[0].stride,
                                    pd->dst.buf, pd->dst.stride,
                                    &xd->mi[0].bmi[i].as_mv[0].as_mv,
                                    &xd->block_refs[0]->sf,
                                    4 * num_4x4_blocks_wide,
                                    4 * num_4x4_blocks_high, 0,
                                    vp9_get_interp_kernel(mbmi->interp_filter),
                                    MV_PRECISION_Q3,
                                    mi_col * MI_SIZE + 4 * (i & 0x01),
                                    mi_row * MI_SIZE + 4 * (i >> 1), xd->bd);
          } else {
#endif
            vp9_build_inter_predictor(pd->pre[0].buf, pd->pre[0].stride,
                                     pd->dst.buf, pd->dst.stride,
                                     &xd->mi[0].bmi[i].as_mv[0].as_mv,
                                     &xd->block_refs[0]->sf,
                                     4 * num_4x4_blocks_wide,
                                     4 * num_4x4_blocks_high, 0,
                                     vp9_get_interp_kernel(mbmi->interp_filter),
                                     MV_PRECISION_Q3,
                                     mi_col * MI_SIZE + 4 * (i & 0x01),
                                     mi_row * MI_SIZE + 4 * (i >> 1));

#if CONFIG_VP9_HIGHBITDEPTH
          }
#endif

          model_rd_for_sb_y(cpi, bsize, x, xd, &this_rdc.rate, &this_rdc.dist,
                            &var_y, &sse_y);

          this_rdc.rate += b_rate;
          this_rdc.rdcost = RDCOST(x->rdmult, x->rddiv,
                                   this_rdc.rate, this_rdc.dist);
          if (this_rdc.rdcost < b_best_rd) {
            b_best_rd = this_rdc.rdcost;
            bsi[ref_frame][i].as_mode = this_mode;
            bsi[ref_frame][i].as_mv[0].as_mv = xd->mi[0].bmi[i].as_mv[0].as_mv;
          }
        }  // mode search

        // restore source and prediction buffer pointers.
        p->src = orig_src;
        pd->pre[0] = orig_pre[0];
        pd->dst = orig_dst;
        this_rd += b_best_rd;

        xd->mi[0].bmi[i] = bsi[ref_frame][i];
        if (num_4x4_blocks_wide > 1)
          xd->mi[0].bmi[i + 1] = xd->mi[0].bmi[i];
        if (num_4x4_blocks_high > 1)
          xd->mi[0].bmi[i + 2] = xd->mi[0].bmi[i];
      }
    }  // loop through sub8x8 blocks

    if (this_rd < best_rd) {
      best_rd = this_rd;
      best_ref_frame = ref_frame;
    }
  }  // reference frames

  mbmi->tx_size = TX_4X4;
  mbmi->ref_frame[0] = best_ref_frame;
  for (idy = 0; idy < 2; idy += num_4x4_blocks_high) {
    for (idx = 0; idx < 2; idx += num_4x4_blocks_wide) {
      const int block = idy * 2 + idx;
      xd->mi[0].bmi[block] = bsi[best_ref_frame][block];
      if (num_4x4_blocks_wide > 1)
        xd->mi[0].bmi[block + 1] = bsi[best_ref_frame][block];
      if (num_4x4_blocks_high > 1)
        xd->mi[0].bmi[block + 2] = bsi[best_ref_frame][block];
    }
  }
  mbmi->mode = xd->mi[0].bmi[3].as_mode;
  ctx->mic = *(xd->mi[0].src_mi);
  ctx->skip_txfm[0] = 0;
  ctx->skip = 0;
  // Dummy assignment for speed -5. No effect in speed -6.
  rd_cost->rdcost = best_rd;
}
