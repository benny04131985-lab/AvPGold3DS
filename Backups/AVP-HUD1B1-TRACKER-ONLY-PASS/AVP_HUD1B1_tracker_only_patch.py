from pathlib import Path
import shutil

main_path = Path('src/main_3ds.c')
ogl_path = Path('src/opengl.c')
hud_path = Path('src/avp/hud.c')

main_text = main_path.read_text(encoding='utf-8')
ogl_text = ogl_path.read_text(encoding='utf-8')
hud_text = hud_path.read_text(encoding='utf-8')

marker = 'AVP-HUD1B1 tracker-only capture and replay'

if 'AVP-HUD1B0 diagnostic grid' not in main_text:
    raise SystemExit(
        'ERROR: expected AVP-HUD1B0 grid-only baseline was not found.'
    )

for source_path in (main_path, ogl_path, hud_path):
    backup_path = source_path.with_name(
        source_path.name + '.pre-AVP-HUD1B1-TRACKER.bak'
    )

    if not backup_path.exists():
        shutil.copy2(source_path, backup_path)
        print(f'Created backup: {backup_path}')
    else:
        print(f'Backup already exists: {backup_path}')

if marker in main_text or marker in ogl_text or marker in hud_text:
    raise SystemExit('ERROR: tracker-only patch already present.')

# ---------- main_3ds.c ----------
old = '''#define AVP3DS_GAME_VERTEX_STRIDE   40U
#define AVP3DS_GAME_MAX_VERTICES    65536U
#define AVP3DS_GAME_MAX_TRIANGLES   65536U
'''
new = '''#define AVP3DS_GAME_VERTEX_STRIDE   40U
#define AVP3DS_GAME_MAX_VERTICES    65536U
#define AVP3DS_GAME_MAX_TRIANGLES   65536U

#define AVP3DS_TRACKER_CAPTURE_MAX_BATCHES 128U
#define AVP3DS_BOTTOM_GRID_MAX_VERTICES   2048U

typedef struct AvP3DS_GameVertex
{
    float position[4];
    float texcoord[2];
    float primary[4];
} AvP3DS_GameVertex;
'''
if old not in main_text:
    raise SystemExit('ERROR: main limits anchor not found.')
main_text = main_text.replace(old, new, 1)

old = '''typedef struct AvP3DS_NativeTexture
{
    C3D_Tex texture;
    unsigned int sourceWidth;
    unsigned int sourceHeight;
    unsigned int textureWidth;
    unsigned int textureHeight;
    bool initialized;
} AvP3DS_NativeTexture;
'''
new = '''typedef struct AvP3DS_NativeTexture
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
} AvP3DS_TrackerCaptureBatch;

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

static AvP3DS_TrackerCaptureBatch
    avp3ds_tracker_capture_batches[
        AVP3DS_TRACKER_CAPTURE_MAX_BATCHES];
'''
if old not in main_text:
    raise SystemExit('ERROR: native texture typedef anchor not found.')
main_text = main_text.replace(old, new, 1)

old = '''void AvP3DS_MarkHUDFrame(void)
{
    avp3ds_hud_seen_this_frame = true;
}
'''
new = '''void AvP3DS_MarkHUDFrame(void)
{
    avp3ds_hud_seen_this_frame = true;
}

void AvP3DS_SetTrackerCaptureEnabled(int enabled)
{
    avp3ds_tracker_capture_enabled = enabled != 0;
}
'''
if old not in main_text:
    raise SystemExit('ERROR: HUD marker function anchor not found.')
main_text = main_text.replace(old, new, 1)

old = '''    avp3ds_hud_seen_this_frame = false;

    {
        u64 beginStartTick;
'''
new = '''    avp3ds_hud_seen_this_frame = false;
    avp3ds_tracker_capture_enabled = false;
    avp3ds_tracker_capture_overflow = false;
    avp3ds_tracker_capture_count = 0;

    {
        u64 beginStartTick;
'''
if old not in main_text:
    raise SystemExit('ERROR: frame reset anchor not found.')
main_text = main_text.replace(old, new, 1)

local_typedef = '''    typedef struct
    {
        float position[4];
        float texcoord[2];
        float primary[4];
    } AvP3DS_GameVertex;

'''
if local_typedef not in main_text:
    raise SystemExit('ERROR: local GameVertex typedef not found.')
main_text = main_text.replace(local_typedef, '', 1)

old = '''    size_t expandedVertexCount;
    size_t writtenVertexCount;
    size_t triangleIndex;
'''
new = '''    size_t expandedVertexCount;
    size_t writtenVertexCount;
    size_t triangleIndex;
    size_t batchVertexOffset;
'''
if old not in main_text:
    raise SystemExit('ERROR: DrawTriangles local size anchor not found.')
main_text = main_text.replace(old, new, 1)

old = '''    destination =
        (AvP3DS_GameVertex *)(
            avp3ds_game_vertices +
            avp3ds_game_vertex_cursor *
            AVP3DS_GAME_VERTEX_STRIDE);
'''
new = '''    batchVertexOffset = avp3ds_game_vertex_cursor;

    destination =
        (AvP3DS_GameVertex *)(
            avp3ds_game_vertices +
            avp3ds_game_vertex_cursor *
            AVP3DS_GAME_VERTEX_STRIDE);
'''
if old not in main_text:
    raise SystemExit('ERROR: DrawTriangles destination anchor not found.')
main_text = main_text.replace(old, new, 1)

old = '''    C3D_DrawArrays(
        GPU_TRIANGLES,
        0,
        (int)writtenVertexCount);

    avp3ds_game_vertex_cursor +=
        writtenVertexCount;
}
'''
new = '''    C3D_DrawArrays(
        GPU_TRIANGLES,
        0,
        (int)writtenVertexCount);

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
        }
        else
        {
            avp3ds_tracker_capture_overflow = true;
        }
    }

    avp3ds_game_vertex_cursor +=
        writtenVertexCount;
}
'''
if old not in main_text:
    raise SystemExit('ERROR: DrawTriangles submission anchor not found.')
main_text = main_text.replace(old, new, 1)

# Replace exact current bottom function using brace matching.
def replace_function(text: str, signature: str, replacement: str) -> str:
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f'ERROR: function not found: {signature}')
    brace = text.find('{', start)
    if brace < 0:
        raise SystemExit(f'ERROR: opening brace not found: {signature}')
    depth = 0
    end = None
    for i in range(brace, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                end = i + 1
                break
    if end is None:
        raise SystemExit(f'ERROR: closing brace not found: {signature}')
    return text[:start] + replacement + text[end:]

bottom_replacement = r'''static void AvP3DS_SetBottomVertex(
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

/*
 * AVP-HUD1B1 tracker-only capture and replay.
 *
 * The grid and the live tracker are drawn through one native Citro3D
 * pipeline at frame end. No Citro2D/native state handoff occurs inside
 * the gameplay frame.
 */
static void AvP3DS_DrawBottomFrame(void)
{
    AvP3DS_GameVertex *gridVertices;
    C3D_BufInfo *bufferInfo;
    C3D_TexEnv *textureEnvironment;

    size_t gridVertexOffset;
    size_t gridVertexCount = 0;
    size_t captureIndex;

    unsigned int cellX;
    unsigned int cellY;

    if (avp3ds_bottom_target == NULL)
        return;

    C3D_RenderTargetClear(
        avp3ds_bottom_target,
        C3D_CLEAR_ALL,
        0x080B0EFF,
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

    gridVertexOffset = avp3ds_game_vertex_cursor;

    gridVertices =
        (AvP3DS_GameVertex *)(
            avp3ds_game_vertices +
            gridVertexOffset *
            AVP3DS_GAME_VERTEX_STRIDE);

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

    for (captureIndex = 0;
         captureIndex < avp3ds_tracker_capture_count;
         ++captureIndex)
    {
        const AvP3DS_TrackerCaptureBatch *capture =
            &avp3ds_tracker_capture_batches[captureIndex];

        AvP3DS_GameVertex *captureVertices =
            (AvP3DS_GameVertex *)(
                avp3ds_game_vertices +
                capture->vertexOffset *
                AVP3DS_GAME_VERTEX_STRIDE);

        if (!AvP3DS_ConfigureBottomTrackerBatch(capture))
            continue;

        bufferInfo = C3D_GetBufInfo();
        BufInfo_Init(bufferInfo);

        BufInfo_Add(
            bufferInfo,
            captureVertices,
            sizeof(AvP3DS_GameVertex),
            3,
            0x210);

        C3D_DrawArrays(
            GPU_TRIANGLES,
            0,
            (int)capture->vertexCount);
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
}'''

main_text = replace_function(
    main_text,
    'static void AvP3DS_DrawBottomFrame(void)',
    bottom_replacement,
)

# ---------- opengl.c ----------
old = '''extern void AvP3DS_GameFrameBegin(void);


extern void AvP3DS_DestroyNativeTexture(
'''
new = '''extern void AvP3DS_GameFrameBegin(void);
extern void AvP3DS_SetTrackerCaptureEnabled(int enabled);

extern void AvP3DS_DestroyNativeTexture(
'''
if old not in ogl_text:
    raise SystemExit('ERROR: opengl 3DS extern anchor not found.')
ogl_text = ogl_text.replace(old, new, 1)

old = '''static void CheckBoundTextureIsCorrect(D3DTexture *tex)
'''
new = '''#ifdef __3DS__
/*
 * AVP-HUD1B1 tracker-only capture and replay.
 *
 * Flush before each state transition so only geometry produced by the
 * genuine DoMotionTracker() call is retained for lower-screen replay.
 */
void AvP3DS_BeginTrackerCapture(void)
{
    FlushTriangleBuffers(1);
    AvP3DS_SetTrackerCaptureEnabled(1);
}

void AvP3DS_EndTrackerCapture(void)
{
    FlushTriangleBuffers(1);
    AvP3DS_SetTrackerCaptureEnabled(0);
}
#endif

static void CheckBoundTextureIsCorrect(D3DTexture *tex)
'''
if old not in ogl_text:
    raise SystemExit('ERROR: opengl capture insertion anchor not found.')
ogl_text = ogl_text.replace(old, new, 1)

# ---------- hud.c ----------
old = '''#ifdef __3DS__
extern void AvP3DS_MarkHUDFrame(void);
#endif
'''
new = '''#ifdef __3DS__
extern void AvP3DS_MarkHUDFrame(void);
extern void AvP3DS_BeginTrackerCapture(void);
extern void AvP3DS_EndTrackerCapture(void);
#endif
'''
if old not in hud_text:
    raise SystemExit('ERROR: hud 3DS extern anchor not found.')
hud_text = hud_text.replace(old, new, 1)

old = '''\t  \t \tif (CurrentVisionMode==VISION_MODE_NORMAL) DoMotionTracker();
'''
if old not in hud_text:
    # account for exact whitespace variant from file
    old = '''\t  \t \tif (CurrentVisionMode==VISION_MODE_NORMAL) DoMotionTracker();\n'''
new = '''\t  \t \tif (CurrentVisionMode==VISION_MODE_NORMAL)
\t\t\t\t{
#ifdef __3DS__
\t\t\t\t\tAvP3DS_BeginTrackerCapture();
#endif

\t\t\t\t\tDoMotionTracker();

#ifdef __3DS__
\t\t\t\t\tAvP3DS_EndTrackerCapture();
#endif
\t\t\t\t}
'''
if old not in hud_text:
    # semantic fallback, still exact single occurrence
    target = 'if (CurrentVisionMode==VISION_MODE_NORMAL) DoMotionTracker();'
    if hud_text.count(target) != 1:
        raise SystemExit('ERROR: Marine tracker call not uniquely found.')
    hud_text = hud_text.replace(target, new.strip('\n'), 1)
else:
    hud_text = hud_text.replace(old, new, 1)

main_path.write_text(main_text, encoding='utf-8')
ogl_path.write_text(ogl_text, encoding='utf-8')
hud_path.write_text(hud_text, encoding='utf-8')

print('AVP-HUD1B1 tracker-only patch applied.')
