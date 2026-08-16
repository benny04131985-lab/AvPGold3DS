#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL2/SDL.h>

#include "oglfunc.h"

// Base OpenGL / OpenGL ES
avpPFNGLACTIVETEXTUREPROC pglActiveTexture;
avpPFNGLBINDTEXTUREPROC		pglBindTexture;
avpPFNGLBLENDFUNCPROC		pglBlendFunc;
avpPFNGLCLEARPROC			pglClear;
avpPFNGLCLEARCOLORPROC		pglClearColor;
avpPFNGLCULLFACEPROC		pglCullFace;
avpPFNGLDELETETEXTURESPROC		pglDeleteTextures;
avpPFNGLDEPTHFUNCPROC		pglDepthFunc;
avpPFNGLDEPTHMASKPROC		pglDepthMask;
avpPFNGLDEPTHRANGEPROC		pglDepthRange;
avpPFNGLDISABLEPROC		pglDisable;
avpPFNGLDRAWELEMENTSPROC		pglDrawElements;
avpPFNGLENABLEPROC			pglEnable;
avpPFNGLFRONTFACEPROC		pglFrontFace;
avpPFNGLGENTEXTURESPROC		pglGenTextures;
avpPFNGLGETERRORPROC		pglGetError;
avpPFNGLGETFLOATVPROC		pglGetFloatv;
avpPFNGLGETINTEGERVPROC		pglGetIntegerv;
avpPFNGLGETSTRINGPROC		pglGetString;
avpPFNGLGETTEXPARAMETERFVPROC	pglGetTexParameterfv;
avpPFNGLHINTPROC			pglHint;
avpPFNGLPIXELSTOREIPROC		pglPixelStorei;
avpPFNGLPOLYGONOFFSETPROC		pglPolygonOffset;
avpPFNGLREADPIXELSPROC		pglReadPixels;
avpPFNGLTEXIMAGE2DPROC		pglTexImage2D;
avpPFNGLTEXPARAMETERFPROC		pglTexParameterf;
avpPFNGLTEXPARAMETERIPROC		pglTexParameteri;
avpPFNGLTEXSUBIMAGE2DPROC		pglTexSubImage2D;
avpPFNGLVIEWPORTPROC		pglViewport;

// OpenGL 2.1 / OpenGL ES 2.0
avpPFNGLATTACHSHADERPROC pglAttachShader;
avpPFNGLBINDATTRIBLOCATIONPROC pglBindAttribLocation;
avpPFNGLBINDBUFFERPROC pglBindBuffer;
avpPFNGLBUFFERDATAPROC pglBufferData;
avpPFNGLBUFFERSUBDATAPROC pglBufferSubData;
avpPFNGLCREATEPROGRAMPROC pglCreateProgram;
avpPFNGLCREATESHADERPROC pglCreateShader;
avpPFNGLCOMPILESHADERPROC pglCompileShader;
avpPFNGLDELETEBUFFERSPROC pglDeleteBuffers;
avpPFNGLDELETEPROGRAMPROC pglDeleteProgram;
avpPFNGLDELETESHADERPROC pglDeleteShader;
avpPFNGLDISABLEVERTEXATTRIBARRAYPROC pglDisableVertexAttribArray;
avpPFNGLENABLEVERTEXATTRIBARRAYPROC pglEnableVertexAttribArray;
avpPFNGLGENBUFFERSPROC pglGenBuffers;
avpPFNGLGETATTRIBLOCATIONPROC pglGetAttribLocation;
avpPFNGLGETPROGRAMINFOLOGPROC pglGetProgramInfoLog;
avpPFNGLGETPROGRAMIVPROC pglGetProgramiv;
avpPFNGLGETSHADERINFOLOGPROC pglGetShaderInfoLog;
avpPFNGLGETSHADERIVPROC pglGetShaderiv;
avpPFNGLGETUNIFORMLOCATIONPROC pglGetUniformLocation;
avpPFNGLLINKPROGRAMPROC pglLinkProgram;
avpPFNGLSHADERSOURCEPROC pglShaderSource;
avpPFNGLVALIDATEPROGRAMPROC pglValidateProgram;
avpPFNGLVERTEXATTRIBPOINTERPROC pglVertexAttribPointer;
avpPFNGLUNIFORM1IPROC pglUniform1i;
avpPFNGLUNIFORMMATRIX4FVPROC pglUniformMatrix4fv;
avpPFNGLUSEPROGRAMPROC pglUseProgram;

// GL_EXT_framebuffer_object / GL_ARB_framebuffer_object / OpenGL ES 2.0
avpPFNGLBINDFRAMEBUFFERPROC pglBindFramebuffer;
avpPFNGLBINDRENDERBUFFERPROC pglBindRenderbuffer;
avpPFNGLCHECKFRAMEBUFFERSTATUSPROC pglCheckFramebufferStatus;
avpPFNGLDELETEFRAMEBUFFERSPROC pglDeleteFramebuffers;
avpPFNGLDELETERENDERBUFFERSPROC pglDeleteRenderbuffers;
avpPFNGLFRAMEBUFFERRENDERBUFFERPROC pglFramebufferRenderbuffer;
avpPFNGLFRAMEBUFFERTEXTURE2DPROC pglFramebufferTexture2D;
avpPFNGLGENERATEMIPMAPPROC pglGenerateMipmap;
avpPFNGLGENFRAMEBUFFERSPROC pglGenFramebuffers;
avpPFNGLGENRENDERBUFFERSPROC pglGenRenderbuffers;
avpPFNGLRENDERBUFFERSTORAGEPROC pglRenderbufferStorage;

int ogl_have_multisample_filter_hint;
int ogl_have_texture_filter_anisotropic;
int ogl_have_framebuffer_object;

int ogl_use_multisample_filter_hint;
int ogl_use_texture_filter_anisotropic;
int ogl_use_framebuffer_object;

static void dummyfunc()
{
}

#ifdef __3DS__

/*
 * Temporary null OpenGL backend for 3DS engine bring-up.
 *
 * This prevents every unsupported desktop pgl* call from branching through
 * NULL. It does not render gameplay geometry. It only lets AvP continue far
 * enough to test level loading, gameplay logic, input, and engine stability.
 */

static GLuint null_gl_next_id = 1;

static void APIENTRY null_gl_void(void)
{
}

static GLuint APIENTRY null_gl_create_id(void)
{
    return null_gl_next_id++;
}

static void APIENTRY null_gl_gen_ids(GLsizei count, GLuint *ids)
{
    GLsizei i;

    if (ids == NULL)
        return;

    for (i = 0; i < count; ++i)
        ids[i] = null_gl_next_id++;
}

static GLenum APIENTRY null_gl_get_error(void)
{
    return GL_NO_ERROR;
}

static const GLubyte *APIENTRY null_gl_get_string(GLenum name)
{
    (void)name;
    return (const GLubyte *)"AvP 3DS Null OpenGL";
}

static void APIENTRY null_gl_get_floatv(GLenum name, GLfloat *value)
{
    (void)name;

    if (value != NULL)
        *value = 1.0f;
}

static void APIENTRY null_gl_get_integerv(GLenum name, GLint *value)
{
    if (value == NULL)
        return;

#ifdef GL_MAX_TEXTURE_SIZE
    if (name == GL_MAX_TEXTURE_SIZE) {
        *value = 1024;
        return;
    }
#endif

    *value = 0;
}

static void APIENTRY null_gl_get_tex_parameterfv(
    GLenum target,
    GLenum name,
    GLfloat *value)
{
    (void)target;
    (void)name;

    if (value != NULL)
        *value = 1.0f;
}

static GLint APIENTRY null_gl_get_location(
    GLuint object,
    const GLchar *name)
{
    (void)object;
    (void)name;
    return 0;
}

static void APIENTRY null_gl_get_object_iv(
    GLuint object,
    GLenum name,
    GLint *value)
{
    (void)object;

    if (value == NULL)
        return;

#ifdef GL_INFO_LOG_LENGTH
    if (name == GL_INFO_LOG_LENGTH) {
        *value = 0;
        return;
    }
#endif

    *value = GL_TRUE;
}

static void APIENTRY null_gl_get_info_log(
    GLuint object,
    GLsizei bufferSize,
    GLsizei *length,
    GLchar *buffer)
{
    (void)object;

    if (length != NULL)
        *length = 0;

    if (buffer != NULL && bufferSize > 0)
        buffer[0] = '\0';
}

static GLenum APIENTRY null_gl_framebuffer_status(GLenum target)
{
    (void)target;

#ifdef GL_FRAMEBUFFER_COMPLETE
    return GL_FRAMEBUFFER_COMPLETE;
#else
    return (GLenum)0x8CD5;
#endif
}

static void load_ogl_functions_3ds_null(void)
{
#define NULL_GL_VOID(type, variable) \
    variable = (type)null_gl_void

    /* Base OpenGL. */
    NULL_GL_VOID(avpPFNGLACTIVETEXTUREPROC, pglActiveTexture);
    NULL_GL_VOID(avpPFNGLBINDTEXTUREPROC, pglBindTexture);
    NULL_GL_VOID(avpPFNGLBLENDFUNCPROC, pglBlendFunc);
    NULL_GL_VOID(avpPFNGLCLEARPROC, pglClear);
    NULL_GL_VOID(avpPFNGLCLEARCOLORPROC, pglClearColor);
    NULL_GL_VOID(avpPFNGLCULLFACEPROC, pglCullFace);
    NULL_GL_VOID(avpPFNGLDELETETEXTURESPROC, pglDeleteTextures);
    NULL_GL_VOID(avpPFNGLDEPTHFUNCPROC, pglDepthFunc);
    NULL_GL_VOID(avpPFNGLDEPTHMASKPROC, pglDepthMask);
    NULL_GL_VOID(avpPFNGLDEPTHRANGEPROC, pglDepthRange);
    NULL_GL_VOID(avpPFNGLDISABLEPROC, pglDisable);
    NULL_GL_VOID(avpPFNGLDRAWELEMENTSPROC, pglDrawElements);
    NULL_GL_VOID(avpPFNGLENABLEPROC, pglEnable);
    NULL_GL_VOID(avpPFNGLFRONTFACEPROC, pglFrontFace);
    pglGenTextures = null_gl_gen_ids;
    pglGetError = null_gl_get_error;
    pglGetFloatv = null_gl_get_floatv;
    pglGetIntegerv = null_gl_get_integerv;
    pglGetString = null_gl_get_string;
    pglGetTexParameterfv = null_gl_get_tex_parameterfv;
    NULL_GL_VOID(avpPFNGLHINTPROC, pglHint);
    NULL_GL_VOID(avpPFNGLPIXELSTOREIPROC, pglPixelStorei);
    NULL_GL_VOID(avpPFNGLPOLYGONOFFSETPROC, pglPolygonOffset);
    NULL_GL_VOID(avpPFNGLREADPIXELSPROC, pglReadPixels);
    NULL_GL_VOID(avpPFNGLTEXIMAGE2DPROC, pglTexImage2D);
    NULL_GL_VOID(avpPFNGLTEXPARAMETERFPROC, pglTexParameterf);
    NULL_GL_VOID(avpPFNGLTEXPARAMETERIPROC, pglTexParameteri);
    NULL_GL_VOID(avpPFNGLTEXSUBIMAGE2DPROC, pglTexSubImage2D);
    NULL_GL_VOID(avpPFNGLVIEWPORTPROC, pglViewport);

    /* Shader and buffer API. */
    NULL_GL_VOID(avpPFNGLATTACHSHADERPROC, pglAttachShader);
    NULL_GL_VOID(
        avpPFNGLBINDATTRIBLOCATIONPROC,
        pglBindAttribLocation);
    NULL_GL_VOID(avpPFNGLBINDBUFFERPROC, pglBindBuffer);
    NULL_GL_VOID(avpPFNGLBUFFERDATAPROC, pglBufferData);
    NULL_GL_VOID(avpPFNGLBUFFERSUBDATAPROC, pglBufferSubData);
    pglCreateProgram = null_gl_create_id;
    pglCreateShader =
        (avpPFNGLCREATESHADERPROC)null_gl_create_id;
    NULL_GL_VOID(avpPFNGLCOMPILESHADERPROC, pglCompileShader);
    NULL_GL_VOID(avpPFNGLDELETEBUFFERSPROC, pglDeleteBuffers);
    NULL_GL_VOID(avpPFNGLDELETEPROGRAMPROC, pglDeleteProgram);
    NULL_GL_VOID(avpPFNGLDELETESHADERPROC, pglDeleteShader);
    NULL_GL_VOID(
        avpPFNGLDISABLEVERTEXATTRIBARRAYPROC,
        pglDisableVertexAttribArray);
    NULL_GL_VOID(
        avpPFNGLENABLEVERTEXATTRIBARRAYPROC,
        pglEnableVertexAttribArray);
    pglGenBuffers = null_gl_gen_ids;
    pglGetAttribLocation = null_gl_get_location;
    pglGetProgramInfoLog = null_gl_get_info_log;
    pglGetProgramiv = null_gl_get_object_iv;
    pglGetShaderInfoLog = null_gl_get_info_log;
    pglGetShaderiv = null_gl_get_object_iv;
    pglGetUniformLocation = null_gl_get_location;
    NULL_GL_VOID(avpPFNGLLINKPROGRAMPROC, pglLinkProgram);
    NULL_GL_VOID(avpPFNGLSHADERSOURCEPROC, pglShaderSource);
    NULL_GL_VOID(
        avpPFNGLVALIDATEPROGRAMPROC,
        pglValidateProgram);
    NULL_GL_VOID(
        avpPFNGLVERTEXATTRIBPOINTERPROC,
        pglVertexAttribPointer);
    NULL_GL_VOID(avpPFNGLUNIFORM1IPROC, pglUniform1i);
    NULL_GL_VOID(
        avpPFNGLUNIFORMMATRIX4FVPROC,
        pglUniformMatrix4fv);
    NULL_GL_VOID(avpPFNGLUSEPROGRAMPROC, pglUseProgram);

    /* Framebuffer API. */
    NULL_GL_VOID(
        avpPFNGLBINDFRAMEBUFFERPROC,
        pglBindFramebuffer);
    NULL_GL_VOID(
        avpPFNGLBINDRENDERBUFFERPROC,
        pglBindRenderbuffer);
    pglCheckFramebufferStatus = null_gl_framebuffer_status;
    NULL_GL_VOID(
        avpPFNGLDELETEFRAMEBUFFERSPROC,
        pglDeleteFramebuffers);
    NULL_GL_VOID(
        avpPFNGLDELETERENDERBUFFERSPROC,
        pglDeleteRenderbuffers);
    NULL_GL_VOID(
        avpPFNGLFRAMEBUFFERRENDERBUFFERPROC,
        pglFramebufferRenderbuffer);
    NULL_GL_VOID(
        avpPFNGLFRAMEBUFFERTEXTURE2DPROC,
        pglFramebufferTexture2D);
    NULL_GL_VOID(
        avpPFNGLGENERATEMIPMAPPROC,
        pglGenerateMipmap);
    pglGenFramebuffers = null_gl_gen_ids;
    pglGenRenderbuffers = null_gl_gen_ids;
    NULL_GL_VOID(
        avpPFNGLRENDERBUFFERSTORAGEPROC,
        pglRenderbufferStorage);

    ogl_have_multisample_filter_hint = 0;
    ogl_have_texture_filter_anisotropic = 0;
    ogl_have_framebuffer_object = 0;

    ogl_use_multisample_filter_hint = 0;
    ogl_use_texture_filter_anisotropic = 0;
    ogl_use_framebuffer_object = 0;

#undef NULL_GL_VOID
}

#endif

#define LoadOGLProc_(type, func, name) {                    \
	if (!mode) p##func = (type) dummyfunc; else			\
	p##func = (type) SDL_GL_GetProcAddress(#name);			\
	if (p##func == NULL) {						\
		if (!ogl_missing_func) ogl_missing_func = #func;	\
	}								\
}

#define LoadOGLProc(type, func)						\
	LoadOGLProc_(type, func, func)

#define LoadOGLProc2(type, func1, func2)					\
	LoadOGLProc_(type, func1, func1); \
	if (p##func1 == NULL) { \
		ogl_missing_func = NULL; \
		LoadOGLProc_(type, func1, func2); \
	}

#define LoadOGLExtProc(e, type, func)						\
	if ((e)) { \
		LoadOGLProc(type, func); \
	} else { \
		p##func = NULL; \
	}

#define LoadOGLExtProc2(e, type, func1, func2)					\
	if ((e)) { \
		LoadOGLProc2(type, func1, func2); \
	} else { \
		p##func = NULL; \
	}

static int check_token(const char *string, const char *token)
{
	const char *s = string;
	int len = strlen(token);
	
	while ((s = strstr(s, token)) != NULL) {
		const char *next = s + len;
		
		if ((s == string || *(s-1) == ' ') &&
			(*next == 0 || *next == ' ')) {
			
			return 1;
		}
		
		s = next;
	}
	
	return 0;
}

void load_ogl_functions(int mode)
{
#ifdef __3DS__
    (void)mode;
    load_ogl_functions_3ds_null();
    return;
#endif
	const char* ogl_missing_func;
	const char* ext;

	int base_framebuffer_object;
	int ext_framebuffer_object;
	int arb_framebuffer_object;

	ogl_missing_func = NULL;
	
	// Base OpenGL / OpenGL ES
	LoadOGLProc(avpPFNGLACTIVETEXTUREPROC, glActiveTexture);
	LoadOGLProc(avpPFNGLBINDTEXTUREPROC, glBindTexture);
	LoadOGLProc(avpPFNGLBLENDFUNCPROC, glBlendFunc);
	LoadOGLProc(avpPFNGLCLEARPROC, glClear);
	LoadOGLProc(avpPFNGLCLEARCOLORPROC, glClearColor);
	LoadOGLProc(avpPFNGLCULLFACEPROC, glCullFace);
	LoadOGLProc(avpPFNGLDELETETEXTURESPROC, glDeleteTextures);
	LoadOGLProc(avpPFNGLDEPTHFUNCPROC, glDepthFunc);
	LoadOGLProc(avpPFNGLDEPTHMASKPROC, glDepthMask);
	LoadOGLProc2(avpPFNGLDEPTHRANGEPROC, glDepthRange, glDepthRangef);
	LoadOGLProc(avpPFNGLDISABLEPROC, glDisable);
	LoadOGLProc(avpPFNGLDRAWELEMENTSPROC, glDrawElements);
	LoadOGLProc(avpPFNGLENABLEPROC, glEnable);
	LoadOGLProc(avpPFNGLFRONTFACEPROC, glFrontFace);
	LoadOGLProc(avpPFNGLGENTEXTURESPROC, glGenTextures);
	LoadOGLProc(avpPFNGLGETERRORPROC, glGetError);
	LoadOGLProc(avpPFNGLGETFLOATVPROC, glGetFloatv);
	LoadOGLProc(avpPFNGLGETINTEGERVPROC, glGetIntegerv);
	LoadOGLProc(avpPFNGLGETSTRINGPROC, glGetString);
	LoadOGLProc(avpPFNGLGETTEXPARAMETERFVPROC, glGetTexParameterfv);
	LoadOGLProc(avpPFNGLHINTPROC, glHint);
	LoadOGLProc(avpPFNGLPIXELSTOREIPROC, glPixelStorei);
	LoadOGLProc(avpPFNGLPOLYGONOFFSETPROC, glPolygonOffset);
	LoadOGLProc(avpPFNGLREADPIXELSPROC, glReadPixels);
	LoadOGLProc(avpPFNGLTEXIMAGE2DPROC, glTexImage2D);
	LoadOGLProc(avpPFNGLTEXPARAMETERFPROC, glTexParameterf);
	LoadOGLProc(avpPFNGLTEXPARAMETERIPROC, glTexParameteri);
	LoadOGLProc(avpPFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D);
	LoadOGLProc(avpPFNGLVIEWPORTPROC, glViewport);

	// OpenGL 2.1 / OpenGL ES 2.0
	LoadOGLProc(avpPFNGLATTACHSHADERPROC, glAttachShader);
	LoadOGLProc(avpPFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation);
	LoadOGLProc(avpPFNGLBINDBUFFERPROC, glBindBuffer);
	LoadOGLProc(avpPFNGLBUFFERDATAPROC, glBufferData);
	LoadOGLProc(avpPFNGLBUFFERSUBDATAPROC, glBufferSubData);
	LoadOGLProc(avpPFNGLCREATEPROGRAMPROC, glCreateProgram);
	LoadOGLProc(avpPFNGLCREATESHADERPROC, glCreateShader);
	LoadOGLProc(avpPFNGLCOMPILESHADERPROC, glCompileShader);
	LoadOGLProc(avpPFNGLDELETEBUFFERSPROC, glDeleteBuffers);
	LoadOGLProc(avpPFNGLDELETEPROGRAMPROC, glDeleteProgram);
	LoadOGLProc(avpPFNGLDELETESHADERPROC, glDeleteShader);
	LoadOGLProc(avpPFNGLDISABLEVERTEXATTRIBARRAYPROC, glDisableVertexAttribArray);
	LoadOGLProc(avpPFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray);
	LoadOGLProc(avpPFNGLGENBUFFERSPROC, glGenBuffers);
	LoadOGLProc(avpPFNGLGETATTRIBLOCATIONPROC, glGetAttribLocation);
	LoadOGLProc(avpPFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog);
	LoadOGLProc(avpPFNGLGETPROGRAMIVPROC, glGetProgramiv);
	LoadOGLProc(avpPFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog);
	LoadOGLProc(avpPFNGLGETSHADERIVPROC, glGetShaderiv);
	LoadOGLProc(avpPFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation);
	LoadOGLProc(avpPFNGLLINKPROGRAMPROC, glLinkProgram);
	LoadOGLProc(avpPFNGLSHADERSOURCEPROC, glShaderSource);
	LoadOGLProc(avpPFNGLVALIDATEPROGRAMPROC, glValidateProgram);
	LoadOGLProc(avpPFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer);
	LoadOGLProc(avpPFNGLUNIFORM1IPROC, glUniform1i);
	LoadOGLProc(avpPFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv);
	LoadOGLProc(avpPFNGLUSEPROGRAMPROC, glUseProgram);

	if (!mode) {
		return;
	}
	
	if (ogl_missing_func) {
		fprintf(stderr, "Unable to load OpenGL Library: missing function %s\n", ogl_missing_func);
		exit(EXIT_FAILURE);
	}
	
#if !defined(NDEBUG)
	printf("GL_VENDOR: %s\n", pglGetString(GL_VENDOR));
	printf("GL_RENDERER: %s\n", pglGetString(GL_RENDERER));
	printf("GL_VERSION: %s\n", pglGetString(GL_VERSION));
	printf("GL_SHADING_LANGUAGE_VERSION: %s\n", pglGetString(GL_SHADING_LANGUAGE_VERSION));
	printf("GL_EXTENSIONS: %s\n", pglGetString(GL_EXTENSIONS));
#endif

	ext = (const char *) pglGetString(GL_EXTENSIONS);

	// GL_EXT_framebuffer_object / GL_ARB_framebuffer_object / OpenGL ES 2.0
	// figure out which version of framebuffer objects to use, if any
	ext_framebuffer_object = check_token(ext, "GL_EXT_framebuffer_object");
	arb_framebuffer_object = check_token(ext, "GL_ARB_framebuffer_object");

#if defined(USE_OPENGL_ES)
	// not quite right as ARB fbo includes functionality not present in ES2.
	base_framebuffer_object = 1;
#else
	base_framebuffer_object = arb_framebuffer_object;
#endif

	ogl_missing_func = NULL;
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLGENERATEMIPMAPPROC, glGenerateMipmap);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers);
	LoadOGLExtProc(base_framebuffer_object, avpPFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage);
	if (base_framebuffer_object != 0 && ogl_missing_func == NULL) {
		ogl_have_framebuffer_object = 1;

#if !defined(NDEBUG)
		printf("ARB/ES2 framebuffer objects enabled.\n");
#endif
	}

	if (ext_framebuffer_object != 0 && ogl_have_framebuffer_object == 0) {
		// try the EXT suffixed functions
		ogl_missing_func = NULL;
		LoadOGLProc_(avpPFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer, glBindFramebufferEXT);
		LoadOGLProc_(avpPFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer, glBindRenderbufferEXT);
		LoadOGLProc_(avpPFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus, glCheckFramebufferStatusEXT);
		LoadOGLProc_(avpPFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers, glDeleteFramebuffersEXT);
		LoadOGLProc_(avpPFNGLDELETERENDERBUFFERSPROC, glDeleteRenderbuffers, glDeleteRenderbuffersEXT);
		LoadOGLProc_(avpPFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer, glFramebufferRenderbufferEXT);
		LoadOGLProc_(avpPFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D, glFramebufferTexture2DEXT);
		LoadOGLProc_(avpPFNGLGENERATEMIPMAPPROC, glGenerateMipmap, glGenerateMipmapEXT);
		LoadOGLProc_(avpPFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers, glGenFramebuffersEXT);
		LoadOGLProc_(avpPFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers, glGenRenderbuffersEXT);
		LoadOGLProc_(avpPFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage, glRenderbufferStorageEXT);
		if (ogl_missing_func == NULL) {
			ogl_have_framebuffer_object = 1;

#if !defined(NDEBUG)
			printf("EXT framebuffer objects enabled.\n");
#endif
		}
	}

	// other extensions
	ogl_have_multisample_filter_hint = check_token(ext, "GL_NV_multisample_filter_hint");
	ogl_have_texture_filter_anisotropic = check_token(ext, "GL_EXT_texture_filter_anisotropic");

	ogl_use_multisample_filter_hint = ogl_have_multisample_filter_hint;
	ogl_use_texture_filter_anisotropic = ogl_have_texture_filter_anisotropic;
	ogl_use_framebuffer_object = ogl_have_framebuffer_object;
}

int check_for_errors_(const char *file, int line)
{
	GLenum error;
	int diderror = 0;
	
	while ((error = pglGetError()) != GL_NO_ERROR) {
		fprintf(stderr, "OPENGL ERROR: %04X (%s:%d)\n", error, file, line);
		
		diderror = 1;
	}
	
	return diderror;
}
