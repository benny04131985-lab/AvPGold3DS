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

#define AVP3DS_TRACKER_CAPTURE_MAX_BATCHES 128U
#define AVP3DS_BOTTOM_GRID_MAX_VERTICES   2048U

#define AVP3DS_MARINE_HUD_SOURCE_WIDTH     320U
#define AVP3DS_MARINE_HUD_SOURCE_HEIGHT    240U
#define AVP3DS_MARINE_HUD_TEXTURE_WIDTH    512U
#define AVP3DS_MARINE_HUD_TEXTURE_HEIGHT   256U
#define AVP3DS_MARINE_HUD_RAW_BYTES \
    ((size_t)AVP3DS_MARINE_HUD_SOURCE_WIDTH * \
     (size_t)AVP3DS_MARINE_HUD_SOURCE_HEIGHT * 4U)

typedef struct AvP3DS_GameVertex
{
    float position[4];
    float texcoord[2];
    float primary[4];
} AvP3DS_GameVertex;

/*
 * AVP-STEREO-S2C2A1-CMDBUF-512K.
 *
 * Stereo's duplicated draws and per-eye uniform changes can
 * exceed Citro3D's 0x40000-byte default during peak effects.
 */
#define AVP3DS_C3D_CMDBUF_SIZE (C3D_DEFAULT_CMDBUF_SIZE * 2U)

static bool avp3ds_c3d_initialized = false;
static bool avp3ds_c2d_initialized = false;
static bool avp3ds_texture_initialized = false;
static bool avp3ds_video_ready = false;

static C3D_RenderTarget *avp3ds_top_target = NULL;
/* AVP-STEREO-S1A2-RIGHT-EYE-PROOF: independent top-right diagnostic target. */
static C3D_RenderTarget *avp3ds_top_right_target = NULL;
static C3D_RenderTarget *avp3ds_bottom_target = NULL;
static C3D_Tex avp3ds_texture;

/* AVP-HUD1D native Citro3D Marine backdrop. */
static C3D_Tex avp3ds_marine_hud_texture;
static bool avp3ds_marine_hud_texture_initialized = false;

/* PRED-HUD1C3-NATIVE-BACKDROP: independent Predator lower-screen backdrop. */
static C3D_Tex avp3ds_predator_hud_texture;
static bool avp3ds_predator_hud_texture_initialized = false;

/* ALIEN-HUD1C3-NATIVE-BACKDROP: independent Alien lower-screen backdrop. */
static C3D_Tex avp3ds_alien_hud_texture;
static bool avp3ds_alien_hud_texture_initialized = false;

static shaderProgram_s avp3ds_game_program;
static DVLB_s *avp3ds_game_dvlb = NULL;
static bool avp3ds_game_shader_ready = false;

static int avp3ds_game_projection_location = -1;
static C3D_Mtx avp3ds_game_projection;

/*
 * AVP-STEREO-S2B-DEPTH-WARP.
 *
 * AvP's source vertices retain homogeneous view depth in W. A small matrix
 * shear therefore produces real depth-dependent disparity without rebuilding
 * the original camera or touching the completed lower-screen path.
 */
static C3D_Mtx avp3ds_game_projection_right;

/*
 * AVP-STEREO-S2C1A2-FLAT-STATES.
 *
 * The game loop sets this immediately before native frame
 * creation. A flat frame keeps 3D display mode active but
 * gives the right eye the same projection as the left.
 */
static bool avp3ds_stereo_flat_frame = false;

/*
 * AVP-STEREO-S2C2-FLAT-SIGHTS: applies only to explicitly isolated sight batches.
 */
static bool avp3ds_stereo_flat_batch = false;

/*
 * Conservative S2B proof tuning.
 *
 * At full slider, the nearest legal geometry can reach roughly ten physical
 * top-screen pixels of right-eye disparity, while geometry near the selected
 * convergence depth remains almost coincident.
 */
#define AVP3DS_STEREO_MAX_STRENGTH       0.025f
#define AVP3DS_STEREO_CONVERGENCE_NDC    0.970f

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

/* AVP-SHIP1-PERFLOG1A3: fresh hardware benchmark capture. */
/* AVP-PERFLOG-UNIQUEPATH1:
 * Hardware logs must never reuse a filename.  The SD/FTP workflow can surface
 * an older host-side file when a pathname is reused, so generate one pathname
 * per application launch and let every existing PERFLOG fopen() use it.
 */
static char avp3ds_ship1_perf_log_path[128];

static const char *AvP3DS_Ship1PerfLogPath(void)
{
    if (avp3ds_ship1_perf_log_path[0] == '\0')
    {
        const unsigned long long session_ms =
            (unsigned long long)osGetTime();
        const unsigned long tick_low =
            (unsigned long)(svcGetSystemTick() & 0xffffffffULL);

        snprintf(
            avp3ds_ship1_perf_log_path,
            sizeof(avp3ds_ship1_perf_log_path),
            "sdmc:/AVP_SHIP1_PERF_%llu_%08lx.log",
            session_ms,
            tick_low);
    }

    return avp3ds_ship1_perf_log_path;
}

#define AVP3DS_SHIP1_PERF_LOG_PATH AvP3DS_Ship1PerfLogPath()
static bool avp3ds_ship1_perf_log_initialized = false;

/* AVP-HEADROOM2-SUBBUCKET1A3: diagnostic subsystem timing totals. */
#define AVP3DS_HEADROOM_BUCKET_COUNT 18
static u64 avp3ds_headroom_ticks[AVP3DS_HEADROOM_BUCKET_COUNT];
static u32 avp3ds_headroom_calls[AVP3DS_HEADROOM_BUCKET_COUNT];

unsigned long long AvP3DS_HeadroomTick(void)
{
    return (unsigned long long)svcGetSystemTick();
}

void AvP3DS_HeadroomAccumulate(int bucket, unsigned long long startTick)
{
    if (bucket >= 0 && bucket < AVP3DS_HEADROOM_BUCKET_COUNT)
    {
        const u64 finishTick = svcGetSystemTick();
        avp3ds_headroom_ticks[bucket] += finishTick - (u64)startTick;
        ++avp3ds_headroom_calls[bucket];
    }
}

static double AvP3DS_HeadroomMsPerFrame(int bucket, double sampleCount)
{
    if (bucket < 0 || bucket >= AVP3DS_HEADROOM_BUCKET_COUNT)
        return 0.0;

    return ((double)avp3ds_headroom_ticks[bucket] / sampleCount) /
        CPU_TICKS_PER_MSEC;
}

static double AvP3DS_HeadroomCallsPerFrame(int bucket, double sampleCount)
{
    if (bucket < 0 || bucket >= AVP3DS_HEADROOM_BUCKET_COUNT)
        return 0.0;

    return (double)avp3ds_headroom_calls[bucket] / sampleCount;
}

static void AvP3DS_WriteHeadroomReport(FILE *stream, u32 frame, double sampleCount)
{
    if (stream == NULL)
        return;

    fprintf(
        stream,
        "HEADROOM2A3 frame=%lu\n"
        "U shape=%.2f pherP=%.2f pherA=%.2f hive=%.2f squad=%.2f "
        "obj=%.2f player=%.2f dyn=%.2f\n"
        "U netC=%.2f netS=%.2f sndMgmt=%.2f plySnd=%.2f\n"
        "P view=%.2f hud=%.2f input=%.2f sound=%.2f menu=%.2f finish=%.2f\n"
        "C netC=%.2f netS=%.2f menuProbe=%.2f\n",
        (unsigned long)frame,
        AvP3DS_HeadroomMsPerFrame(0, sampleCount),
        AvP3DS_HeadroomMsPerFrame(1, sampleCount),
        AvP3DS_HeadroomMsPerFrame(2, sampleCount),
        AvP3DS_HeadroomMsPerFrame(3, sampleCount),
        AvP3DS_HeadroomMsPerFrame(4, sampleCount),
        AvP3DS_HeadroomMsPerFrame(5, sampleCount),
        AvP3DS_HeadroomMsPerFrame(6, sampleCount),
        AvP3DS_HeadroomMsPerFrame(7, sampleCount),
        AvP3DS_HeadroomMsPerFrame(8, sampleCount),
        AvP3DS_HeadroomMsPerFrame(9, sampleCount),
        AvP3DS_HeadroomMsPerFrame(10, sampleCount),
        AvP3DS_HeadroomMsPerFrame(11, sampleCount),
        AvP3DS_HeadroomMsPerFrame(12, sampleCount),
        AvP3DS_HeadroomMsPerFrame(13, sampleCount),
        AvP3DS_HeadroomMsPerFrame(14, sampleCount),
        AvP3DS_HeadroomMsPerFrame(15, sampleCount),
        AvP3DS_HeadroomMsPerFrame(16, sampleCount),
        AvP3DS_HeadroomMsPerFrame(17, sampleCount),
        AvP3DS_HeadroomCallsPerFrame(8, sampleCount),
        AvP3DS_HeadroomCallsPerFrame(9, sampleCount),
        AvP3DS_HeadroomCallsPerFrame(16, sampleCount));
}

static void AvP3DS_ResetHeadroomReport(void)
{
    int i;
    for (i = 0; i < AVP3DS_HEADROOM_BUCKET_COUNT; ++i)
    {
        avp3ds_headroom_ticks[i] = 0;
        avp3ds_headroom_calls[i] = 0;
    }
}


typedef struct AvP3DS_NativeTexture
{
    C3D_Tex texture;
    unsigned int sourceWidth;
    unsigned int sourceHeight;
    unsigned int textureWidth;
    unsigned int textureHeight;
    bool initialized;
} AvP3DS_NativeTexture;

typedef struct AvP3DS_TrackerCaptureBatch
{
    size_t vertexOffset;
    size_t vertexCount;
    AvP3DS_NativeTexture *nativeTexture;
    int translucencyMode;
    int hudGroup;
} AvP3DS_TrackerCaptureBatch;

typedef struct AvP3DS_HUDLayoutBox
{
    float x;
    float y;
    float width;
    float height;
    float maximumScale;
} AvP3DS_HUDLayoutBox;

#define AVP3DS_HUD_GROUP_TRACKER   0
#define AVP3DS_HUD_GROUP_STATUS    1
#define AVP3DS_HUD_GROUP_AMMO      2
#define AVP3DS_HUD_GROUP_MESSAGES    3
#define AVP3DS_HUD_GROUP_PRED_WRIST  4
#define AVP3DS_HUD_GROUP_PRED_STATUS   5
#define AVP3DS_HUD_GROUP_PRED_MESSAGES 6
#define AVP3DS_HUD_GROUP_PRED_AMMO     7
#define AVP3DS_HUD_GROUP_ALIEN_STATUS   8
#define AVP3DS_HUD_GROUP_ALIEN_MESSAGES 9
#define AVP3DS_HUD_GROUP_COUNT          10
/*
 * AVP-HUD1C Marine lower-screen layout groups.
 * Coordinates are measured directly against the 320x240 diagnostic grid.
 */
static const AvP3DS_HUDLayoutBox avp3ds_hud_layout_boxes[
    AVP3DS_HUD_GROUP_COUNT] =
{
    /*
     * AVP-HUD1E geometry-first Marine layout.
     *
     * These positions intentionally ignore the current decorative panel.
     * They create four clearly separated, practical HUD regions so the
     * final backdrop can be drawn around the proven live placements.
     */

     /* Motion tracker: slightly right and farther up. */
     { 84.0f, 48.0f, 144.0f, 88.0f, 1.35f },

     /* Health and armor: move up another 14 pixels. */
     { 10.0f, 136.0f, 86.0f, 78.0f, 1.00f },

     /* Ammunition: move up another 14 pixels. */
     { 208.0f, 136.0f, 112.0f, 78.0f, 0.82f },

     /* Mission/objective text: move to the top of its range. */
     { 40.0f, 12.0f, 240.0f, 32.0f, 0.86f },

    /* PRED-HUD1A: Predator wrist diagnostic box. */
    { 80.0f, 72.0f, 160.0f, 96.0f, 1.00f },

    /* PRED-HUD1A: Predator health/energy full-screen rails. */
    { 0.0f, 0.0f, 320.0f, 240.0f, 1.00f },

    /* PRED-HUD1B2: Predator ticker in second checker row. */
    { 40.0f, 16.0f, 240.0f, 32.0f, 0.86f },

    /* PRED-HUD1F: spear-gun ammo in center red box. */
    { 116.0f, 72.0f, 88.0f, 32.0f, 1.25f },

    /* ALIEN-HUD1A: Alien health along the lower edge. */
    { 64.0f, 170.0f, 192.0f, 44.0f, 1.00f },

    /* ALIEN-HUD1D-RAISE-MSG-HEALTH: Alien ticker raised to fit the amber panel. */
    { 40.0f, 8.0f, 240.0f, 32.0f, 0.86f }
};

/*
 * AVP-HUD1B1 tracker-only capture and replay.
 *
 * The tracker executes once on the normal top-screen path. We retain the
 * already-expanded live GPU batches and replay those same batches at the
 * end of the frame on the lower diagnostic screen.
 */
static bool avp3ds_tracker_capture_enabled = false;
static bool avp3ds_tracker_capture_overflow = false;
static size_t avp3ds_tracker_capture_count = 0;
static int avp3ds_hud_capture_group = -1;

static AvP3DS_TrackerCaptureBatch
    avp3ds_tracker_capture_batches[
        AVP3DS_TRACKER_CAPTURE_MAX_BATCHES];

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

void AvP3DS_SetHUDCaptureGroup(int group)
{
    if (group < 0 || group >= AVP3DS_HUD_GROUP_COUNT)
    {
        avp3ds_tracker_capture_enabled = false;
        avp3ds_hud_capture_group = -1;
        return;
    }

    avp3ds_hud_capture_group = group;
    avp3ds_tracker_capture_enabled = true;
}

void AvP3DS_BenchmarkFrameStart(void)
{

/*
 * AVP-SHIP1-PERFLOG1A3:
 * One fresh performance log per application launch.
 */
if (!avp3ds_ship1_perf_log_initialized)
{
    FILE *avp3ds_ship1_perf_file =
        fopen(AVP3DS_SHIP1_PERF_LOG_PATH, "w");

    avp3ds_ship1_perf_log_initialized = true;

    if (avp3ds_ship1_perf_file != NULL)
    {
        fprintf(
            avp3ds_ship1_perf_file,
            "AVP-SHIP1-PERFLOG1A3\n"
            "Fresh session begin\n"
            "120-frame performance windows follow.\n\n");
        fclose(avp3ds_ship1_perf_file);
    }
}


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
     * AVP-MONO1-SELECT-QUIT-DISABLED.
     *
     * The temporary SELECT -> immediate application exit is disabled for
     * MONO v1.0. SELECT remains mapped by AvP3DS_ButtonsHeld() so it can be
     * assigned a future in-game function without restoring the quit path.
     */
    return 0;
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

/*
 * AVP-HUD1D native Citro3D Marine backdrop.
 *
 * The background is loaded as raw RGBA and tiled directly into a PICA
 * texture. No Citro2D draw calls are issued during gameplay, so this
 * resource cannot leak into the later software loading presenter.
 */
static bool AvP3DS_LoadMarineHUDTexture(void)
{
    FILE *file;
    unsigned char *pixels;
    size_t bytesRead;
    unsigned int y;

    memset(
        &avp3ds_marine_hud_texture,
        0,
        sizeof(avp3ds_marine_hud_texture));

    file = fopen(
        "romfs:/Marine_WY_HUD.rgba",
        "rb");

    if (file == NULL)
    {
        printf("Marine HUD raw backdrop was not found.\n");
        return false;
    }

    pixels = malloc(AVP3DS_MARINE_HUD_RAW_BYTES);

    if (pixels == NULL)
    {
        fclose(file);
        printf("Marine HUD raw backdrop allocation failed.\n");
        return false;
    }

    bytesRead = fread(
        pixels,
        1,
        AVP3DS_MARINE_HUD_RAW_BYTES,
        file);

    fclose(file);

    if (bytesRead != AVP3DS_MARINE_HUD_RAW_BYTES)
    {
        free(pixels);
        printf(
            "Marine HUD raw backdrop was truncated: %lu bytes.\n",
            (unsigned long)bytesRead);
        return false;
    }

    if (!C3D_TexInit(
            &avp3ds_marine_hud_texture,
            AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
            AVP3DS_MARINE_HUD_TEXTURE_HEIGHT,
            GPU_RGBA8))
    {
        free(pixels);
        printf("Marine HUD native texture initialization failed.\n");
        return false;
    }

    avp3ds_marine_hud_texture_initialized = true;

    C3D_TexSetFilter(
        &avp3ds_marine_hud_texture,
        GPU_NEAREST,
        GPU_NEAREST);

    C3D_TexSetWrap(
        &avp3ds_marine_hud_texture,
        GPU_CLAMP_TO_EDGE,
        GPU_CLAMP_TO_EDGE);

    memset(
        avp3ds_marine_hud_texture.data,
        0,
        avp3ds_marine_hud_texture.size);

    for (y = 0;
         y < AVP3DS_MARINE_HUD_SOURCE_HEIGHT;
         ++y)
    {
        unsigned int x;

        for (x = 0;
             x < AVP3DS_MARINE_HUD_SOURCE_WIDTH;
             ++x)
        {
            const unsigned char *sourcePixel =
                pixels +
                (((size_t)y *
                  AVP3DS_MARINE_HUD_SOURCE_WIDTH +
                  x) * 4U);

            unsigned char *destinationPixel =
                (unsigned char *)
                    avp3ds_marine_hud_texture.data +
                AvP3DS_TiledOffsetForWidth(
                    AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
                    x,
                    y) * 4U;

            /* CPU RGBA -> PICA RGBA8 byte order A, B, G, R. */
            destinationPixel[0] = sourcePixel[3];
            destinationPixel[1] = sourcePixel[2];
            destinationPixel[2] = sourcePixel[1];
            destinationPixel[3] = sourcePixel[0];
        }
    }

    free(pixels);

    GSPGPU_FlushDataCache(
        avp3ds_marine_hud_texture.data,
        avp3ds_marine_hud_texture.size);

    return true;
}

/*
 * PRED-HUD1C3-NATIVE-BACKDROP.
 *
 * Deliberately separate from the sealed Marine texture lifecycle.
 * The asset shares the same 320x240 source and 512x256 PICA texture sizes.
 */
static bool AvP3DS_LoadPredatorHUDTexture(void)
{
    FILE *file;
    unsigned char *pixels;
    size_t bytesRead;
    unsigned int y;

    memset(
        &avp3ds_predator_hud_texture,
        0,
        sizeof(avp3ds_predator_hud_texture));

    file = fopen(
        "romfs:/Predator_HUD_Backdrop_320x240.rgba",
        "rb");

    if (file == NULL)
    {
        printf("Predator HUD raw backdrop was not found.\n");
        return false;
    }

    pixels = malloc(AVP3DS_MARINE_HUD_RAW_BYTES);

    if (pixels == NULL)
    {
        fclose(file);
        printf("Predator HUD raw backdrop allocation failed.\n");
        return false;
    }

    bytesRead = fread(
        pixels,
        1,
        AVP3DS_MARINE_HUD_RAW_BYTES,
        file);

    fclose(file);

    if (bytesRead != AVP3DS_MARINE_HUD_RAW_BYTES)
    {
        free(pixels);
        printf(
            "Predator HUD raw backdrop was truncated: %lu bytes.\n",
            (unsigned long)bytesRead);
        return false;
    }

    if (!C3D_TexInit(
            &avp3ds_predator_hud_texture,
            AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
            AVP3DS_MARINE_HUD_TEXTURE_HEIGHT,
            GPU_RGBA8))
    {
        free(pixels);
        printf("Predator HUD native texture initialization failed.\n");
        return false;
    }

    avp3ds_predator_hud_texture_initialized = true;

    C3D_TexSetFilter(
        &avp3ds_predator_hud_texture,
        GPU_NEAREST,
        GPU_NEAREST);

    C3D_TexSetWrap(
        &avp3ds_predator_hud_texture,
        GPU_CLAMP_TO_EDGE,
        GPU_CLAMP_TO_EDGE);

    memset(
        avp3ds_predator_hud_texture.data,
        0,
        avp3ds_predator_hud_texture.size);

    for (y = 0;
         y < AVP3DS_MARINE_HUD_SOURCE_HEIGHT;
         ++y)
    {
        unsigned int x;

        for (x = 0;
             x < AVP3DS_MARINE_HUD_SOURCE_WIDTH;
             ++x)
        {
            const unsigned char *sourcePixel =
                pixels +
                (((size_t)y *
                  AVP3DS_MARINE_HUD_SOURCE_WIDTH +
                  x) * 4U);

            unsigned char *destinationPixel =
                (unsigned char *)
                    avp3ds_predator_hud_texture.data +
                AvP3DS_TiledOffsetForWidth(
                    AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
                    x,
                    y) * 4U;

            /* CPU RGBA -> PICA RGBA8 byte order A, B, G, R. */
            destinationPixel[0] = sourcePixel[3];
            destinationPixel[1] = sourcePixel[2];
            destinationPixel[2] = sourcePixel[1];
            destinationPixel[3] = sourcePixel[0];
        }
    }

    free(pixels);

    GSPGPU_FlushDataCache(
        avp3ds_predator_hud_texture.data,
        avp3ds_predator_hud_texture.size);

    return true;
}

static bool AvP3DS_LoadAlienHUDTexture(void)
{
    FILE *file;
    unsigned char *pixels;
    size_t bytesRead;
    unsigned int y;

    memset(
        &avp3ds_alien_hud_texture,
        0,
        sizeof(avp3ds_alien_hud_texture));

    file = fopen(
        "romfs:/Alien_HUD_Backdrop_320x240.rgba",
        "rb");

    if (file == NULL)
    {
        printf("Alien HUD raw backdrop was not found.\n");
        return false;
    }

    pixels = malloc(AVP3DS_MARINE_HUD_RAW_BYTES);

    if (pixels == NULL)
    {
        fclose(file);
        printf("Alien HUD raw backdrop allocation failed.\n");
        return false;
    }

    bytesRead = fread(
        pixels,
        1,
        AVP3DS_MARINE_HUD_RAW_BYTES,
        file);

    fclose(file);

    if (bytesRead != AVP3DS_MARINE_HUD_RAW_BYTES)
    {
        free(pixels);
        printf(
            "Alien HUD raw backdrop was truncated: %lu bytes.\n",
            (unsigned long)bytesRead);
        return false;
    }

    if (!C3D_TexInit(
            &avp3ds_alien_hud_texture,
            AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
            AVP3DS_MARINE_HUD_TEXTURE_HEIGHT,
            GPU_RGBA8))
    {
        free(pixels);
        printf("Alien HUD native texture initialization failed.\n");
        return false;
    }

    avp3ds_alien_hud_texture_initialized = true;

    C3D_TexSetFilter(
        &avp3ds_alien_hud_texture,
        GPU_NEAREST,
        GPU_NEAREST);

    C3D_TexSetWrap(
        &avp3ds_alien_hud_texture,
        GPU_CLAMP_TO_EDGE,
        GPU_CLAMP_TO_EDGE);

    memset(
        avp3ds_alien_hud_texture.data,
        0,
        avp3ds_alien_hud_texture.size);

    for (y = 0;
         y < AVP3DS_MARINE_HUD_SOURCE_HEIGHT;
         ++y)
    {
        unsigned int x;

        for (x = 0;
             x < AVP3DS_MARINE_HUD_SOURCE_WIDTH;
             ++x)
        {
            const unsigned char *sourcePixel =
                pixels +
                (((size_t)y *
                  AVP3DS_MARINE_HUD_SOURCE_WIDTH +
                  x) * 4U);

            unsigned char *destinationPixel =
                (unsigned char *)
                    avp3ds_alien_hud_texture.data +
                AvP3DS_TiledOffsetForWidth(
                    AVP3DS_MARINE_HUD_TEXTURE_WIDTH,
                    x,
                    y) * 4U;

            /* CPU RGBA -> PICA RGBA8 byte order A, B, G, R. */
            destinationPixel[0] = sourcePixel[3];
            destinationPixel[1] = sourcePixel[2];
            destinationPixel[2] = sourcePixel[1];
            destinationPixel[3] = sourcePixel[0];
        }
    }

    free(pixels);

    GSPGPU_FlushDataCache(
        avp3ds_alien_hud_texture.data,
        avp3ds_alien_hud_texture.size);

    return true;
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
    /*
     * AVP-STEREO-S1A2-RIGHT-EYE-PROOF: disable stereo output and release the independent right eye
     * before the shared Citro3D shutdown continues.
     */
    gfxSet3D(false);

    if (avp3ds_top_right_target != NULL)
    {
        C3D_RenderTargetDelete(avp3ds_top_right_target);
        avp3ds_top_right_target = NULL;
    }

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

    if (avp3ds_marine_hud_texture_initialized)
    {
        C3D_TexDelete(&avp3ds_marine_hud_texture);
        memset(
            &avp3ds_marine_hud_texture,
            0,
            sizeof(avp3ds_marine_hud_texture));
        avp3ds_marine_hud_texture_initialized = false;
    }

    /* PRED-HUD1C3-NATIVE-BACKDROP: Predator texture cleanup is independent. */
    if (avp3ds_predator_hud_texture_initialized)
    {
        C3D_TexDelete(&avp3ds_predator_hud_texture);
        memset(
            &avp3ds_predator_hud_texture,
            0,
            sizeof(avp3ds_predator_hud_texture));
        avp3ds_predator_hud_texture_initialized = false;
    }

    if (avp3ds_alien_hud_texture_initialized)
    {
        C3D_TexDelete(&avp3ds_alien_hud_texture);
        memset(
            &avp3ds_alien_hud_texture,
            0,
            sizeof(avp3ds_alien_hud_texture));
        avp3ds_alien_hud_texture_initialized = false;
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

    if (!C3D_Init(AVP3DS_C3D_CMDBUF_SIZE))
    {
        printf("C3D_Init failed.\n");
        return false;
    }

    avp3ds_c3d_initialized = true;

    printf("AvP3DS Citro3D command buffer: 512 KiB.\n");

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

    /*
     * Start with exact left/right parity. GameFrameBegin rebuilds the right
     * matrix from this immutable base whenever the physical slider changes.
     */
    Mtx_Copy(
        &avp3ds_game_projection_right,
        &avp3ds_game_projection);

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

    if (!AvP3DS_LoadMarineHUDTexture())
    {
        printf(
            "Marine HUD native backdrop unavailable; "
            "keeping diagnostic grid.\n");
    }

    if (!AvP3DS_LoadPredatorHUDTexture())
    {
        printf(
            "Predator HUD native backdrop unavailable; "
            "keeping diagnostic grid.\n");
    }

    if (!AvP3DS_LoadAlienHUDTexture())
    {
        printf(
            "Alien HUD native backdrop unavailable; "
            "keeping diagnostic grid.\n");
    }

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
     * AVP-STEREO-S1A2-RIGHT-EYE-PROOF.
     *
     * Match the proven left-eye target dimensions and depth format, but send
     * this target to the physical right eye. S1 intentionally renders only a
     * magenta diagnostic clear here; real eye-separated world projection
     * comes later.
     */
    avp3ds_top_right_target =
        C3D_RenderTargetCreate(
            240,
            400,
            GPU_RB_RGBA8,
            GPU_RB_DEPTH24_STENCIL8);

    if (avp3ds_top_right_target != NULL)
    {
        C3D_RenderTargetSetOutput(
            avp3ds_top_right_target,
            GFX_TOP,
            GFX_RIGHT,
            AVP3DS_DISPLAY_TRANSFER_FLAGS);
    }

    if (avp3ds_top_right_target == NULL)
    {
        printf("C2D top-right stereo target failed.\n");
        AvP3DS_VideoShutdown();
        return false;
    }

    gfxSet3D(true);

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

void AvP3DS_SetStereoFlatFrame(int flat)
{
    /* AVP-STEREO-S2C1A2-FLAT-STATES: collapse only stereo disparity. */
    avp3ds_stereo_flat_frame = (flat != 0);
}

void AvP3DS_SetStereoFlatBatch(int flat)
{
    /* AVP-STEREO-S2C2-FLAT-SIGHTS: collapse disparity for one flushed batch range. */
    avp3ds_stereo_flat_batch = (flat != 0);
}

void AvP3DS_GameFrameBegin(void)
{
    C3D_AttrInfo *attributeInfo;
    C3D_TexEnv *textureEnvironment;
    int stage;

    if (!avp3ds_video_ready ||
        !avp3ds_game_shader_ready ||
        avp3ds_top_target == NULL ||
        avp3ds_top_right_target == NULL ||
        avp3ds_bottom_target == NULL ||
        avp3ds_game_frame_active)
    {
        return;
    }

    avp3ds_hud_seen_this_frame = false;
    avp3ds_tracker_capture_enabled = false;
    avp3ds_tracker_capture_overflow = false;
    avp3ds_tracker_capture_count = 0;
    avp3ds_hud_capture_group = -1;

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

    /*
     * AVP-STEREO-S2B-DEPTH-WARP: build one right-eye matrix per accepted gameplay frame.
     *
     * The warp is applied before the existing tilted orthographic pass:
     *
     *   x' = x + strength * (z - convergence*w)
     *
     * Since z/w is AvP's normalized depth, this is zero at the convergence
     * plane and changes smoothly with actual scene depth.
     */
    {
        C3D_Mtx stereoWarp;
        const float stereoSlider =
            osGet3DSliderState();

        const float stereoStrength =
            avp3ds_stereo_flat_frame
                ? 0.0f
                : stereoSlider *
                  AVP3DS_STEREO_MAX_STRENGTH;

        Mtx_Identity(&stereoWarp);

        stereoWarp.r[0].z =
            stereoStrength;

        stereoWarp.r[0].w =
            -stereoStrength *
            AVP3DS_STEREO_CONVERGENCE_NDC;

        Mtx_Multiply(
            &avp3ds_game_projection_right,
            &avp3ds_game_projection,
            &stereoWarp);
    }

    /*
     * AVP-STEREO-S2A-DUAL-EYE-PARITY.
     *
     * The right eye now starts as a normal black gameplay target. Ordinary
     * upper-screen batches are duplicated to it by AvP3DS_DrawTriangles().
     * Both eyes still use the exact same projection in S2A.
     */
    C3D_RenderTargetClear(
        avp3ds_top_right_target,
        C3D_CLEAR_ALL,
        0x000000FF,
        0);

    if (!C3D_FrameDrawOn(avp3ds_top_right_target))
    {
        C3D_FrameEnd(0);
        avp3ds_game_frame_active = false;
        return;
    }

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

void AvP3DS_DrawTriangles(
    const void *vertices,
    int vertexCount,
    const void *triangles,
    int triangleCount,
    void *textureHandle,
    int translucencyMode)
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

    const AvP3DS_SourceVertex *sourceVertices;
    const AvP3DS_SourceTriangle *sourceTriangles;

    AvP3DS_GameVertex *destination;
    AvP3DS_NativeTexture *nativeTexture;

    size_t expandedVertexCount;
    size_t writtenVertexCount;
    size_t triangleIndex;
    size_t batchVertexOffset;

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

    batchVertexOffset = avp3ds_game_vertex_cursor;

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
     * AVP-HUD1F: captured Marine HUD geometry belongs on the lower
     * screen only. Preserve the vertices and capture metadata for
     * lower-screen replay, but do not submit them to the upper target.
     */
    /*
     * PRED-HUD1H-UPPER-SUPPRESS.
     *
     * Every captured lower-screen HUD group is now lower-screen-only.
     * This suppresses the completed Marine and Predator HUD elements upstairs
     * while preserving world geometry, weapon models, sights, vision effects,
     * scanlines, and all non-captured rendering.
     */
    if (!avp3ds_tracker_capture_enabled)
    {
        /*
         * AVP-STEREO-S2A-DUAL-EYE-PARITY: submit this exact expanded batch to both eyes.
         *
         * Texture binding, TexEnv, blending, alpha/depth state, and the vertex
         * buffer were configured immediately above and remain valid while the
         * physical render target changes.
         */
        C3D_DrawArrays(
            GPU_TRIANGLES,
            0,
            (int)writtenVertexCount);

        if (C3D_FrameDrawOn(avp3ds_top_right_target))
        {
            /*
             * AVP-STEREO-S2B-DEPTH-WARP: the command list records this right-eye matrix with the
             * following draw. No CPU vertex-buffer mutation is required.
             */
            C3D_FVUnifMtx4x4(
                GPU_VERTEX_SHADER,
                avp3ds_game_projection_location,
                avp3ds_stereo_flat_batch
                    ? &avp3ds_game_projection
                    : &avp3ds_game_projection_right);

            C3D_DrawArrays(
                GPU_TRIANGLES,
                0,
                (int)writtenVertexCount);
        }

        /*
         * Every legacy call expects subsequent gameplay batches to continue on
         * the normal left target with the original projection.
         */
        C3D_FrameDrawOn(avp3ds_top_target);

        C3D_FVUnifMtx4x4(
            GPU_VERTEX_SHADER,
            avp3ds_game_projection_location,
            &avp3ds_game_projection);
    }

    if (avp3ds_tracker_capture_enabled)
    {
        if (avp3ds_tracker_capture_count <
            AVP3DS_TRACKER_CAPTURE_MAX_BATCHES)
        {
            AvP3DS_TrackerCaptureBatch *capture =
                &avp3ds_tracker_capture_batches[
                    avp3ds_tracker_capture_count++];

            capture->vertexOffset = batchVertexOffset;
            capture->vertexCount = writtenVertexCount;
            capture->nativeTexture = nativeTexture;
            capture->translucencyMode = translucencyMode;
            capture->hudGroup = avp3ds_hud_capture_group;
        }
        else
        {
            avp3ds_tracker_capture_overflow = true;
        }
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
static void AvP3DS_SetBottomVertex(
    AvP3DS_GameVertex *vertex,
    float pixelX,
    float pixelY,
    unsigned int red,
    unsigned int green,
    unsigned int blue,
    unsigned int alpha)
{
    vertex->position[0] =
        pixelX / (AVP3DS_BOTTOM_WIDTH * 0.5f) - 1.0f;

    vertex->position[1] =
        1.0f - pixelY / (AVP3DS_BOTTOM_HEIGHT * 0.5f);

    vertex->position[2] = 0.0f;
    vertex->position[3] = 1.0f;

    vertex->texcoord[0] = 0.0f;
    vertex->texcoord[1] = 0.0f;

    vertex->primary[0] = (float)red / 255.0f;
    vertex->primary[1] = (float)green / 255.0f;
    vertex->primary[2] = (float)blue / 255.0f;
    vertex->primary[3] = (float)alpha / 255.0f;
}

static void AvP3DS_AppendBottomRect(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount,
    float x,
    float y,
    float width,
    float height,
    unsigned int red,
    unsigned int green,
    unsigned int blue,
    unsigned int alpha)
{
    AvP3DS_GameVertex *output =
        vertices + *vertexCount;

    AvP3DS_SetBottomVertex(
        &output[0], x, y,
        red, green, blue, alpha);

    AvP3DS_SetBottomVertex(
        &output[1], x + width, y,
        red, green, blue, alpha);

    AvP3DS_SetBottomVertex(
        &output[2], x + width, y + height,
        red, green, blue, alpha);

    AvP3DS_SetBottomVertex(
        &output[3], x, y,
        red, green, blue, alpha);

    AvP3DS_SetBottomVertex(
        &output[4], x + width, y + height,
        red, green, blue, alpha);

    AvP3DS_SetBottomVertex(
        &output[5], x, y + height,
        red, green, blue, alpha);

    *vertexCount += 6U;
}

static void AvP3DS_SetBottomTexturedVertex(
    AvP3DS_GameVertex *vertex,
    float pixelX,
    float pixelY,
    float textureU,
    float textureV)
{
    AvP3DS_SetBottomVertex(
        vertex,
        pixelX,
        pixelY,
        255U,
        255U,
        255U,
        255U);

    vertex->texcoord[0] = textureU;
    vertex->texcoord[1] = textureV;
}

/*
 * AVP-MARINE-HUD-DOWN12: translate the complete Marine presentation down 12 pixels.
 */
static void AvP3DS_AppendMarineHUDBackdrop(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount)
{
    const float maximumU =
        (float)AVP3DS_MARINE_HUD_SOURCE_WIDTH /
        (float)AVP3DS_MARINE_HUD_TEXTURE_WIDTH;

    const float maximumV =
        (float)AVP3DS_MARINE_HUD_SOURCE_HEIGHT /
        (float)AVP3DS_MARINE_HUD_TEXTURE_HEIGHT;

    const float topY = 12.0f;
    const float bottomY =
        AVP3DS_BOTTOM_HEIGHT + 12.0f;

    AvP3DS_GameVertex *output =
        vertices + *vertexCount;

    /*
     * Preserve the proven upright V orientation while translating the
     * backdrop by exactly the same amount as every Marine HUD layout box.
     */
    AvP3DS_SetBottomTexturedVertex(
        &output[0],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[1],
        AVP3DS_BOTTOM_WIDTH,
        topY,
        maximumU,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[2],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[3],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[4],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[5],
        0.0f,
        bottomY,
        0.0f,
        0.0f);

    *vertexCount += 6U;
}

/*
 * PRED-HUD1D2-BACKDROP-DOWN8.
 *
 * Predator-only copy of the existing textured backdrop quad.
 * Geometry is translated down by 12.0 lower-screen pixels.
 */
static void AvP3DS_AppendPredatorHUDBackdrop(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount)
{
    const float maximumU =
        (float)AVP3DS_MARINE_HUD_SOURCE_WIDTH /
        (float)AVP3DS_MARINE_HUD_TEXTURE_WIDTH;

    const float maximumV =
        (float)AVP3DS_MARINE_HUD_SOURCE_HEIGHT /
        (float)AVP3DS_MARINE_HUD_TEXTURE_HEIGHT;

    const float topY = 12.0f;
    const float bottomY =
        AVP3DS_BOTTOM_HEIGHT + 12.0f;

    AvP3DS_GameVertex *output =
        vertices + *vertexCount;

    AvP3DS_SetBottomTexturedVertex(
        &output[0],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[1],
        AVP3DS_BOTTOM_WIDTH,
        topY,
        maximumU,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[2],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[3],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[4],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[5],
        0.0f,
        bottomY,
        0.0f,
        0.0f);

    *vertexCount += 6U;
}

static void AvP3DS_AppendAlienHUDBackdrop(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount)
{
    const float maximumU =
        (float)AVP3DS_MARINE_HUD_SOURCE_WIDTH /
        (float)AVP3DS_MARINE_HUD_TEXTURE_WIDTH;

    const float maximumV =
        (float)AVP3DS_MARINE_HUD_SOURCE_HEIGHT /
        (float)AVP3DS_MARINE_HUD_TEXTURE_HEIGHT;

    const float topY = 0.0f;
    const float bottomY =
        AVP3DS_BOTTOM_HEIGHT;

    AvP3DS_GameVertex *output =
        vertices + *vertexCount;

    AvP3DS_SetBottomTexturedVertex(
        &output[0],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[1],
        AVP3DS_BOTTOM_WIDTH,
        topY,
        maximumU,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[2],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[3],
        0.0f,
        topY,
        0.0f,
        maximumV);

    AvP3DS_SetBottomTexturedVertex(
        &output[4],
        AVP3DS_BOTTOM_WIDTH,
        bottomY,
        maximumU,
        0.0f);

    AvP3DS_SetBottomTexturedVertex(
        &output[5],
        0.0f,
        bottomY,
        0.0f,
        0.0f);

    *vertexCount += 6U;
}

static void AvP3DS_BindBottomGameplayPipeline(void)
{
    C3D_AttrInfo *attributeInfo;
    int stage;

    C3D_FrameDrawOn(avp3ds_bottom_target);
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

    C3D_CullFace(GPU_CULL_NONE);
}

static bool AvP3DS_ConfigureBottomTrackerBatch(
    const AvP3DS_TrackerCaptureBatch *capture)
{
    C3D_TexEnv *textureEnvironment;

    if (capture == NULL ||
        capture->vertexCount == 0)
    {
        return false;
    }

    textureEnvironment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(textureEnvironment);

    if (capture->nativeTexture != NULL &&
        capture->nativeTexture->initialized)
    {
        C3D_TexBind(
            0,
            &capture->nativeTexture->texture);

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

    switch (capture->translucencyMode)
    {
        default:
        case 0:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ONE,
                GPU_ZERO,
                GPU_ONE,
                GPU_ZERO);
            break;

        case 1:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_SRC_ALPHA,
                GPU_ONE_MINUS_SRC_ALPHA,
                GPU_SRC_ALPHA,
                GPU_ONE_MINUS_SRC_ALPHA);
            break;

        case 2:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ZERO,
                GPU_ONE_MINUS_SRC_COLOR,
                GPU_ZERO,
                GPU_ONE);
            break;

        case 3:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ZERO,
                GPU_SRC_COLOR,
                GPU_ZERO,
                GPU_ONE);
            break;

        case 4:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_SRC_ALPHA,
                GPU_ONE,
                GPU_SRC_ALPHA,
                GPU_ONE);
            break;

        case 5:
            C3D_AlphaBlend(
                GPU_BLEND_ADD,
                GPU_BLEND_ADD,
                GPU_ONE_MINUS_DST_COLOR,
                GPU_ZERO,
                GPU_ONE,
                GPU_ZERO);
            break;

        case 6:
            /* Depth-only batches have no useful lower-HUD pixels. */
            return false;
    }

    C3D_DepthTest(
        false,
        GPU_ALWAYS,
        GPU_WRITE_COLOR);

    return true;
}

typedef struct AvP3DS_HUDGroupTransform
{
    bool valid;
    float sourceMinX;
    float sourceMinY;
    float scale;
    float offsetX;
    float offsetY;
} AvP3DS_HUDGroupTransform;

/*
 * AVP-HUD1G1 tracker transform lock.
 *
 * The tracker contains animated geometry whose live bounds change as the
 * sweep and heading update. Lock its first complete lower-screen transform
 * so those changing bounds cannot resize or reposition the instrument.
 */
static bool avp3ds_tracker_transform_locked = false;
static AvP3DS_HUDGroupTransform avp3ds_tracker_locked_transform;

/* PRED-HUD1F3-SPEAR-AMMO-PINNED: stable Predator rails independent of selected weapon. */
static bool avp3ds_predator_status_transform_locked = false;
static AvP3DS_HUDGroupTransform
    avp3ds_predator_status_locked_transform;

static float AvP3DS_MinFloat(float left, float right)
{
    return left < right ? left : right;
}

static void AvP3DS_CollectHUDGroupTransforms(
    AvP3DS_HUDGroupTransform transforms[AVP3DS_HUD_GROUP_COUNT])
{
    float minX[AVP3DS_HUD_GROUP_COUNT];
    float minY[AVP3DS_HUD_GROUP_COUNT];
    float maxX[AVP3DS_HUD_GROUP_COUNT];
    float maxY[AVP3DS_HUD_GROUP_COUNT];
    bool found[AVP3DS_HUD_GROUP_COUNT];
    size_t captureIndex;
    int group;

    for (group = 0; group < AVP3DS_HUD_GROUP_COUNT; ++group)
    {
        minX[group] = 1000000.0f;
        minY[group] = 1000000.0f;
        maxX[group] = -1000000.0f;
        maxY[group] = -1000000.0f;
        found[group] = false;
        transforms[group].valid = false;
    }

    for (captureIndex = 0;
         captureIndex < avp3ds_tracker_capture_count;
         ++captureIndex)
    {
        const AvP3DS_TrackerCaptureBatch *capture =
            &avp3ds_tracker_capture_batches[captureIndex];

        const AvP3DS_GameVertex *vertices;
        size_t vertexIndex;

        group = capture->hudGroup;

        if (group < 0 || group >= AVP3DS_HUD_GROUP_COUNT)
            continue;

        vertices =
            (const AvP3DS_GameVertex *)(
                avp3ds_game_vertices +
                capture->vertexOffset *
                AVP3DS_GAME_VERTEX_STRIDE);

        for (vertexIndex = 0;
             vertexIndex < capture->vertexCount;
             ++vertexIndex)
        {
            const float pixelX =
                (vertices[vertexIndex].position[0] + 1.0f) *
                (AVP3DS_BOTTOM_WIDTH * 0.5f);

            const float pixelY =
                (1.0f - vertices[vertexIndex].position[1]) *
                (AVP3DS_BOTTOM_HEIGHT * 0.5f);

            if (pixelX < minX[group]) minX[group] = pixelX;
            if (pixelY < minY[group]) minY[group] = pixelY;
            if (pixelX > maxX[group]) maxX[group] = pixelX;
            if (pixelY > maxY[group]) maxY[group] = pixelY;

            found[group] = true;
        }
    }

    for (group = 0; group < AVP3DS_HUD_GROUP_COUNT; ++group)
    {
        const AvP3DS_HUDLayoutBox *box =
            &avp3ds_hud_layout_boxes[group];

        float sourceWidth;
        float sourceHeight;
        float fitX;
        float fitY;
        float scale;
        float transformedWidth;
        float transformedHeight;

        if (!found[group])
            continue;

        sourceWidth = maxX[group] - minX[group];
        sourceHeight = maxY[group] - minY[group];

        if (sourceWidth < 1.0f) sourceWidth = 1.0f;
        if (sourceHeight < 1.0f) sourceHeight = 1.0f;

        fitX = box->width / sourceWidth;
        fitY = box->height / sourceHeight;
        scale = AvP3DS_MinFloat(fitX, fitY);

        if (scale > box->maximumScale)
            scale = box->maximumScale;

        transformedWidth = sourceWidth * scale;
        transformedHeight = sourceHeight * scale;

        transforms[group].valid = true;
        transforms[group].sourceMinX = minX[group];
        transforms[group].sourceMinY = minY[group];
        transforms[group].scale = scale;

        transforms[group].offsetX =
            box->x +
            (box->width - transformedWidth) * 0.5f;

        transforms[group].offsetY =
            box->y +
            (box->height - transformedHeight) * 0.5f;

        /*
         * AVP-HUD1G1 tracker transform lock.
         *
         * Status, ammo and messages continue using live bounds. The tracker
         * keeps the transform established by its first complete capture.
         */
        if (group == AVP3DS_HUD_GROUP_TRACKER)
        {
            if (!avp3ds_tracker_transform_locked)
            {
                avp3ds_tracker_locked_transform = transforms[group];
                avp3ds_tracker_transform_locked = true;
            }
            else
            {
                transforms[group] =
                    avp3ds_tracker_locked_transform;
            }
        }
        else if (group == AVP3DS_HUD_GROUP_PRED_STATUS)
        {
            /*
             * PRED-HUD1F3-SPEAR-AMMO-PINNED: after spear ammo is separated, the first status
             * capture contains only the two rails. Keep that transform.
             */
            if (!avp3ds_predator_status_transform_locked)
            {
                avp3ds_predator_status_locked_transform =
                    transforms[group];

                avp3ds_predator_status_transform_locked = true;
            }
            else
            {
                transforms[group] =
                    avp3ds_predator_status_locked_transform;
            }
        }

        /*
         * PRED-HUD1G-MESSAGE-RECENTER:
         * Predator messages use the normal fitted transform so their
         * live bounds are centered inside the existing message box.
         * Spear ammo is now a separate group, so weapon changes cannot
         * disturb this message placement.
         */

    }
}

static void AvP3DS_TransformHUDVertex(
    AvP3DS_GameVertex *destination,
    const AvP3DS_GameVertex *source,
    const AvP3DS_HUDGroupTransform *transform)
{
    float sourcePixelX;
    float sourcePixelY;
    float destinationPixelX;
    float destinationPixelY;

    memcpy(destination, source, sizeof(*destination));

    sourcePixelX =
        (source->position[0] + 1.0f) *
        (AVP3DS_BOTTOM_WIDTH * 0.5f);

    sourcePixelY =
        (1.0f - source->position[1]) *
        (AVP3DS_BOTTOM_HEIGHT * 0.5f);

    destinationPixelX =
        transform->offsetX +
        (sourcePixelX - transform->sourceMinX) *
        transform->scale;

    destinationPixelY =
        transform->offsetY +
        (sourcePixelY - transform->sourceMinY) *
        transform->scale;

    destination->position[0] =
        destinationPixelX /
        (AVP3DS_BOTTOM_WIDTH * 0.5f) - 1.0f;

    destination->position[1] =
        1.0f -
        destinationPixelY /
        (AVP3DS_BOTTOM_HEIGHT * 0.5f);
}

static void AvP3DS_AppendBottomOutline(
    AvP3DS_GameVertex *vertices,
    size_t *vertexCount,
    const AvP3DS_HUDLayoutBox *box,
    unsigned int red,
    unsigned int green,
    unsigned int blue)
{
    AvP3DS_AppendBottomRect(
        vertices, vertexCount,
        box->x, box->y,
        box->width, 1.0f,
        red, green, blue, 255U);

    AvP3DS_AppendBottomRect(
        vertices, vertexCount,
        box->x, box->y + box->height - 1.0f,
        box->width, 1.0f,
        red, green, blue, 255U);

    AvP3DS_AppendBottomRect(
        vertices, vertexCount,
        box->x, box->y,
        1.0f, box->height,
        red, green, blue, 255U);

    AvP3DS_AppendBottomRect(
        vertices, vertexCount,
        box->x + box->width - 1.0f, box->y,
        1.0f, box->height,
        red, green, blue, 255U);
}

/*
 * AVP-HUD1C Marine lower-screen layout groups.
 *
 * The diagnostic outlines correspond to Benny's annotated target zones:
 * green messages, blue tracker, white health/armor, red ammunition.
 */
static void AvP3DS_DrawBottomFrame(void)
{
    AvP3DS_GameVertex *gridVertices;
    C3D_BufInfo *bufferInfo;
    C3D_TexEnv *textureEnvironment;

    size_t gridVertexOffset;
    size_t gridVertexCount = 0;
    size_t captureIndex;

    AvP3DS_HUDGroupTransform transforms[
        AVP3DS_HUD_GROUP_COUNT];

    unsigned int cellX;
    unsigned int cellY;

    /* PRED-HUD1A-SPECIES-ISOLATED: true only for Predator capture frames. */
    bool predatorHUDCaptured = false;

    /* ALIEN-HUD1A2-LOWER-HEALTH: true only for Alien health capture frames. */
    bool alienHUDCaptured = false;

    if (avp3ds_bottom_target == NULL)
        return;

    /*
     * PRED-HUD1E3-SWAP-AND-DEATH-BLACK: clear bottom target to black so frames that
     * intentionally skip lower-screen HUD replay stay black.
     */
    C3D_RenderTargetClear(
        avp3ds_bottom_target,
        C3D_CLEAR_ALL,
        0x000000FF,
        0);

    if (!C3D_FrameDrawOn(avp3ds_bottom_target))
        return;

    if (!avp3ds_hud_seen_this_frame)
    {
        C3D_FrameDrawOn(avp3ds_top_target);
        return;
    }

    if (avp3ds_game_vertex_cursor +
            AVP3DS_BOTTOM_GRID_MAX_VERTICES >
        AVP3DS_GAME_MAX_VERTICES)
    {
        C3D_FrameDrawOn(avp3ds_top_target);
        return;
    }

    /* PRED-HUD1A-SPECIES-ISOLATED: detect Predator by its dedicated capture IDs. */
    for (captureIndex = 0;
         captureIndex < avp3ds_tracker_capture_count;
         ++captureIndex)
    {
        const int capturedGroup =
            avp3ds_tracker_capture_batches[captureIndex].hudGroup;

        if (capturedGroup == AVP3DS_HUD_GROUP_ALIEN_STATUS ||
            capturedGroup == AVP3DS_HUD_GROUP_ALIEN_MESSAGES)
        {
            alienHUDCaptured = true;
            break;
        }

        if (capturedGroup == AVP3DS_HUD_GROUP_PRED_WRIST ||
            capturedGroup == AVP3DS_HUD_GROUP_PRED_STATUS ||
            capturedGroup == AVP3DS_HUD_GROUP_PRED_MESSAGES ||
            capturedGroup == AVP3DS_HUD_GROUP_PRED_AMMO)
        {
            predatorHUDCaptured = true;
            break;
        }
    }

    gridVertexOffset = avp3ds_game_vertex_cursor;

    gridVertices =
        (AvP3DS_GameVertex *)(
            avp3ds_game_vertices +
            gridVertexOffset *
            AVP3DS_GAME_VERTEX_STRIDE);

    if (alienHUDCaptured &&
        avp3ds_alien_hud_texture_initialized)
    {
        AvP3DS_AppendAlienHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else if (predatorHUDCaptured &&
             avp3ds_predator_hud_texture_initialized)
    {
        AvP3DS_AppendPredatorHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else if (!predatorHUDCaptured &&
             !alienHUDCaptured &&
             avp3ds_marine_hud_texture_initialized)
    {
        AvP3DS_AppendMarineHUDBackdrop(
            gridVertices,
            &gridVertexCount);
    }
    else
    {
    for (cellY = 0; cellY < 15U; ++cellY)
    {
        for (cellX = 0; cellX < 20U; ++cellX)
        {
            const bool alternate =
                ((cellX + cellY) & 1U) != 0;

            AvP3DS_AppendBottomRect(
                gridVertices,
                &gridVertexCount,
                (float)(cellX * 16U),
                (float)(cellY * 16U),
                16.0f,
                16.0f,
                alternate ? 31U : 18U,
                alternate ? 42U : 25U,
                alternate ? 52U : 32U,
                255U);
        }
    }

    for (cellX = 0; cellX <= 10U; ++cellX)
    {
        float x = (float)(cellX * 32U);

        if (x >= AVP3DS_BOTTOM_WIDTH)
            x = AVP3DS_BOTTOM_WIDTH - 1.0f;

        AvP3DS_AppendBottomRect(
            gridVertices,
            &gridVertexCount,
            x,
            0.0f,
            1.0f,
            AVP3DS_BOTTOM_HEIGHT,
            83U, 103U, 119U, 255U);
    }

    for (cellY = 0; cellY <= 7U; ++cellY)
    {
        float y = (float)(cellY * 32U);

        if (y >= AVP3DS_BOTTOM_HEIGHT)
            y = AVP3DS_BOTTOM_HEIGHT - 1.0f;

        AvP3DS_AppendBottomRect(
            gridVertices,
            &gridVertexCount,
            0.0f,
            y,
            AVP3DS_BOTTOM_WIDTH,
            1.0f,
            83U, 103U, 119U, 255U);
    }

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        159.0f,
        0.0f,
        2.0f,
        AVP3DS_BOTTOM_HEIGHT,
        205U, 235U, 245U, 255U);

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        0.0f,
        119.0f,
        AVP3DS_BOTTOM_WIDTH,
        2.0f,
        205U, 235U, 245U, 255U);

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        0.0f, 0.0f,
        AVP3DS_BOTTOM_WIDTH, 2.0f,
        132U, 158U, 174U, 255U);

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        0.0f, AVP3DS_BOTTOM_HEIGHT - 2.0f,
        AVP3DS_BOTTOM_WIDTH, 2.0f,
        132U, 158U, 174U, 255U);

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        0.0f, 0.0f,
        2.0f, AVP3DS_BOTTOM_HEIGHT,
        132U, 158U, 174U, 255U);

    AvP3DS_AppendBottomRect(
        gridVertices,
        &gridVertexCount,
        AVP3DS_BOTTOM_WIDTH - 2.0f, 0.0f,
        2.0f, AVP3DS_BOTTOM_HEIGHT,
        132U, 158U, 174U, 255U);

    }

if (gridVertexCount >
        AVP3DS_BOTTOM_GRID_MAX_VERTICES)
    {
        C3D_FrameDrawOn(avp3ds_top_target);
        return;
    }

    GSPGPU_FlushDataCache(
        gridVertices,
        gridVertexCount *
            sizeof(AvP3DS_GameVertex));

    AvP3DS_BindBottomGameplayPipeline();

    textureEnvironment = C3D_GetTexEnv(0);
    C3D_TexEnvInit(textureEnvironment);

    if (alienHUDCaptured &&
        avp3ds_alien_hud_texture_initialized)
    {
        C3D_TexBind(
            0,
            &avp3ds_alien_hud_texture);

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
    else if (predatorHUDCaptured &&
             avp3ds_predator_hud_texture_initialized)
    {
        C3D_TexBind(
            0,
            &avp3ds_predator_hud_texture);

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
    else if (!predatorHUDCaptured &&
             !alienHUDCaptured &&
             avp3ds_marine_hud_texture_initialized)
    {
        C3D_TexBind(
            0,
            &avp3ds_marine_hud_texture);

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

    C3D_AlphaBlend(
        GPU_BLEND_ADD,
        GPU_BLEND_ADD,
        GPU_ONE,
        GPU_ZERO,
        GPU_ONE,
        GPU_ZERO);

    C3D_DepthTest(
        false,
        GPU_ALWAYS,
        GPU_WRITE_COLOR);

    bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);

    BufInfo_Add(
        bufferInfo,
        gridVertices,
        sizeof(AvP3DS_GameVertex),
        3,
        0x210);

    C3D_DrawArrays(
        GPU_TRIANGLES,
        0,
        (int)gridVertexCount);

    avp3ds_game_vertex_cursor +=
        gridVertexCount;

    AvP3DS_CollectHUDGroupTransforms(transforms);

    for (captureIndex = 0;
         captureIndex < avp3ds_tracker_capture_count;
         ++captureIndex)
    {
        const AvP3DS_TrackerCaptureBatch *capture =
            &avp3ds_tracker_capture_batches[captureIndex];

        const AvP3DS_GameVertex *captureVertices =
            (const AvP3DS_GameVertex *)(
                avp3ds_game_vertices +
                capture->vertexOffset *
                AVP3DS_GAME_VERTEX_STRIDE);

        AvP3DS_GameVertex *layoutVertices;
        const AvP3DS_HUDGroupTransform *transform;
        size_t layoutVertexOffset;
        size_t vertexIndex;
        int group = capture->hudGroup;

        if (group < 0 || group >= AVP3DS_HUD_GROUP_COUNT)
            continue;

        transform = &transforms[group];

        if (!transform->valid ||
            avp3ds_game_vertex_cursor + capture->vertexCount >
                AVP3DS_GAME_MAX_VERTICES)
        {
            continue;
        }

        layoutVertexOffset = avp3ds_game_vertex_cursor;

        layoutVertices =
            (AvP3DS_GameVertex *)(
                avp3ds_game_vertices +
                layoutVertexOffset *
                AVP3DS_GAME_VERTEX_STRIDE);

        for (vertexIndex = 0;
             vertexIndex < capture->vertexCount;
             ++vertexIndex)
        {
            AvP3DS_TransformHUDVertex(
                &layoutVertices[vertexIndex],
                &captureVertices[vertexIndex],
                transform);

            /*
             * PRED-HUD1B2-SPECIES-ISOLATED: Predator status rails only.
             * Left rail moves right 32 px; right rail moves left 32 px.
             */
            if (group == AVP3DS_HUD_GROUP_PRED_STATUS)
            {
                const float sourcePixelX =
                    (captureVertices[vertexIndex].position[0] + 1.0f) *
                    (AVP3DS_BOTTOM_WIDTH * 0.5f);
                float destinationPixelX =
                    (layoutVertices[vertexIndex].position[0] + 1.0f) *
                    (AVP3DS_BOTTOM_WIDTH * 0.5f);

                if (sourcePixelX < AVP3DS_BOTTOM_WIDTH * 0.5f)
                    destinationPixelX += 32.0f;
                else
                    destinationPixelX -= 32.0f;

                layoutVertices[vertexIndex].position[0] =
                    destinationPixelX /
                    (AVP3DS_BOTTOM_WIDTH * 0.5f) - 1.0f;
            }
        }

        GSPGPU_FlushDataCache(
            layoutVertices,
            capture->vertexCount *
                sizeof(AvP3DS_GameVertex));

        if (!AvP3DS_ConfigureBottomTrackerBatch(capture))
            continue;

        bufferInfo = C3D_GetBufInfo();
        BufInfo_Init(bufferInfo);

        BufInfo_Add(
            bufferInfo,
            layoutVertices,
            sizeof(AvP3DS_GameVertex),
            3,
            0x210);

        C3D_DrawArrays(
            GPU_TRIANGLES,
            0,
            (int)capture->vertexCount);

        avp3ds_game_vertex_cursor +=
            capture->vertexCount;
    }

    C3D_FrameDrawOn(avp3ds_top_target);

    if (avp3ds_tracker_capture_overflow)
    {
        static bool overflowReported = false;

        if (!overflowReported)
        {
            printf(
                "Tracker capture exceeded %u batches.\n",
                AVP3DS_TRACKER_CAPTURE_MAX_BATCHES);

            overflowReported = true;
        }
    }
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

        /* AVP-SHIP1-PERFLOG1A3: exact measured report copy to SD. */
        {
            FILE *avp3ds_ship1_perf_file =
                fopen(AVP3DS_SHIP1_PERF_LOG_PATH, "a");

            if (avp3ds_ship1_perf_file != NULL)
            {
                fprintf(avp3ds_ship1_perf_file, 
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
                fclose(avp3ds_ship1_perf_file);
            }
        }

        /* AVP-HEADROOM2-SUBBUCKET1A3: same 120-frame reporting window as YEET28. */
        AvP3DS_WriteHeadroomReport(
            stdout,
            avp3ds_diag_frame_count,
            sampleCount);

        {
            FILE *avp3ds_headroom_file =
                fopen(AVP3DS_SHIP1_PERF_LOG_PATH, "a");

            if (avp3ds_headroom_file != NULL)
            {
                AvP3DS_WriteHeadroomReport(
                    avp3ds_headroom_file,
                    avp3ds_diag_frame_count,
                    sampleCount);
                fclose(avp3ds_headroom_file);
            }
        }

        AvP3DS_ResetHeadroomReport();

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
void AvP3DS_DrawLoadingProgress(
    int fixedPosition,
    int visible)
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

    if (!visible ||
        !avp3ds_video_ready ||
        avp3ds_top_target == NULL ||
        avp3ds_game_frame_active)
    {
        return;
    }

    if (fixedPosition < 0)
        fixedPosition = 0;

    if (fixedPosition > 65536)
        fixedPosition = 65536;

    fraction =
        (float)fixedPosition /
        65536.0f;

    filledWidth =
        innerWidth * fraction;

    /*
     * The software loading screen was presented immediately before this
     * call. Do not clear the target; preserve that frame and overlay the
     * progress bar on top.
     */
    C2D_Prepare();

    if (!C3D_FrameBegin(C3D_FRAME_SYNCDRAW))
        return;

    C2D_SceneBegin(avp3ds_top_target);

    /* Dark outer trough. */
    C2D_DrawRectSolid(
        outerX,
        outerY,
        0.5f,
        outerWidth,
        outerHeight,
        C2D_Color32(18, 18, 18, 255));

    /* Dim empty interior. */
    C2D_DrawRectSolid(
        innerX,
        innerY,
        0.6f,
        innerWidth,
        innerHeight,
        C2D_Color32(64, 64, 64, 255));

    /* Live neutral progress fill. */
    if (filledWidth > 0.0f)
    {
        C2D_DrawRectSolid(
            innerX,
            innerY,
            0.7f,
            filledWidth,
            innerHeight,
            C2D_Color32(205, 205, 205, 255));

        /*
         * Thin brighter upper edge to retain the appearance of the
         * clean native loading strip.
         */
        C2D_DrawRectSolid(
            innerX,
            innerY,
            0.8f,
            filledWidth,
            2.0f,
            C2D_Color32(255, 255, 255, 255));
    }

    C3D_FrameEnd(0);
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
