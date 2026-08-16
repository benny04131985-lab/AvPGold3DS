#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "win95/aw.h"
#include "../avp3ds_shbin.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AVP3DS_TEXTURE_WIDTH   1024
#define AVP3DS_TEXTURE_HEIGHT   512

#define AVP3DS_TOP_WIDTH       400.0f
#define AVP3DS_TOP_HEIGHT      240.0f

#define AVP3DS_BOTTOM_WIDTH    320.0f
#define AVP3DS_BOTTOM_HEIGHT   240.0f

#define AVP3DS_DRAW_ROUTE_TOP             0
#define AVP3DS_DRAW_ROUTE_MIRROR_BOTTOM   1

#define AVP3DS_DISPLAY_TRANSFER_FLAGS \
    (GX_TRANSFER_FLIP_VERT(0) | \
     GX_TRANSFER_OUT_TILED(0) | \
     GX_TRANSFER_RAW_COPY(0) | \
     GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | \
     GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) | \
     GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO))

#define AVP3DS_TEXTURE_BYTES \
    ((size_t)AVP3DS_TEXTURE_WIDTH * \
     (size_t)AVP3DS_TEXTURE_HEIGHT * sizeof(u16))

#define AVP3DS_GAME_VERTEX_STRIDE   40U
#define AVP3DS_GAME_MAX_VERTICES    65536U
#define AVP3DS_GAME_MAX_TRIANGLES   65536U

static bool avp3ds_c3d_initialized = false;
static bool avp3ds_c2d_initialized = false;
static bool avp3ds_texture_initialized = false;
static bool avp3ds_video_ready = false;

static C3D_RenderTarget *avp3ds_top_target = NULL;
static C3D_RenderTarget *avp3ds_bottom_target = NULL;
static C3D_Tex avp3ds_texture;

static shaderProgram_s avp3ds_game_program;
static DVLB_s *avp3ds_game_dvlb = NULL;
static bool avp3ds_game_shader_ready = false;

static int avp3ds_game_projection_location = -1;
static C3D_Mtx avp3ds_game_projection;

static unsigned char *avp3ds_game_vertices = NULL;
static unsigned short *avp3ds_game_indices = NULL;

static size_t avp3ds_game_vertex_cursor = 0;
static size_t avp3ds_game_index_cursor = 0;

static bool avp3ds_game_frame_active = false;

/*
 * AVP-HUD1A gameplay-only lower screen.
 *
 * Reset at native frame begin and marked from MaintainHUD().
 */
static bool avp3ds_hud_seen_this_frame = false;

/*
 * AVP-HUD1B1 Marine raw stable-bar route.
 */
static bool avp3ds_bottom_grid_ready_this_frame = false;

/*
 * The software presenter consumes this one-shot loading state in the
 * same GPU frame as the briefing image.
 */
static int avp3ds_loading_progress_fixed = 0;
static bool avp3ds_loading_progress_pending = false;

/*
 * YEET31 Citro2D state restore:
 * native gameplay modifies Citro3D state behind Citro2D's back.
 */
static bool avp3ds_c2d_prepare_required = false;

static unsigned int avp3ds_diag_frame_count = 0;
static unsigned int avp3ds_diag_draw_batches = 0;
static unsigned int avp3ds_diag_triangles = 0;
static unsigned int avp3ds_diag_texture_handles = 0;
static unsigned int avp3ds_diag_native_success = 0;
static unsigned int avp3ds_diag_native_failure = 0;
static unsigned int avp3ds_diag_untextured = 0;

/* YEET28 rolling gameplay benchmark state. */
static u64 avp3ds_bench_frame_start_tick = 0;
static u64 avp3ds_bench_update_start_tick = 0;
static u64 avp3ds_bench_render_start_tick = 0;

static u64 avp3ds_bench_frame_update_ticks = 0;
static u64 avp3ds_bench_frame_render_ticks = 0;

static u64 avp3ds_bench_total_frame_ticks = 0;
static u64 avp3ds_bench_total_update_ticks = 0;
static u64 avp3ds_bench_total_render_ticks = 0;
static u64 avp3ds_bench_total_begin_ticks = 0;
static u64 avp3ds_bench_total_end_ticks = 0;

static unsigned int avp3ds_bench_begin_attempts = 0;
static unsigned int avp3ds_bench_begin_failures = 0;

static bool avp3ds_bench_frame_started = false;
static bool avp3ds_bench_update_started = false;
static bool avp3ds_bench_render_started = false;

typedef struct AvP3DS_NativeTexture
{
    C3D_Tex texture;
    unsigned int sourceWidth;
    unsigned int sourceHeight;
    unsigned int textureWidth;
    unsigned int textureHeight;
    bool initialized;
} AvP3DS_NativeTexture;

static unsigned int AvP3DS_NextPowerOfTwo(
    unsigned int value);

static AvP3DS_NativeTexture *AvP3DS_GetOrCreateNativeTexture(
    void *textureHandle);

void AvP3DS_DestroyNativeTexture(
    void *textureHandle);

extern int AvP_LegacyMain(int argc, char *argv[]);

/*
 * Called only from AvP's real MaintainHUD() path.
 */
void AvP3DS_MarkHUDFrame(void)
{
    avp3ds_hud_seen_this_frame = true;
}

void AvP3DS_BenchmarkFrameStart(void)
{
    avp3ds_bench_frame_start_tick = svcGetSystemTick();
    avp3ds_bench_frame_update_ticks = 0;
    avp3ds_bench_frame_render_ticks = 0;

    avp3ds_bench_frame_started = true;
    avp3ds_bench_update_started = false;
    avp3ds_bench_render_started = false;
}

void AvP3DS_BenchmarkUpdateStart(void)
{
    avp3ds_bench_update_start_tick = svcGetSystemTick();
    avp3ds_bench_update_started = true;
}

void AvP3DS_BenchmarkUpdateEnd(void)
{
    if (!avp3ds_bench_update_started)
        return;

    avp3ds_bench_frame_update_ticks =
        svcGetSystemTick() -
        avp3ds_bench_update_start_tick;

    avp3ds_bench_update_started = false;
}

void AvP3DS_BenchmarkRenderStart(void)
{
    avp3ds_bench_render_start_tick = svcGetSystemTick();
    avp3ds_bench_render_started = true;
}

void AvP3DS_BenchmarkRenderEnd(void)
{
    if (!avp3ds_bench_render_started)
        return;

    avp3ds_bench_frame_render_ticks =
        svcGetSystemTick() -
        avp3ds_bench_render_start_tick;

    avp3ds_bench_render_started = false;
}

int AvP3DS_AppRunning(void)
{
    return aptMainLoop() ? 1 : 0;
}

int AvP3DS_StartPressed(void)
{
    /*
     * Development safe exit:
     * SELECT exits the application while START is reserved
     * for AvP's normal Escape/menu function.
     */
    hidScanInput();
    return (hidKeysDown() & KEY_SELECT) != 0;
}


#define AVP3DS_BUTTON_A       (1U << 0)
#define AVP3DS_BUTTON_B       (1U << 1)
#define AVP3DS_BUTTON_X       (1U << 2)
#define AVP3DS_BUTTON_Y       (1U << 3)
#define AVP3DS_BUTTON_UP      (1U << 4)
#define AVP3DS_BUTTON_DOWN    (1U << 5)
#define AVP3DS_BUTTON_LEFT    (1U << 6)
#define AVP3DS_BUTTON_RIGHT   (1U << 7)
#define AVP3DS_BUTTON_SELECT  (1U << 8)
#define AVP3DS_BUTTON_L       (1U << 9)
#define AVP3DS_BUTTON_R       (1U << 10)

#define AVP3DS_BUTTON_ZL      (1U << 11)
#define AVP3DS_BUTTON_ZR      (1U << 12)
#define AVP3DS_BUTTON_START   (1U << 13)
unsigned int AvP3DS_ButtonsHeld(void)
{
    u32 keys;
    unsigned int result = 0;

    hidScanInput();
    keys = hidKeysHeld();

    if (keys & KEY_A)      result |= AVP3DS_BUTTON_A;
    if (keys & KEY_B)      result |= AVP3DS_BUTTON_B;
    if (keys & KEY_X)      result |= AVP3DS_BUTTON_X;
    if (keys & KEY_Y)      result |= AVP3DS_BUTTON_Y;

    if (keys & KEY_DUP)    result |= AVP3DS_BUTTON_UP;
    if (keys & KEY_DDOWN)  result |= AVP3DS_BUTTON_DOWN;
    if (keys & KEY_DLEFT)  result |= AVP3DS_BUTTON_LEFT;
    if (keys & KEY_DRIGHT) result |= AVP3DS_BUTTON_RIGHT;

    if (keys & KEY_SELECT) result |= AVP3DS_BUTTON_SELECT;
    if (keys & KEY_L)      result |= AVP3DS_BUTTON_L;
    if (keys & KEY_R)      result |= AVP3DS_BUTTON_R;

    if (keys & KEY_ZL)     result |= AVP3DS_BUTTON_ZL;
    if (keys & KEY_ZR)     result |= AVP3DS_BUTTON_ZR;
    if (keys & KEY_START)  result |= AVP3DS_BUTTON_START;
    return result;
}

void AvP3DS_ReadCirclePad(int *x, int *y)
{
    circlePosition circle;

    hidCircleRead(&circle);

    if (x != NULL)
        *x = circle.dx;

    if (y != NULL)
        *y = circle.dy;
}

void AvP3DS_ReadCStick(int *x, int *y)
{
    circlePosition cstick;

    hidCstickRead(&cstick);

    if (x != NULL)
        *x = cstick.dx;

    if (y != NULL)
        *y = cstick.dy;
}


int AvP3DS_ShowKeyboard(
    char *buffer,
    int bufferBytes,
    const char *hint)
{
    SwkbdState keyboard;
    SwkbdButton button;

    if (buffer == NULL || bufferBytes < 2)
        return 0;

    swkbdInit(
        &keyboard,
        SWKBD_TYPE_NORMAL,
        2,
        bufferBytes - 1);

    swkbdSetValidation(
        &keyboard,
        SWKBD_NOTEMPTY_NOTBLANK,
        0,
        0);

    swkbdSetFeatures(
        &keyboard,
        SWKBD_DARKEN_TOP_SCREEN |
        SWKBD_DEFAULT_QWERTY);

    swkbdSetButton(
        &keyboard,
        SWKBD_BUTTON_LEFT,
        "Cancel",
        false);

    swkbdSetButton(
        &keyboard,
        SWKBD_BUTTON_RIGHT,
        "OK",
        true);

    if (hint != NULL)
        swkbdSetHintText(&keyboard, hint);

    swkbdSetInitialText(&keyboard, buffer);

    button = swkbdInputText(
        &keyboard,
        buffer,
        (size_t)bufferBytes);

    return button == SWKBD_BUTTON_CONFIRM;
}

/*
 * Native PICA texture ordering:
 *
 * Pixels are grouped into physical 8x8 tiles. Pixels inside each tile
 * use 3-bit Morton ordering:
 *
 *     x0, y0, x1, y1, x2, y2
 */
static inline unsigned int AvP3DS_Morton8(
    unsigned int x,
    unsigned int y)
{
    static const unsigned char mortonX[8] = {
        0, 1, 4, 5, 16, 17, 20, 21
    };

    static const unsigned char mortonY[8] = {
        0, 2, 8, 10, 32, 34, 40, 42
    };

    return (unsigned int)mortonX[x & 7] +
           (unsigned int)mortonY[y & 7];
}

static inline size_t AvP3DS_TiledOffsetForWidth(
    unsigned int textureWidth,
    unsigned int x,
    unsigned int y)
{
    const unsigned int tilesPerRow =
        textureWidth / 8U;

    const unsigned int tileX = x >> 3;
    const unsigned int tileY = y >> 3;

    const size_t tileBase =
        ((size_t)tileY * (size_t)tilesPerRow +
         (size_t)tileX) * 64U;

    return tileBase + (size_t)AvP3DS_Morton8(x, y);
}

static unsigned int AvP3DS_NextPowerOfTwo(
    unsigned int value)
{
    unsigned int result = 8U;

    while (result < value && result < 1024U)
        result <<= 1;

    return result;
}

static AvP3DS_NativeTexture *AvP3DS_GetOrCreateNativeTexture(
    void *textureHandle)
{
    D3DTexture *source;
    AvP3DS_NativeTexture *nativeTexture;
    unsigned int textureWidth;
    unsigned int textureHeight;
    unsigned int y;

    source = (D3DTexture *)textureHandle;

    if (source == NULL ||
        source->buf == NULL ||
        source->w == 0 ||
        source->h == 0)
    {
        return NULL;
    }

    if (source->nativeTexture != NULL)
    {
        return (AvP3DS_NativeTexture *)
            source->nativeTexture;
    }

    textureWidth =
        AvP3DS_NextPowerOfTwo(source->w);

    textureHeight =
        AvP3DS_NextPowerOfTwo(source->h);

    if (textureWidth < source->w ||
        textureHeight < source->h ||
        textureWidth > 1024U ||
        textureHeight > 1024U)
    {
        return NULL;
    }

    nativeTexture = calloc(
        1,
        sizeof(*nativeTexture));

    if (nativeTexture == NULL)
        return NULL;

    if (!C3D_TexInit(
            &nativeTexture->texture,
            (u16)textureWidth,
            (u16)textureHeight,
            GPU_RGBA8))
    {
        free(nativeTexture);
        return NULL;
    }

    nativeTexture->initialized = true;
    nativeTexture->sourceWidth = source->w;
    nativeTexture->sourceHeight = source->h;
    nativeTexture->textureWidth = textureWidth;
    nativeTexture->textureHeight = textureHeight;

    C3D_TexSetFilter(
        &nativeTexture->texture,
        source->filter == 0
            ? GPU_NEAREST
            : GPU_LINEAR,
        source->filter == 0
            ? GPU_NEAREST
            : GPU_LINEAR);

    C3D_TexSetWrap(
        &nativeTexture->texture,
        source->IsNpot
            ? GPU_CLAMP_TO_EDGE
            : GPU_REPEAT,
        source->IsNpot
            ? GPU_CLAMP_TO_EDGE
            : GPU_REPEAT);

    memset(
        nativeTexture->texture.data,
        0,
        nativeTexture->texture.size);

    /*
     * AvP keeps decoded pixels as conventional RGBA bytes.
     *
     * GPU_RGBA8 texture memory uses the byte order A, B, G, R on this
     * little-endian CPU, producing the packed PICA value 0xRRGGBBAA.
     */
    for (y = 0; y < source->h; ++y)
    {
        unsigned int x;

        for (x = 0; x < source->w; ++x)
        {
            const unsigned char *sourcePixel;
            unsigned char *destinationPixel;
            size_t sourceOffset;
            size_t tiledOffset;

            sourceOffset =
                ((size_t)y * (size_t)source->w +
                 (size_t)x) * 4U;

            tiledOffset =
                AvP3DS_TiledOffsetForWidth(
                    textureWidth,
                    x,
                    y);

            sourcePixel =
                source->buf + sourceOffset;

            destinationPixel =
                (unsigned char *)
                    nativeTexture->texture.data +
                tiledOffset * 4U;

            destinationPixel[0] = sourcePixel[3];
            destinationPixel[1] = sourcePixel[2];
            destinationPixel[2] = sourcePixel[1];
            destinationPixel[3] = sourcePixel[0];
        }
    }

    GSPGPU_FlushDataCache(
        nativeTexture->texture.data,
        nativeTexture->texture.size);

    source->nativeTexture = nativeTexture;

    return nativeTexture;
}

void AvP3DS_DestroyNativeTexture(
    void *textureHandle)
{
    D3DTexture *source;
    AvP3DS_NativeTexture *nativeTexture;

    source = (D3DTexture *)textureHandle;

    if (source == NULL ||
        source->nativeTexture == NULL)
    {
        return;
    }

    nativeTexture =
        (AvP3DS_NativeTexture *)
            source->nativeTexture;

    if (nativeTexture->initialized)
    {
        C3D_TexDelete(
            &nativeTexture->texture);

        nativeTexture->initialized = false;
    }

    free(nativeTexture);
    source->nativeTexture = NULL;
}

static void AvP3DS_VideoShutdown(void)
{
    avp3ds_video_ready = false;

    if (avp3ds_bottom_target != NULL)
    {
        C3D_RenderTargetDelete(avp3ds_bottom_target);
        avp3ds_bottom_target = NULL;
    }

    if (avp3ds_top_target != NULL)
    {
        C3D_RenderTargetDelete(avp3ds_top_target);
        avp3ds_top_target = NULL;
    }

    if (avp3ds_texture_initialized)
    {
        C3D_TexDelete(&avp3ds_texture);
        avp3ds_texture_initialized = false;
    }

    if (avp3ds_game_frame_active)
    {
        C3D_FrameEnd(0);
        avp3ds_game_frame_active = false;
    }

    if (avp3ds_game_vertices != NULL)
    {
        linearFree(avp3ds_game_vertices);
        avp3ds_game_vertices = NULL;
    }

    if (avp3ds_game_indices != NULL)
    {
        linearFree(avp3ds_game_indices);
        avp3ds_game_indices = NULL;
    }

    if (avp3ds_game_shader_ready)
    {
        shaderProgramFree(&avp3ds_game_program);
        avp3ds_game_shader_ready = false;
    }

    if (avp3ds_game_dvlb != NULL)
    {
        DVLB_Free(avp3ds_game_dvlb);
        avp3ds_game_dvlb = NULL;
    }

    if (avp3ds_c2d_initialized)
    {
        C2D_Fini();
        avp3ds_c2d_initialized = false;
    }

    if (avp3ds_c3d_initialized)
    {
        C3D_Fini();
        avp3ds_c3d_initialized = false;
    }
}

static bool AvP3DS_VideoInit(void)
{
    memset(&avp3ds_texture, 0, sizeof(avp3ds_texture));

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE))
    {
        printf("C3D_Init failed.\n");
        return false;
    }

    avp3ds_c3d_initialized = true;

    shaderProgramInit(&avp3ds_game_program);

    avp3ds_game_dvlb = DVLB_ParseFile(
        (u32 *)avp3ds_shbin,
        avp3ds_shbin_size);

    if (avp3ds_game_dvlb == NULL)
    {
        printf("AvP gameplay shader parse failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    shaderProgramSetVsh(
        &avp3ds_game_program,
        &avp3ds_game_dvlb->DVLE[0]);

    avp3ds_game_projection_location =
        shaderInstanceGetUniformLocation(
            avp3ds_game_program.vertexShader,
            "projection");

    if (avp3ds_game_projection_location < 0)
    {
        printf("AvP gameplay projection uniform missing.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    Mtx_OrthoTilt(
        &avp3ds_game_projection,
        -1.0f,
         1.0f,
        -1.0f,
         1.0f,
        -1.0f,
         1.0f,
        true);

    avp3ds_game_shader_ready = true;

    avp3ds_game_vertices = linearAlloc(
        AVP3DS_GAME_MAX_VERTICES *
        AVP3DS_GAME_VERTEX_STRIDE);

    avp3ds_game_indices = linearAlloc(
        AVP3DS_GAME_MAX_TRIANGLES *
        3U *
        sizeof(unsigned short));

    if (avp3ds_game_vertices == NULL ||
        avp3ds_game_indices == NULL)
    {
        printf("AvP gameplay buffers failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    if (!C2D_Init(C2D_DEFAULT_MAX_OBJECTS))
    {
        printf("C2D_Init failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    avp3ds_c2d_initialized = true;
    C2D_Prepare();

    /*
     * Gameplay requires a real depth attachment. The previous Citro2D
     * convenience target had no explicit depth buffer, allowing later world
     * polygons to overwrite nearer geometry.
     */
    avp3ds_top_target =
        C3D_RenderTargetCreate(
            240,
            400,
            GPU_RB_RGBA8,
            GPU_RB_DEPTH24_STENCIL8);

    if (avp3ds_top_target != NULL)
    {
        C3D_RenderTargetSetOutput(
            avp3ds_top_target,
            GFX_TOP,
            GFX_LEFT,
            AVP3DS_DISPLAY_TRANSFER_FLAGS);
    }

    if (avp3ds_top_target == NULL)
    {
        printf("C2D top target failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    /*
     * AVP-HUD0 bottom-screen proof:
     * create a native 320x240 lower-screen render target.
     */
    avp3ds_bottom_target =
        C3D_RenderTargetCreate(
            240,
            320,
            GPU_RB_RGBA8,
            GPU_RB_DEPTH24_STENCIL8);

    if (avp3ds_bottom_target != NULL)
    {
        C3D_RenderTargetSetOutput(
            avp3ds_bottom_target,
            GFX_BOTTOM,
            GFX_LEFT,
            AVP3DS_DISPLAY_TRANSFER_FLAGS);
    }

    if (avp3ds_bottom_target == NULL)
    {
        printf("C2D bottom target failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    if (!C3D_TexInit(
            &avp3ds_texture,
            AVP3DS_TEXTURE_WIDTH,
            AVP3DS_TEXTURE_HEIGHT,
            GPU_RGB565))
    {
        printf("RGB565 texture initialization failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    avp3ds_texture_initialized = true;

    C3D_TexSetFilter(
        &avp3ds_texture,
        GPU_NEAREST,
        GPU_NEAREST);

    C3D_TexSetWrap(
        &avp3ds_texture,
        GPU_CLAMP_TO_EDGE,
        GPU_CLAMP_TO_EDGE);

    memset(
        avp3ds_texture.data,
        0,
        AVP3DS_TEXTURE_BYTES);

    GSPGPU_FlushDataCache(
        avp3ds_texture.data,
        AVP3DS_TEXTURE_BYTES);

    avp3ds_video_ready = true;
    return true;
}

void AvP3DS_GameFrameBegin(void)
{
    C3D_AttrInfo *attributeInfo;
    C3D_TexEnv *textureEnvironment;
    int stage;

    if (!avp3ds_video_ready ||
        !avp3ds_game_shader_ready ||
        avp3ds_top_target == NULL ||
        avp3ds_bottom_target == NULL ||
        avp3ds_game_frame_active)
    {
        return;
    }

    avp3ds_hud_seen_this_frame = false;
    avp3ds_bottom_grid_ready_this_frame = false;

    {
        u64 beginStartTick;
        u64 beginFinishTick;
        bool frameAccepted;

        beginStartTick = svcGetSystemTick();

        frameAccepted =
            C3D_FrameBegin(C3D_FRAME_NONBLOCK);

        beginFinishTick = svcGetSystemTick();

        avp3ds_bench_total_begin_ticks +=
            beginFinishTick - beginStartTick;

        ++avp3ds_bench_begin_attempts;

        if (!frameAccepted)
        {
            ++avp3ds_bench_begin_failures;
            return;
        }
    }

    avp3ds_game_frame_active = true;
    avp3ds_c2d_prepare_required = true;
    avp3ds_game_vertex_cursor = 0;
    avp3ds_game_index_cursor = 0;

    C3D_RenderTargetClear(
        avp3ds_top_target,
        C3D_CLEAR_ALL,
        0x000000FF,
        0);

    if (!C3D_FrameDrawOn(avp3ds_top_target))
    {
        C3D_FrameEnd(0);
        avp3ds_game_frame_active = false;
        return;
    }

    C3D_BindProgram(&avp3ds_game_program);

    attributeInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attributeInfo);

    AttrInfo_AddLoader(
        attributeInfo,
        0,
        GPU_FLOAT,
        4);

    /*
     * YEET24B streamed gameplay vertex:
     *
     *   attribute 0: clip-space position, float4
     *   attribute 1: texture coordinate, float2
     *   attribute 2: primary lighting/color, float4
     */
    AttrInfo_AddLoader(
        attributeInfo,
        1,
        GPU_FLOAT,
        2);

    AttrInfo_AddLoader(
        attributeInfo,
        2,
        GPU_FLOAT,
        4);

    C3D_FVUnifMtx4x4(
        GPU_VERTEX_SHADER,
        avp3ds_game_projection_location,
        &avp3ds_game_projection);

    /*
     * The draw routine selects either:
     *
     *   texture × primary vertex color
     *
     * or primary color alone for untextured batches.
     */
    for (stage = 0; stage < 6; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));

    textureEnvironment = C3D_GetTexEnv(0);

    C3D_TexEnvSrc(
        textureEnvironment,
        C3D_Both,
        GPU_PRIMARY_COLOR,
        0,
        0);

    C3D_TexEnvFunc(
        textureEnvironment,
        C3D_Both,
        GPU_REPLACE);

    C3D_CullFace(GPU_CULL_NONE);

    /*
     * AvP's clip-space conversion maps the near plane toward the larger
     * depth value, matching Citro3D's conventional GPU_GREATER setup.
     */
    C3D_DepthTest(
        true,
        GPU_GREATER,
        GPU_WRITE_ALL);

}

static void AvP3DS_DrawBottomFrame(void);

/*
 * Citro2D changes the active shader, attributes, buffers and texture
 * combiners. Restore AvP's native pipeline after drawing the diagnostic
 * grid in the middle of a gameplay frame.
 */
static void AvP3DS_RestoreGameplayPipelineAfterC2D(void)
{
    C3D_AttrInfo *attributeInfo;
    C3D_TexEnv *textureEnvironment;
    int stage;

    if (!avp3ds_game_frame_active ||
        avp3ds_top_target == NULL ||
        !avp3ds_game_shader_ready)
    {
        return;
    }

    C3D_FrameDrawOn(avp3ds_top_target);
    C3D_BindProgram(&avp3ds_game_program);

    attributeInfo = C3D_GetAttrInfo();
    AttrInfo_Init(attributeInfo);

    AttrInfo_AddLoader(
        attributeInfo,
        0,
        GPU_FLOAT,
        4);

    AttrInfo_AddLoader(
        attributeInfo,
        1,
        GPU_FLOAT,
        2);

    AttrInfo_AddLoader(
        attributeInfo,
        2,
        GPU_FLOAT,
        4);

    C3D_FVUnifMtx4x4(
        GPU_VERTEX_SHADER,
        avp3ds_game_projection_location,
        &avp3ds_game_projection);

    for (stage = 0; stage < 6; ++stage)
        C3D_TexEnvInit(C3D_GetTexEnv(stage));

    textureEnvironment = C3D_GetTexEnv(0);

    C3D_TexEnvSrc(
        textureEnvironment,
        C3D_Both,
        GPU_PRIMARY_COLOR,
        0,
        0);

    C3D_TexEnvFunc(
        textureEnvironment,
        C3D_Both,
        GPU_REPLACE);

    C3D_CullFace(GPU_CULL_NONE);

    C3D_DepthTest(
        true,
        GPU_GREATER,
        GPU_WRITE_ALL);
}

/*
 * Draw the diagnostic grid exactly once, immediately before the first
 * routed Marine HUD batch.
 */
static void AvP3DS_EnsureBottomGrid(void)
{
    if (avp3ds_bottom_grid_ready_this_frame ||
        !avp3ds_hud_seen_this_frame ||
        !avp3ds_game_frame_active)
    {
        return;
    }

    AvP3DS_DrawBottomFrame();

    avp3ds_bottom_grid_ready_this_frame = true;

    AvP3DS_RestoreGameplayPipelineAfterC2D();
}

void AvP3DS_DrawTriangles(
    const void *vertices,
    int vertexCount,
    const void *triangles,
    int triangleCount,
    void *textureHandle,
    int translucencyMode,
    int drawRoute)
{
    typedef struct
    {
        float position[4];
        float texcoord[2];
        unsigned char primary[4];
        unsigned char secondary[4];
    } AvP3DS_SourceVertex;

    typedef struct
    {
        unsigned short a;
        unsigned short b;
        unsigned short c;
    } AvP3DS_SourceTriangle;

    typedef struct
    {
        float position[4];
        float texcoord[2];
        float primary[4];
    } AvP3DS_GameVertex;

    const AvP3DS_SourceVertex *sourceVertices;
    const AvP3DS_SourceTriangle *sourceTriangles;

    AvP3DS_GameVertex *destination;
    AvP3DS_NativeTexture *nativeTexture;

    size_t expandedVertexCount;
    size_t writtenVertexCount;
    size_t triangleIndex;

    C3D_BufInfo *bufferInfo;
    C3D_TexEnv *textureEnvironment;

    if (!avp3ds_game_frame_active ||
        vertices == NULL ||
        triangles == NULL ||
        vertexCount <= 0 ||
        triangleCount <= 0)
    {
        return;
    }

    if (drawRoute == AVP3DS_DRAW_ROUTE_MIRROR_BOTTOM)
        AvP3DS_EnsureBottomGrid();

    sourceVertices =
        (const AvP3DS_SourceVertex *)vertices;

    sourceTriangles =
        (const AvP3DS_SourceTriangle *)triangles;

    ++avp3ds_diag_draw_batches;
    avp3ds_diag_triangles += (unsigned int)triangleCount;

    if (textureHandle != NULL)
    {
        ++avp3ds_diag_texture_handles;
    }
    else
    {
        ++avp3ds_diag_untextured;
    }

    nativeTexture =
        AvP3DS_GetOrCreateNativeTexture(textureHandle);

    if (nativeTexture != NULL &&
        nativeTexture->initialized)
    {
        ++avp3ds_diag_native_success;
    }
    else if (textureHandle != NULL)
    {
        ++avp3ds_diag_native_failure;
    }

    expandedVertexCount =
        (size_t)triangleCount * 3U;

    if (avp3ds_game_vertex_cursor +
            expandedVertexCount >
        AVP3DS_GAME_MAX_VERTICES)
    {
        return;
    }

    destination =
        (AvP3DS_GameVertex *)(
            avp3ds_game_vertices +
            avp3ds_game_vertex_cursor *
            AVP3DS_GAME_VERTEX_STRIDE);

    writtenVertexCount = 0;

    for (triangleIndex = 0;
         triangleIndex < (size_t)triangleCount;
         ++triangleIndex)
    {
        const unsigned short indices[3] =
        {
            sourceTriangles[triangleIndex].a,
            sourceTriangles[triangleIndex].b,
            sourceTriangles[triangleIndex].c
        };

        int corner;

        if (indices[0] >= vertexCount ||
            indices[1] >= vertexCount ||
            indices[2] >= vertexCount)
        {
            continue;
        }

        for (corner = 0; corner < 3; ++corner)
        {
            const AvP3DS_SourceVertex *sourceVertex =
                &sourceVertices[indices[corner]];

            AvP3DS_GameVertex *destinationVertex =
                &destination[writtenVertexCount];

            memcpy(
                destinationVertex->position,
                sourceVertex->position,
                sizeof(destinationVertex->position));

            destinationVertex->texcoord[0] =
                sourceVertex->texcoord[0];

            /*
             * AvP's original OpenGL path uses bottom-origin texture
             * coordinates. The PICA texture data was uploaded from
             * top-origin CPU rows, so invert V during vertex expansion.
             */
            destinationVertex->texcoord[1] =
                1.0f - sourceVertex->texcoord[1];

            /*
             * Restore AvP's original per-vertex lighting and alpha.
             * Source colors are unsigned bytes normalized to 0.0-1.0.
             */
            destinationVertex->primary[0] =
                (float)sourceVertex->primary[0] / 255.0f;

            destinationVertex->primary[1] =
                (float)sourceVertex->primary[1] / 255.0f;

            destinationVertex->primary[2] =
                (float)sourceVertex->primary[2] / 255.0f;

            destinationVertex->primary[3] =
                (float)sourceVertex->primary[3] / 255.0f;

            ++writtenVertexCount;
        }
    }

    if (writtenVertexCount == 0)
        return;

    GSPGPU_FlushDataCache(
        destination,
        writtenVertexCount *
            sizeof(AvP3DS_GameVertex));

    /*
     * Force texture unit zero dirty for every material batch. This also
     * invalidates stale PICA texture-cache lines after the CPU upload.
     */
    textureEnvironment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(textureEnvironment);

    if (nativeTexture != NULL &&
        nativeTexture->initialized)
    {
        C3D_TexBind(
            0,
            &nativeTexture->texture);

        /*
         * Restore AvP material lighting:
         * sampled texture RGBA multiplied by primary vertex RGBA.
         */
        C3D_TexEnvSrc(
            textureEnvironment,
            C3D_Both,
            GPU_TEXTURE0,
            GPU_PRIMARY_COLOR,
            0);

        C3D_TexEnvFunc(
            textureEnvironment,
            C3D_Both,
            GPU_MODULATE);
    }
    else
    {
        C3D_TexEnvSrc(
            textureEnvironment,
            C3D_Both,
            GPU_PRIMARY_COLOR,
            0,
            0);

        C3D_TexEnvFunc(
            textureEnvironment,
            C3D_Both,
            GPU_REPLACE);
    }


    /*
     * AvP translucency modes from win95/d3_func.h:
     *
     * 0 OFF
     * 1 NORMAL
     * 2 INVCOLOUR
     * 3 COLOUR
     * 4 GLOWING
     * 5 DARKENINGCOLOUR
     * 6 JUSTSETZ
     *
     * Transparent materials keep depth testing enabled but do not write
     * depth, matching the behavior expected by particles, coronas and
     * decals. Opaque materials update both color and depth.
     */
    switch (translucencyMode)
    {
        default:
        case 0: /* TRANSLUCENCY_OFF */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ONE,
                GPU_ZERO,
                GPU_ONE,
                GPU_ZERO);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_ALL);
            break;

        case 1: /* TRANSLUCENCY_NORMAL */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_SRC_ALPHA,
                GPU_ONE_MINUS_SRC_ALPHA,
                GPU_SRC_ALPHA,
                GPU_ONE_MINUS_SRC_ALPHA);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_COLOR);
            break;

        case 2: /* TRANSLUCENCY_INVCOLOUR */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ZERO,
                GPU_ONE_MINUS_SRC_COLOR,
                GPU_ZERO,
                GPU_ONE);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_COLOR);
            break;

        case 3: /* TRANSLUCENCY_COLOUR */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ZERO,
                GPU_SRC_COLOR,
                GPU_ZERO,
                GPU_ONE);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_COLOR);
            break;

        case 4: /* TRANSLUCENCY_GLOWING */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_SRC_ALPHA,
                GPU_ONE,
                GPU_SRC_ALPHA,
                GPU_ONE);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_COLOR);
            break;

        case 5: /* TRANSLUCENCY_DARKENINGCOLOUR */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ONE_MINUS_DST_COLOR,
                GPU_ZERO,
                GPU_ONE,
                GPU_ZERO);

            C3D_DepthTest(
                true,
                GPU_GREATER,
                GPU_WRITE_COLOR);
            break;

        case 6: /* TRANSLUCENCY_JUSTSETZ */
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ZERO,
                GPU_ONE,
                GPU_ZERO,
                GPU_ONE);

            C3D_DepthTest(
                true,
                GPU_ALWAYS,
                GPU_WRITE_DEPTH);
            break;
    }

    bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);

    BufInfo_Add(
        bufferInfo,
        destination,
        sizeof(AvP3DS_GameVertex),
        3,
        0x210);

    /*
     * Keep the original top HUD during the raw comparison build.
     */
    C3D_FrameDrawOn(avp3ds_top_target);

    C3D_DrawArrays(
        GPU_TRIANGLES,
        0,
        (int)writtenVertexCount);

    if (drawRoute == AVP3DS_DRAW_ROUTE_MIRROR_BOTTOM &&
        avp3ds_bottom_grid_ready_this_frame &&
        avp3ds_bottom_target != NULL)
    {
        C3D_FrameDrawOn(avp3ds_bottom_target);

        /*
         * HUD graphics always sit over the diagnostic grid regardless
         * of their original top-screen depth value.
         */
        C3D_DepthTest(
            false,
            GPU_ALWAYS,
            GPU_WRITE_COLOR);

        C3D_DrawArrays(
            GPU_TRIANGLES,
            0,
            (int)writtenVertexCount);

        C3D_FrameDrawOn(avp3ds_top_target);
    }

    avp3ds_game_vertex_cursor +=
        writtenVertexCount;
}

/*
 * AVP-HUD1A gameplay-only lower screen.
 *
 * Every native frame clears the lower screen to black. The temporary
 * dashboard is drawn only when AvP's genuine MaintainHUD() path marked
 * this as a live gameplay-HUD frame.
 */
static void AvP3DS_DrawBottomFrame(void)
{
    unsigned int cellX;
    unsigned int cellY;

    if (avp3ds_bottom_target == NULL ||
        avp3ds_bottom_grid_ready_this_frame)
    {
        return;
    }

    /*
     * AVP-HUD1B0 diagnostic grid:
     * restore Citro2D after native gameplay rendering.
     */
    C2D_Prepare();

    C2D_TargetClear(
        avp3ds_bottom_target,
        C2D_Color32(8, 11, 14, 255));

    C2D_SceneBegin(avp3ds_bottom_target);

    /*
     * Loading, menus and other non-HUD frames remain blank.
     */
    if (!avp3ds_hud_seen_this_frame)
        return;

    /*
     * 320x240 diagnostic checker:
     *
     *   20 columns x 15 rows
     *   each cell = 16x16 pixels
     */
    for (cellY = 0; cellY < 15U; ++cellY)
    {
        for (cellX = 0; cellX < 20U; ++cellX)
        {
            const bool alternate =
                ((cellX + cellY) & 1U) != 0;

            const u32 colour =
                alternate
                    ? C2D_Color32(31, 42, 52, 255)
                    : C2D_Color32(18, 25, 32, 255);

            C2D_DrawRectSolid(
                (float)(cellX * 16U),
                (float)(cellY * 16U),
                0.0f,
                16.0f,
                16.0f,
                colour);
        }
    }

    /*
     * Strong vertical guides every 32 pixels.
     */
    for (cellX = 0; cellX <= 10U; ++cellX)
    {
        float x = (float)(cellX * 32U);

        if (x >= AVP3DS_BOTTOM_WIDTH)
            x = AVP3DS_BOTTOM_WIDTH - 1.0f;

        C2D_DrawRectSolid(
            x,
            0.0f,
            0.1f,
            1.0f,
            AVP3DS_BOTTOM_HEIGHT,
            C2D_Color32(83, 103, 119, 255));
    }

    /*
     * Strong horizontal guides every 32 pixels.
     */
    for (cellY = 0; cellY <= 7U; ++cellY)
    {
        float y = (float)(cellY * 32U);

        if (y >= AVP3DS_BOTTOM_HEIGHT)
            y = AVP3DS_BOTTOM_HEIGHT - 1.0f;

        C2D_DrawRectSolid(
            0.0f,
            y,
            0.1f,
            AVP3DS_BOTTOM_WIDTH,
            1.0f,
            C2D_Color32(83, 103, 119, 255));
    }

    /*
     * Exact center axes:
     *
     *   vertical:   X = 160
     *   horizontal: Y = 120
     */
    C2D_DrawRectSolid(
        159.0f,
        0.0f,
        0.2f,
        2.0f,
        AVP3DS_BOTTOM_HEIGHT,
        C2D_Color32(205, 235, 245, 255));

    C2D_DrawRectSolid(
        0.0f,
        119.0f,
        0.2f,
        AVP3DS_BOTTOM_WIDTH,
        2.0f,
        C2D_Color32(205, 235, 245, 255));

    /*
     * Two-pixel screen boundary.
     */
    C2D_DrawRectSolid(
        0.0f,
        0.0f,
        0.3f,
        AVP3DS_BOTTOM_WIDTH,
        2.0f,
        C2D_Color32(132, 158, 174, 255));

    C2D_DrawRectSolid(
        0.0f,
        AVP3DS_BOTTOM_HEIGHT - 2.0f,
        0.3f,
        AVP3DS_BOTTOM_WIDTH,
        2.0f,
        C2D_Color32(132, 158, 174, 255));

    C2D_DrawRectSolid(
        0.0f,
        0.0f,
        0.3f,
        2.0f,
        AVP3DS_BOTTOM_HEIGHT,
        C2D_Color32(132, 158, 174, 255));

    C2D_DrawRectSolid(
        AVP3DS_BOTTOM_WIDTH - 2.0f,
        0.0f,
        0.3f,
        2.0f,
        AVP3DS_BOTTOM_HEIGHT,
        C2D_Color32(132, 158, 174, 255));
}

void AvP3DS_GameFrameEnd(void)
{
    u64 endStartTick;
    u64 endFinishTick;

    if (!avp3ds_game_frame_active)
        return;

    endStartTick = svcGetSystemTick();

    AvP3DS_DrawBottomFrame();

    C3D_FrameEnd(0);

    endFinishTick = svcGetSystemTick();

    avp3ds_bench_total_end_ticks +=
        endFinishTick - endStartTick;

    avp3ds_game_frame_active = false;

    if (avp3ds_bench_frame_started)
    {
        avp3ds_bench_total_frame_ticks +=
            endFinishTick -
            avp3ds_bench_frame_start_tick;

        avp3ds_bench_total_update_ticks +=
            avp3ds_bench_frame_update_ticks;

        avp3ds_bench_total_render_ticks +=
            avp3ds_bench_frame_render_ticks;

        avp3ds_bench_frame_started = false;
    }

    ++avp3ds_diag_frame_count;

    if ((avp3ds_diag_frame_count % 120U) == 0U)
    {
        const double sampleCount = 120.0;

        const double frameMs =
            ((double)avp3ds_bench_total_frame_ticks /
             sampleCount) /
            CPU_TICKS_PER_MSEC;

        const double updateMs =
            ((double)avp3ds_bench_total_update_ticks /
             sampleCount) /
            CPU_TICKS_PER_MSEC;

        const double renderMs =
            ((double)avp3ds_bench_total_render_ticks /
             sampleCount) /
            CPU_TICKS_PER_MSEC;

        const double beginMs =
            avp3ds_bench_begin_attempts
                ? ((double)avp3ds_bench_total_begin_ticks /
                   (double)avp3ds_bench_begin_attempts) /
                  CPU_TICKS_PER_MSEC
                : 0.0;

        const double endMs =
            ((double)avp3ds_bench_total_end_ticks /
             sampleCount) /
            CPU_TICKS_PER_MSEC;

        const double estimatedFps =
            frameMs > 0.0
                ? 1000.0 / frameMs
                : 0.0;

        printf(
            "\nYEET28 BENCH frame=%u\n"
            "fps=%.1f frame=%.2fms\n"
            "update=%.2fms render=%.2fms\n"
            "begin=%.3fms end=%.3fms\n"
            "beginFail=%u/%u\n"
            "batches/f=%.1f tris/f=%.1f\n"
            "handles=%u nativeOK=%u nativeFAIL=%u\n"
            "untextured=%u\n",
            avp3ds_diag_frame_count,
            estimatedFps,
            frameMs,
            updateMs,
            renderMs,
            beginMs,
            endMs,
            avp3ds_bench_begin_failures,
            avp3ds_bench_begin_attempts,
            (double)avp3ds_diag_draw_batches / sampleCount,
            (double)avp3ds_diag_triangles / sampleCount,
            avp3ds_diag_texture_handles,
            avp3ds_diag_native_success,
            avp3ds_diag_native_failure,
            avp3ds_diag_untextured);

        avp3ds_diag_draw_batches = 0;
        avp3ds_diag_triangles = 0;
        avp3ds_diag_texture_handles = 0;
        avp3ds_diag_native_success = 0;
        avp3ds_diag_native_failure = 0;
        avp3ds_diag_untextured = 0;

        avp3ds_bench_total_frame_ticks = 0;
        avp3ds_bench_total_update_ticks = 0;
        avp3ds_bench_total_render_ticks = 0;
        avp3ds_bench_total_begin_ticks = 0;
        avp3ds_bench_total_end_ticks = 0;

        avp3ds_bench_begin_attempts = 0;
        avp3ds_bench_begin_failures = 0;
    }
}

/*
 * AVP-LOADBAR1 native loading overlay.
 *
 * The original loading code still calculates progress correctly, but its
 * OpenGL-era BltImage path does not survive the native 3DS presentation
 * conversion. Draw one simple neutral progress strip over the already
 * presented software loading frame.
 *
 * fixedPosition is a 16.16 fixed-point value from 0 through 65536.
 */
/*
 * AVP-HUD1B1 stable native loading bar.
 *
 * progress_bar.cpp sets this immediately before presenting the software
 * loading frame. The presenter consumes it in that same GPU frame.
 */
void AvP3DS_SetLoadingProgress(
    int fixedPosition,
    int visible)
{
    if (fixedPosition < 0)
        fixedPosition = 0;

    if (fixedPosition > 65536)
        fixedPosition = 65536;

    avp3ds_loading_progress_fixed = fixedPosition;
    avp3ds_loading_progress_pending = visible != 0;
}

static void AvP3DS_DrawLoadingProgressInCurrentFrame(void)
{
    float fraction;
    float filledWidth;

    const float outerX = 16.0f;
    const float outerY = 216.0f;
    const float outerWidth = 368.0f;
    const float outerHeight = 16.0f;

    const float innerX = 19.0f;
    const float innerY = 219.0f;
    const float innerWidth = 362.0f;
    const float innerHeight = 10.0f;

    if (!avp3ds_loading_progress_pending)
        return;

    fraction =
        (float)avp3ds_loading_progress_fixed /
        65536.0f;

    filledWidth =
        innerWidth * fraction;

    /*
     * These rectangles are submitted into the already-open software
     * presentation frame. There is no intermediate bar-less LCD frame.
     */
    C2D_DrawRectSolid(
        outerX,
        outerY,
        0.5f,
        outerWidth,
        outerHeight,
        C2D_Color32(18, 18, 18, 255));

    C2D_DrawRectSolid(
        innerX,
        innerY,
        0.6f,
        innerWidth,
        innerHeight,
        C2D_Color32(64, 64, 64, 255));

    if (filledWidth > 0.0f)
    {
        C2D_DrawRectSolid(
            innerX,
            innerY,
            0.7f,
            filledWidth,
            innerHeight,
            C2D_Color32(205, 205, 205, 255));

        C2D_DrawRectSolid(
            innerX,
            innerY,
            0.8f,
            filledWidth,
            2.0f,
            C2D_Color32(255, 255, 255, 255));
    }

    /*
     * One-shot state: unrelated later menu frames cannot inherit a stale
     * loading bar. The loader sets it again before every progress update.
     */
    avp3ds_loading_progress_pending = false;
}

void AvP_PresentSurface3DS(SDL_Surface *source)
{
    u16 *texturePixels;
    int contentWidth;
    int contentHeight;
    bool surfaceLocked = false;

    Tex3DS_SubTexture subtexture;
    C2D_Image image;
    C2D_DrawParams parameters;

    float scaleX;
    float scaleY;
    float scale;
    float destinationWidth;
    float destinationHeight;
    float destinationX;
    float destinationY;

    if (!avp3ds_video_ready ||
        source == NULL ||
        source->pixels == NULL ||
        source->format == NULL ||
        source->format->BytesPerPixel != 2 ||
        source->w <= 0 ||
        source->h <= 0)
    {
        return;
    }

    if (avp3ds_c2d_prepare_required)
    {
        /*
         * Gameplay directly configures Citro3D shaders, buffers, TEV,
         * blending and depth state. Re-prepare Citro2D before the next
         * software-menu frame.
         */
        C2D_Prepare();
        avp3ds_c2d_prepare_required = false;
    }

    {
        static bool diagnosticsPrinted = false;

        if (!diagnosticsPrinted)
        {
            printf("\nPresenter reached.\n");
            printf("Surface: %dx%d\n", source->w, source->h);
            printf("Pitch: %d\n", source->pitch);
            printf("BPP: %u\n", source->format->BytesPerPixel);
            printf("Format: 0x%08lX\n",
                   (unsigned long)source->format->format);
            diagnosticsPrinted = true;
        }
    }

    /*
     * Normally AvP supplies 640x480, which fits unchanged inside the
     * 1024x512 texture. Larger modes are proportionally reduced.
     */
    contentWidth = source->w;
    contentHeight = source->h;

    if (contentWidth > AVP3DS_TEXTURE_WIDTH ||
        contentHeight > AVP3DS_TEXTURE_HEIGHT)
    {
        const float widthScale =
            (float)AVP3DS_TEXTURE_WIDTH /
            (float)contentWidth;

        const float heightScale =
            (float)AVP3DS_TEXTURE_HEIGHT /
            (float)contentHeight;

        const float textureScale =
            widthScale < heightScale
                ? widthScale
                : heightScale;

        contentWidth =
            (int)((float)contentWidth * textureScale);

        contentHeight =
            (int)((float)contentHeight * textureScale);
    }

    if (contentWidth <= 0 || contentHeight <= 0)
        return;

    /*
     * Synchronize the previous GPU frame before changing texture memory.
     */
    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
        return;

    if (SDL_MUSTLOCK(source))
    {
        if (SDL_LockSurface(source) < 0)
        {
            C3D_FrameEnd(0);
            return;
        }

        surfaceLocked = true;
    }

    texturePixels = (u16 *)avp3ds_texture.data;

    /*
     * Copy the SDL RGB565 software surface directly into the PICA texture's
     * native tiled/Morton representation.
     */
    for (int destinationY = 0;
         destinationY < contentHeight;
         ++destinationY)
    {
        int sourceY =
            (destinationY * source->h) /
            contentHeight;

        const u16 *sourceRow;

        if (sourceY >= source->h)
            sourceY = source->h - 1;

        sourceRow = (const u16 *)(
            (const u8 *)source->pixels +
            (size_t)sourceY * (size_t)source->pitch
        );

        for (int destinationX = 0;
             destinationX < contentWidth;
             ++destinationX)
        {
            int sourceX =
                (destinationX * source->w) /
                contentWidth;

            size_t tiledOffset;

            if (sourceX >= source->w)
                sourceX = source->w - 1;

            tiledOffset = AvP3DS_TiledOffsetForWidth(
                AVP3DS_TEXTURE_WIDTH,
                (unsigned int)destinationX,
                (unsigned int)destinationY);

            texturePixels[tiledOffset] =
                sourceRow[sourceX];
        }
    }

    if (surfaceLocked)
        SDL_UnlockSurface(source);

    GSPGPU_FlushDataCache(
        avp3ds_texture.data,
        AVP3DS_TEXTURE_BYTES);

    memset(&subtexture, 0, sizeof(subtexture));
    memset(&parameters, 0, sizeof(parameters));

    subtexture.width = (u16)contentWidth;
    subtexture.height = (u16)contentHeight;

    subtexture.left = 0.0f;
    subtexture.right =
        (float)contentWidth /
        (float)AVP3DS_TEXTURE_WIDTH;

    /*
     * Citro2D texture coordinates use a bottom-origin vertical convention.
     */
    subtexture.top = 1.0f;
    subtexture.bottom =
        1.0f -
        ((float)contentHeight /
         (float)AVP3DS_TEXTURE_HEIGHT);

    image.tex = &avp3ds_texture;
    image.subtex = &subtexture;

    /*
     * Preserve the original aspect ratio.
     *
     * A 640x480 AvP frame becomes 320x240 and is centered on the
     * 400x240 upper LCD, matching the successful Citadel presentation.
     */
    scaleX =
        AVP3DS_TOP_WIDTH /
        (float)contentWidth;

    scaleY =
        AVP3DS_TOP_HEIGHT /
        (float)contentHeight;

    scale = scaleX < scaleY ? scaleX : scaleY;

    destinationWidth =
        (float)contentWidth * scale;

    destinationHeight =
        (float)contentHeight * scale;

    destinationX =
        (AVP3DS_TOP_WIDTH - destinationWidth) * 0.5f;

    destinationY =
        (AVP3DS_TOP_HEIGHT - destinationHeight) * 0.5f;

    parameters.pos.x = destinationX;
    parameters.pos.y = destinationY;
    parameters.pos.w = destinationWidth;
    parameters.pos.h = destinationHeight;

    parameters.center.x = 0.0f;
    parameters.center.y = 0.0f;
    parameters.depth = 0.0f;
    parameters.angle = 0.0f;

    C2D_TargetClear(
        avp3ds_top_target,
        C2D_Color32(0, 0, 0, 255));

    C2D_SceneBegin(avp3ds_top_target);

    /*
     * Force the texture unit dirty after every CPU upload. This prevents
     * the PICA200 from displaying cached pixels from an older frame.
     */
    C3D_TexBind(0, &avp3ds_texture);

    C2D_DrawImage(
        image,
        &parameters,
        NULL);

    /*
     * Draw progress in the same GPU frame as the loading image.
     */
    AvP3DS_DrawLoadingProgressInCurrentFrame();

    /*
     * AVP-HUD1A:
     * software-presented menus and loading screens use a blank lower LCD.
     */
    if (avp3ds_bottom_target != NULL)
    {
        C2D_TargetClear(
            avp3ds_bottom_target,
            C2D_Color32(0, 0, 0, 255));

        C2D_SceneBegin(avp3ds_bottom_target);
    }

    C3D_FrameEnd(0);
}


/*
 * AVP-SPLASH1 boot splash:
 * load the embedded Hack-i-Ben texture from RomFS and display it using
 * the already initialized Citro2D/Citro3D top-screen render target.
 */
static bool AvP3DS_ShowBootSplash(void)
{
    C2D_SpriteSheet sheet;
    C2D_Image image;
    C2D_DrawParams parameters;

    float sourceWidth;
    float sourceHeight;
    float scale;
    float destinationWidth;
    float destinationHeight;

    unsigned int frame;
    bool drawSucceeded;

    sheet =
        C2D_SpriteSheetLoad(
            "romfs:/Hack-i-Ben_AVP3DS.t3x");

    if (sheet == NULL)
    {
        printf("Splash sprite sheet load failed.\n");
        return false;
    }

    if (C2D_SpriteSheetCount(sheet) < 1)
    {
        printf("Splash sprite sheet contains no images.\n");
        C2D_SpriteSheetFree(sheet);
        return false;
    }

    image = C2D_SpriteSheetGetImage(sheet, 0);

    if (image.subtex == NULL)
    {
        printf("Splash image metadata missing.\n");
        C2D_SpriteSheetFree(sheet);
        return false;
    }

    sourceWidth = (float)image.subtex->width;
    sourceHeight = (float)image.subtex->height;

    if (sourceWidth <= 0.0f || sourceHeight <= 0.0f)
    {
        printf("Splash image dimensions invalid.\n");
        C2D_SpriteSheetFree(sheet);
        return false;
    }

    /*
     * Preserve the original aspect ratio and center the artwork.
     * Exact 400x240 artwork therefore fills the complete top screen.
     */
    scale = AVP3DS_TOP_WIDTH / sourceWidth;

    if ((sourceHeight * scale) > AVP3DS_TOP_HEIGHT)
        scale = AVP3DS_TOP_HEIGHT / sourceHeight;

    destinationWidth = sourceWidth * scale;
    destinationHeight = sourceHeight * scale;

    memset(&parameters, 0, sizeof(parameters));

    parameters.pos.x =
        (AVP3DS_TOP_WIDTH - destinationWidth) * 0.5f;

    parameters.pos.y =
        (AVP3DS_TOP_HEIGHT - destinationHeight) * 0.5f;

    parameters.pos.w = destinationWidth;
    parameters.pos.h = destinationHeight;

    parameters.center.x = 0.0f;
    parameters.center.y = 0.0f;
    parameters.depth = 0.0f;
    parameters.angle = 0.0f;

    C2D_Prepare();

    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
    {
        printf("Splash frame begin failed.\n");
        C2D_SpriteSheetFree(sheet);
        return false;
    }

    C2D_TargetClear(
        avp3ds_top_target,
        C2D_Color32(0, 0, 0, 255));

    C2D_SceneBegin(avp3ds_top_target);

    drawSucceeded =
        C2D_DrawImage(
            image,
            &parameters,
            NULL);

    C3D_FrameEnd(0);

    /*
     * Keep aptMainLoop active during the approximately two-second display.
     */
    for (frame = 0; frame < 120 && aptMainLoop(); ++frame)
        gspWaitForVBlank();

    C2D_SpriteSheetFree(sheet);

    if (!drawSucceeded)
        printf("Splash draw submission failed.\n");

    return drawSucceeded;
}

int main(int argc, char **argv)
{
    bool avp3ds_romfs_ready;
    int result = EXIT_FAILURE;

    gfxInitDefault();

    /*
     * AVP-SPLASH2 direct startup:
     * no temporary console and no A-button bootstrap.
     */
    avp3ds_romfs_ready =
        R_SUCCEEDED(romfsInit());

    osSetSpeedupEnable(true);

    if (AvP3DS_VideoInit())
    {
        if (avp3ds_romfs_ready)
            AvP3DS_ShowBootSplash();

        result = AvP_LegacyMain(argc, argv);
    }

    AvP3DS_VideoShutdown();

    if (avp3ds_romfs_ready)
        romfsExit();

    gfxExit();
    return result;
}
