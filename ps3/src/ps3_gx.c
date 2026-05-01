/* ps3_gx.c - PS3 GX compatibility backend.
 *
 * The original renderer emits GameCube GX primitives. On PS3 we keep the GX
 * contract and rasterize into RSX-backed XRGB buffers in software, then flip
 * those buffers with the RSX. This gives the port deterministic behaviour on
 * RPCS3 and hardware while the higher-level game code still uses GX calls.
 */
#include "ps3_platform.h"
#include <dolphin/gx.h>
#include <math.h>

#if defined(TARGET_PS3)
#include <malloc.h>
#include <unistd.h>
#include <sysutil/video.h>
#include <rsx/gcm_sys.h>
#include <rsx/rsx.h>
#endif

#define PS3_GX_CB_SIZE        0x100000
#define PS3_GX_HOST_SIZE      (32 * 1024 * 1024)
#define PS3_GX_MAX_VERTS      8192
#define PS3_GX_LABEL_INDEX    255
#define PS3_GX_MAX_MTX        64
#define PS3_GX_MAX_TEX_MTX    10
#define PS3_GX_MAX_TLUTS      1024
#define PS3_GX_MAX_LIGHTS     8

void OSReport(const char* fmt, ...);

typedef struct {
    int width;
    int height;
    int id;
    u32* ptr;
    u32 offset;
} PS3GxBuffer;

typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 sx, sy, sz;
    f32 rw;
    f32 s[GX_MAX_TEXCOORD];
    f32 t[GX_MAX_TEXCOORD];
    u8 has_texcoord[GX_MAX_TEXCOORD];
    u8 r, g, b, a;
} PS3GxVertex;

typedef struct {
    void* image;
    u16 width;
    u16 height;
    u32 format;
    u32 wrap_s;
    u32 wrap_t;
    u32 tlut;
    u8 mipmap;
} PS3GxTexObj;

typedef struct {
    uintptr_t lut;
    u32 format;
    u16 n_entries;
    u16 reserved;
} PS3GxTlutObj;

typedef struct {
    GXTevColorArg color_in[4];
    GXTevAlphaArg alpha_in[4];
    GXTevOp color_op;
    GXTevOp alpha_op;
    GXTevBias color_bias;
    GXTevBias alpha_bias;
    GXTevScale color_scale;
    GXTevScale alpha_scale;
    GXBool color_clamp;
    GXBool alpha_clamp;
    GXTevRegID color_out;
    GXTevRegID alpha_out;
    GXTexMapID tex_map;
    GXTexCoordID tex_coord;
    GXChannelID color_chan;
    GXTevKColorSel k_color_sel;
    GXTevKAlphaSel k_alpha_sel;
    GXTevSwapSel ras_swap;
    GXTevSwapSel tex_swap;
} PS3GxTevStage;

typedef struct {
    int active;
    u8* buf;
    u32 size;
    u32 off;
    int overflow;
} PS3GxDisplayList;

typedef struct {
    GXBool enable;
    GXColorSrc amb_src;
    GXColorSrc mat_src;
    u32 light_mask;
    GXDiffuseFn diff_fn;
    GXAttnFn attn_fn;
} PS3GxChanCtrl;

typedef struct {
    GXTexGenType func;
    GXTexGenSrc src;
    u32 mtx;
    u32 postmtx;
    GXBool normalize;
    GXBool enabled;
} PS3GxTexGen;

typedef struct {
    GXColor color;
    f32 pos[3];
    f32 dir[3];
    f32 attn_a[3];
    f32 attn_k[3];
} PS3GxLightObj;

enum {
    PS3GX_DL_OP_TEXCOPY_SRC = 0x1001,
    PS3GX_DL_OP_TEXCOPY_DST = 0x1002,
    PS3GX_DL_OP_COPY_FILTER = 0x1003,
    PS3GX_DL_OP_COPY_TEX = 0x1004,
};

static int s_gx_initialized = 0;
static u16 s_screen_w = PS3_SCREEN_WIDTH;
static u16 s_screen_h = PS3_SCREEN_HEIGHT;
static PS3GxBuffer s_buffers[2];
static int s_current_buffer = 0;
static GXColor s_clear_color = { 0, 0, 0, 0xFF };
static GXBool s_blend_enable = GX_FALSE;
static GXBlendMode s_blend_mode = GX_BM_NONE;
static GXBlendFactor s_blend_src = GX_BL_ONE;
static GXBlendFactor s_blend_dst = GX_BL_ZERO;
static GXLogicOp s_logic_op = GX_LO_COPY;
static GXBool s_color_update = GX_TRUE;
static GXBool s_alpha_update = GX_TRUE;
static GXBool s_dst_alpha_enable = GX_FALSE;
static u8 s_dst_alpha = 0;
static f32 s_view_left = 0.0f;
static f32 s_view_top = 0.0f;
static f32 s_view_w = PS3_SCREEN_WIDTH;
static f32 s_view_h = PS3_SCREEN_HEIGHT;
static f32 s_projection[16];
static int s_projection_valid = 0;
static GXProjectionType s_projection_type = GX_ORTHOGRAPHIC;
static f32 s_pos_mtx[PS3_GX_MAX_MTX][12];
static f32 s_tex_mtx[PS3_GX_MAX_TEX_MTX][12];
static GXTexMtxType s_tex_mtx_type[PS3_GX_MAX_TEX_MTX];
static u32 s_current_mtx = 0;
static const void* s_array_base[GX_VA_MAX_ATTR];
static u8 s_array_stride[GX_VA_MAX_ATTR];
static GXPrimitive s_primitive = GX_TRIANGLES;
static u16 s_expected_verts = 0;
static int s_in_begin = 0;
static int s_vertex_pending = 0;
static PS3GxVertex s_current_vertex;
static PS3GxVertex s_vertices[PS3_GX_MAX_VERTS];
static int s_vertex_count = 0;
static PS3GxTexObj s_texmaps[GX_MAX_TEXMAP];
static PS3GxTlutObj s_tluts[PS3_GX_MAX_TLUTS];
static GXTexMapID s_active_texmap = GX_TEXMAP_NULL;
static PS3GxTexGen s_tex_gens[GX_MAX_TEXCOORD];
static u8 s_num_tex_gens = 0;
static PS3GxTevStage s_tev_stages[GX_MAX_TEVSTAGE];
static u8 s_num_tev_stages = 1;
static u8 s_tev_swap_table[GX_MAX_TEVSWAP][4];
static GXColor s_tev_regs[GX_MAX_TEVREG] = {
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
};
static GXColor s_k_colors[GX_MAX_KCOLOR] = {
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
    { 255, 255, 255, 255 },
};

typedef struct {
    u8* base;
    u8* top;
    u32 size;
    u32 hiWatermark;
    u32 loWatermark;
    void* rdPtr;
    void* wrPtr;
    s32 count;
    u8 bind_cpu;
    u8 bind_gp;
} PS3GxFifoObj;

static GXFifoObj s_fifo;
static u8 s_fifo_buffer[PS3_FIFO_SIZE] __attribute__((aligned(32)));
static GXFifoObj* s_cpu_fifo = &s_fifo;
static GXFifoObj* s_gp_fifo = &s_fifo;
static OSThread* s_gx_thread = NULL;
static int s_fifo_ready = 0;
static int s_fifo_fallback_logged = 0;
static int s_gx_thread_logged = 0;
static PS3GxDisplayList s_dl;
static GXDrawSyncCallback s_draw_sync_cb = NULL;
static GXDrawDoneCallback s_draw_done_cb = NULL;
static GXBreakPtCallback s_breakpt_cb = NULL;
static u16 s_draw_token = 0;
static float* s_z_buffer = NULL;
static u8* s_alpha_buffer = NULL;
static float s_clear_z = 1.0f;
static GXBool s_z_compare_enable = GX_FALSE;
static GXCompare s_z_func = GX_LEQUAL;
static GXBool s_z_update_enable = GX_FALSE;
static f32 s_view_nearz = 0.0f;
static f32 s_view_farz = 1.0f;
static u16 s_tex_copy_src[4] = { 0, 0, PS3_SCREEN_WIDTH, PS3_SCREEN_HEIGHT };
static u16 s_tex_copy_dst_w = PS3_SCREEN_WIDTH;
static u16 s_tex_copy_dst_h = PS3_SCREEN_HEIGHT;
static GXTexFmt s_tex_copy_fmt = GX_TF_RGB565;
static GXBool s_tex_copy_mipmap = GX_FALSE;
static u16 s_disp_copy_src[4] = { 0, 0, PS3_SCREEN_WIDTH, PS3_SCREEN_HEIGHT };
static u16 s_disp_copy_dst_w = PS3_SCREEN_WIDTH;
static u16 s_disp_copy_dst_h = PS3_SCREEN_HEIGHT;
static s32 s_scissor_left = 0;
static s32 s_scissor_top = 0;
static s32 s_scissor_right = PS3_SCREEN_WIDTH - 1;
static s32 s_scissor_bottom = PS3_SCREEN_HEIGHT - 1;
static u8 s_line_width = 1;
static u8 s_point_size = 1;
static GXCompare s_alpha_comp0 = GX_ALWAYS;
static GXCompare s_alpha_comp1 = GX_ALWAYS;
static GXAlphaOp s_alpha_op = GX_AOP_AND;
static u8 s_alpha_ref0 = 0;
static u8 s_alpha_ref1 = 0;
static GXFogType s_fog_type = GX_FOG_NONE;
static f32 s_fog_start = 0.0f;
static f32 s_fog_end = 1.0f;
static GXColor s_fog_color = { 0, 0, 0, 255 };
static GXCullMode s_cull_mode = GX_CULL_NONE;
static u8 s_num_chans = 0;
static PS3GxChanCtrl s_chan_ctrl[2];
static PS3GxChanCtrl s_alpha_chan_ctrl[2];
static GXColor s_chan_amb_color[2];
static GXColor s_chan_mat_color[2];
static PS3GxLightObj s_lights[PS3_GX_MAX_LIGHTS];
static u8 s_light_loaded[PS3_GX_MAX_LIGHTS];

#if defined(TARGET_PS3)
static gcmContextData* s_rsx_context = NULL;
static void* s_host_addr = NULL;
static u32* s_depth_buffer = NULL;
static u32 s_depth_offset = 0;
static u32 s_depth_pitch = 0;
#endif

static PS3GxFifoObj* gx_fifo_cast(GXFifoObj* fifo) {
    return (PS3GxFifoObj*)(fifo ? fifo : &s_fifo);
}

static GXBool gx_fifo_ptr_in_range(const PS3GxFifoObj* f, const void* ptr) {
    const u8* p = (const u8*)ptr;
    return (f && f->base && p >= f->base && p < f->top) ? GX_TRUE : GX_FALSE;
}

static GXFifoObj* gx_fifo_ensure(GXFifoObj* fifo, const char* caller) {
    GXFifoObj* obj = fifo ? fifo : &s_fifo;
    PS3GxFifoObj* f = gx_fifo_cast(obj);

    if (!s_fifo_ready || !f->base || !f->top || f->size < 0x4000 || f->top <= f->base) {
        memset(obj, 0, sizeof(*obj));
        f = gx_fifo_cast(obj);
        f->base = s_fifo_buffer;
        f->top = s_fifo_buffer + sizeof(s_fifo_buffer);
        f->size = (u32)sizeof(s_fifo_buffer);
        f->loWatermark = 0;
        f->hiWatermark = f->size - 32;
        f->rdPtr = f->base;
        f->wrPtr = f->base;
        f->count = 0;
        f->bind_cpu = (obj == s_cpu_fifo);
        f->bind_gp = (obj == s_gp_fifo);
        s_fifo_ready = 1;
        if (!s_fifo_fallback_logged) {
            OSReport("[PS3/GX] %s: initialized safe FIFO obj=%p base=%p size=%u\n",
                     caller ? caller : "fifo", obj, f->base, f->size);
            s_fifo_fallback_logged = 1;
        }
    }

    if (!gx_fifo_ptr_in_range(f, f->rdPtr)) {
        f->rdPtr = f->base;
    }
    if (!gx_fifo_ptr_in_range(f, f->wrPtr)) {
        f->wrPtr = f->rdPtr;
    }
    if (f->count < 0 || (u32)f->count > f->size) {
        f->count = 0;
    }
    return obj;
}

static u32 pack_xrgb(u8 r, u8 g, u8 b) {
    return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static u8 expand5(u32 v) { return (u8)((v << 3) | (v >> 2)); }
static u8 expand6(u32 v) { return (u8)((v << 2) | (v >> 4)); }
static u8 expand4(u32 v) { return (u8)((v << 4) | v); }

static int wrap_coord(float coord, int size, u32 mode) {
    int v;
    if (size <= 0) return 0;
    if (fabsf(coord) <= 1.0f) {
        coord *= (float)(size - 1);
    }
    v = (int)floorf(coord);
    if (mode == GX_REPEAT || mode == GX_MIRROR) {
        int period = size;
        int q = v / period;
        v %= period;
        if (v < 0) v += period;
        if (mode == GX_MIRROR && (q & 1)) {
            v = (period - 1) - v;
        }
        return v;
    }
    if (v < 0) return 0;
    if (v >= size) return size - 1;
    return v;
}

static u16 read_be16(const u8* p) {
    return ((u16)p[0] << 8) | (u16)p[1];
}

static u32 read_be32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

static u32 gx_ceil_div_u32(u32 n, u32 d) {
    return (n + d - 1u) / d;
}

static u32 gx_tiled_block_offset(u32 x, u32 y, u32 width, u32 block_w, u32 block_h, u32 block_size) {
    u32 blocks_w = gx_ceil_div_u32(width, block_w);
    return (((y / block_h) * blocks_w) + (x / block_w)) * block_size;
}

static u8 gx_clamp_u8(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return (u8)(v + 0.5f);
}

static float gx_clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

static void gx_normalize3(float* x, float* y, float* z) {
    float len = sqrtf((*x * *x) + (*y * *y) + (*z * *z));
    if (len > 0.000001f) {
        float inv = 1.0f / len;
        *x *= inv;
        *y *= inv;
        *z *= inv;
    }
}

static int gx_tex_mtx_index(u32 id) {
    if (id >= GX_TEXMTX0 && id <= GX_TEXMTX9 && ((id - GX_TEXMTX0) % 3u) == 0u) {
        return (int)((id - GX_TEXMTX0) / 3u);
    }
    return -1;
}

static int gx_chan_index(GXChannelID chan) {
    switch (chan) {
        case GX_COLOR1:
        case GX_ALPHA1:
        case GX_COLOR1A1:
            return 1;
        case GX_COLOR0:
        case GX_ALPHA0:
        case GX_COLOR0A0:
        default:
            return 0;
    }
}

static void gx_default_vertex_color(GXColor* color) {
    if (s_chan_ctrl[0].mat_src == GX_SRC_REG) {
        *color = s_chan_mat_color[0];
    } else {
        color->r = 255;
        color->g = 255;
        color->b = 255;
        color->a = 255;
    }
}

static void gx_reset_current_vertex(void) {
    GXColor color;
    memset(&s_current_vertex, 0, sizeof(s_current_vertex));
    s_current_vertex.nz = 1.0f;
    s_current_vertex.rw = 1.0f;
    gx_default_vertex_color(&color);
    s_current_vertex.r = color.r;
    s_current_vertex.g = color.g;
    s_current_vertex.b = color.b;
    s_current_vertex.a = color.a;
}

static int gx_next_texcoord_slot(void) {
    int limit = s_num_tex_gens ? s_num_tex_gens : GX_MAX_TEXCOORD;
    if (limit > GX_MAX_TEXCOORD) {
        limit = GX_MAX_TEXCOORD;
    }
    for (int i = 0; i < limit; i++) {
        if (!s_current_vertex.has_texcoord[i]) {
            return i;
        }
    }
    return 0;
}

static void decode_rgb565(u16 px, GXColor* color) {
    color->r = expand5((px >> 11) & 0x1F);
    color->g = expand6((px >> 5) & 0x3F);
    color->b = expand5(px & 0x1F);
    color->a = 255;
}

static void decode_rgb5a3(u16 px, GXColor* color) {
    if (px & 0x8000) {
        color->a = 255;
        color->r = expand5((px >> 10) & 0x1F);
        color->g = expand5((px >> 5) & 0x1F);
        color->b = expand5(px & 0x1F);
    } else {
        color->a = (u8)((((px >> 12) & 0x7) * 255) / 7);
        color->r = expand4((px >> 8) & 0x0F);
        color->g = expand4((px >> 4) & 0x0F);
        color->b = expand4(px & 0x0F);
    }
}

static void decode_tlut_entry(const PS3GxTlutObj* tlut, u32 index, GXColor* color) {
    const u8* lut;
    u16 px;

    if (!tlut || !tlut->lut || !tlut->n_entries || index >= tlut->n_entries) {
        u8 v = (u8)index;
        color->r = color->g = color->b = v;
        color->a = 255;
        return;
    }

    lut = (const u8*)(uintptr_t)tlut->lut;
    px = read_be16(lut + index * 2u);
    switch ((GXTlutFmt)tlut->format) {
        case GX_TL_IA8:
            color->r = color->g = color->b = (u8)(px >> 8);
            color->a = (u8)px;
            break;
        case GX_TL_RGB565:
            decode_rgb565(px, color);
            break;
        case GX_TL_RGB5A3:
        default:
            decode_rgb5a3(px, color);
            break;
    }
}

static void sample_ci_texel(const PS3GxTexObj* tex, const u8* image, u32 x, u32 y, GXColor* color) {
    u32 offset;
    u32 index = 0;
    const PS3GxTlutObj* tlut = tex->tlut < PS3_GX_MAX_TLUTS ? &s_tluts[tex->tlut] : NULL;

    switch ((GXCITexFmt)tex->format) {
        case GX_TF_C4: {
            offset = gx_tiled_block_offset(x, y, tex->width, 8, 8, 32) + (y & 7u) * 4u + ((x & 7u) >> 1);
            u8 b = image[offset];
            index = (x & 1u) ? (b & 0x0F) : (b >> 4);
            break;
        }
        case GX_TF_C8:
            offset = gx_tiled_block_offset(x, y, tex->width, 8, 4, 32) + (y & 3u) * 8u + (x & 7u);
            index = image[offset];
            break;
        case GX_TF_C14X2:
            offset = gx_tiled_block_offset(x, y, tex->width, 4, 4, 32) + (y & 3u) * 8u + (x & 3u) * 2u;
            index = read_be16(image + offset) & 0x3FFFu;
            break;
        default:
            color->r = color->g = color->b = 255;
            color->a = 255;
            return;
    }

    decode_tlut_entry(tlut, index, color);
}

static void sample_cmpr_texel(const u8* image, u32 width, u32 x, u32 y, GXColor* color) {
    GXColor pal[4];
    u32 block = gx_tiled_block_offset(x, y, width, 8, 8, 32);
    u32 sx = x & 7u;
    u32 sy = y & 7u;
    u32 sub = ((sy >> 2) * 2u) + (sx >> 2);
    const u8* p = image + block + sub * 8u;
    u16 c0 = read_be16(p);
    u16 c1 = read_be16(p + 2);
    u32 bits = read_be32(p + 4);
    u32 texel = ((sy & 3u) * 4u) + (sx & 3u);
    u32 idx = (bits >> (30u - texel * 2u)) & 3u;

    decode_rgb565(c0, &pal[0]);
    decode_rgb565(c1, &pal[1]);
    if (c0 > c1) {
        pal[2].r = (u8)((2u * pal[0].r + pal[1].r) / 3u);
        pal[2].g = (u8)((2u * pal[0].g + pal[1].g) / 3u);
        pal[2].b = (u8)((2u * pal[0].b + pal[1].b) / 3u);
        pal[2].a = 255;
        pal[3].r = (u8)((pal[0].r + 2u * pal[1].r) / 3u);
        pal[3].g = (u8)((pal[0].g + 2u * pal[1].g) / 3u);
        pal[3].b = (u8)((pal[0].b + 2u * pal[1].b) / 3u);
        pal[3].a = 255;
    } else {
        pal[2].r = (u8)((pal[0].r + pal[1].r) / 2u);
        pal[2].g = (u8)((pal[0].g + pal[1].g) / 2u);
        pal[2].b = (u8)((pal[0].b + pal[1].b) / 2u);
        pal[2].a = 255;
        pal[3].r = pal[3].g = pal[3].b = 0;
        pal[3].a = 0;
    }

    *color = pal[idx];
}

static void sample_texel(const PS3GxTexObj* tex, float s, float t, GXColor* color) {
    const u8* image;
    int x, y;
    u32 ux, uy;
    u32 offset;

    if (!tex || !tex->image || tex->width == 0 || tex->height == 0 || !color) {
        return;
    }

    image = (const u8*)tex->image;
    x = wrap_coord(s, tex->width, tex->wrap_s);
    y = wrap_coord(t, tex->height, tex->wrap_t);
    ux = (u32)x;
    uy = (u32)y;

    switch (tex->format) {
        case GX_TF_I4: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 8, 8, 32) + (uy & 7u) * 4u + ((ux & 7u) >> 1);
            u8 b = image[offset];
            u8 v = expand4((ux & 1u) ? (b & 0x0F) : (b >> 4));
            color->r = color->g = color->b = v;
            color->a = 255;
            break;
        }
        case GX_TF_I8: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 8, 4, 32) + (uy & 3u) * 8u + (ux & 7u);
            u8 v = image[offset];
            color->r = color->g = color->b = v;
            color->a = 255;
            break;
        }
        case GX_TF_IA4: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 8, 4, 32) + (uy & 3u) * 8u + (ux & 7u);
            u8 b = image[offset];
            u8 i = expand4(b & 0x0F);
            color->r = color->g = color->b = i;
            color->a = expand4(b >> 4);
            break;
        }
        case GX_TF_IA8: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 4, 4, 32) + (uy & 3u) * 8u + (ux & 3u) * 2u;
            const u8* p = image + offset;
            color->a = p[0];
            color->r = color->g = color->b = p[1];
            break;
        }
        case GX_TF_RGB565: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 4, 4, 32) + (uy & 3u) * 8u + (ux & 3u) * 2u;
            decode_rgb565(read_be16(image + offset), color);
            break;
        }
        case GX_TF_RGB5A3: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 4, 4, 32) + (uy & 3u) * 8u + (ux & 3u) * 2u;
            decode_rgb5a3(read_be16(image + offset), color);
            break;
        }
        case GX_TF_RGBA8:
        case GX_CTF_YUVA8: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 4, 4, 64) + (uy & 3u) * 8u + (ux & 3u) * 2u;
            color->a = image[offset];
            color->r = image[offset + 1u];
            color->g = image[offset + 32u];
            color->b = image[offset + 33u];
            break;
        }
        case GX_TF_CMPR:
            sample_cmpr_texel(image, tex->width, ux, uy, color);
            break;
        case GX_TF_C4:
        case GX_TF_C8:
        case GX_TF_C14X2:
            sample_ci_texel(tex, image, ux, uy, color);
            break;
        case GX_TF_A8: {
            offset = gx_tiled_block_offset(ux, uy, tex->width, 8, 4, 32) + (uy & 3u) * 8u + (ux & 7u);
            color->r = color->g = color->b = 255;
            color->a = image[offset];
            break;
        }
        default:
            break;
    }
}

static void gx_color_to_float(GXColor c, float out[4]) {
    out[0] = (float)c.r / 255.0f;
    out[1] = (float)c.g / 255.0f;
    out[2] = (float)c.b / 255.0f;
    out[3] = (float)c.a / 255.0f;
}

static void gx_apply_tev_swap(GXTevSwapSel sel, const float in[4], float out[4]) {
    const u8* table;
    float src[4];
    if (sel >= GX_MAX_TEVSWAP) {
        sel = GX_TEV_SWAP0;
    }
    memcpy(src, in, sizeof(src));
    table = s_tev_swap_table[sel];
    out[0] = src[table[0] & 3u];
    out[1] = src[table[1] & 3u];
    out[2] = src[table[2] & 3u];
    out[3] = src[table[3] & 3u];
}

static void gx_sample_stage_texture(const PS3GxTevStage* stage, const float s[GX_MAX_TEXCOORD],
                                    const float t[GX_MAX_TEXCOORD], float out[4]) {
    GXColor texel = { 255, 255, 255, 255 };
    GXTexMapID map = stage->tex_map;
    GXTexCoordID coord = stage->tex_coord;
    float ts = s[0];
    float tt = t[0];

    if (map == GX_TEXMAP_NULL || map == GX_TEX_DISABLE) {
        map = s_active_texmap;
    }
    if (coord < GX_MAX_TEXCOORD) {
        ts = s[coord];
        tt = t[coord];
    }
    if (map < GX_MAX_TEXMAP) {
        sample_texel(&s_texmaps[map], ts, tt, &texel);
    }
    gx_color_to_float(texel, out);
}

static void gx_select_konst_color(GXTevKColorSel sel, float out[4]) {
    static const float frac[8] = { 1.0f, 7.0f / 8.0f, 6.0f / 8.0f, 5.0f / 8.0f,
                                  4.0f / 8.0f, 3.0f / 8.0f, 2.0f / 8.0f, 1.0f / 8.0f };
    int id;
    int chan;
    if (sel <= GX_TEV_KCSEL_1_8) {
        out[0] = out[1] = out[2] = frac[sel & 7];
        out[3] = 1.0f;
        return;
    }
    id = (sel >= GX_TEV_KCSEL_K0 && sel <= GX_TEV_KCSEL_K3) ? (int)(sel - GX_TEV_KCSEL_K0) : -1;
    if (id >= 0 && id < GX_MAX_KCOLOR) {
        gx_color_to_float(s_k_colors[id], out);
        return;
    }
    id = (sel - GX_TEV_KCSEL_K0_R) / 4;
    chan = (sel - GX_TEV_KCSEL_K0_R) & 3;
    if (id >= 0 && id < GX_MAX_KCOLOR) {
        float c[4];
        gx_color_to_float(s_k_colors[id], c);
        out[0] = out[1] = out[2] = c[chan];
        out[3] = 1.0f;
        return;
    }
    out[0] = out[1] = out[2] = out[3] = 1.0f;
}

static float gx_select_konst_alpha(GXTevKAlphaSel sel) {
    static const float frac[8] = { 1.0f, 7.0f / 8.0f, 6.0f / 8.0f, 5.0f / 8.0f,
                                  4.0f / 8.0f, 3.0f / 8.0f, 2.0f / 8.0f, 1.0f / 8.0f };
    int id;
    int chan;
    if (sel <= GX_TEV_KASEL_1_8) {
        return frac[sel & 7];
    }
    id = (sel - GX_TEV_KASEL_K0_R) / 4;
    chan = (sel - GX_TEV_KASEL_K0_R) & 3;
    if (id >= 0 && id < GX_MAX_KCOLOR) {
        float c[4];
        gx_color_to_float(s_k_colors[id], c);
        return c[chan];
    }
    return 1.0f;
}

static float gx_tev_color_arg(GXTevColorArg arg, int chan, const float prev[4], const float regs[GX_MAX_TEVREG][4],
                              const float tex[4], const float ras[4], const float konst[4]) {
    switch (arg) {
        case GX_CC_CPREV: return prev[chan];
        case GX_CC_APREV: return prev[3];
        case GX_CC_C0: return regs[GX_TEVREG0][chan];
        case GX_CC_A0: return regs[GX_TEVREG0][3];
        case GX_CC_C1: return regs[GX_TEVREG1][chan];
        case GX_CC_A1: return regs[GX_TEVREG1][3];
        case GX_CC_C2: return regs[GX_TEVREG2][chan];
        case GX_CC_A2: return regs[GX_TEVREG2][3];
        case GX_CC_TEXC: return tex[chan];
        case GX_CC_TEXA: return tex[3];
        case GX_CC_RASC: return ras[chan];
        case GX_CC_RASA: return ras[3];
        case GX_CC_ONE: return 1.0f;
        case GX_CC_HALF: return 0.5f;
        case GX_CC_KONST: return konst[chan];
        case GX_CC_ZERO:
        default:
            return 0.0f;
    }
}

static float gx_tev_alpha_arg(GXTevAlphaArg arg, const float prev[4], const float regs[GX_MAX_TEVREG][4],
                              const float tex[4], const float ras[4], float konst) {
    switch (arg) {
        case GX_CA_APREV: return prev[3];
        case GX_CA_A0: return regs[GX_TEVREG0][3];
        case GX_CA_A1: return regs[GX_TEVREG1][3];
        case GX_CA_A2: return regs[GX_TEVREG2][3];
        case GX_CA_TEXA: return tex[3];
        case GX_CA_RASA: return ras[3];
        case GX_CA_KONST: return konst;
        case GX_CA_ZERO:
        default:
            return 0.0f;
    }
}

static float gx_tev_bias(GXTevBias bias) {
    if (bias == GX_TB_ADDHALF) return 0.5f;
    if (bias == GX_TB_SUBHALF) return -0.5f;
    return 0.0f;
}

static float gx_tev_scale(GXTevScale scale) {
    switch (scale) {
        case GX_CS_SCALE_2: return 2.0f;
        case GX_CS_SCALE_4: return 4.0f;
        case GX_CS_DIVIDE_2: return 0.5f;
        case GX_CS_SCALE_1:
        default:
            return 1.0f;
    }
}

static float gx_tev_apply_op(float a, float b, float c, float d, GXTevOp op, GXTevBias bias, GXTevScale scale,
                             GXBool clamp) {
    float mix = (1.0f - c) * a + c * b;
    float out = (op == GX_TEV_SUB) ? (d - mix) : (d + mix);
    out = (out + gx_tev_bias(bias)) * gx_tev_scale(scale);
    return clamp ? gx_clamp01(out) : out;
}

static u32 gx_tev_cmp_pack8(float v) {
    return (u32)gx_clamp_u8(gx_clamp01(v) * 255.0f);
}

static void gx_tev_apply_color_op_vec(const float a[3], const float b[3], const float c[3], const float d[3],
                                      GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, float out[3]) {
    int cmp;

    switch (op) {
        case GX_TEV_COMP_R8_GT:
            cmp = gx_tev_cmp_pack8(a[0]) > gx_tev_cmp_pack8(b[0]);
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        case GX_TEV_COMP_R8_EQ:
            cmp = gx_tev_cmp_pack8(a[0]) == gx_tev_cmp_pack8(b[0]);
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        case GX_TEV_COMP_GR16_GT: {
            u32 av = (gx_tev_cmp_pack8(a[0]) << 8) | gx_tev_cmp_pack8(a[1]);
            u32 bv = (gx_tev_cmp_pack8(b[0]) << 8) | gx_tev_cmp_pack8(b[1]);
            cmp = av > bv;
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        }
        case GX_TEV_COMP_GR16_EQ: {
            u32 av = (gx_tev_cmp_pack8(a[0]) << 8) | gx_tev_cmp_pack8(a[1]);
            u32 bv = (gx_tev_cmp_pack8(b[0]) << 8) | gx_tev_cmp_pack8(b[1]);
            cmp = av == bv;
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        }
        case GX_TEV_COMP_BGR24_GT: {
            u32 av = (gx_tev_cmp_pack8(a[2]) << 16) | (gx_tev_cmp_pack8(a[1]) << 8) | gx_tev_cmp_pack8(a[0]);
            u32 bv = (gx_tev_cmp_pack8(b[2]) << 16) | (gx_tev_cmp_pack8(b[1]) << 8) | gx_tev_cmp_pack8(b[0]);
            cmp = av > bv;
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        }
        case GX_TEV_COMP_BGR24_EQ: {
            u32 av = (gx_tev_cmp_pack8(a[2]) << 16) | (gx_tev_cmp_pack8(a[1]) << 8) | gx_tev_cmp_pack8(a[0]);
            u32 bv = (gx_tev_cmp_pack8(b[2]) << 16) | (gx_tev_cmp_pack8(b[1]) << 8) | gx_tev_cmp_pack8(b[0]);
            cmp = av == bv;
            for (int ch = 0; ch < 3; ch++) out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            return;
        }
        case GX_TEV_COMP_RGB8_GT:
            for (int ch = 0; ch < 3; ch++) {
                cmp = gx_tev_cmp_pack8(a[ch]) > gx_tev_cmp_pack8(b[ch]);
                out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            }
            return;
        case GX_TEV_COMP_RGB8_EQ:
            for (int ch = 0; ch < 3; ch++) {
                cmp = gx_tev_cmp_pack8(a[ch]) == gx_tev_cmp_pack8(b[ch]);
                out[ch] = clamp ? gx_clamp01(d[ch] + (cmp ? c[ch] : 0.0f)) : d[ch] + (cmp ? c[ch] : 0.0f);
            }
            return;
        default:
            for (int ch = 0; ch < 3; ch++) {
                out[ch] = gx_tev_apply_op(a[ch], b[ch], c[ch], d[ch], op, bias, scale, clamp);
            }
            return;
    }
}

static void apply_tev(const float s[GX_MAX_TEXCOORD], const float t[GX_MAX_TEXCOORD],
                      float z, u8* r, u8* g, u8* b, u8* a) {
    float prev[4];
    float regs[GX_MAX_TEVREG][4];
    float ras[4];
    float ras_stage[4];
    int stages = s_num_tev_stages;
    GXColor ras_color = { *r, *g, *b, *a };

    gx_color_to_float(ras_color, ras);
    gx_color_to_float(ras_color, prev);
    for (int i = 0; i < GX_MAX_TEVREG; i++) {
        gx_color_to_float(s_tev_regs[i], regs[i]);
    }
    if (stages <= 0) stages = 1;
    if (stages > GX_MAX_TEVSTAGE) stages = GX_MAX_TEVSTAGE;

    for (int stage_idx = 0; stage_idx < stages; stage_idx++) {
        const PS3GxTevStage* st = &s_tev_stages[stage_idx];
        float tex[4];
        float konst[4];
        float next[4] = { prev[0], prev[1], prev[2], prev[3] };
        gx_sample_stage_texture(st, s, t, tex);
        gx_apply_tev_swap(st->tex_swap, tex, tex);
        gx_apply_tev_swap(st->ras_swap, ras, ras_stage);
        gx_select_konst_color(st->k_color_sel, konst);

        {
            float av[3], bv[3], cv[3], dv[3];
            for (int ch = 0; ch < 3; ch++) {
                av[ch] = gx_tev_color_arg(st->color_in[0], ch, prev, regs, tex, ras_stage, konst);
                bv[ch] = gx_tev_color_arg(st->color_in[1], ch, prev, regs, tex, ras_stage, konst);
                cv[ch] = gx_tev_color_arg(st->color_in[2], ch, prev, regs, tex, ras_stage, konst);
                dv[ch] = gx_tev_color_arg(st->color_in[3], ch, prev, regs, tex, ras_stage, konst);
            }
            gx_tev_apply_color_op_vec(av, bv, cv, dv, st->color_op, st->color_bias, st->color_scale, st->color_clamp, next);
        }
        {
            float ka = gx_select_konst_alpha(st->k_alpha_sel);
            float av = gx_tev_alpha_arg(st->alpha_in[0], prev, regs, tex, ras_stage, ka);
            float bv = gx_tev_alpha_arg(st->alpha_in[1], prev, regs, tex, ras_stage, ka);
            float cv = gx_tev_alpha_arg(st->alpha_in[2], prev, regs, tex, ras_stage, ka);
            float dv = gx_tev_alpha_arg(st->alpha_in[3], prev, regs, tex, ras_stage, ka);
            next[3] = gx_tev_apply_op(av, bv, cv, dv, st->alpha_op, st->alpha_bias, st->alpha_scale, st->alpha_clamp);
        }

        if (st->color_out < GX_MAX_TEVREG) {
            regs[st->color_out][0] = next[0];
            regs[st->color_out][1] = next[1];
            regs[st->color_out][2] = next[2];
        }
        if (st->alpha_out < GX_MAX_TEVREG) {
            regs[st->alpha_out][3] = next[3];
        }
        memcpy(prev, next, sizeof(prev));
    }

    if (s_fog_type != GX_FOG_NONE && s_fog_end != s_fog_start) {
        float f = gx_clamp01((z - s_fog_start) / (s_fog_end - s_fog_start));
        float fog[4];
        gx_color_to_float(s_fog_color, fog);
        prev[0] = prev[0] * (1.0f - f) + fog[0] * f;
        prev[1] = prev[1] * (1.0f - f) + fog[1] * f;
        prev[2] = prev[2] * (1.0f - f) + fog[2] * f;
    }

    *r = gx_clamp_u8(prev[0] * 255.0f);
    *g = gx_clamp_u8(prev[1] * 255.0f);
    *b = gx_clamp_u8(prev[2] * 255.0f);
    *a = gx_clamp_u8(prev[3] * 255.0f);
}

static void gx_identity_mtx(f32* m) {
    memset(m, 0, sizeof(f32) * 12);
    m[0] = 1.0f;
    m[5] = 1.0f;
    m[10] = 1.0f;
}

static void gx_set_defaults(void) {
    int i;
    for (i = 0; i < PS3_GX_MAX_MTX; i++) {
        gx_identity_mtx(s_pos_mtx[i]);
    }
    for (i = 0; i < PS3_GX_MAX_TEX_MTX; i++) {
        gx_identity_mtx(s_tex_mtx[i]);
        s_tex_mtx_type[i] = GX_MTX3x4;
    }
    memset(s_projection, 0, sizeof(s_projection));
    s_projection[0] = 1.0f;
    s_projection[5] = 1.0f;
    s_projection[10] = 1.0f;
    s_projection[15] = 1.0f;
    s_projection_valid = 0;
    s_projection_type = GX_ORTHOGRAPHIC;
    memset(s_texmaps, 0, sizeof(s_texmaps));
    memset(s_tluts, 0, sizeof(s_tluts));
    s_active_texmap = GX_TEXMAP_NULL;
    memset(s_tex_gens, 0, sizeof(s_tex_gens));
    s_num_tex_gens = 0;
    for (i = 0; i < GX_MAX_TEVSTAGE; i++) {
        PS3GxTevStage* st = &s_tev_stages[i];
        memset(st, 0, sizeof(*st));
        st->color_in[0] = GX_CC_ZERO;
        st->color_in[1] = GX_CC_TEXC;
        st->color_in[2] = GX_CC_RASC;
        st->color_in[3] = GX_CC_ZERO;
        st->alpha_in[0] = GX_CA_ZERO;
        st->alpha_in[1] = GX_CA_TEXA;
        st->alpha_in[2] = GX_CA_RASA;
        st->alpha_in[3] = GX_CA_ZERO;
        st->color_op = GX_TEV_ADD;
        st->alpha_op = GX_TEV_ADD;
        st->color_bias = GX_TB_ZERO;
        st->alpha_bias = GX_TB_ZERO;
        st->color_scale = GX_CS_SCALE_1;
        st->alpha_scale = GX_CS_SCALE_1;
        st->color_clamp = GX_TRUE;
        st->alpha_clamp = GX_TRUE;
        st->color_out = GX_TEVPREV;
        st->alpha_out = GX_TEVPREV;
        st->tex_map = GX_TEXMAP_NULL;
        st->tex_coord = GX_TEXCOORD_NULL;
        st->color_chan = GX_COLOR_NULL;
        st->k_color_sel = GX_TEV_KCSEL_1;
        st->k_alpha_sel = GX_TEV_KASEL_1;
        st->ras_swap = GX_TEV_SWAP0;
        st->tex_swap = GX_TEV_SWAP0;
    }
    s_tev_swap_table[GX_TEV_SWAP0][0] = GX_CH_RED;
    s_tev_swap_table[GX_TEV_SWAP0][1] = GX_CH_GREEN;
    s_tev_swap_table[GX_TEV_SWAP0][2] = GX_CH_BLUE;
    s_tev_swap_table[GX_TEV_SWAP0][3] = GX_CH_ALPHA;
    s_tev_swap_table[GX_TEV_SWAP1][0] = GX_CH_RED;
    s_tev_swap_table[GX_TEV_SWAP1][1] = GX_CH_RED;
    s_tev_swap_table[GX_TEV_SWAP1][2] = GX_CH_RED;
    s_tev_swap_table[GX_TEV_SWAP1][3] = GX_CH_ALPHA;
    s_tev_swap_table[GX_TEV_SWAP2][0] = GX_CH_GREEN;
    s_tev_swap_table[GX_TEV_SWAP2][1] = GX_CH_GREEN;
    s_tev_swap_table[GX_TEV_SWAP2][2] = GX_CH_GREEN;
    s_tev_swap_table[GX_TEV_SWAP2][3] = GX_CH_ALPHA;
    s_tev_swap_table[GX_TEV_SWAP3][0] = GX_CH_BLUE;
    s_tev_swap_table[GX_TEV_SWAP3][1] = GX_CH_BLUE;
    s_tev_swap_table[GX_TEV_SWAP3][2] = GX_CH_BLUE;
    s_tev_swap_table[GX_TEV_SWAP3][3] = GX_CH_ALPHA;
    s_num_tev_stages = 1;
    for (i = 0; i < GX_MAX_TEVREG; i++) {
        s_tev_regs[i].r = 255;
        s_tev_regs[i].g = 255;
        s_tev_regs[i].b = 255;
        s_tev_regs[i].a = 255;
    }
    for (i = 0; i < GX_MAX_KCOLOR; i++) {
        s_k_colors[i].r = 255;
        s_k_colors[i].g = 255;
        s_k_colors[i].b = 255;
        s_k_colors[i].a = 255;
    }
    s_num_chans = 0;
    for (i = 0; i < 2; i++) {
        s_chan_ctrl[i].enable = GX_FALSE;
        s_chan_ctrl[i].amb_src = GX_SRC_REG;
        s_chan_ctrl[i].mat_src = GX_SRC_VTX;
        s_chan_ctrl[i].light_mask = GX_LIGHT_NULL;
        s_chan_ctrl[i].diff_fn = GX_DF_NONE;
        s_chan_ctrl[i].attn_fn = GX_AF_NONE;
        s_alpha_chan_ctrl[i] = s_chan_ctrl[i];
        s_chan_amb_color[i].r = 0;
        s_chan_amb_color[i].g = 0;
        s_chan_amb_color[i].b = 0;
        s_chan_amb_color[i].a = 255;
        s_chan_mat_color[i].r = 255;
        s_chan_mat_color[i].g = 255;
        s_chan_mat_color[i].b = 255;
        s_chan_mat_color[i].a = 255;
    }
    memset(s_lights, 0, sizeof(s_lights));
    memset(s_light_loaded, 0, sizeof(s_light_loaded));
    gx_reset_current_vertex();
    s_blend_enable = GX_FALSE;
    s_blend_mode = GX_BM_NONE;
    s_blend_src = GX_BL_ONE;
    s_blend_dst = GX_BL_ZERO;
    s_logic_op = GX_LO_COPY;
    s_color_update = GX_TRUE;
    s_alpha_update = GX_TRUE;
    s_dst_alpha_enable = GX_FALSE;
    s_dst_alpha = 0;
    s_z_compare_enable = GX_FALSE;
    s_z_func = GX_LEQUAL;
    s_z_update_enable = GX_FALSE;
    s_clear_z = 1.0f;
    s_view_nearz = 0.0f;
    s_view_farz = 1.0f;
    s_alpha_comp0 = GX_ALWAYS;
    s_alpha_comp1 = GX_ALWAYS;
    s_alpha_op = GX_AOP_AND;
    s_alpha_ref0 = 0;
    s_alpha_ref1 = 0;
    s_fog_type = GX_FOG_NONE;
    s_fog_start = 0.0f;
    s_fog_end = 1.0f;
    s_fog_color.r = s_fog_color.g = s_fog_color.b = 0;
    s_fog_color.a = 255;
    s_cull_mode = GX_CULL_NONE;
    s_scissor_left = 0;
    s_scissor_top = 0;
    s_scissor_right = (s32)s_screen_w - 1;
    s_scissor_bottom = (s32)s_screen_h - 1;
    s_line_width = 1;
    s_point_size = 1;
    s_tex_copy_src[0] = 0;
    s_tex_copy_src[1] = 0;
    s_tex_copy_src[2] = s_screen_w;
    s_tex_copy_src[3] = s_screen_h;
    s_tex_copy_dst_w = s_screen_w;
    s_tex_copy_dst_h = s_screen_h;
    s_disp_copy_src[0] = 0;
    s_disp_copy_src[1] = 0;
    s_disp_copy_src[2] = s_screen_w;
    s_disp_copy_src[3] = s_screen_h;
    s_disp_copy_dst_w = s_screen_w;
    s_disp_copy_dst_h = s_screen_h;
}

static u32* gx_framebuffer(void) {
    if (!s_gx_initialized) {
        return NULL;
    }
    return s_buffers[s_current_buffer].ptr;
}

static void gx_clear_framebuffer(void) {
    u32* fb = gx_framebuffer();
    u32 color = pack_xrgb(s_clear_color.r, s_clear_color.g, s_clear_color.b);
    u32 pixels = (u32)s_screen_w * (u32)s_screen_h;
    if (fb) {
        for (u32 i = 0; i < pixels; i++) {
            fb[i] = color;
        }
    }
    if (s_z_buffer) {
        for (u32 i = 0; i < pixels; i++) {
            s_z_buffer[i] = s_clear_z;
        }
    }
    if (s_alpha_buffer) {
        memset(s_alpha_buffer, s_clear_color.a, pixels);
    }
}

static int gx_in_scissor(int x, int y) {
    return x >= s_scissor_left && x <= s_scissor_right &&
           y >= s_scissor_top && y <= s_scissor_bottom;
}

static u32 gx_logic_apply(u32 src, u32 dst) {
    switch (s_logic_op) {
        case GX_LO_CLEAR: return 0;
        case GX_LO_AND: return src & dst;
        case GX_LO_REVAND: return src & ~dst;
        case GX_LO_INVAND: return ~src & dst;
        case GX_LO_NOOP: return dst;
        case GX_LO_XOR: return src ^ dst;
        case GX_LO_OR: return src | dst;
        case GX_LO_NOR: return ~(src | dst);
        case GX_LO_EQUIV: return ~(src ^ dst);
        case GX_LO_INV: return ~dst;
        case GX_LO_REVOR: return src | ~dst;
        case GX_LO_INVCOPY: return ~src;
        case GX_LO_INVOR: return ~src | dst;
        case GX_LO_NAND: return ~(src & dst);
        case GX_LO_SET: return 0x00FFFFFFu;
        case GX_LO_COPY:
        default:
            return src;
    }
}

static float gx_blend_factor(GXBlendFactor factor, int for_src, u8 src_c, u8 dst_c, u8 src_a, u8 dst_a) {
    switch (factor) {
        case GX_BL_ZERO:
            return 0.0f;
        case GX_BL_ONE:
            return 1.0f;
        case GX_BL_SRCCLR:
            return (float)(for_src ? dst_c : src_c) / 255.0f;
        case GX_BL_INVSRCCLR:
            return 1.0f - ((float)(for_src ? dst_c : src_c) / 255.0f);
        case GX_BL_SRCALPHA:
            return (float)src_a / 255.0f;
        case GX_BL_INVSRCALPHA:
            return 1.0f - ((float)src_a / 255.0f);
        case GX_BL_DSTALPHA:
            return (float)dst_a / 255.0f;
        case GX_BL_INVDSTALPHA:
            return 1.0f - ((float)dst_a / 255.0f);
        default:
            return 1.0f;
    }
}

static u8 gx_blend_channel(u8 src, u8 dst, u8 src_a, u8 dst_a) {
    float sf = gx_blend_factor(s_blend_src, 1, src, dst, src_a, dst_a);
    float df = gx_blend_factor(s_blend_dst, 0, src, dst, src_a, dst_a);
    return gx_clamp_u8((float)src * sf + (float)dst * df);
}

static void gx_plot(int x, int y, u8 r, u8 g, u8 b, u8 a) {
    u32* fb = gx_framebuffer();
    u32* dst;
    u32 idx;
    u32 dc, dr, dg, db, da;
    u32 src;

    if ((!fb || !s_color_update) && (!s_alpha_buffer || !s_alpha_update)) return;
    if (x < 0 || y < 0 || x >= (int)s_screen_w || y >= (int)s_screen_h) return;
    if (!gx_in_scissor(x, y)) return;

    idx = (u32)y * s_screen_w + (u32)x;
    if (fb && s_color_update) {
        dst = &fb[idx];
        src = pack_xrgb(r, g, b);
        if (!s_blend_enable || s_blend_mode == GX_BM_NONE) {
            *dst = src;
        } else {
            dc = *dst;
            dr = (dc >> 16) & 0xFF;
            dg = (dc >> 8) & 0xFF;
            db = dc & 0xFF;
            da = s_alpha_buffer ? s_alpha_buffer[idx] : 255;

            if (s_blend_mode == GX_BM_LOGIC) {
                *dst = gx_logic_apply(src, dc) & 0x00FFFFFFu;
            } else if (s_blend_mode == GX_BM_SUBTRACT) {
                *dst = pack_xrgb(gx_clamp_u8((float)dr - (float)r),
                                 gx_clamp_u8((float)dg - (float)g),
                                 gx_clamp_u8((float)db - (float)b));
            } else {
                *dst = pack_xrgb(gx_blend_channel(r, (u8)dr, a, (u8)da),
                                 gx_blend_channel(g, (u8)dg, a, (u8)da),
                                 gx_blend_channel(b, (u8)db, a, (u8)da));
            }
        }
    }
    if (s_alpha_update && s_alpha_buffer) {
        s_alpha_buffer[idx] = s_dst_alpha_enable ? s_dst_alpha : a;
    }
}

static int gx_depth_compare(float z, float current) {
    switch (s_z_func) {
        case GX_NEVER: return 0;
        case GX_LESS: return z < current;
        case GX_EQUAL: return fabsf(z - current) <= 0.000001f;
        case GX_LEQUAL: return z <= current;
        case GX_GREATER: return z > current;
        case GX_NEQUAL: return fabsf(z - current) > 0.000001f;
        case GX_GEQUAL: return z >= current;
        case GX_ALWAYS:
        default:
            return 1;
    }
}

static int gx_compare_u8(u8 value, GXCompare comp, u8 ref) {
    switch (comp) {
        case GX_NEVER: return 0;
        case GX_LESS: return value < ref;
        case GX_EQUAL: return value == ref;
        case GX_LEQUAL: return value <= ref;
        case GX_GREATER: return value > ref;
        case GX_NEQUAL: return value != ref;
        case GX_GEQUAL: return value >= ref;
        case GX_ALWAYS:
        default:
            return 1;
    }
}

static int gx_alpha_test(u8 alpha) {
    int a = gx_compare_u8(alpha, s_alpha_comp0, s_alpha_ref0);
    int b = gx_compare_u8(alpha, s_alpha_comp1, s_alpha_ref1);
    switch (s_alpha_op) {
        case GX_AOP_OR: return a || b;
        case GX_AOP_XOR: return !!a != !!b;
        case GX_AOP_XNOR: return !!a == !!b;
        case GX_AOP_AND:
        default:
            return a && b;
    }
}

static int gx_depth_test_at(int x, int y, float z) {
    u32 idx;
    if (!s_z_buffer || (!s_z_compare_enable && !s_z_update_enable)) {
        return 1;
    }
    if (x < 0 || y < 0 || x >= (int)s_screen_w || y >= (int)s_screen_h) {
        return 0;
    }
    if (!gx_in_scissor(x, y)) {
        return 0;
    }
    idx = (u32)y * s_screen_w + (u32)x;
    if (s_z_compare_enable && !gx_depth_compare(z, s_z_buffer[idx])) {
        return 0;
    }
    if (s_z_update_enable) {
        s_z_buffer[idx] = z;
    }
    return 1;
}

static void gx_plot_depth(int x, int y, float z, u8 r, u8 g, u8 b, u8 a) {
    if (gx_depth_test_at(x, y, z)) {
        gx_plot(x, y, r, g, b, a);
    }
}

static void gx_plot_square_depth(int x, int y, int radius, float z, u8 r, u8 g, u8 b, u8 a) {
    for (int py = y - radius; py <= y + radius; py++) {
        for (int px = x - radius; px <= x + radius; px++) {
            gx_plot_depth(px, py, z, r, g, b, a);
        }
    }
}

static void gx_apply_lighting(PS3GxVertex* v) {
    const PS3GxChanCtrl* ctrl = &s_chan_ctrl[0];
    GXColor base = { v->r, v->g, v->b, v->a };
    u8 out_alpha = (s_alpha_chan_ctrl[0].mat_src == GX_SRC_REG) ? s_chan_mat_color[0].a : v->a;
    float lr, lg, lb;
    float nx, ny, nz;

    if (ctrl->mat_src == GX_SRC_REG) {
        base = s_chan_mat_color[0];
    }
    if (!ctrl->enable || ctrl->light_mask == GX_LIGHT_NULL) {
        v->r = base.r;
        v->g = base.g;
        v->b = base.b;
        v->a = out_alpha;
        return;
    }

    lr = (float)((ctrl->amb_src == GX_SRC_REG) ? s_chan_amb_color[0].r : v->r) / 255.0f;
    lg = (float)((ctrl->amb_src == GX_SRC_REG) ? s_chan_amb_color[0].g : v->g) / 255.0f;
    lb = (float)((ctrl->amb_src == GX_SRC_REG) ? s_chan_amb_color[0].b : v->b) / 255.0f;

    nx = v->nx;
    ny = v->ny;
    nz = v->nz;
    gx_normalize3(&nx, &ny, &nz);

    for (int i = 0; i < PS3_GX_MAX_LIGHTS; i++) {
        float lx, ly, lz, dot;
        const PS3GxLightObj* light;
        if (!(ctrl->light_mask & (1u << i)) || !s_light_loaded[i]) {
            continue;
        }
        light = &s_lights[i];
        if (ctrl->attn_fn == GX_AF_NONE) {
            lx = -light->dir[0];
            ly = -light->dir[1];
            lz = -light->dir[2];
            if (fabsf(lx) + fabsf(ly) + fabsf(lz) < 0.000001f) {
                lx = light->pos[0];
                ly = light->pos[1];
                lz = light->pos[2];
            }
        } else {
            lx = light->pos[0] - v->x;
            ly = light->pos[1] - v->y;
            lz = light->pos[2] - v->z;
        }
        gx_normalize3(&lx, &ly, &lz);
        dot = nx * lx + ny * ly + nz * lz;
        if (ctrl->diff_fn == GX_DF_CLAMP && dot < 0.0f) {
            dot = 0.0f;
        } else if (ctrl->diff_fn == GX_DF_NONE) {
            dot = 1.0f;
        }
        if (dot > 0.0f) {
            lr += dot * ((float)light->color.r / 255.0f);
            lg += dot * ((float)light->color.g / 255.0f);
            lb += dot * ((float)light->color.b / 255.0f);
        }
    }

    v->r = gx_clamp_u8((float)base.r * gx_clamp01(lr));
    v->g = gx_clamp_u8((float)base.g * gx_clamp01(lg));
    v->b = gx_clamp_u8((float)base.b * gx_clamp01(lb));
    v->a = out_alpha;
}

static void gx_texgen_source(const PS3GxVertex* v, GXTexGenSrc src, const float raw_s[GX_MAX_TEXCOORD],
                             const float raw_t[GX_MAX_TEXCOORD], float* x, float* y, float* z) {
    if (src >= GX_TG_TEX0 && src <= GX_TG_TEX7) {
        int idx = (int)(src - GX_TG_TEX0);
        *x = raw_s[idx];
        *y = raw_t[idx];
        *z = 1.0f;
        return;
    }
    if (src >= GX_TG_TEXCOORD0 && src <= GX_TG_TEXCOORD6) {
        int idx = (int)(src - GX_TG_TEXCOORD0);
        *x = raw_s[idx];
        *y = raw_t[idx];
        *z = 1.0f;
        return;
    }
    switch (src) {
        case GX_TG_NRM:
        case GX_TG_BINRM:
        case GX_TG_TANGENT:
            *x = v->nx;
            *y = v->ny;
            *z = v->nz;
            break;
        case GX_TG_COLOR0:
        case GX_TG_COLOR1:
            *x = (float)v->r / 255.0f;
            *y = (float)v->g / 255.0f;
            *z = (float)v->b / 255.0f;
            break;
        case GX_TG_POS:
        default:
            *x = v->x;
            *y = v->y;
            *z = v->z;
            break;
    }
}

static void gx_generate_texcoords(PS3GxVertex* v) {
    float raw_s[GX_MAX_TEXCOORD];
    float raw_t[GX_MAX_TEXCOORD];
    int gens = s_num_tex_gens;

    for (int i = 0; i < GX_MAX_TEXCOORD; i++) {
        raw_s[i] = v->s[i];
        raw_t[i] = v->t[i];
    }
    if (gens > GX_MAX_TEXCOORD) {
        gens = GX_MAX_TEXCOORD;
    }

    for (int i = 0; i < gens; i++) {
        const PS3GxTexGen* gen = &s_tex_gens[i];
        float x, y, z;
        float ns, nt, nq;
        int midx;
        const f32* m;
        GXTexMtxType mtype;
        if (!gen->enabled) {
            continue;
        }
        gx_texgen_source(v, gen->src, raw_s, raw_t, &x, &y, &z);
        if (gen->normalize) {
            gx_normalize3(&x, &y, &z);
        }
        midx = gx_tex_mtx_index(gen->mtx);
        if (gen->mtx == GX_IDENTITY || midx < 0) {
            v->s[i] = x;
            v->t[i] = y;
            v->has_texcoord[i] = 1;
            continue;
        }
        m = s_tex_mtx[midx];
        mtype = s_tex_mtx_type[midx];
        ns = m[0] * x + m[1] * y + m[2] * z + m[3];
        nt = m[4] * x + m[5] * y + m[6] * z + m[7];
        if (gen->func == GX_TG_MTX3x4 || mtype == GX_MTX3x4) {
            nq = m[8] * x + m[9] * y + m[10] * z + m[11];
            if (fabsf(nq) > 0.000001f) {
                ns /= nq;
                nt /= nq;
            }
        }
        v->s[i] = ns;
        v->t[i] = nt;
        v->has_texcoord[i] = 1;
    }
}

static void gx_transform_vertex(PS3GxVertex* v) {
    const f32* m = s_pos_mtx[s_current_mtx % PS3_GX_MAX_MTX];
    f32 wx = m[0] * v->x + m[1] * v->y + m[2] * v->z + m[3];
    f32 wy = m[4] * v->x + m[5] * v->y + m[6] * v->z + m[7];
    f32 wz = m[8] * v->x + m[9] * v->y + m[10] * v->z + m[11];
    v->rw = 1.0f;
    gx_generate_texcoords(v);
    gx_apply_lighting(v);

    if (s_projection_valid) {
        f32 cx = s_projection[0] * wx + s_projection[1] * wy + s_projection[2] * wz + s_projection[3];
        f32 cy = s_projection[4] * wx + s_projection[5] * wy + s_projection[6] * wz + s_projection[7];
        f32 cz = s_projection[8] * wx + s_projection[9] * wy + s_projection[10] * wz + s_projection[11];
        f32 cw = s_projection[12] * wx + s_projection[13] * wy + s_projection[14] * wz + s_projection[15];
        if (fabsf(cw) > 0.00001f) {
            f32 invw = 1.0f / cw;
            cx *= invw;
            cy *= invw;
            cz *= invw;
            if (s_projection_type == GX_PERSPECTIVE) {
                v->rw = invw;
            }
        }
        v->sx = s_view_left + (cx + 1.0f) * 0.5f * s_view_w;
        v->sy = s_view_top + (1.0f - (cy + 1.0f) * 0.5f) * s_view_h;
        v->sz = s_view_nearz + ((cz + 1.0f) * 0.5f) * (s_view_farz - s_view_nearz);
    } else {
        v->sx = wx;
        v->sy = wy;
        v->sz = wz;
    }
}

static float gx_edge(const PS3GxVertex* a, const PS3GxVertex* b, float x, float y) {
    return (x - a->sx) * (b->sy - a->sy) - (y - a->sy) * (b->sx - a->sx);
}

static void gx_flush_vertices(void);

static void gx_draw_triangle(const PS3GxVertex* a, const PS3GxVertex* b, const PS3GxVertex* c) {
    float area = gx_edge(a, b, c->sx, c->sy);
    int min_x, max_x, min_y, max_y;
    if (fabsf(area) < 0.0001f) return;
    if (s_cull_mode == GX_CULL_ALL) return;
    if (s_cull_mode == GX_CULL_BACK && area < 0.0f) return;
    if (s_cull_mode == GX_CULL_FRONT && area > 0.0f) return;

    min_x = (int)floorf(fminf(a->sx, fminf(b->sx, c->sx)));
    max_x = (int)ceilf(fmaxf(a->sx, fmaxf(b->sx, c->sx)));
    min_y = (int)floorf(fminf(a->sy, fminf(b->sy, c->sy)));
    max_y = (int)ceilf(fmaxf(a->sy, fmaxf(b->sy, c->sy)));
    if (min_x < 0) min_x = 0;
    if (min_y < 0) min_y = 0;
    if (max_x >= (int)s_screen_w) max_x = (int)s_screen_w - 1;
    if (max_y >= (int)s_screen_h) max_y = (int)s_screen_h - 1;
    if (min_x < s_scissor_left) min_x = s_scissor_left;
    if (min_y < s_scissor_top) min_y = s_scissor_top;
    if (max_x > s_scissor_right) max_x = s_scissor_right;
    if (max_y > s_scissor_bottom) max_y = s_scissor_bottom;
    if (min_x > max_x || min_y > max_y) return;

    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            float px = (float)x + 0.5f;
            float py = (float)y + 0.5f;
            float w0 = gx_edge(b, c, px, py);
            float w1 = gx_edge(c, a, px, py);
            float w2 = gx_edge(a, b, px, py);
            int inside = area > 0.0f ? (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f)
                                      : (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
            if (inside) {
                float iw = 1.0f / area;
                float l0 = w0 * iw;
                float l1 = w1 * iw;
                float l2 = w2 * iw;
                float rw = a->rw * l0 + b->rw * l1 + c->rw * l2;
                float pa = l0;
                float pb = l1;
                float pc = l2;
                u8 r, g, bch, al;
                float ts[GX_MAX_TEXCOORD];
                float tt[GX_MAX_TEXCOORD];
                float z = a->sz * l0 + b->sz * l1 + c->sz * l2;

                if (fabsf(rw) > 0.000001f) {
                    pa = (a->rw * l0) / rw;
                    pb = (b->rw * l1) / rw;
                    pc = (c->rw * l2) / rw;
                }

                r = gx_clamp_u8(a->r * pa + b->r * pb + c->r * pc);
                g = gx_clamp_u8(a->g * pa + b->g * pb + c->g * pc);
                bch = gx_clamp_u8(a->b * pa + b->b * pb + c->b * pc);
                al = gx_clamp_u8(a->a * pa + b->a * pb + c->a * pc);
                for (int coord = 0; coord < GX_MAX_TEXCOORD; coord++) {
                    ts[coord] = a->s[coord] * pa + b->s[coord] * pb + c->s[coord] * pc;
                    tt[coord] = a->t[coord] * pa + b->t[coord] * pb + c->t[coord] * pc;
                }
                apply_tev(ts, tt, z, &r, &g, &bch, &al);
                if (!gx_alpha_test(al)) {
                    continue;
                }
                if (!gx_depth_test_at(x, y, z)) {
                    continue;
                }
                gx_plot(x, y, r, g, bch, al);
            }
        }
    }
}

static void gx_draw_line(const PS3GxVertex* a, const PS3GxVertex* b) {
    int x0 = (int)a->sx, y0 = (int)a->sy;
    int x1 = (int)b->sx, y1 = (int)b->sy;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int steps = abs(x1 - x0);
    int ysteps = abs(y1 - y0);
    int n = 0;
    int radius = (int)(s_line_width > 0 ? (s_line_width - 1) / 2 : 0);
    if (ysteps > steps) steps = ysteps;

    for (;;) {
        float f = steps > 0 ? (float)n / (float)steps : 0.0f;
        float z = a->sz + (b->sz - a->sz) * f;
        float ts[GX_MAX_TEXCOORD];
        float tt[GX_MAX_TEXCOORD];
        u8 r = (u8)(a->r + (b->r - a->r) * f);
        u8 g = (u8)(a->g + (b->g - a->g) * f);
        u8 bch = (u8)(a->b + (b->b - a->b) * f);
        u8 al = (u8)(a->a + (b->a - a->a) * f);
        for (int coord = 0; coord < GX_MAX_TEXCOORD; coord++) {
            ts[coord] = a->s[coord] + (b->s[coord] - a->s[coord]) * f;
            tt[coord] = a->t[coord] + (b->t[coord] - a->t[coord]) * f;
        }
        apply_tev(ts, tt, z, &r, &g, &bch, &al);
        if (gx_alpha_test(al)) {
            gx_plot_square_depth(x0, y0, radius, z, r, g, bch, al);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
        n++;
    }
}

static void gx_commit_pending_vertex(void) {
    if (s_vertex_pending && s_vertex_count < PS3_GX_MAX_VERTS) {
        gx_transform_vertex(&s_current_vertex);
        s_vertices[s_vertex_count++] = s_current_vertex;
        s_vertex_pending = 0;
        gx_reset_current_vertex();
        if (s_expected_verts != 0 && s_vertex_count >= s_expected_verts) {
            gx_flush_vertices();
        }
    }
}

static void gx_flush_vertices(void) {
    gx_commit_pending_vertex();
    if (!s_in_begin && s_vertex_count == 0) return;

    switch (s_primitive) {
        case GX_TRIANGLES:
            for (int i = 0; i + 2 < s_vertex_count; i += 3)
                gx_draw_triangle(&s_vertices[i], &s_vertices[i + 1], &s_vertices[i + 2]);
            break;
        case GX_QUADS:
            for (int i = 0; i + 3 < s_vertex_count; i += 4) {
                gx_draw_triangle(&s_vertices[i], &s_vertices[i + 1], &s_vertices[i + 2]);
                gx_draw_triangle(&s_vertices[i], &s_vertices[i + 2], &s_vertices[i + 3]);
            }
            break;
        case GX_TRIANGLESTRIP:
            for (int i = 0; i + 2 < s_vertex_count; i++)
                gx_draw_triangle(&s_vertices[i], &s_vertices[i + 1], &s_vertices[i + 2]);
            break;
        case GX_TRIANGLEFAN:
            for (int i = 1; i + 1 < s_vertex_count; i++)
                gx_draw_triangle(&s_vertices[0], &s_vertices[i], &s_vertices[i + 1]);
            break;
        case GX_LINES:
            for (int i = 0; i + 1 < s_vertex_count; i += 2)
                gx_draw_line(&s_vertices[i], &s_vertices[i + 1]);
            break;
        case GX_LINESTRIP:
            for (int i = 0; i + 1 < s_vertex_count; i++)
                gx_draw_line(&s_vertices[i], &s_vertices[i + 1]);
            break;
        case GX_POINTS:
            for (int i = 0; i < s_vertex_count; i++) {
                u8 r = s_vertices[i].r;
                u8 g = s_vertices[i].g;
                u8 b = s_vertices[i].b;
                u8 a = s_vertices[i].a;
                int radius = (int)(s_point_size > 0 ? (s_point_size - 1) / 2 : 0);
                apply_tev(s_vertices[i].s, s_vertices[i].t, s_vertices[i].sz, &r, &g, &b, &a);
                if (!gx_alpha_test(a)) {
                    continue;
                }
                gx_plot_square_depth((int)s_vertices[i].sx, (int)s_vertices[i].sy, radius, s_vertices[i].sz, r, g, b, a);
            }
            break;
        default:
            break;
    }

    s_vertex_count = 0;
    s_in_begin = 0;
    s_expected_verts = 0;
}

static int gx_alloc_depth_buffer(void) {
    u32 pixels = (u32)s_screen_w * (u32)s_screen_h;
    free(s_z_buffer);
    free(s_alpha_buffer);
    s_z_buffer = (float*)malloc(sizeof(float) * pixels);
    s_alpha_buffer = (u8*)malloc(pixels);
    if (!s_z_buffer || !s_alpha_buffer) {
        free(s_z_buffer);
        free(s_alpha_buffer);
        s_z_buffer = NULL;
        s_alpha_buffer = NULL;
        return 0;
    }
    return 1;
}

static void gx_dl_write(const void* data, u32 len) {
    if (!s_dl.active || s_dl.overflow) {
        return;
    }
    if (s_dl.off + len > s_dl.size) {
        s_dl.overflow = 1;
        return;
    }
    memcpy(s_dl.buf + s_dl.off, data, len);
    s_dl.off += len;
}

static GXColor gx_read_framebuffer_color(int x, int y) {
    GXColor out = { 0, 0, 0, 255 };
    u32* fb = gx_framebuffer();
    u32 idx;
    u32 px;
    if (!fb || x < 0 || y < 0 || x >= (int)s_screen_w || y >= (int)s_screen_h) {
        return out;
    }
    idx = (u32)y * s_screen_w + (u32)x;
    px = fb[idx];
    out.r = (u8)((px >> 16) & 0xFF);
    out.g = (u8)((px >> 8) & 0xFF);
    out.b = (u8)(px & 0xFF);
    if (s_alpha_buffer) {
        out.a = s_alpha_buffer[idx];
    }
    return out;
}

static u16 gx_pack_rgb565(GXColor c) {
    return (u16)(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

static u16 gx_pack_rgb5a3(GXColor c) {
    if (c.a >= 224) {
        return (u16)(0x8000 | ((c.r >> 3) << 10) | ((c.g >> 3) << 5) | (c.b >> 3));
    }
    return (u16)(((c.a >> 5) << 12) | ((c.r >> 4) << 8) | ((c.g >> 4) << 4) | (c.b >> 4));
}

static void gx_write_be16(u8* p, u16 value) {
    p[0] = (u8)(value >> 8);
    p[1] = (u8)value;
}

static void gx_write_copied_texel(u8* dest, u32 width, u32 x, u32 y, GXTexFmt fmt, GXColor c) {
    u32 offset;
    switch (fmt) {
        case GX_TF_RGB565:
            offset = gx_tiled_block_offset(x, y, width, 4, 4, 32) + (y & 3u) * 8u + (x & 3u) * 2u;
            gx_write_be16(dest + offset, gx_pack_rgb565(c));
            break;
        case GX_TF_RGB5A3:
            offset = gx_tiled_block_offset(x, y, width, 4, 4, 32) + (y & 3u) * 8u + (x & 3u) * 2u;
            gx_write_be16(dest + offset, gx_pack_rgb5a3(c));
            break;
        case GX_TF_RGBA8:
        case GX_CTF_YUVA8:
            offset = gx_tiled_block_offset(x, y, width, 4, 4, 64) + (y & 3u) * 8u + (x & 3u) * 2u;
            dest[offset] = c.a;
            dest[offset + 1u] = c.r;
            dest[offset + 32u] = c.g;
            dest[offset + 33u] = c.b;
            break;
        case GX_TF_I8:
        case GX_TF_A8:
        case GX_CTF_R8:
            offset = gx_tiled_block_offset(x, y, width, 8, 4, 32) + (y & 3u) * 8u + (x & 7u);
            dest[offset] = (u8)(((u32)c.r + c.g + c.b) / 3u);
            break;
        default:
            break;
    }
}

static void gx_copy_tex_execute(void* dest, GXBool clear) {
    u8* out = (u8*)dest;
    u32 dst_w = s_tex_copy_dst_w ? s_tex_copy_dst_w : s_screen_w;
    u32 dst_h = s_tex_copy_dst_h ? s_tex_copy_dst_h : s_screen_h;
    u32 src_w = s_tex_copy_src[2] ? s_tex_copy_src[2] : s_screen_w;
    u32 src_h = s_tex_copy_src[3] ? s_tex_copy_src[3] : s_screen_h;

    if (out) {
        for (u32 y = 0; y < dst_h; y++) {
            for (u32 x = 0; x < dst_w; x++) {
                int sx = (int)s_tex_copy_src[0] + (int)((u64)x * src_w / dst_w);
                int sy = (int)s_tex_copy_src[1] + (int)((u64)y * src_h / dst_h);
                GXColor c = gx_read_framebuffer_color(sx, sy);
                gx_write_copied_texel(out, dst_w, x, y, s_tex_copy_fmt, c);
            }
        }
    }
    if (clear) {
        gx_clear_framebuffer();
    }
}

#if defined(TARGET_PS3)
static void ps3_gx_wait_flip(void) {
    while (gcmGetFlipStatus() != 0) {
        usleep(200);
    }
    gcmResetFlipStatus();
}

static int ps3_gx_make_buffer(PS3GxBuffer* buffer, u16 width, u16 height, int id) {
    int pitch = (int)width * (int)sizeof(u32);
    int size = pitch * (int)height;
    memset(buffer, 0, sizeof(*buffer));
    buffer->ptr = (u32*)rsxMemalign(64, size);
    if (!buffer->ptr) return 0;
    if (rsxAddressToOffset(buffer->ptr, &buffer->offset) != 0) return 0;
    if (gcmSetDisplayBuffer(id, buffer->offset, pitch, width, height) != 0) return 0;
    buffer->width = width;
    buffer->height = height;
    buffer->id = id;
    return 1;
}

static void ps3_gx_set_render_target(PS3GxBuffer* buffer) {
    gcmSurface sf;
    memset(&sf, 0, sizeof(sf));
    sf.colorFormat = GCM_SURFACE_X8R8G8B8;
    sf.colorTarget = GCM_SURFACE_TARGET_0;
    sf.colorLocation[0] = GCM_LOCATION_RSX;
    sf.colorOffset[0] = buffer->offset;
    sf.colorPitch[0] = s_screen_w * sizeof(u32);
    sf.colorLocation[1] = GCM_LOCATION_RSX;
    sf.colorLocation[2] = GCM_LOCATION_RSX;
    sf.colorLocation[3] = GCM_LOCATION_RSX;
    sf.colorPitch[1] = 64;
    sf.colorPitch[2] = 64;
    sf.colorPitch[3] = 64;
    sf.depthFormat = GCM_SURFACE_ZETA_Z16;
    sf.depthLocation = GCM_LOCATION_RSX;
    sf.depthOffset = s_depth_offset;
    sf.depthPitch = s_depth_pitch;
    sf.type = GCM_TEXTURE_LINEAR;
    sf.antiAlias = GCM_SURFACE_CENTER_1;
    sf.width = buffer->width;
    sf.height = buffer->height;
    rsxSetSurface(s_rsx_context, &sf);
}

static void ps3_gx_wait_rsx_idle(void) {
    static u32 label = 1;
    rsxSetWriteBackendLabel(s_rsx_context, PS3_GX_LABEL_INDEX, label);
    rsxSetWaitLabel(s_rsx_context, PS3_GX_LABEL_INDEX, label);
    rsxFlushBuffer(s_rsx_context);
    while (*(vu32*)gcmGetLabelAddress(PS3_GX_LABEL_INDEX) != label) {
        usleep(30);
    }
    label++;
}
#endif

void ps3_gx_init(void) {
    if (s_gx_initialized) return;
    gx_set_defaults();

#if defined(TARGET_PS3)
    {
        videoState state;
        videoConfiguration vconfig;
        videoResolution res;

        s_host_addr = memalign(1024 * 1024, PS3_GX_HOST_SIZE);
        if (!s_host_addr) goto fail;
        if (rsxInit(&s_rsx_context, PS3_GX_CB_SIZE, PS3_GX_HOST_SIZE, s_host_addr) != 0 || !s_rsx_context)
            goto fail;
        if (videoGetState(0, 0, &state) != 0 || state.state != 0) goto fail;
        if (videoGetResolution(state.displayMode.resolution, &res) != 0) goto fail;

        s_screen_w = res.width;
        s_screen_h = res.height;
        s_view_w = (f32)s_screen_w;
        s_view_h = (f32)s_screen_h;
        s_scissor_left = 0;
        s_scissor_top = 0;
        s_scissor_right = (s32)s_screen_w - 1;
        s_scissor_bottom = (s32)s_screen_h - 1;
        s_tex_copy_src[2] = s_screen_w;
        s_tex_copy_src[3] = s_screen_h;
        s_tex_copy_dst_w = s_screen_w;
        s_tex_copy_dst_h = s_screen_h;
        s_disp_copy_src[2] = s_screen_w;
        s_disp_copy_src[3] = s_screen_h;
        s_disp_copy_dst_w = s_screen_w;
        s_disp_copy_dst_h = s_screen_h;

        memset(&vconfig, 0, sizeof(vconfig));
        vconfig.resolution = state.displayMode.resolution;
        vconfig.format = VIDEO_BUFFER_FORMAT_XRGB;
        vconfig.pitch = s_screen_w * sizeof(u32);
        vconfig.aspect = state.displayMode.aspect;

        ps3_gx_wait_rsx_idle();
        if (videoConfigure(0, &vconfig, NULL, 0) != 0) goto fail;
        gcmSetFlipMode(GCM_FLIP_VSYNC);

        if (!ps3_gx_make_buffer(&s_buffers[0], s_screen_w, s_screen_h, 0)) goto fail;
        if (!ps3_gx_make_buffer(&s_buffers[1], s_screen_w, s_screen_h, 1)) goto fail;
        if (!gx_alloc_depth_buffer()) goto fail;

        s_depth_pitch = s_screen_w * sizeof(u32);
        s_depth_buffer = (u32*)rsxMemalign(64, s_depth_pitch * s_screen_h * 2);
        if (!s_depth_buffer) goto fail;
        if (rsxAddressToOffset(s_depth_buffer, &s_depth_offset) != 0) goto fail;

        gcmResetFlipStatus();
        s_gx_initialized = 1;
        ps3_gx_set_render_target(&s_buffers[s_current_buffer]);
        gx_clear_framebuffer();
        return;
    }
fail:
    fprintf(stderr, "[PS3/GX] RSX initialization failed\n");
#else
    s_buffers[0].ptr = (u32*)calloc(PS3_SCREEN_WIDTH * PS3_SCREEN_HEIGHT, sizeof(u32));
    s_buffers[1].ptr = (u32*)calloc(PS3_SCREEN_WIDTH * PS3_SCREEN_HEIGHT, sizeof(u32));
    s_buffers[0].width = s_buffers[1].width = PS3_SCREEN_WIDTH;
    s_buffers[0].height = s_buffers[1].height = PS3_SCREEN_HEIGHT;
    s_gx_initialized = s_buffers[0].ptr && s_buffers[1].ptr;
    if (s_gx_initialized) {
        s_gx_initialized = gx_alloc_depth_buffer();
    }
    gx_clear_framebuffer();
#endif
}

void ps3_gx_shutdown(void) {
    if (!s_gx_initialized) return;
    gx_flush_vertices();
#if defined(TARGET_PS3)
    if (s_rsx_context) rsxFinish(s_rsx_context, 0);
    if (s_depth_buffer) rsxFree(s_depth_buffer);
    if (s_buffers[0].ptr) rsxFree(s_buffers[0].ptr);
    if (s_buffers[1].ptr) rsxFree(s_buffers[1].ptr);
    if (s_host_addr) free(s_host_addr);
#else
    free(s_buffers[0].ptr);
    free(s_buffers[1].ptr);
#endif
    free(s_z_buffer);
    free(s_alpha_buffer);
    s_z_buffer = NULL;
    s_alpha_buffer = NULL;
    memset(s_buffers, 0, sizeof(s_buffers));
    s_gx_initialized = 0;
}

void ps3_gx_begin_frame(void) {
    if (!s_gx_initialized) ps3_gx_init();
    gx_clear_framebuffer();
}

void ps3_gx_present(void) {
    if (!s_gx_initialized) return;
    gx_flush_vertices();
#if defined(TARGET_PS3)
    if (gcmSetFlip(s_rsx_context, s_current_buffer) == 0) {
        rsxFlushBuffer(s_rsx_context);
        gcmSetWaitFlip(s_rsx_context);
        ps3_gx_wait_flip();
    }
#endif
    s_current_buffer ^= 1;
#if defined(TARGET_PS3)
    ps3_gx_set_render_target(&s_buffers[s_current_buffer]);
#endif
    gx_clear_framebuffer();
}

GXFifoObj* GXInit(void* base, u32 size) {
    ps3_gx_init();
    GXInitFifoBase(&s_fifo, base, size);
    GXInitFifoPtrs(&s_fifo, NULL, NULL);
    GXSetCPUFifo(&s_fifo);
    GXSetGPFifo(&s_fifo);
    return &s_fifo;
}

void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts) {
    gx_flush_vertices();
    s_primitive = type;
    (void)vtxfmt;
    s_expected_verts = nverts;
    s_vertex_count = 0;
    s_in_begin = 1;
    s_vertex_pending = 0;
    gx_reset_current_vertex();
}

void GXEnd(void) { gx_flush_vertices(); }
void GXFlush(void) { gx_flush_vertices(); }
void GXDrawDone(void) { gx_flush_vertices(); if (s_draw_done_cb) s_draw_done_cb(); }
void GXWaitDrawDone(void) { gx_flush_vertices(); }
void GXSetDrawDone(void) { gx_flush_vertices(); }
void GXPixModeSync(void) {}
void GXTexModeSync(void) {}
void GXResetWriteGatherPipe(void) {}
void GXRestoreWriteGatherPipe(void) {}
void GXAbortFrame(void) { s_vertex_count = 0; s_in_begin = 0; s_vertex_pending = 0; }
BOOL IsWriteGatherBufferEmpty(void) { return TRUE; }

void GXPosition3f32(f32 x, f32 y, f32 z) {
    gx_commit_pending_vertex();
    s_current_vertex.x = x;
    s_current_vertex.y = y;
    s_current_vertex.z = z;
    s_vertex_pending = 1;
}
void GXPosition3u16(u16 x, u16 y, u16 z) { GXPosition3f32((f32)x, (f32)y, (f32)z); }
void GXPosition3s16(s16 x, s16 y, s16 z) { GXPosition3f32((f32)x, (f32)y, (f32)z); }
void GXPosition3u8(u8 x, u8 y, u8 z) { GXPosition3f32((f32)x, (f32)y, (f32)z); }
void GXPosition3s8(s8 x, s8 y, s8 z) { GXPosition3f32((f32)x, (f32)y, (f32)z); }
void GXPosition2f32(f32 x, f32 y) { GXPosition3f32(x, y, 0.0f); }
void GXPosition2u16(u16 x, u16 y) { GXPosition3f32((f32)x, (f32)y, 0.0f); }
void GXPosition2s16(s16 x, s16 y) { GXPosition3f32((f32)x, (f32)y, 0.0f); }
void GXPosition2u8(u8 x, u8 y) { GXPosition3f32((f32)x, (f32)y, 0.0f); }
void GXPosition2s8(s8 x, s8 y) { GXPosition3f32((f32)x, (f32)y, 0.0f); }
void GXPosition1x16(u16 index) {
    if (s_array_base[GX_VA_POS]) {
        const u8* base = (const u8*)s_array_base[GX_VA_POS];
        const f32* pos = (const f32*)(base + index * s_array_stride[GX_VA_POS]);
        GXPosition3f32(pos[0], pos[1], pos[2]);
    }
}
void GXPosition1x8(u8 index) { GXPosition1x16(index); }

void GXNormal3f32(f32 x, f32 y, f32 z) {
    s_current_vertex.nx = x;
    s_current_vertex.ny = y;
    s_current_vertex.nz = z;
}
void GXNormal3s16(s16 x, s16 y, s16 z) {
    GXNormal3f32((f32)x / 32767.0f, (f32)y / 32767.0f, (f32)z / 32767.0f);
}
void GXNormal3s8(s8 x, s8 y, s8 z) {
    GXNormal3f32((f32)x / 127.0f, (f32)y / 127.0f, (f32)z / 127.0f);
}
void GXNormal1x16(u16 index) {
    if (s_array_base[GX_VA_NRM]) {
        const u8* base = (const u8*)s_array_base[GX_VA_NRM];
        const f32* nrm = (const f32*)(base + index * s_array_stride[GX_VA_NRM]);
        GXNormal3f32(nrm[0], nrm[1], nrm[2]);
    }
}
void GXNormal1x8(u8 index) { GXNormal1x16(index); }

void GXColor4u8(u8 r, u8 g, u8 b, u8 a) {
    s_current_vertex.r = r;
    s_current_vertex.g = g;
    s_current_vertex.b = b;
    s_current_vertex.a = a;
}
void GXColor3u8(u8 r, u8 g, u8 b) { GXColor4u8(r, g, b, 255); }
void GXColor1u32(u32 clr) { GXColor4u8((clr >> 24) & 0xFF, (clr >> 16) & 0xFF, (clr >> 8) & 0xFF, clr & 0xFF); }
void GXColor1u16(u16 clr) { GXColor1u32((u32)clr << 16); }
void GXColor1x16(u16 index) {
    if (s_array_base[GX_VA_CLR0]) {
        const u8* base = (const u8*)s_array_base[GX_VA_CLR0];
        const u8* c = base + index * s_array_stride[GX_VA_CLR0];
        GXColor4u8(c[0], c[1], c[2], c[3]);
    }
}
void GXColor1x8(u8 index) { GXColor1x16(index); }
void GXColor4f32(float r, float g, float b, float a) {
    GXColor4u8((u8)(r * 255.0f), (u8)(g * 255.0f), (u8)(b * 255.0f), (u8)(a * 255.0f));
}

void GXTexCoord2f32(f32 s, f32 t) {
    int coord = gx_next_texcoord_slot();
    s_current_vertex.s[coord] = s;
    s_current_vertex.t[coord] = t;
    s_current_vertex.has_texcoord[coord] = 1;
}
void GXTexCoord2u16(u16 s, u16 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord2s16(s16 s, s16 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord2u8(u8 s, u8 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord2s8(s8 s, s8 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord1f32(f32 s, f32 t) { GXTexCoord2f32(s, t); }
void GXTexCoord1u16(u16 s, u16 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord1s16(s16 s, s16 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord1u8(u8 s, u8 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord1s8(s8 s, s8 t) { GXTexCoord2f32((f32)s, (f32)t); }
void GXTexCoord1x16(u16 index) {
    if (s_array_base[GX_VA_TEX0]) {
        const u8* base = (const u8*)s_array_base[GX_VA_TEX0];
        const f32* tc = (const f32*)(base + index * s_array_stride[GX_VA_TEX0]);
        GXTexCoord2f32(tc[0], tc[1]);
    }
}
void GXTexCoord1x8(u8 index) { GXTexCoord1x16(index); }

void GXSetProjection(const void* mtx, GXProjectionType type) {
    s_projection_type = type;
    if (mtx) {
        memcpy(s_projection, mtx, sizeof(s_projection));
        s_projection_valid = 1;
    }
}
void GXLoadPosMtxImm(const void* mtx, u32 id) {
    if (mtx && id < PS3_GX_MAX_MTX) memcpy(s_pos_mtx[id], mtx, sizeof(s_pos_mtx[id]));
}
void GXLoadNrmMtxImm(const void* mtx, u32 id) { (void)mtx; (void)id; }
void GXLoadTexMtxImm(const void* mtx, u32 id, GXTexMtxType type) {
    int idx = gx_tex_mtx_index(id);
    if (idx >= 0 && mtx) {
        memcpy(s_tex_mtx[idx], mtx, sizeof(s_tex_mtx[idx]));
        s_tex_mtx_type[idx] = type;
    }
}
void GXSetCurrentMtx(u32 id) { s_current_mtx = id % PS3_GX_MAX_MTX; }
void GXSetViewport(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
    s_view_left = left; s_view_top = top; s_view_w = wd; s_view_h = ht;
    s_view_nearz = nearz;
    s_view_farz = farz;
}
void GXSetViewportJitter(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz, u32 field) {
    (void)field; GXSetViewport(left, top, wd, ht, nearz, farz);
}
void GXSetScissor(u32 left, u32 top, u32 wd, u32 ht) {
    s32 right;
    s32 bottom;

    if (wd == 0 || ht == 0) {
        s_scissor_left = 1;
        s_scissor_top = 1;
        s_scissor_right = 0;
        s_scissor_bottom = 0;
        return;
    }

    right = (s32)(left + wd - 1u);
    bottom = (s32)(top + ht - 1u);
    s_scissor_left = (s32)left;
    s_scissor_top = (s32)top;
    s_scissor_right = right;
    s_scissor_bottom = bottom;

    if (s_scissor_left < 0) s_scissor_left = 0;
    if (s_scissor_top < 0) s_scissor_top = 0;
    if (s_scissor_right >= (s32)s_screen_w) s_scissor_right = (s32)s_screen_w - 1;
    if (s_scissor_bottom >= (s32)s_screen_h) s_scissor_bottom = (s32)s_screen_h - 1;
}
void GXSetScissorBoxOffset(s32 x_off, s32 y_off) { (void)x_off; (void)y_off; }
void GXSetClipMode(GXClipMode mode) { (void)mode; }

void GXSetVtxDesc(GXAttr attr, GXAttrType type) { (void)attr; (void)type; }
void GXSetVtxDescv(const GXVtxDescList* list) { (void)list; }
void GXClearVtxDesc(void) {}
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac) {
    (void)vtxfmt; (void)attr; (void)cnt; (void)type; (void)frac;
}
void GXSetArray(GXAttr attr, const void* data, u8 stride) {
    if (attr < GX_VA_MAX_ATTR) {
        s_array_base[attr] = data;
        s_array_stride[attr] = stride;
    }
}
void GXInvalidateVtxCache(void) {}
void GXSetNumTexGens(u8 nTexGens) {
    s_num_tex_gens = nTexGens;
    if (s_num_tex_gens > GX_MAX_TEXCOORD) {
        s_num_tex_gens = GX_MAX_TEXCOORD;
    }
}
void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx,
                       GXBool normalize, u32 postmtx) {
    if (dst_coord < GX_MAX_TEXCOORD) {
        s_tex_gens[dst_coord].func = func;
        s_tex_gens[dst_coord].src = src_param;
        s_tex_gens[dst_coord].mtx = mtx;
        s_tex_gens[dst_coord].postmtx = postmtx;
        s_tex_gens[dst_coord].normalize = normalize;
        s_tex_gens[dst_coord].enabled = GX_TRUE;
    }
}
void GXSetLineWidth(u8 width, GXTexOffset texOffsets) {
    (void)texOffsets;
    s_line_width = (u8)((width + 5u) / 6u);
    if (s_line_width == 0) s_line_width = 1;
}
void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets) {
    (void)texOffsets;
    s_point_size = (u8)((pointSize + 5u) / 6u);
    if (s_point_size == 0) s_point_size = 1;
}
void GXEnableTexOffsets(GXTexCoordID coord, GXBool line_enable, GXBool point_enable) {
    (void)coord; (void)line_enable; (void)point_enable;
}

void GXSetCopyClear(GXColor clear_clr, u32 clear_z) {
    s_clear_color = clear_clr;
    s_clear_z = (float)(clear_z & 0x00FFFFFFu) / 16777215.0f;
}
void GXCopyDisp(void* dest, GXBool clear) {
    u32* out = (u32*)dest;
    u32 dst_w = s_disp_copy_dst_w ? s_disp_copy_dst_w : s_screen_w;
    u32 dst_h = s_disp_copy_dst_h ? s_disp_copy_dst_h : s_screen_h;
    u32 src_w = s_disp_copy_src[2] ? s_disp_copy_src[2] : s_screen_w;
    u32 src_h = s_disp_copy_src[3] ? s_disp_copy_src[3] : s_screen_h;

    if (out) {
        for (u32 y = 0; y < dst_h; y++) {
            for (u32 x = 0; x < dst_w; x++) {
                int sx = (int)s_disp_copy_src[0] + (int)((u64)x * src_w / dst_w);
                int sy = (int)s_disp_copy_src[1] + (int)((u64)y * src_h / dst_h);
                GXColor c = gx_read_framebuffer_color(sx, sy);
                out[y * dst_w + x] = pack_xrgb(c.r, c.g, c.b);
            }
        }
    }
    if (clear) gx_clear_framebuffer();
}
void GXSetDispCopyGamma(GXGamma gamma) { (void)gamma; }
void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    s_disp_copy_src[0] = left;
    s_disp_copy_src[1] = top;
    s_disp_copy_src[2] = wd;
    s_disp_copy_src[3] = ht;
}
void GXSetDispCopyDst(u16 wd, u16 ht) {
    s_disp_copy_dst_w = wd;
    s_disp_copy_dst_h = ht;
}
f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) {
    return (efbHeight && xfbHeight) ? ((f32)xfbHeight / (f32)efbHeight) : 1.0f;
}
u32 GXSetDispCopyYScale(f32 vscale) { return (u32)(s_screen_h * vscale); }
u16 GXGetNumXfbLines(u16 efbHeight, f32 yScale) { return (u16)((f32)efbHeight * yScale); }
void GXSetCopyFilter(GXBool aa, const u8 sample_pattern[12][2], GXBool vf, const u8 vfilter[7]) {
    if (s_dl.active) {
        u32 op = PS3GX_DL_OP_COPY_FILTER;
        gx_dl_write(&op, sizeof(op));
        return;
    }
    (void)aa; (void)sample_pattern; (void)vf; (void)vfilter;
}
void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout, u16 hor, u16 ver) {
    if (rmin && rmout) { *rmout = *rmin; rmout->viWidth -= hor * 2; rmout->viHeight -= ver * 2; }
}
void GXSetPixelFmt(GXPixelFmt pix_fmt, GXZFmt16 z_fmt) { (void)pix_fmt; (void)z_fmt; }
void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
    if (s_dl.active) {
        u32 pkt[5] = { PS3GX_DL_OP_TEXCOPY_SRC, left, top, wd, ht };
        gx_dl_write(pkt, sizeof(pkt));
        return;
    }
    s_tex_copy_src[0] = left;
    s_tex_copy_src[1] = top;
    s_tex_copy_src[2] = wd;
    s_tex_copy_src[3] = ht;
}
void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {
    if (s_dl.active) {
        u32 pkt[5] = { PS3GX_DL_OP_TEXCOPY_DST, wd, ht, (u32)fmt, mipmap ? 1u : 0u };
        gx_dl_write(pkt, sizeof(pkt));
        return;
    }
    s_tex_copy_dst_w = wd;
    s_tex_copy_dst_h = ht;
    s_tex_copy_fmt = fmt;
    s_tex_copy_mipmap = mipmap;
}
void GXCopyTex(void* dest, GXBool clear) {
    if (s_dl.active) {
        u32 op = PS3GX_DL_OP_COPY_TEX;
        u64 dest64 = (u64)(uintptr_t)dest;
        u32 clear_u32 = clear ? 1u : 0u;
        gx_dl_write(&op, sizeof(op));
        gx_dl_write(&dest64, sizeof(dest64));
        gx_dl_write(&clear_u32, sizeof(clear_u32));
        return;
    }
    gx_copy_tex_execute(dest, clear);
}
void GXSetCopyClamp(GXFBClamp clamp) { (void)clamp; }

static PS3GxTexObj* tex_obj(GXTexObj* obj) { return (PS3GxTexObj*)obj; }
static const PS3GxTexObj* ctex_obj(const GXTexObj* obj) { return (const PS3GxTexObj*)obj; }
static PS3GxTlutObj* tlut_obj(GXTlutObj* obj) { return (PS3GxTlutObj*)obj; }
static const PS3GxTlutObj* ctlut_obj(const GXTlutObj* obj) { return (const PS3GxTlutObj*)obj; }
void GXInitTexObj(GXTexObj* obj, void* image_ptr, u16 width, u16 height, GXTexFmt format,
                  GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, u8 mipmap) {
    PS3GxTexObj* t = tex_obj(obj);
    memset(t, 0, sizeof(*t));
    t->image = image_ptr; t->width = width; t->height = height; t->format = format;
    t->wrap_s = wrap_s; t->wrap_t = wrap_t; t->mipmap = mipmap;
}
void GXInitTexObjCI(GXTexObj* obj, void* image_ptr, u16 width, u16 height, GXCITexFmt format,
                    GXTexWrapMode wrap_s, GXTexWrapMode wrap_t, u8 mipmap, u32 tlut_name) {
    GXInitTexObj(obj, image_ptr, width, height, (GXTexFmt)format, wrap_s, wrap_t, mipmap);
    tex_obj(obj)->tlut = tlut_name;
}
void GXInitTexObjData(GXTexObj* obj, void* image_ptr) { tex_obj(obj)->image = image_ptr; }
void GXInitTexObjLOD(GXTexObj* obj, GXTexFilter min_filt, GXTexFilter mag_filt, f32 min_lod,
                     f32 max_lod, f32 lod_bias, GXBool bias_clamp, GXBool do_edge_lod,
                     GXAnisotropy max_aniso) {
    (void)obj; (void)min_filt; (void)mag_filt; (void)min_lod; (void)max_lod; (void)lod_bias;
    (void)bias_clamp; (void)do_edge_lod; (void)max_aniso;
}
void GXLoadTexObj(GXTexObj* obj, GXTexMapID id) {
    if (obj && id < GX_MAX_TEXMAP) {
        s_texmaps[id] = *ctex_obj(obj);
        if (s_active_texmap == GX_TEXMAP_NULL) {
            s_active_texmap = id;
        }
    }
}
u32 GXGetTexBufferSize(u16 width, u16 height, u32 format, GXBool mipmap, u8 max_lod) {
    u32 size;
    (void)mipmap;
    (void)max_lod;
    switch (format) {
        case GX_TF_I4:
        case GX_TF_C4:
            size = gx_ceil_div_u32(width, 8) * gx_ceil_div_u32(height, 8) * 32u;
            break;
        case GX_TF_I8:
        case GX_TF_IA4:
        case GX_TF_A8:
        case GX_TF_C8:
        case GX_CTF_R8:
            size = gx_ceil_div_u32(width, 8) * gx_ceil_div_u32(height, 4) * 32u;
            break;
        case GX_TF_IA8:
        case GX_TF_RGB565:
        case GX_TF_RGB5A3:
        case GX_TF_C14X2:
            size = gx_ceil_div_u32(width, 4) * gx_ceil_div_u32(height, 4) * 32u;
            break;
        case GX_TF_RGBA8:
        case GX_CTF_YUVA8:
            size = gx_ceil_div_u32(width, 4) * gx_ceil_div_u32(height, 4) * 64u;
            break;
        case GX_TF_CMPR:
            size = gx_ceil_div_u32(width, 8) * gx_ceil_div_u32(height, 8) * 32u;
            break;
        default:
            size = (u32)width * (u32)height * 4u;
            break;
    }
    return size;
}
void GXInvalidateTexAll(void) {}
void GXInvalidateTexRegion(GXTexRegion* region) { (void)region; }
void GXInitTexObjWrapMode(GXTexObj* obj, GXTexWrapMode s, GXTexWrapMode t) { tex_obj(obj)->wrap_s = s; tex_obj(obj)->wrap_t = t; }
void GXDestroyTexObj(GXTexObj* obj) { if (obj) memset(obj, 0, sizeof(*obj)); }
void GXDestroyTlutObj(GXTlutObj* obj) { if (obj) memset(obj, 0, sizeof(*obj)); }
void GXInitTlutObj(GXTlutObj* obj, void* lut, GXTlutFmt fmt, u16 n_entries) {
    PS3GxTlutObj* t;
    if (!obj) return;
    t = tlut_obj(obj);
    memset(t, 0, sizeof(*t));
    t->lut = (uintptr_t)lut;
    t->format = fmt;
    t->n_entries = n_entries;
}
void GXLoadTlut(GXTlutObj* obj, u32 idx) {
    if (obj && idx < PS3_GX_MAX_TLUTS) {
        s_tluts[idx] = *ctlut_obj(obj);
    }
}
void GXInitTexCacheRegion(GXTexRegion* region, GXBool is_32b_mipmap, u32 tmem_even, GXTexCacheSize size_even,
                          u32 tmem_odd, GXTexCacheSize size_odd) {
    (void)region; (void)is_32b_mipmap; (void)tmem_even; (void)size_even; (void)tmem_odd; (void)size_odd;
}
void GXInitTlutRegion(GXTlutRegion* region, u32 tmem_addr, GXTlutSize tlut_size) { (void)region; (void)tmem_addr; (void)tlut_size; }
GXTexRegionCallback GXSetTexRegionCallback(GXTexRegionCallback callback) { (void)callback; return NULL; }
void GXSetTexCoordScaleManually(GXTexCoordID coord, GXBool enable, u16 ss, u16 ts) { (void)coord; (void)enable; (void)ss; (void)ts; }
void GXSetTexCoordBias(GXTexCoordID coord, u8 s_enable, u8 t_enable) { (void)coord; (void)s_enable; (void)t_enable; }
GXBool GXGetTexObjMipMap(const GXTexObj* obj) { return ctex_obj(obj)->mipmap; }
GXTexFmt GXGetTexObjFmt(const GXTexObj* obj) { return (GXTexFmt)ctex_obj(obj)->format; }
u16 GXGetTexObjHeight(const GXTexObj* obj) { return ctex_obj(obj)->height; }
u16 GXGetTexObjWidth(const GXTexObj* obj) { return ctex_obj(obj)->width; }
GXTexWrapMode GXGetTexObjWrapS(const GXTexObj* obj) { return (GXTexWrapMode)ctex_obj(obj)->wrap_s; }
GXTexWrapMode GXGetTexObjWrapT(const GXTexObj* obj) { return (GXTexWrapMode)ctex_obj(obj)->wrap_t; }
void* GXGetTexObjData(const GXTexObj* obj) { return ctex_obj(obj)->image; }

void GXSetBlendMode(GXBlendMode type, GXBlendFactor src_fact, GXBlendFactor dst_fact, GXLogicOp op) {
    s_blend_mode = type;
    s_blend_src = src_fact;
    s_blend_dst = dst_fact;
    s_logic_op = op;
    s_blend_enable = (type != GX_BM_NONE) ? GX_TRUE : GX_FALSE;
}
void GXSetColorUpdate(GXBool update_enable) { s_color_update = update_enable; }
void GXSetAlphaUpdate(GXBool update_enable) { s_alpha_update = update_enable; }
void GXSetZMode(GXBool compare_enable, GXCompare func, GXBool update_enable) {
    s_z_compare_enable = compare_enable;
    s_z_func = func;
    s_z_update_enable = update_enable;
}
void GXSetZCompLoc(GXBool before_tex) { (void)before_tex; }
void GXSetDither(GXBool dither) { (void)dither; }
void GXSetDstAlpha(GXBool enable, u8 alpha) {
    s_dst_alpha_enable = enable;
    s_dst_alpha = alpha;
}
void GXSetFieldMask(GXBool odd_mask, GXBool even_mask) { (void)odd_mask; (void)even_mask; }
void GXSetFieldMode(GXBool field_mode, GXBool half_aspect_ratio) { (void)field_mode; (void)half_aspect_ratio; }
void GXSetFog(GXFogType type, f32 startz, f32 endz, f32 nearz, f32 farz, GXColor color) {
    (void)nearz;
    (void)farz;
    s_fog_type = type;
    s_fog_start = startz;
    s_fog_end = endz;
    s_fog_color = color;
}
void GXInitFogAdjTable(GXFogAdjTable* table, u16 width, f32 proj[4][4]) { (void)table; (void)width; (void)proj; }
void GXSetFogRangeAdj(GXBool enable, u16 center, GXFogAdjTable* table) { (void)enable; (void)center; (void)table; }

void GXSetNumTevStages(u8 nStages) {
    s_num_tev_stages = nStages ? nStages : 1;
    if (s_num_tev_stages > GX_MAX_TEVSTAGE) {
        s_num_tev_stages = GX_MAX_TEVSTAGE;
    }
}
void GXSetTevOp(GXTevStageID id, GXTevMode mode) {
    if (id < GX_MAX_TEVSTAGE) {
        switch (mode) {
            case GX_MODULATE:
                GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
                GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
                break;
            case GX_DECAL:
                GXSetTevColorIn(id, GX_CC_RASC, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
                GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
                break;
            case GX_BLEND:
                GXSetTevColorIn(id, GX_CC_ONE, GX_CC_RASC, GX_CC_TEXC, GX_CC_ZERO);
                GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
                break;
            case GX_REPLACE:
                GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
                GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
                break;
            case GX_PASSCLR:
                GXSetTevColorIn(id, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
                GXSetTevAlphaIn(id, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
                break;
            default:
                return;
        }
        GXSetTevColorOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GXSetTevAlphaOp(id, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    }
}
void GXSetTevColorIn(GXTevStageID stage, GXTevColorArg a, GXTevColorArg b, GXTevColorArg c, GXTevColorArg d) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].color_in[0] = a;
        s_tev_stages[stage].color_in[1] = b;
        s_tev_stages[stage].color_in[2] = c;
        s_tev_stages[stage].color_in[3] = d;
    }
}
void GXSetTevAlphaIn(GXTevStageID stage, GXTevAlphaArg a, GXTevAlphaArg b, GXTevAlphaArg c, GXTevAlphaArg d) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].alpha_in[0] = a;
        s_tev_stages[stage].alpha_in[1] = b;
        s_tev_stages[stage].alpha_in[2] = c;
        s_tev_stages[stage].alpha_in[3] = d;
    }
}
void GXSetTevColorOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].color_op = op;
        s_tev_stages[stage].color_bias = bias;
        s_tev_stages[stage].color_scale = scale;
        s_tev_stages[stage].color_clamp = clamp;
        s_tev_stages[stage].color_out = out_reg;
    }
}
void GXSetTevAlphaOp(GXTevStageID stage, GXTevOp op, GXTevBias bias, GXTevScale scale, GXBool clamp, GXTevRegID out_reg) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].alpha_op = op;
        s_tev_stages[stage].alpha_bias = bias;
        s_tev_stages[stage].alpha_scale = scale;
        s_tev_stages[stage].alpha_clamp = clamp;
        s_tev_stages[stage].alpha_out = out_reg;
    }
}
void GXSetTevColor(GXTevRegID id, GXColor color) {
    if (id < GX_MAX_TEVREG) {
        s_tev_regs[id] = color;
    }
}
void GXSetTevColorS10(GXTevRegID id, GXColorS10 color) {
    if (id < GX_MAX_TEVREG) {
        s_tev_regs[id].r = gx_clamp_u8((float)color.r);
        s_tev_regs[id].g = gx_clamp_u8((float)color.g);
        s_tev_regs[id].b = gx_clamp_u8((float)color.b);
        s_tev_regs[id].a = gx_clamp_u8((float)color.a);
    }
}
void GXSetTevKColor(GXTevKColorID id, GXColor color) {
    if (id < GX_MAX_KCOLOR) {
        s_k_colors[id] = color;
    }
}
void GXSetTevKColorSel(GXTevStageID stage, GXTevKColorSel sel) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].k_color_sel = sel;
    }
}
void GXSetTevKAlphaSel(GXTevStageID stage, GXTevKAlphaSel sel) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].k_alpha_sel = sel;
    }
}
void GXSetTevSwapMode(GXTevStageID stage, GXTevSwapSel ras_sel, GXTevSwapSel tex_sel) {
    if (stage < GX_MAX_TEVSTAGE) {
        s_tev_stages[stage].ras_swap = (ras_sel < GX_MAX_TEVSWAP) ? ras_sel : GX_TEV_SWAP0;
        s_tev_stages[stage].tex_swap = (tex_sel < GX_MAX_TEVSWAP) ? tex_sel : GX_TEV_SWAP0;
    }
}
void GXSetTevSwapModeTable(GXTevSwapSel table, GXTevColorChan red, GXTevColorChan green, GXTevColorChan blue, GXTevColorChan alpha) {
    if (table < GX_MAX_TEVSWAP) {
        s_tev_swap_table[table][0] = (u8)red;
        s_tev_swap_table[table][1] = (u8)green;
        s_tev_swap_table[table][2] = (u8)blue;
        s_tev_swap_table[table][3] = (u8)alpha;
    }
}
void GXSetAlphaCompare(GXCompare comp0, u8 ref0, GXAlphaOp op, GXCompare comp1, u8 ref1) {
    s_alpha_comp0 = comp0;
    s_alpha_ref0 = ref0;
    s_alpha_op = op;
    s_alpha_comp1 = comp1;
    s_alpha_ref1 = ref1;
}
void GXSetZTexture(GXZTexOp op, GXTexFmt fmt, u32 bias) { (void)op; (void)fmt; (void)bias; }
void GXSetTevOrder(GXTevStageID stage, GXTexCoordID coord, GXTexMapID map, GXChannelID color) {
    (void)stage;
    (void)coord;
    (void)color;
    if (map < GX_MAX_TEXMAP) {
        s_active_texmap = map;
        if (stage < GX_MAX_TEVSTAGE) {
            s_tev_stages[stage].tex_map = map;
            s_tev_stages[stage].tex_coord = coord;
            s_tev_stages[stage].color_chan = color;
        }
    } else if (map == GX_TEXMAP_NULL || map == GX_TEX_DISABLE) {
        s_active_texmap = GX_TEXMAP_NULL;
        if (stage < GX_MAX_TEVSTAGE) {
            s_tev_stages[stage].tex_map = map;
            s_tev_stages[stage].tex_coord = coord;
            s_tev_stages[stage].color_chan = color;
        }
    }
}

void GXSetTevDirect(GXTevStageID tev_stage) { (void)tev_stage; }
void GXSetNumIndStages(u8 nIndStages) { (void)nIndStages; }
void GXSetIndTexMtx(GXIndTexMtxID mtx_sel, const void* offset, s8 scale_exp) { (void)mtx_sel; (void)offset; (void)scale_exp; }
void GXSetIndTexOrder(GXIndTexStageID ind_stage, GXTexCoordID tex_coord, GXTexMapID tex_map) { (void)ind_stage; (void)tex_coord; (void)tex_map; }
void GXSetTevIndirect(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXIndTexFormat format,
                      GXIndTexBiasSel bias_sel, GXIndTexMtxID matrix_sel, GXIndTexWrap wrap_s,
                      GXIndTexWrap wrap_t, GXBool add_prev, GXBool ind_lod, GXIndTexAlphaSel alpha_sel) {
    (void)tev_stage; (void)ind_stage; (void)format; (void)bias_sel; (void)matrix_sel;
    (void)wrap_s; (void)wrap_t; (void)add_prev; (void)ind_lod; (void)alpha_sel;
}
void GXSetTevIndWarp(GXTevStageID tev_stage, GXIndTexStageID ind_stage, GXBool signed_offsets, GXBool replace_mode, GXIndTexMtxID matrix_sel) {
    (void)tev_stage; (void)ind_stage; (void)signed_offsets; (void)replace_mode; (void)matrix_sel;
}
void GXSetIndTexCoordScale(GXIndTexStageID ind_state, GXIndTexScale scale_s, GXIndTexScale scale_t) { (void)ind_state; (void)scale_s; (void)scale_t; }
void __GXSetIndirectMask(u32 mask) { (void)mask; }

void GXSetNumChans(u8 nChans) {
    s_num_chans = nChans > 2 ? 2 : nChans;
}
void GXSetChanCtrl(GXChannelID chan, GXBool enable, GXColorSrc amb_src, GXColorSrc mat_src, u32 light_mask, GXDiffuseFn diff_fn, GXAttnFn attn_fn) {
    int idx = gx_chan_index(chan);
    PS3GxChanCtrl next;
    next.enable = enable;
    next.amb_src = amb_src;
    next.mat_src = mat_src;
    next.light_mask = light_mask;
    next.diff_fn = diff_fn;
    next.attn_fn = attn_fn;

    if (chan == GX_ALPHA0 || chan == GX_ALPHA1) {
        s_alpha_chan_ctrl[idx] = next;
    } else if (chan == GX_COLOR0A0 || chan == GX_COLOR1A1) {
        s_chan_ctrl[idx] = next;
        s_alpha_chan_ctrl[idx] = next;
    } else {
        s_chan_ctrl[idx] = next;
    }
}
void GXSetChanAmbColor(GXChannelID chan, GXColor amb_color) {
    s_chan_amb_color[gx_chan_index(chan)] = amb_color;
}
void GXSetChanMatColor(GXChannelID chan, GXColor mat_color) {
    s_chan_mat_color[gx_chan_index(chan)] = mat_color;
}
static PS3GxLightObj* gx_light_obj(GXLightObj* lt_obj) { return (PS3GxLightObj*)lt_obj; }
static const PS3GxLightObj* gx_clight_obj(const GXLightObj* lt_obj) { return (const PS3GxLightObj*)lt_obj; }
void GXInitLightSpot(GXLightObj* lt_obj, f32 cutoff, GXSpotFn spot_func) { (void)lt_obj; (void)cutoff; (void)spot_func; }
void GXInitLightDistAttn(GXLightObj* lt_obj, f32 ref_distance, f32 ref_brightness, GXDistAttnFn dist_func) { (void)lt_obj; (void)ref_distance; (void)ref_brightness; (void)dist_func; }
void GXInitLightPos(GXLightObj* lt_obj, f32 x, f32 y, f32 z) {
    if (lt_obj) {
        PS3GxLightObj* l = gx_light_obj(lt_obj);
        l->pos[0] = x;
        l->pos[1] = y;
        l->pos[2] = z;
    }
}
void GXInitLightDir(GXLightObj* lt_obj, f32 nx, f32 ny, f32 nz) {
    if (lt_obj) {
        PS3GxLightObj* l = gx_light_obj(lt_obj);
        l->dir[0] = nx;
        l->dir[1] = ny;
        l->dir[2] = nz;
    }
}
void GXInitLightColor(GXLightObj* lt_obj, GXColor color) {
    if (lt_obj) {
        gx_light_obj(lt_obj)->color = color;
    }
}
void GXInitLightAttn(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2, f32 k0, f32 k1, f32 k2) {
    if (lt_obj) {
        PS3GxLightObj* l = gx_light_obj(lt_obj);
        l->attn_a[0] = a0;
        l->attn_a[1] = a1;
        l->attn_a[2] = a2;
        l->attn_k[0] = k0;
        l->attn_k[1] = k1;
        l->attn_k[2] = k2;
    }
}
void GXInitLightAttnA(GXLightObj* lt_obj, f32 a0, f32 a1, f32 a2) {
    if (lt_obj) {
        PS3GxLightObj* l = gx_light_obj(lt_obj);
        l->attn_a[0] = a0;
        l->attn_a[1] = a1;
        l->attn_a[2] = a2;
    }
}
void GXInitLightAttnK(GXLightObj* lt_obj, f32 k0, f32 k1, f32 k2) {
    if (lt_obj) {
        PS3GxLightObj* l = gx_light_obj(lt_obj);
        l->attn_k[0] = k0;
        l->attn_k[1] = k1;
        l->attn_k[2] = k2;
    }
}
void GXLoadLightObjImm(GXLightObj* lt_obj, GXLightID light) {
    if (!lt_obj) {
        return;
    }
    for (int i = 0; i < PS3_GX_MAX_LIGHTS; i++) {
        if (((u32)light) & (1u << i)) {
            s_lights[i] = *gx_clight_obj(lt_obj);
            s_light_loaded[i] = 1;
            break;
        }
    }
}
void GXGetLightPos(GXLightObj* lt_obj, f32* x, f32* y, f32* z) {
    const PS3GxLightObj* l = gx_clight_obj(lt_obj);
    if (x) *x = l ? l->pos[0] : 0.0f;
    if (y) *y = l ? l->pos[1] : 0.0f;
    if (z) *z = l ? l->pos[2] : 0.0f;
}
void GXGetLightColor(GXLightObj* lt_obj, GXColor* color) {
    if (!color) return;
    if (lt_obj) *color = gx_clight_obj(lt_obj)->color;
    else memset(color, 0, sizeof(*color));
}

void GXSetMisc(GXMiscToken token, u32 val) { (void)token; (void)val; }
void GXSetDrawSync(u16 token) { s_draw_token = token; if (s_draw_sync_cb) s_draw_sync_cb(token); }
u16 GXReadDrawSync(void) { return s_draw_token; }
GXDrawSyncCallback GXSetDrawSyncCallback(GXDrawSyncCallback cb) { GXDrawSyncCallback old = s_draw_sync_cb; s_draw_sync_cb = cb; return old; }
GXDrawDoneCallback GXSetDrawDoneCallback(GXDrawDoneCallback cb) { GXDrawDoneCallback old = s_draw_done_cb; s_draw_done_cb = cb; return old; }

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
    GXFifoObj* obj = fifo ? fifo : &s_fifo;
    PS3GxFifoObj* f = gx_fifo_cast(obj);
    u8* safe_base = (u8*)base;
    u32 safe_size = size;

    if (!safe_base || safe_size < 0x4000) {
        safe_base = s_fifo_buffer;
        safe_size = (u32)sizeof(s_fifo_buffer);
        if (!s_fifo_fallback_logged) {
            OSReport("[PS3/GX] GXInitFifoBase: using fallback FIFO base=%p size=%u\n", safe_base, safe_size);
            s_fifo_fallback_logged = 1;
        }
    }

    memset(obj, 0, sizeof(*obj));
    f->base = safe_base;
    f->top = safe_base + safe_size;
    f->size = safe_size;
    f->loWatermark = 0;
    f->hiWatermark = safe_size - 32;
    f->rdPtr = safe_base;
    f->wrPtr = safe_base;
    f->count = 0;
    s_fifo_ready = 1;
}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {
    GXFifoObj* obj = gx_fifo_ensure(fifo, "GXInitFifoPtrs");
    PS3GxFifoObj* f = gx_fifo_cast(obj);
    f->rdPtr = gx_fifo_ptr_in_range(f, readPtr) ? readPtr : f->base;
    f->wrPtr = gx_fifo_ptr_in_range(f, writePtr) ? writePtr : f->rdPtr;
    f->count = (s32)((u8*)f->wrPtr - (u8*)f->rdPtr);
    if (f->count < 0) {
        f->count += (s32)f->size;
    }
}

void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWatermark, u32 loWatermark) {
    GXFifoObj* obj = gx_fifo_ensure(fifo, "GXInitFifoLimits");
    PS3GxFifoObj* f = gx_fifo_cast(obj);
    f->loWatermark = (loWatermark < f->size) ? loWatermark : 0;
    f->hiWatermark = (hiWatermark > f->loWatermark && hiWatermark < f->size) ? hiWatermark : (f->size - 32);
}

void GXSetCPUFifo(GXFifoObj* fifo) {
    s_cpu_fifo = gx_fifo_ensure(fifo, "GXSetCPUFifo");
    gx_fifo_cast(s_cpu_fifo)->bind_cpu = 1;
}

void GXSetGPFifo(GXFifoObj* fifo) {
    s_gp_fifo = gx_fifo_ensure(fifo, "GXSetGPFifo");
    gx_fifo_cast(s_gp_fifo)->bind_gp = 1;
}

void GXSaveCPUFifo(GXFifoObj* fifo) { if (fifo) *fifo = *gx_fifo_ensure(s_cpu_fifo, "GXSaveCPUFifo"); }
void GXSaveGPFifo(GXFifoObj* fifo) { if (fifo) *fifo = *gx_fifo_ensure(s_gp_fifo, "GXSaveGPFifo"); }
void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
    if (overhi) *overhi = GX_FALSE;
    if (underlow) *underlow = GX_FALSE;
    if (readIdle) *readIdle = GX_TRUE;
    if (cmdIdle) *cmdIdle = GX_TRUE;
    if (brkpt) *brkpt = GX_FALSE;
}
void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underflow, u32* fifoCount, GXBool* cpuWrite, GXBool* gpRead, GXBool* fifowrap) {
    PS3GxFifoObj* f = gx_fifo_cast(gx_fifo_ensure(fifo, "GXGetFifoStatus"));
    if (overhi) *overhi = GX_FALSE;
    if (underflow) *underflow = GX_FALSE;
    if (fifoCount) *fifoCount = (u32)f->count;
    if (cpuWrite) *cpuWrite = f->bind_cpu ? GX_TRUE : GX_FALSE;
    if (gpRead) *gpRead = f->bind_gp ? GX_TRUE : GX_FALSE;
    if (fifowrap) *fifowrap = GX_FALSE;
}
void GXGetFifoPtrs(GXFifoObj* fifo, void** readPtr, void** writePtr) {
    PS3GxFifoObj* f = gx_fifo_cast(gx_fifo_ensure(fifo, "GXGetFifoPtrs"));
    if (readPtr) *readPtr = f->rdPtr;
    if (writePtr) *writePtr = f->wrPtr;
}
void* GXGetFifoBase(GXFifoObj* fifo) {
    return gx_fifo_cast(gx_fifo_ensure(fifo, "GXGetFifoBase"))->base;
}
u32 GXGetFifoSize(GXFifoObj* fifo) {
    return gx_fifo_cast(gx_fifo_ensure(fifo, "GXGetFifoSize"))->size;
}
void GXGetFifoLimits(GXFifoObj* fifo, u32* hi, u32* lo) {
    PS3GxFifoObj* f = gx_fifo_cast(gx_fifo_ensure(fifo, "GXGetFifoLimits"));
    if (hi) *hi = f->hiWatermark;
    if (lo) *lo = f->loWatermark;
}
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback cb) { GXBreakPtCallback old = s_breakpt_cb; s_breakpt_cb = cb; return old; }
void GXEnableBreakPt(void* break_pt) { (void)break_pt; if (s_breakpt_cb) s_breakpt_cb(); }
void GXDisableBreakPt(void) {}
OSThread* GXSetCurrentGXThread(void) {
    OSThread* old = s_gx_thread;
    s_gx_thread = OSGetCurrentThread();
    if (!s_gx_thread) {
        s_gx_thread = old;
    }
    if (!s_gx_thread && !s_gx_thread_logged) {
        OSReport("[PS3/GX] GXSetCurrentGXThread: OSGetCurrentThread returned null\n");
        s_gx_thread_logged = 1;
    }
    return old;
}
OSThread* GXGetCurrentGXThread(void) {
    if (!s_gx_thread) {
        s_gx_thread = OSGetCurrentThread();
        if (s_gx_thread && !s_gx_thread_logged) {
            OSReport("[PS3/GX] GXGetCurrentGXThread: defaulting to current thread %p\n", s_gx_thread);
            s_gx_thread_logged = 1;
        }
    }
    return s_gx_thread;
}
GXFifoObj* GXGetCPUFifo(void) { return gx_fifo_ensure(s_cpu_fifo, "GXGetCPUFifo"); }
GXFifoObj* GXGetGPFifo(void) { return gx_fifo_ensure(s_gp_fifo, "GXGetGPFifo"); }
u32 GXGetOverflowCount(void) { return 0; }
u32 GXResetOverflowCount(void) { return 0; }
volatile void* GXRedirectWriteGatherPipe(void* ptr) { return ptr; }

void GXBeginDisplayList(void* list, u32 size) {
    s_dl.active = 1;
    s_dl.buf = (u8*)list;
    s_dl.size = size;
    s_dl.off = 0;
    s_dl.overflow = 0;
}
u32 GXEndDisplayList(void) {
    u32 nbytes = (s_dl.active && !s_dl.overflow) ? s_dl.off : 0;
    memset(&s_dl, 0, sizeof(s_dl));
    return nbytes;
}
void GXCallDisplayList(void* list, u32 nbytes) {
    const u8* p = (const u8*)list;
    u32 off = 0;
    if (!p || nbytes == 0) {
        return;
    }
    while (off + sizeof(u32) <= nbytes) {
        u32 op = 0;
        memcpy(&op, p + off, sizeof(op));
        off += sizeof(op);
        switch (op) {
            case PS3GX_DL_OP_TEXCOPY_SRC: {
                u32 v[4];
                if (off + sizeof(v) > nbytes) return;
                memcpy(v, p + off, sizeof(v));
                off += sizeof(v);
                GXSetTexCopySrc((u16)v[0], (u16)v[1], (u16)v[2], (u16)v[3]);
                break;
            }
            case PS3GX_DL_OP_TEXCOPY_DST: {
                u32 v[4];
                if (off + sizeof(v) > nbytes) return;
                memcpy(v, p + off, sizeof(v));
                off += sizeof(v);
                GXSetTexCopyDst((u16)v[0], (u16)v[1], (GXTexFmt)v[2], (GXBool)(v[3] ? GX_TRUE : GX_FALSE));
                break;
            }
            case PS3GX_DL_OP_COPY_FILTER:
                break;
            case PS3GX_DL_OP_COPY_TEX: {
                u64 dest64 = 0;
                u32 clear = 0;
                if (off + sizeof(dest64) + sizeof(clear) > nbytes) return;
                memcpy(&dest64, p + off, sizeof(dest64));
                off += sizeof(dest64);
                memcpy(&clear, p + off, sizeof(clear));
                off += sizeof(clear);
                gx_copy_tex_execute((void*)(uintptr_t)dest64, (GXBool)(clear ? GX_TRUE : GX_FALSE));
                break;
            }
            default:
                return;
        }
    }
}
void GXDrawSphere(u8 numMajor, u8 numMinor) { (void)numMajor; (void)numMinor; }
void GXSetCullMode(GXCullMode mode) { s_cull_mode = mode; }
void GXSetCoPlanar(GXBool enable) { (void)enable; }
void GXReadXfRasMetric(u32* xf_wait_in, u32* xf_wait_out, u32* ras_busy, u32* clocks) {
    if (xf_wait_in) *xf_wait_in = 0;
    if (xf_wait_out) *xf_wait_out = 0;
    if (ras_busy) *ras_busy = 0;
    if (clocks) *clocks = 0;
}
void GXGetProjectionv(f32* p) { if (p) memcpy(p, s_projection, sizeof(f32) * GX_PROJECTION_SZ); }
void GXGetVtxAttrFmt(GXVtxFmt id, GXAttr attr, GXCompCnt* compCnt, GXCompType* compType, u8* frac) {
    (void)id; (void)attr; if (compCnt) *compCnt = 0; if (compType) *compType = 0; if (frac) *frac = 0;
}
void GXSetVerifyLevel(GXWarningLevel level) { (void)level; }
GXVerifyCallback GXSetVerifyCallback(GXVerifyCallback cb) { (void)cb; return NULL; }
