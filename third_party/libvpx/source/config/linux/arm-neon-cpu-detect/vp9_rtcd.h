#ifndef VP9_RTCD_H_
#define VP9_RTCD_H_

#ifdef RTCD_C
#define RTCD_EXTERN
#else
#define RTCD_EXTERN extern
#endif

/*
 * VP9
 */

#include "vpx/vpx_integer.h"
#include "vp9/common/vp9_common.h"
#include "vp9/common/vp9_enums.h"

struct macroblockd;

/* Encoder forward decls */
struct macroblock;
struct vp9_variance_vtable;
struct search_site_config;
struct mv;
union int_mv;
struct yv12_buffer_config;

#ifdef __cplusplus
extern "C" {
#endif

unsigned int vp9_avg_4x4_c(const uint8_t *, int p);
#define vp9_avg_4x4 vp9_avg_4x4_c

unsigned int vp9_avg_8x8_c(const uint8_t *, int p);
unsigned int vp9_avg_8x8_neon(const uint8_t *, int p);
RTCD_EXTERN unsigned int (*vp9_avg_8x8)(const uint8_t *, int p);

int64_t vp9_block_error_c(const tran_low_t *coeff, const tran_low_t *dqcoeff, intptr_t block_size, int64_t *ssz);
#define vp9_block_error vp9_block_error_c

void vp9_convolve8_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve8_avg_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_avg_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8_avg)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve8_avg_horiz_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_avg_horiz_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8_avg_horiz)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve8_avg_vert_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_avg_vert_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8_avg_vert)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve8_horiz_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_horiz_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8_horiz)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve8_vert_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve8_vert_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve8_vert)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve_avg_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve_avg_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve_avg)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_convolve_copy_c(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
void vp9_convolve_copy_neon(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);
RTCD_EXTERN void (*vp9_convolve_copy)(const uint8_t *src, ptrdiff_t src_stride, uint8_t *dst, ptrdiff_t dst_stride, const int16_t *filter_x, int x_step_q4, const int16_t *filter_y, int y_step_q4, int w, int h);

void vp9_d117_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d117_predictor_16x16 vp9_d117_predictor_16x16_c

void vp9_d117_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d117_predictor_32x32 vp9_d117_predictor_32x32_c

void vp9_d117_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d117_predictor_4x4 vp9_d117_predictor_4x4_c

void vp9_d117_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d117_predictor_8x8 vp9_d117_predictor_8x8_c

void vp9_d135_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d135_predictor_16x16 vp9_d135_predictor_16x16_c

void vp9_d135_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d135_predictor_32x32 vp9_d135_predictor_32x32_c

void vp9_d135_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d135_predictor_4x4 vp9_d135_predictor_4x4_c

void vp9_d135_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d135_predictor_8x8 vp9_d135_predictor_8x8_c

void vp9_d153_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d153_predictor_16x16 vp9_d153_predictor_16x16_c

void vp9_d153_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d153_predictor_32x32 vp9_d153_predictor_32x32_c

void vp9_d153_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d153_predictor_4x4 vp9_d153_predictor_4x4_c

void vp9_d153_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d153_predictor_8x8 vp9_d153_predictor_8x8_c

void vp9_d207_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d207_predictor_16x16 vp9_d207_predictor_16x16_c

void vp9_d207_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d207_predictor_32x32 vp9_d207_predictor_32x32_c

void vp9_d207_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d207_predictor_4x4 vp9_d207_predictor_4x4_c

void vp9_d207_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d207_predictor_8x8 vp9_d207_predictor_8x8_c

void vp9_d45_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d45_predictor_16x16 vp9_d45_predictor_16x16_c

void vp9_d45_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d45_predictor_32x32 vp9_d45_predictor_32x32_c

void vp9_d45_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d45_predictor_4x4 vp9_d45_predictor_4x4_c

void vp9_d45_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d45_predictor_8x8 vp9_d45_predictor_8x8_c

void vp9_d63_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d63_predictor_16x16 vp9_d63_predictor_16x16_c

void vp9_d63_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d63_predictor_32x32 vp9_d63_predictor_32x32_c

void vp9_d63_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d63_predictor_4x4 vp9_d63_predictor_4x4_c

void vp9_d63_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_d63_predictor_8x8 vp9_d63_predictor_8x8_c

void vp9_dc_128_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_128_predictor_16x16 vp9_dc_128_predictor_16x16_c

void vp9_dc_128_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_128_predictor_32x32 vp9_dc_128_predictor_32x32_c

void vp9_dc_128_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_128_predictor_4x4 vp9_dc_128_predictor_4x4_c

void vp9_dc_128_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_128_predictor_8x8 vp9_dc_128_predictor_8x8_c

void vp9_dc_left_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_left_predictor_16x16 vp9_dc_left_predictor_16x16_c

void vp9_dc_left_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_left_predictor_32x32 vp9_dc_left_predictor_32x32_c

void vp9_dc_left_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_left_predictor_4x4 vp9_dc_left_predictor_4x4_c

void vp9_dc_left_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_left_predictor_8x8 vp9_dc_left_predictor_8x8_c

void vp9_dc_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_predictor_16x16 vp9_dc_predictor_16x16_c

void vp9_dc_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_predictor_32x32 vp9_dc_predictor_32x32_c

void vp9_dc_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_predictor_4x4 vp9_dc_predictor_4x4_c

void vp9_dc_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_predictor_8x8 vp9_dc_predictor_8x8_c

void vp9_dc_top_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_top_predictor_16x16 vp9_dc_top_predictor_16x16_c

void vp9_dc_top_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_top_predictor_32x32 vp9_dc_top_predictor_32x32_c

void vp9_dc_top_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_top_predictor_4x4 vp9_dc_top_predictor_4x4_c

void vp9_dc_top_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
#define vp9_dc_top_predictor_8x8 vp9_dc_top_predictor_8x8_c

int vp9_denoiser_filter_c(const uint8_t *sig, int sig_stride, const uint8_t *mc_avg, int mc_avg_stride, uint8_t *avg, int avg_stride, int increase_denoising, BLOCK_SIZE bs, int motion_magnitude);
#define vp9_denoiser_filter vp9_denoiser_filter_c

int vp9_diamond_search_sad_c(const struct macroblock *x, const struct search_site_config *cfg,  struct mv *ref_mv, struct mv *best_mv, int search_param, int sad_per_bit, int *num00, const struct vp9_variance_vtable *fn_ptr, const struct mv *center_mv);
#define vp9_diamond_search_sad vp9_diamond_search_sad_c

void vp9_fdct16x16_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct16x16 vp9_fdct16x16_c

void vp9_fdct16x16_1_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct16x16_1 vp9_fdct16x16_1_c

void vp9_fdct32x32_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct32x32 vp9_fdct32x32_c

void vp9_fdct32x32_1_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct32x32_1 vp9_fdct32x32_1_c

void vp9_fdct32x32_rd_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct32x32_rd vp9_fdct32x32_rd_c

void vp9_fdct4x4_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct4x4 vp9_fdct4x4_c

void vp9_fdct4x4_1_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fdct4x4_1 vp9_fdct4x4_1_c

void vp9_fdct8x8_c(const int16_t *input, tran_low_t *output, int stride);
void vp9_fdct8x8_neon(const int16_t *input, tran_low_t *output, int stride);
RTCD_EXTERN void (*vp9_fdct8x8)(const int16_t *input, tran_low_t *output, int stride);

void vp9_fdct8x8_1_c(const int16_t *input, tran_low_t *output, int stride);
void vp9_fdct8x8_1_neon(const int16_t *input, tran_low_t *output, int stride);
RTCD_EXTERN void (*vp9_fdct8x8_1)(const int16_t *input, tran_low_t *output, int stride);

void vp9_fdct8x8_quant_c(const int16_t *input, int stride, tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
void vp9_fdct8x8_quant_neon(const int16_t *input, int stride, tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
RTCD_EXTERN void (*vp9_fdct8x8_quant)(const int16_t *input, int stride, tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);

void vp9_fht16x16_c(const int16_t *input, tran_low_t *output, int stride, int tx_type);
#define vp9_fht16x16 vp9_fht16x16_c

void vp9_fht4x4_c(const int16_t *input, tran_low_t *output, int stride, int tx_type);
#define vp9_fht4x4 vp9_fht4x4_c

void vp9_fht8x8_c(const int16_t *input, tran_low_t *output, int stride, int tx_type);
#define vp9_fht8x8 vp9_fht8x8_c

int vp9_full_range_search_c(const struct macroblock *x, const struct search_site_config *cfg, struct mv *ref_mv, struct mv *best_mv, int search_param, int sad_per_bit, int *num00, const struct vp9_variance_vtable *fn_ptr, const struct mv *center_mv);
#define vp9_full_range_search vp9_full_range_search_c

int vp9_full_search_sad_c(const struct macroblock *x, const struct mv *ref_mv, int sad_per_bit, int distance, const struct vp9_variance_vtable *fn_ptr, const struct mv *center_mv, struct mv *best_mv);
#define vp9_full_search_sad vp9_full_search_sad_c

void vp9_fwht4x4_c(const int16_t *input, tran_low_t *output, int stride);
#define vp9_fwht4x4 vp9_fwht4x4_c

void vp9_get16x16var_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);
void vp9_get16x16var_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);
RTCD_EXTERN void (*vp9_get16x16var)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);

void vp9_get8x8var_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);
void vp9_get8x8var_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);
RTCD_EXTERN void (*vp9_get8x8var)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, int *sum);

unsigned int vp9_get_mb_ss_c(const int16_t *);
#define vp9_get_mb_ss vp9_get_mb_ss_c

void vp9_h_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_h_predictor_16x16_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_h_predictor_16x16)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_h_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_h_predictor_32x32_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_h_predictor_32x32)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_h_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_h_predictor_4x4_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_h_predictor_4x4)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_h_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_h_predictor_8x8_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_h_predictor_8x8)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_idct16x16_10_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct16x16_10_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct16x16_10_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct16x16_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct16x16_1_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct16x16_1_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct16x16_256_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct16x16_256_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct16x16_256_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct32x32_1024_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct32x32_1024_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct32x32_1024_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct32x32_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct32x32_1_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct32x32_1_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct32x32_34_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct32x32_1024_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct32x32_34_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct4x4_16_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct4x4_16_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct4x4_16_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct4x4_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct4x4_1_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct4x4_1_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct8x8_12_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct8x8_12_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct8x8_12_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct8x8_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct8x8_1_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct8x8_1_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_idct8x8_64_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
void vp9_idct8x8_64_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride);
RTCD_EXTERN void (*vp9_idct8x8_64_add)(const tran_low_t *input, uint8_t *dest, int dest_stride);

void vp9_iht16x16_256_add_c(const tran_low_t *input, uint8_t *output, int pitch, int tx_type);
#define vp9_iht16x16_256_add vp9_iht16x16_256_add_c

void vp9_iht4x4_16_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);
void vp9_iht4x4_16_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);
RTCD_EXTERN void (*vp9_iht4x4_16_add)(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);

void vp9_iht8x8_64_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);
void vp9_iht8x8_64_add_neon(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);
RTCD_EXTERN void (*vp9_iht8x8_64_add)(const tran_low_t *input, uint8_t *dest, int dest_stride, int tx_type);

int16_t vp9_int_pro_col_c(uint8_t const *ref, const int width);
#define vp9_int_pro_col vp9_int_pro_col_c

void vp9_int_pro_row_c(int16_t *hbuf, uint8_t const *ref, const int ref_stride, const int height);
#define vp9_int_pro_row vp9_int_pro_row_c

void vp9_iwht4x4_16_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
#define vp9_iwht4x4_16_add vp9_iwht4x4_16_add_c

void vp9_iwht4x4_1_add_c(const tran_low_t *input, uint8_t *dest, int dest_stride);
#define vp9_iwht4x4_1_add vp9_iwht4x4_1_add_c

void vp9_lpf_horizontal_16_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
void vp9_lpf_horizontal_16_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
RTCD_EXTERN void (*vp9_lpf_horizontal_16)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);

void vp9_lpf_horizontal_4_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
void vp9_lpf_horizontal_4_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
RTCD_EXTERN void (*vp9_lpf_horizontal_4)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);

void vp9_lpf_horizontal_4_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
void vp9_lpf_horizontal_4_dual_neon(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
RTCD_EXTERN void (*vp9_lpf_horizontal_4_dual)(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);

void vp9_lpf_horizontal_8_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
void vp9_lpf_horizontal_8_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
RTCD_EXTERN void (*vp9_lpf_horizontal_8)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);

void vp9_lpf_horizontal_8_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
void vp9_lpf_horizontal_8_dual_neon(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
RTCD_EXTERN void (*vp9_lpf_horizontal_8_dual)(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);

void vp9_lpf_vertical_16_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
void vp9_lpf_vertical_16_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
RTCD_EXTERN void (*vp9_lpf_vertical_16)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);

void vp9_lpf_vertical_16_dual_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
void vp9_lpf_vertical_16_dual_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);
RTCD_EXTERN void (*vp9_lpf_vertical_16_dual)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh);

void vp9_lpf_vertical_4_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
void vp9_lpf_vertical_4_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
RTCD_EXTERN void (*vp9_lpf_vertical_4)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);

void vp9_lpf_vertical_4_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
void vp9_lpf_vertical_4_dual_neon(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
RTCD_EXTERN void (*vp9_lpf_vertical_4_dual)(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);

void vp9_lpf_vertical_8_c(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
void vp9_lpf_vertical_8_neon(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);
RTCD_EXTERN void (*vp9_lpf_vertical_8)(uint8_t *s, int pitch, const uint8_t *blimit, const uint8_t *limit, const uint8_t *thresh, int count);

void vp9_lpf_vertical_8_dual_c(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
void vp9_lpf_vertical_8_dual_neon(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);
RTCD_EXTERN void (*vp9_lpf_vertical_8_dual)(uint8_t *s, int pitch, const uint8_t *blimit0, const uint8_t *limit0, const uint8_t *thresh0, const uint8_t *blimit1, const uint8_t *limit1, const uint8_t *thresh1);

unsigned int vp9_mse16x16_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define vp9_mse16x16 vp9_mse16x16_c

unsigned int vp9_mse16x8_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define vp9_mse16x8 vp9_mse16x8_c

unsigned int vp9_mse8x16_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define vp9_mse8x16 vp9_mse8x16_c

unsigned int vp9_mse8x8_c(const uint8_t *src_ptr, int  source_stride, const uint8_t *ref_ptr, int  recon_stride, unsigned int *sse);
#define vp9_mse8x8 vp9_mse8x8_c

void vp9_quantize_b_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define vp9_quantize_b vp9_quantize_b_c

void vp9_quantize_b_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define vp9_quantize_b_32x32 vp9_quantize_b_32x32_c

void vp9_quantize_fp_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
void vp9_quantize_fp_neon(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
RTCD_EXTERN void (*vp9_quantize_fp)(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);

void vp9_quantize_fp_32x32_c(const tran_low_t *coeff_ptr, intptr_t n_coeffs, int skip_block, const int16_t *zbin_ptr, const int16_t *round_ptr, const int16_t *quant_ptr, const int16_t *quant_shift_ptr, tran_low_t *qcoeff_ptr, tran_low_t *dqcoeff_ptr, const int16_t *dequant_ptr, uint16_t *eob_ptr, const int16_t *scan, const int16_t *iscan);
#define vp9_quantize_fp_32x32 vp9_quantize_fp_32x32_c

unsigned int vp9_sad16x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
unsigned int vp9_sad16x16_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
RTCD_EXTERN unsigned int (*vp9_sad16x16)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);

unsigned int vp9_sad16x16_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad16x16_avg vp9_sad16x16_avg_c

void vp9_sad16x16x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad16x16x3 vp9_sad16x16x3_c

void vp9_sad16x16x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
void vp9_sad16x16x4d_neon(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
RTCD_EXTERN void (*vp9_sad16x16x4d)(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);

void vp9_sad16x16x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad16x16x8 vp9_sad16x16x8_c

unsigned int vp9_sad16x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad16x32 vp9_sad16x32_c

unsigned int vp9_sad16x32_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad16x32_avg vp9_sad16x32_avg_c

void vp9_sad16x32x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad16x32x4d vp9_sad16x32x4d_c

unsigned int vp9_sad16x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
#define vp9_sad16x8 vp9_sad16x8_c

unsigned int vp9_sad16x8_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad16x8_avg vp9_sad16x8_avg_c

void vp9_sad16x8x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad16x8x3 vp9_sad16x8x3_c

void vp9_sad16x8x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad16x8x4d vp9_sad16x8x4d_c

void vp9_sad16x8x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad16x8x8 vp9_sad16x8x8_c

unsigned int vp9_sad32x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad32x16 vp9_sad32x16_c

unsigned int vp9_sad32x16_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad32x16_avg vp9_sad32x16_avg_c

void vp9_sad32x16x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad32x16x4d vp9_sad32x16x4d_c

unsigned int vp9_sad32x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
unsigned int vp9_sad32x32_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
RTCD_EXTERN unsigned int (*vp9_sad32x32)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);

unsigned int vp9_sad32x32_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad32x32_avg vp9_sad32x32_avg_c

void vp9_sad32x32x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad32x32x3 vp9_sad32x32x3_c

void vp9_sad32x32x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
void vp9_sad32x32x4d_neon(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
RTCD_EXTERN void (*vp9_sad32x32x4d)(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);

void vp9_sad32x32x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad32x32x8 vp9_sad32x32x8_c

unsigned int vp9_sad32x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad32x64 vp9_sad32x64_c

unsigned int vp9_sad32x64_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad32x64_avg vp9_sad32x64_avg_c

void vp9_sad32x64x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad32x64x4d vp9_sad32x64x4d_c

unsigned int vp9_sad4x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
#define vp9_sad4x4 vp9_sad4x4_c

unsigned int vp9_sad4x4_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad4x4_avg vp9_sad4x4_avg_c

void vp9_sad4x4x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad4x4x3 vp9_sad4x4x3_c

void vp9_sad4x4x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad4x4x4d vp9_sad4x4x4d_c

void vp9_sad4x4x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad4x4x8 vp9_sad4x4x8_c

unsigned int vp9_sad4x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad4x8 vp9_sad4x8_c

unsigned int vp9_sad4x8_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad4x8_avg vp9_sad4x8_avg_c

void vp9_sad4x8x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array);
#define vp9_sad4x8x4d vp9_sad4x8x4d_c

void vp9_sad4x8x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array);
#define vp9_sad4x8x8 vp9_sad4x8x8_c

unsigned int vp9_sad64x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad64x32 vp9_sad64x32_c

unsigned int vp9_sad64x32_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad64x32_avg vp9_sad64x32_avg_c

void vp9_sad64x32x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad64x32x4d vp9_sad64x32x4d_c

unsigned int vp9_sad64x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
unsigned int vp9_sad64x64_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
RTCD_EXTERN unsigned int (*vp9_sad64x64)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);

unsigned int vp9_sad64x64_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad64x64_avg vp9_sad64x64_avg_c

void vp9_sad64x64x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad64x64x3 vp9_sad64x64x3_c

void vp9_sad64x64x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
void vp9_sad64x64x4d_neon(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
RTCD_EXTERN void (*vp9_sad64x64x4d)(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);

void vp9_sad64x64x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad64x64x8 vp9_sad64x64x8_c

unsigned int vp9_sad8x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
#define vp9_sad8x16 vp9_sad8x16_c

unsigned int vp9_sad8x16_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad8x16_avg vp9_sad8x16_avg_c

void vp9_sad8x16x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad8x16x3 vp9_sad8x16x3_c

void vp9_sad8x16x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad8x16x4d vp9_sad8x16x4d_c

void vp9_sad8x16x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad8x16x8 vp9_sad8x16x8_c

unsigned int vp9_sad8x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride);
#define vp9_sad8x4 vp9_sad8x4_c

unsigned int vp9_sad8x4_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, const uint8_t *second_pred);
#define vp9_sad8x4_avg vp9_sad8x4_avg_c

void vp9_sad8x4x4d_c(const uint8_t *src_ptr, int src_stride, const uint8_t* const ref_ptr[], int ref_stride, unsigned int *sad_array);
#define vp9_sad8x4x4d vp9_sad8x4x4d_c

void vp9_sad8x4x8_c(const uint8_t *src_ptr, int src_stride, const uint8_t *ref_ptr, int ref_stride, uint32_t *sad_array);
#define vp9_sad8x4x8 vp9_sad8x4x8_c

unsigned int vp9_sad8x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
unsigned int vp9_sad8x8_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);
RTCD_EXTERN unsigned int (*vp9_sad8x8)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride);

unsigned int vp9_sad8x8_avg_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, const uint8_t *second_pred);
#define vp9_sad8x8_avg vp9_sad8x8_avg_c

void vp9_sad8x8x3_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int  ref_stride, unsigned int *sad_array);
#define vp9_sad8x8x3 vp9_sad8x8x3_c

void vp9_sad8x8x4d_c(const uint8_t *src_ptr, int  src_stride, const uint8_t* const ref_ptr[], int  ref_stride, unsigned int *sad_array);
#define vp9_sad8x8x4d vp9_sad8x8x4d_c

void vp9_sad8x8x8_c(const uint8_t *src_ptr, int  src_stride, const uint8_t *ref_ptr, int  ref_stride, uint32_t *sad_array);
#define vp9_sad8x8x8 vp9_sad8x8x8_c

unsigned int vp9_sub_pixel_avg_variance16x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance16x16 vp9_sub_pixel_avg_variance16x16_c

unsigned int vp9_sub_pixel_avg_variance16x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance16x32 vp9_sub_pixel_avg_variance16x32_c

unsigned int vp9_sub_pixel_avg_variance16x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance16x8 vp9_sub_pixel_avg_variance16x8_c

unsigned int vp9_sub_pixel_avg_variance32x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance32x16 vp9_sub_pixel_avg_variance32x16_c

unsigned int vp9_sub_pixel_avg_variance32x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance32x32 vp9_sub_pixel_avg_variance32x32_c

unsigned int vp9_sub_pixel_avg_variance32x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance32x64 vp9_sub_pixel_avg_variance32x64_c

unsigned int vp9_sub_pixel_avg_variance4x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance4x4 vp9_sub_pixel_avg_variance4x4_c

unsigned int vp9_sub_pixel_avg_variance4x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance4x8 vp9_sub_pixel_avg_variance4x8_c

unsigned int vp9_sub_pixel_avg_variance64x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance64x32 vp9_sub_pixel_avg_variance64x32_c

unsigned int vp9_sub_pixel_avg_variance64x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance64x64 vp9_sub_pixel_avg_variance64x64_c

unsigned int vp9_sub_pixel_avg_variance8x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance8x16 vp9_sub_pixel_avg_variance8x16_c

unsigned int vp9_sub_pixel_avg_variance8x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance8x4 vp9_sub_pixel_avg_variance8x4_c

unsigned int vp9_sub_pixel_avg_variance8x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse, const uint8_t *second_pred);
#define vp9_sub_pixel_avg_variance8x8 vp9_sub_pixel_avg_variance8x8_c

unsigned int vp9_sub_pixel_variance16x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_sub_pixel_variance16x16_neon(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_sub_pixel_variance16x16)(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_sub_pixel_variance16x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance16x32 vp9_sub_pixel_variance16x32_c

unsigned int vp9_sub_pixel_variance16x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance16x8 vp9_sub_pixel_variance16x8_c

unsigned int vp9_sub_pixel_variance32x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance32x16 vp9_sub_pixel_variance32x16_c

unsigned int vp9_sub_pixel_variance32x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_sub_pixel_variance32x32_neon(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_sub_pixel_variance32x32)(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_sub_pixel_variance32x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance32x64 vp9_sub_pixel_variance32x64_c

unsigned int vp9_sub_pixel_variance4x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance4x4 vp9_sub_pixel_variance4x4_c

unsigned int vp9_sub_pixel_variance4x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance4x8 vp9_sub_pixel_variance4x8_c

unsigned int vp9_sub_pixel_variance64x32_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance64x32 vp9_sub_pixel_variance64x32_c

unsigned int vp9_sub_pixel_variance64x64_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_sub_pixel_variance64x64_neon(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_sub_pixel_variance64x64)(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_sub_pixel_variance8x16_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance8x16 vp9_sub_pixel_variance8x16_c

unsigned int vp9_sub_pixel_variance8x4_c(const uint8_t *src_ptr, int source_stride, int xoffset, int yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_sub_pixel_variance8x4 vp9_sub_pixel_variance8x4_c

unsigned int vp9_sub_pixel_variance8x8_c(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_sub_pixel_variance8x8_neon(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_sub_pixel_variance8x8)(const uint8_t *src_ptr, int source_stride, int xoffset, int  yoffset, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

void vp9_subtract_block_c(int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint8_t *src_ptr, ptrdiff_t src_stride, const uint8_t *pred_ptr, ptrdiff_t pred_stride);
void vp9_subtract_block_neon(int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint8_t *src_ptr, ptrdiff_t src_stride, const uint8_t *pred_ptr, ptrdiff_t pred_stride);
RTCD_EXTERN void (*vp9_subtract_block)(int rows, int cols, int16_t *diff_ptr, ptrdiff_t diff_stride, const uint8_t *src_ptr, ptrdiff_t src_stride, const uint8_t *pred_ptr, ptrdiff_t pred_stride);

void vp9_temporal_filter_apply_c(uint8_t *frame1, unsigned int stride, uint8_t *frame2, unsigned int block_width, unsigned int block_height, int strength, int filter_weight, unsigned int *accumulator, uint16_t *count);
#define vp9_temporal_filter_apply vp9_temporal_filter_apply_c

void vp9_tm_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_tm_predictor_16x16_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_tm_predictor_16x16)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_tm_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_tm_predictor_32x32_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_tm_predictor_32x32)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_tm_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_tm_predictor_4x4_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_tm_predictor_4x4)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_tm_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_tm_predictor_8x8_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_tm_predictor_8x8)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_v_predictor_16x16_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_v_predictor_16x16_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_v_predictor_16x16)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_v_predictor_32x32_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_v_predictor_32x32_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_v_predictor_32x32)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_v_predictor_4x4_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_v_predictor_4x4_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_v_predictor_4x4)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

void vp9_v_predictor_8x8_c(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
void vp9_v_predictor_8x8_neon(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);
RTCD_EXTERN void (*vp9_v_predictor_8x8)(uint8_t *dst, ptrdiff_t y_stride, const uint8_t *above, const uint8_t *left);

unsigned int vp9_variance16x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance16x16_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance16x16)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_variance16x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance16x32 vp9_variance16x32_c

unsigned int vp9_variance16x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance16x8 vp9_variance16x8_c

unsigned int vp9_variance32x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance32x16 vp9_variance32x16_c

unsigned int vp9_variance32x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance32x32_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance32x32)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_variance32x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance32x64_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance32x64)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_variance4x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance4x4 vp9_variance4x4_c

unsigned int vp9_variance4x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance4x8 vp9_variance4x8_c

unsigned int vp9_variance64x32_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance64x32_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance64x32)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_variance64x64_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance64x64_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance64x64)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

unsigned int vp9_variance8x16_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance8x16 vp9_variance8x16_c

unsigned int vp9_variance8x4_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
#define vp9_variance8x4 vp9_variance8x4_c

unsigned int vp9_variance8x8_c(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
unsigned int vp9_variance8x8_neon(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);
RTCD_EXTERN unsigned int (*vp9_variance8x8)(const uint8_t *src_ptr, int source_stride, const uint8_t *ref_ptr, int ref_stride, unsigned int *sse);

int vp9_vector_var_c(int16_t const *ref, int16_t const *src, const int bwl);
#define vp9_vector_var vp9_vector_var_c

void vp9_rtcd(void);

#include "vpx_config.h"

#ifdef RTCD_C
#include "vpx_ports/arm.h"
static void setup_rtcd_internal(void)
{
    int flags = arm_cpu_caps();

    (void)flags;

    vp9_avg_8x8 = vp9_avg_8x8_c;
    if (flags & HAS_NEON) vp9_avg_8x8 = vp9_avg_8x8_neon;
    vp9_convolve8 = vp9_convolve8_c;
    if (flags & HAS_NEON) vp9_convolve8 = vp9_convolve8_neon;
    vp9_convolve8_avg = vp9_convolve8_avg_c;
    if (flags & HAS_NEON) vp9_convolve8_avg = vp9_convolve8_avg_neon;
    vp9_convolve8_avg_horiz = vp9_convolve8_avg_horiz_c;
    if (flags & HAS_NEON) vp9_convolve8_avg_horiz = vp9_convolve8_avg_horiz_neon;
    vp9_convolve8_avg_vert = vp9_convolve8_avg_vert_c;
    if (flags & HAS_NEON) vp9_convolve8_avg_vert = vp9_convolve8_avg_vert_neon;
    vp9_convolve8_horiz = vp9_convolve8_horiz_c;
    if (flags & HAS_NEON) vp9_convolve8_horiz = vp9_convolve8_horiz_neon;
    vp9_convolve8_vert = vp9_convolve8_vert_c;
    if (flags & HAS_NEON) vp9_convolve8_vert = vp9_convolve8_vert_neon;
    vp9_convolve_avg = vp9_convolve_avg_c;
    if (flags & HAS_NEON) vp9_convolve_avg = vp9_convolve_avg_neon;
    vp9_convolve_copy = vp9_convolve_copy_c;
    if (flags & HAS_NEON) vp9_convolve_copy = vp9_convolve_copy_neon;
    vp9_fdct8x8 = vp9_fdct8x8_c;
    if (flags & HAS_NEON) vp9_fdct8x8 = vp9_fdct8x8_neon;
    vp9_fdct8x8_1 = vp9_fdct8x8_1_c;
    if (flags & HAS_NEON) vp9_fdct8x8_1 = vp9_fdct8x8_1_neon;
    vp9_fdct8x8_quant = vp9_fdct8x8_quant_c;
    if (flags & HAS_NEON) vp9_fdct8x8_quant = vp9_fdct8x8_quant_neon;
    vp9_get16x16var = vp9_get16x16var_c;
    if (flags & HAS_NEON) vp9_get16x16var = vp9_get16x16var_neon;
    vp9_get8x8var = vp9_get8x8var_c;
    if (flags & HAS_NEON) vp9_get8x8var = vp9_get8x8var_neon;
    vp9_h_predictor_16x16 = vp9_h_predictor_16x16_c;
    if (flags & HAS_NEON) vp9_h_predictor_16x16 = vp9_h_predictor_16x16_neon;
    vp9_h_predictor_32x32 = vp9_h_predictor_32x32_c;
    if (flags & HAS_NEON) vp9_h_predictor_32x32 = vp9_h_predictor_32x32_neon;
    vp9_h_predictor_4x4 = vp9_h_predictor_4x4_c;
    if (flags & HAS_NEON) vp9_h_predictor_4x4 = vp9_h_predictor_4x4_neon;
    vp9_h_predictor_8x8 = vp9_h_predictor_8x8_c;
    if (flags & HAS_NEON) vp9_h_predictor_8x8 = vp9_h_predictor_8x8_neon;
    vp9_idct16x16_10_add = vp9_idct16x16_10_add_c;
    if (flags & HAS_NEON) vp9_idct16x16_10_add = vp9_idct16x16_10_add_neon;
    vp9_idct16x16_1_add = vp9_idct16x16_1_add_c;
    if (flags & HAS_NEON) vp9_idct16x16_1_add = vp9_idct16x16_1_add_neon;
    vp9_idct16x16_256_add = vp9_idct16x16_256_add_c;
    if (flags & HAS_NEON) vp9_idct16x16_256_add = vp9_idct16x16_256_add_neon;
    vp9_idct32x32_1024_add = vp9_idct32x32_1024_add_c;
    if (flags & HAS_NEON) vp9_idct32x32_1024_add = vp9_idct32x32_1024_add_neon;
    vp9_idct32x32_1_add = vp9_idct32x32_1_add_c;
    if (flags & HAS_NEON) vp9_idct32x32_1_add = vp9_idct32x32_1_add_neon;
    vp9_idct32x32_34_add = vp9_idct32x32_34_add_c;
    if (flags & HAS_NEON) vp9_idct32x32_34_add = vp9_idct32x32_1024_add_neon;
    vp9_idct4x4_16_add = vp9_idct4x4_16_add_c;
    if (flags & HAS_NEON) vp9_idct4x4_16_add = vp9_idct4x4_16_add_neon;
    vp9_idct4x4_1_add = vp9_idct4x4_1_add_c;
    if (flags & HAS_NEON) vp9_idct4x4_1_add = vp9_idct4x4_1_add_neon;
    vp9_idct8x8_12_add = vp9_idct8x8_12_add_c;
    if (flags & HAS_NEON) vp9_idct8x8_12_add = vp9_idct8x8_12_add_neon;
    vp9_idct8x8_1_add = vp9_idct8x8_1_add_c;
    if (flags & HAS_NEON) vp9_idct8x8_1_add = vp9_idct8x8_1_add_neon;
    vp9_idct8x8_64_add = vp9_idct8x8_64_add_c;
    if (flags & HAS_NEON) vp9_idct8x8_64_add = vp9_idct8x8_64_add_neon;
    vp9_iht4x4_16_add = vp9_iht4x4_16_add_c;
    if (flags & HAS_NEON) vp9_iht4x4_16_add = vp9_iht4x4_16_add_neon;
    vp9_iht8x8_64_add = vp9_iht8x8_64_add_c;
    if (flags & HAS_NEON) vp9_iht8x8_64_add = vp9_iht8x8_64_add_neon;
    vp9_lpf_horizontal_16 = vp9_lpf_horizontal_16_c;
    if (flags & HAS_NEON) vp9_lpf_horizontal_16 = vp9_lpf_horizontal_16_neon;
    vp9_lpf_horizontal_4 = vp9_lpf_horizontal_4_c;
    if (flags & HAS_NEON) vp9_lpf_horizontal_4 = vp9_lpf_horizontal_4_neon;
    vp9_lpf_horizontal_4_dual = vp9_lpf_horizontal_4_dual_c;
    if (flags & HAS_NEON) vp9_lpf_horizontal_4_dual = vp9_lpf_horizontal_4_dual_neon;
    vp9_lpf_horizontal_8 = vp9_lpf_horizontal_8_c;
    if (flags & HAS_NEON) vp9_lpf_horizontal_8 = vp9_lpf_horizontal_8_neon;
    vp9_lpf_horizontal_8_dual = vp9_lpf_horizontal_8_dual_c;
    if (flags & HAS_NEON) vp9_lpf_horizontal_8_dual = vp9_lpf_horizontal_8_dual_neon;
    vp9_lpf_vertical_16 = vp9_lpf_vertical_16_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_16 = vp9_lpf_vertical_16_neon;
    vp9_lpf_vertical_16_dual = vp9_lpf_vertical_16_dual_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_16_dual = vp9_lpf_vertical_16_dual_neon;
    vp9_lpf_vertical_4 = vp9_lpf_vertical_4_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_4 = vp9_lpf_vertical_4_neon;
    vp9_lpf_vertical_4_dual = vp9_lpf_vertical_4_dual_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_4_dual = vp9_lpf_vertical_4_dual_neon;
    vp9_lpf_vertical_8 = vp9_lpf_vertical_8_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_8 = vp9_lpf_vertical_8_neon;
    vp9_lpf_vertical_8_dual = vp9_lpf_vertical_8_dual_c;
    if (flags & HAS_NEON) vp9_lpf_vertical_8_dual = vp9_lpf_vertical_8_dual_neon;
    vp9_quantize_fp = vp9_quantize_fp_c;
    if (flags & HAS_NEON) vp9_quantize_fp = vp9_quantize_fp_neon;
    vp9_sad16x16 = vp9_sad16x16_c;
    if (flags & HAS_NEON) vp9_sad16x16 = vp9_sad16x16_neon;
    vp9_sad16x16x4d = vp9_sad16x16x4d_c;
    if (flags & HAS_NEON) vp9_sad16x16x4d = vp9_sad16x16x4d_neon;
    vp9_sad32x32 = vp9_sad32x32_c;
    if (flags & HAS_NEON) vp9_sad32x32 = vp9_sad32x32_neon;
    vp9_sad32x32x4d = vp9_sad32x32x4d_c;
    if (flags & HAS_NEON) vp9_sad32x32x4d = vp9_sad32x32x4d_neon;
    vp9_sad64x64 = vp9_sad64x64_c;
    if (flags & HAS_NEON) vp9_sad64x64 = vp9_sad64x64_neon;
    vp9_sad64x64x4d = vp9_sad64x64x4d_c;
    if (flags & HAS_NEON) vp9_sad64x64x4d = vp9_sad64x64x4d_neon;
    vp9_sad8x8 = vp9_sad8x8_c;
    if (flags & HAS_NEON) vp9_sad8x8 = vp9_sad8x8_neon;
    vp9_sub_pixel_variance16x16 = vp9_sub_pixel_variance16x16_c;
    if (flags & HAS_NEON) vp9_sub_pixel_variance16x16 = vp9_sub_pixel_variance16x16_neon;
    vp9_sub_pixel_variance32x32 = vp9_sub_pixel_variance32x32_c;
    if (flags & HAS_NEON) vp9_sub_pixel_variance32x32 = vp9_sub_pixel_variance32x32_neon;
    vp9_sub_pixel_variance64x64 = vp9_sub_pixel_variance64x64_c;
    if (flags & HAS_NEON) vp9_sub_pixel_variance64x64 = vp9_sub_pixel_variance64x64_neon;
    vp9_sub_pixel_variance8x8 = vp9_sub_pixel_variance8x8_c;
    if (flags & HAS_NEON) vp9_sub_pixel_variance8x8 = vp9_sub_pixel_variance8x8_neon;
    vp9_subtract_block = vp9_subtract_block_c;
    if (flags & HAS_NEON) vp9_subtract_block = vp9_subtract_block_neon;
    vp9_tm_predictor_16x16 = vp9_tm_predictor_16x16_c;
    if (flags & HAS_NEON) vp9_tm_predictor_16x16 = vp9_tm_predictor_16x16_neon;
    vp9_tm_predictor_32x32 = vp9_tm_predictor_32x32_c;
    if (flags & HAS_NEON) vp9_tm_predictor_32x32 = vp9_tm_predictor_32x32_neon;
    vp9_tm_predictor_4x4 = vp9_tm_predictor_4x4_c;
    if (flags & HAS_NEON) vp9_tm_predictor_4x4 = vp9_tm_predictor_4x4_neon;
    vp9_tm_predictor_8x8 = vp9_tm_predictor_8x8_c;
    if (flags & HAS_NEON) vp9_tm_predictor_8x8 = vp9_tm_predictor_8x8_neon;
    vp9_v_predictor_16x16 = vp9_v_predictor_16x16_c;
    if (flags & HAS_NEON) vp9_v_predictor_16x16 = vp9_v_predictor_16x16_neon;
    vp9_v_predictor_32x32 = vp9_v_predictor_32x32_c;
    if (flags & HAS_NEON) vp9_v_predictor_32x32 = vp9_v_predictor_32x32_neon;
    vp9_v_predictor_4x4 = vp9_v_predictor_4x4_c;
    if (flags & HAS_NEON) vp9_v_predictor_4x4 = vp9_v_predictor_4x4_neon;
    vp9_v_predictor_8x8 = vp9_v_predictor_8x8_c;
    if (flags & HAS_NEON) vp9_v_predictor_8x8 = vp9_v_predictor_8x8_neon;
    vp9_variance16x16 = vp9_variance16x16_c;
    if (flags & HAS_NEON) vp9_variance16x16 = vp9_variance16x16_neon;
    vp9_variance32x32 = vp9_variance32x32_c;
    if (flags & HAS_NEON) vp9_variance32x32 = vp9_variance32x32_neon;
    vp9_variance32x64 = vp9_variance32x64_c;
    if (flags & HAS_NEON) vp9_variance32x64 = vp9_variance32x64_neon;
    vp9_variance64x32 = vp9_variance64x32_c;
    if (flags & HAS_NEON) vp9_variance64x32 = vp9_variance64x32_neon;
    vp9_variance64x64 = vp9_variance64x64_c;
    if (flags & HAS_NEON) vp9_variance64x64 = vp9_variance64x64_neon;
    vp9_variance8x8 = vp9_variance8x8_c;
    if (flags & HAS_NEON) vp9_variance8x8 = vp9_variance8x8_neon;
}
#endif

#ifdef __cplusplus
}  // extern "C"
#endif

#endif
