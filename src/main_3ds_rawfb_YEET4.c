#include <3ds.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>


int AvP3DS_AppRunning(void)
{
    return aptMainLoop() ? 1 : 0;
}

int AvP3DS_StartPressed(void)
{
    hidScanInput();
    return (hidKeysDown() & KEY_START) != 0;
}

extern int AvP_LegacyMain(int argc, char *argv[]);

void AvP_PresentSurface3DS(SDL_Surface *source)
{
    u8 *framebuffer;
    int destinationWidth;
    int destinationHeight;

    if (source == NULL || source->pixels == NULL)
        return;

    framebuffer = gfxGetFramebuffer(
        GFX_TOP,
        GFX_LEFT,
        NULL,
        NULL
    );

    if (framebuffer == NULL)
        return;

    destinationWidth = 400;
    destinationHeight = 240;

    for (int y = 0; y < destinationHeight; ++y)
    {
        int sourceY = (y * source->h) / destinationHeight;

        const u16 *sourceRow = (const u16 *)(
            (const u8 *)source->pixels +
            sourceY * source->pitch
        );

        for (int x = 0; x < destinationWidth; ++x)
        {
            int sourceX = (x * source->w) / destinationWidth;
            u16 pixel = sourceRow[sourceX];

            u8 red = (u8)(((pixel >> 11) & 31) * 255 / 31);
            u8 green = (u8)(((pixel >> 5) & 63) * 255 / 63);
            u8 blue = (u8)((pixel & 31) * 255 / 31);

            /*
             * The native 3DS framebuffer is rotated and stored as BGR8.
             */
            size_t destination =
                3U * ((size_t)x * 240U + (size_t)(239 - y));

            framebuffer[destination + 0] = blue;
            framebuffer[destination + 1] = green;
            framebuffer[destination + 2] = red;
        }
    }

    gfxFlushBuffers();
    gfxSwapBuffers();
    gspWaitForVBlank();
}

int main(int argc, char **argv)
{
    gfxInitDefault();
    consoleInit(GFX_TOP, NULL);

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
            printf("START should exit if the loop survives.\n");

            gfxFlushBuffers();
            gfxSwapBuffers();
            gspWaitForVBlank();

            result = AvP_LegacyMain(argc, argv);

            consoleClear();
            printf("AvP engine returned: %d\n", result);
            printf("\nPress START to exit.\n");
        }

        gfxFlushBuffers();
        gfxSwapBuffers();
        gspWaitForVBlank();
    }

    gfxExit();
    return 0;
}
