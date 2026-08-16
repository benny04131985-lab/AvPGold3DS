#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>

#include "../avp3ds_shbin.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define AVP3DS_TEXTURE_WIDTH   1024
#define AVP3DS_TEXTURE_HEIGHT   512

#define AVP3DS_TOP_WIDTH       400.0f
#define AVP3DS_TOP_HEIGHT      240.0f

#define AVP3DS_TEXTURE_BYTES \
    ((size_t)AVP3DS_TEXTURE_WIDTH * \
     (size_t)AVP3DS_TEXTURE_HEIGHT * sizeof(u16))

#define AVP3DS_GAME_VERTEX_STRIDE   32U
#define AVP3DS_GAME_MAX_VERTICES    65536U
#define AVP3DS_GAME_MAX_TRIANGLES   65536U

static bool avp3ds_c3d_initialized = false;
static bool avp3ds_c2d_initialized = false;
static bool avp3ds_texture_initialized = false;
static bool avp3ds_video_ready = false;

static C3D_RenderTarget *avp3ds_top_target = NULL;
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

extern int AvP_LegacyMain(int argc, char *argv[]);

int AvP3DS_AppRunning(void)
{
    return aptMainLoop() ? 1 : 0;
}

int AvP3DS_StartPressed(void)
{
    hidScanInput();
    return (hidKeysDown() & KEY_START) != 0;
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

static inline size_t AvP3DS_TiledOffset(
    unsigned int x,
    unsigned int y)
{
    const unsigned int tilesPerRow =
        AVP3DS_TEXTURE_WIDTH / 8;

    const unsigned int tileX = x >> 3;
    const unsigned int tileY = y >> 3;

    const size_t tileBase =
        ((size_t)tileY * (size_t)tilesPerRow +
         (size_t)tileX) * 64U;

    return tileBase + (size_t)AvP3DS_Morton8(x, y);
}

static void AvP3DS_VideoShutdown(void)
{
    avp3ds_video_ready = false;

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
     * gfxInitDefault() owns the display and uses the normal libctru BGR8
     * framebuffer, so Citro2D's standard screen target is correct here.
     */
    avp3ds_top_target =
        C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);

    if (avp3ds_top_target == NULL)
    {
        printf("C2D top target failed.\n");
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
        avp3ds_game_frame_active)
    {
        return;
    }

    if (!C3D_FrameBegin(C3D_FRAME_NONBLOCK))
        return;

    avp3ds_game_frame_active = true;
    avp3ds_game_vertex_cursor = 0;
    avp3ds_game_index_cursor = 0;

    C3D_RenderTargetClear(
        avp3ds_top_target,
        C3D_CLEAR_COLOR,
        C2D_Color32(255, 0, 255, 255),
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

    AttrInfo_AddFixed(
        attributeInfo,
        1);

    C3D_FixedAttribSet(
        1,
        1.0f,
        1.0f,
        1.0f,
        1.0f);

    C3D_FVUnifMtx4x4(
        GPU_VERTEX_SHADER,
        avp3ds_game_projection_location,
        &avp3ds_game_projection);

    /*
     * First visible-world pass:
     * output AvP's primary per-vertex color directly.
     *
     * Textures, secondary lighting, depth and translucency follow once
     * clip-space geometry is proven on hardware.
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

}

void AvP3DS_DrawTriangles(
    const void *vertices,
    int vertexCount,
    const void *triangles,
    int triangleCount)
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

    float *destination;
    size_t expandedVertexCount;
    size_t writtenVertexCount;
    size_t triangleIndex;

    C3D_BufInfo *bufferInfo;

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

    expandedVertexCount =
        (size_t)triangleCount * 3U;

    if (avp3ds_game_vertex_cursor +
            expandedVertexCount >
        AVP3DS_GAME_MAX_VERTICES)
    {
        return;
    }

    destination =
        (float *)(
            avp3ds_game_vertices +
            avp3ds_game_vertex_cursor *
            sizeof(float[4]));

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
            memcpy(
                &destination[writtenVertexCount * 4U],
                sourceVertices[indices[corner]].position,
                sizeof(float[4]));

            ++writtenVertexCount;
        }
    }

    if (writtenVertexCount == 0)
        return;

    GSPGPU_FlushDataCache(
        destination,
        writtenVertexCount * sizeof(float[4]));

    bufferInfo = C3D_GetBufInfo();
    BufInfo_Init(bufferInfo);

    BufInfo_Add(
        bufferInfo,
        destination,
        sizeof(float[4]),
        1,
        0x0);

    C3D_DrawArrays(
        GPU_TRIANGLES,
        0,
        (int)writtenVertexCount);

    avp3ds_game_vertex_cursor +=
        writtenVertexCount;
}

void AvP3DS_GameFrameEnd(void)
{
    if (!avp3ds_game_frame_active)
        return;

    C3D_FrameEnd(0);
    avp3ds_game_frame_active = false;
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

            tiledOffset = AvP3DS_TiledOffset(
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

    C3D_FrameEnd(0);
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    consoleInit(GFX_BOTTOM, NULL);

    printf("Aliens vs Predator 3DS\n");
    printf("Native bootstrap reached.\n");
    printf("\nPress A to YEET the engine.\n");
    printf("Press START to exit safely.\n");

    while (aptMainLoop())
    {
        hidScanInput();
        u32 keys = hidKeysDown();

        if (keys & KEY_START)
            break;

        if (keys & KEY_A)
        {
            int result;

            printf("\nLaunching full AvP startup...\n");
            printf("Citro2D Morton presenter enabled.\n");
            printf("START should exit safely.\n");

            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();

            if (!AvP3DS_VideoInit())
            {
                printf("\nCitro2D initialization failed.\n");
                printf("Press START to exit.\n");
                continue;
            }

            result = AvP_LegacyMain(argc, argv);

            AvP3DS_VideoShutdown();

            consoleClear();
            printf("AvP engine returned: %d\n", result);
            printf("\nPress START to exit.\n");
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    AvP3DS_VideoShutdown();
    gfxExit();
    return 0;
}
