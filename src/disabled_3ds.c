#ifdef __3DS__

#include <3ds.h>
#include <stdio.h>

#define WEAK __attribute__((weak))

/*
 * Override newlib's assertion handler so failed assertions are visible
 * on the 3DS console instead of immediately entering Luma's panic screen.
 */
void __assert_func(
    const char *file,
    int line,
    const char *function,
    const char *condition)
{
    printf("\x1b[2J\x1b[H");
    printf("AVP ASSERTION FAILED\n\n");

    printf("FILE:\n%s\n\n",
        file ? file : "(unknown)");

    printf("LINE: %d\n\n", line);

    printf("FUNCTION:\n%s\n\n",
        function ? function : "(unknown)");

    printf("CONDITION:\n%s\n\n",
        condition ? condition : "(unknown)");

    printf("Press START to exit.\n");
    fflush(stdout);

    while (aptMainLoop())
    {
        hidScanInput();

        if (hidKeysDown() & KEY_START)
            break;

        gspWaitForVBlank();
    }

    svcExitProcess();

    for (;;)
        ;
}

/*
 * Temporary bring-up stubs.
 *
 * These allow the original game loop to run while OpenGL presentation,
 * FMV playback, CD audio, and sound output are disabled.
 */

WEAK int ScanImagesForFMVs()             { return 0; }
WEAK int ReleaseAllFMVTextures()         { return 0; }
WEAK int UpdateAllFMVTextures()          { return 0; }
WEAK int StartTriggerPlotFMV()           { return 0; }
WEAK int InitialiseTriggeredFMVs()       { return 0; }

#ifndef AVP3DS_REAL_SOUND_BACKEND
WEAK int CDDA_Start()                    { return 0; }
WEAK int CDDA_Stop()                     { return 0; }
WEAK int CDDA_Play()                     { return 0; }
WEAK int CDDA_IsOn()                     { return 0; }
WEAK int CDDA_IsPlaying()                { return 0; }
WEAK int CDDA_CheckNumberOfTracks()      { return 0; }
#endif

WEAK int FlushD3DZBuffer()               { return 0; }
WEAK int ThisFramesRenderingHasBegun()   { return 0; }
WEAK int ThisFramesRenderingHasFinished(){ return 0; }
WEAK int D3D_DrawBackdrop()              { return 0; }
WEAK int D3D_DrawCable()                 { return 0; }
WEAK int D3D_DecalSystem_Setup()         { return 0; }
WEAK int PlatSetEnviroment()             { return 0; }

WEAK int RenderString()                  { return 0; }
WEAK int RenderStringCentred()           { return 0; }

WEAK int InitOpenGL()                    { return 0; }
WEAK int load_ogl_functions()            { return 0; }
WEAK int check_for_errors_()             { return 0; }
WEAK int DrawFullscreenTexture()         { return 0; }
WEAK int SelectProgram()                 { return 0; }

/*
 * Oversized temporary storage prevents immediate failures if disabled
 * sound/FMV code reads one of these legacy global arrays.
 */
#ifndef AVP3DS_REAL_SOUND_BACKEND
WEAK unsigned char ActiveSounds[262144]
    __attribute__((aligned(16)));

WEAK unsigned char GameSounds[262144]
    __attribute__((aligned(16)));
#endif

WEAK unsigned char FmvColourGreen[16]
    __attribute__((aligned(16)));

WEAK unsigned char FmvColourRed[16]
    __attribute__((aligned(16)));

WEAK unsigned char FmvColourBlue[16]
    __attribute__((aligned(16)));

#endif

#ifndef AVP3DS_REAL_SOUND_BACKEND
WEAK void BlankActiveSound() {}
WEAK void BlankGameSound() {}
#endif
#ifndef AVP3DS_REAL_SOUND_BACKEND
WEAK void CDDA_ChangeVolume() {}
WEAK void CDDA_PlayLoop() {}
WEAK void CDDA_SwitchOn() {}
WEAK int CDPlayerVolume;
#endif
WEAK void CheckCDVolume() {}
WEAK void EndMenuBackgroundBink() {}
WEAK int ExtractWavFile() { return 0; }
WEAK int GetFMVInformation() { return 0; }
WEAK void InitialiseBaseFrequency() {}
WEAK int IntroOutroMoviesAreActive;
WEAK int MoviesAreActive;
WEAK int LoadWavFile() { return 0; }
WEAK int LoadWavFromFastFile() { return 0; }
WEAK void PlatChangeGlobalVolume() {}
WEAK void PlatChangeSoundPitch() {}
WEAK void PlatChangeSoundVolume() {}
WEAK void PlatDo3dSound() {}
WEAK void PlatEndGameSound() {}
WEAK int PlatMaxHWSounds() { return 0; }
WEAK int PlatPlaySound() { return 0; }
WEAK int PlatSoundHasStopped() { return 1; }
WEAK int PlatStartSoundSys() { return 0; }
WEAK void PlatStopSound() {}

WEAK void PlatUpdatePlayer(void) {}
WEAK void UpdateSoundFrequencies(void) {}

WEAK void StartMenuBackgroundBink(void) {}
WEAK int PlayMenuBackgroundBink(void) { return 0; }

WEAK int SmackerSoundVolume = 128;

WEAK void StartFMVAtFrame(int number, int frame)
{
    (void)number;
    (void)frame;
}
