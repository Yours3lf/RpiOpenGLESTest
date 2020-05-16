#define _POSIX_C_SOURCE 199309L

//#define USE_PBUFFER //works without x11, but not remotely
//#define USE_X11 //needs x11 (desktop ofc)
#define USE_KMS //works remotely
//#define USE_SURFACELESS //works remotely

#include <gbm.h>
#include <drm/drm.h>
#include <xf86drmMode.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <assert.h>

#define EGL_EGLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglmesaext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
static Display *x_display = NULL;
#endif

#include <dlfcn.h>

enum vertexShaderType
{
	SingleTextureVS = 0,
	SingleTextureClippingPlaneVS,
	MultiTextureVS,
	MultiTextureClippingPlaneVS,
};

enum fragmentShaderType
{
	SingleTextureFS = 0,
	MultiTextureMulFS,
	MultiTextureAddFS,
	SingleTextureClippingPlaneFS,
	MultiTextureMulClippingPlaneFS,
	MultiTextureAddClippingPlaneFS,
};

enum alphaTestType
{
	AlphaTestDisabled = 0,
	AlphaTestGE80 = 3,
	AlphaTestGT0 = 1,
	AlphaTestLT80 = 2
};

const char* vertexShaders[] =
{
	"#version 100\n"
	"precision highp float;\n"
	"uniform mat4 mvp;\n"
	"attribute vec3 in_position;\n"
	"attribute vec4 in_color;\n"
	"attribute vec2 in_tex_coord;\n"
	"varying vec4 frag_color;\n"
	"varying vec2 frag_tex_coord;\n"
	"void main() {\n"
		"gl_Position = mvp *  vec4(in_position, 1.0);\n"
		"frag_color = in_color;\n"
		"frag_tex_coord = in_tex_coord;\n"
	"}\n",

	"#version 100\n"
	"precision highp float;\n"
	"uniform mat4 clip_space_xform;\n"
	"uniform mat4 eye_space_xform;\n"
	"uniform vec4 clipping_plane;\n"
	"attribute vec3 in_position;\n"
	"attribute vec4 in_color;\n"
	"attribute vec2 in_tex_coord;\n"
	"varying vec4 frag_color;\n"
	"varying vec2 frag_tex_coord;\n"
	"varying float clipDistance;\n"
	"void main() {\n"
		"vec4 p = vec4(in_position, 1.0);\n"
		"gl_Position = clip_space_xform * p;\n"
		"clipDistance = dot(clipping_plane, vec4( p * eye_space_xform ));\n"
		"frag_color = in_color;\n"
		"frag_tex_coord = in_tex_coord;\n"
	"}",

	"#version 100\n"
	"precision highp float;\n"
	"uniform mat4 mvp;\n"
	"attribute vec3 in_position;\n"
	"attribute vec4 in_color;\n"
	"attribute vec2 in_tex_coord0;\n"
	"attribute vec2 in_tex_coord1;\n"
	"varying vec4 frag_color;\n"
	"varying vec2 frag_tex_coord0;\n"
	"varying vec2 frag_tex_coord1;\n"
	"void main() {\n"
		"gl_Position = mvp * vec4(in_position, 1.0);\n"
		"frag_color = in_color;\n"
		"frag_tex_coord0 = in_tex_coord0;\n"
		"frag_tex_coord1 = in_tex_coord1;\n"
	"}\n",

	"#version 100\n"
	"precision highp float;\n"
	"uniform mat4 clip_space_xform;\n"
	"uniform mat4 eye_space_xform;\n"
	"uniform vec4 clipping_plane;\n"
	"attribute vec3 in_position;\n"
	"attribute vec4 in_color;\n"
	"attribute vec2 in_tex_coord0;\n"
	"attribute vec2 in_tex_coord1;\n"
	"varying vec4 frag_color;\n"
	"varying vec2 frag_tex_coord0;\n"
	"varying vec2 frag_tex_coord1;\n"
	"varying float clipDistance;\n"
	"void main() {\n"
		"vec4 p = vec4(in_position, 1.0);\n"
		"gl_Position = clip_space_xform * p;\n"
		"clipDistance = dot(clipping_plane, vec4( p * eye_space_xform ));\n"
		"frag_color = in_color;\n"
		"frag_tex_coord0 = in_tex_coord0;\n"
		"frag_tex_coord1 = in_tex_coord1;\n"
	"}\n"
};

const char* fragShaders[][4] =
{
	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n"
	},
	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n",
	},
	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"void main() {\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n",
	},



	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n"
	},
	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord0) * texture2D(texture1, frag_tex_coord1);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n",
	},
	{
		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a == 0.0) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a >= 0.5) discard;\n"
		"}\n",

		"#version 100\n"
		"precision highp float;\n"
		"uniform sampler2D texture0;\n"
		"uniform sampler2D texture1;\n"
		"varying vec4 frag_color;\n"
		"varying vec2 frag_tex_coord0;\n"
		"varying vec2 frag_tex_coord1;\n"
		"varying float clipDistance;\n"
		"void main() {\n"
			"if(clipDistance < 0.0) discard;\n"
			"vec4 color_a = frag_color * texture2D(texture0, frag_tex_coord0);\n"
			"vec4 color_b = texture2D(texture1, frag_tex_coord1);\n"
			"gl_FragColor = vec4(color_a.rgb + color_b.rgb, color_a.a * color_b.a);\n"
			"if (gl_FragColor.a < 0.5) discard;\n"
		"}\n",
	},
};

typedef void (GL_APIENTRY  *GLINVALIDATEFRAMEBUFFER)(GLenum target,
													 GLsizei numAttachments,
													 const GLenum *attachments);

GLINVALIDATEFRAMEBUFFER glInvalidateFramebuffer;

static const EGLint configAttribs[] =
{
#ifdef USE_PBUFFER
	EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
#endif
#ifdef USE_X11
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
#endif
#ifdef USE_KMS
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
#endif

	// Uncomment the following to enable MSAA
	//EGL_SAMPLE_BUFFERS, 1, // <-- Must be set to 1 to enable multisampling!
	//EGL_SAMPLES, 4, // <-- Number of samples

	EGL_DEPTH_SIZE, 24,
	EGL_ALPHA_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_RED_SIZE, 8,

	// Uncomment the following to enable stencil buffer
	EGL_STENCIL_SIZE, 8,

	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	//EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
	EGL_NONE
};

static const EGLint contextAttribs[] =
{
	EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_NONE
};

// The following array holds vec3 data of
// three vertex positions
static const GLfloat vertices1[] =
{
	0, 0, 0,
	0, 0, 0,
	0, 0, 0
};

static const GLfloat vertices2[] =
{
	0, 0, 0, 0,
	0, 0, 0, 0,
	0, 0, 0, 0
};

static const GLfloat vertices3[] =
{
	0, 0,
	0, 0,
	0, 0,
};

//// The following are GLSL shaders for rendering a triangle on the screen
//static const char* vertexShaderCode =
//"#version 100\n"
//"precision highp float;\n"
//"uniform mat4 mvp;\n"
//"attribute vec3 in_position;\n"
//"attribute vec4 in_color;\n"
//"attribute vec2 in_tex_coord;\n"
//"varying vec4 frag_color;\n"
//"varying vec2 frag_tex_coord;\n"
//"void main() {\n"
//"gl_Position = mvp *  vec4(in_position, 1.0);\n"
//"frag_color = in_color;\n"
//"frag_tex_coord = in_tex_coord;\n"
//"}"
//;

//static const char* fragmentShaderCode =
//"#version 100\n"
//"precision mediump float;\n"
//"uniform sampler2D texture0;\n"
//"varying vec4 frag_color;\n"
//"varying vec2 frag_tex_coord;\n"
//"int alpha_test_func = 0;\n"
//"void main() {\n"
//"gl_FragColor = frag_color * texture2D(texture0, frag_tex_coord);\n"
//"}"
//;

#ifdef USE_KMS
struct kms
{
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   drmModeModeInfo mode;
   uint32_t fb_id;
};

static EGLBoolean setup_kms(int fd, struct kms *kms)
{
   drmModeRes *resources;
   drmModeConnector *connector;
   drmModeEncoder *encoder;
   int i;

   resources = drmModeGetResources(fd);
   if (!resources) {
	  fprintf(stderr, "drmModeGetResources failed\n");
	  return EGL_FALSE;
   }

   for (i = 0; i < resources->count_connectors; i++) {
	  connector = drmModeGetConnector(fd, resources->connectors[i]);
	  if (connector == NULL)
	 continue;

	  if (connector->connection == DRM_MODE_CONNECTED &&
	  connector->count_modes > 0)
	 break;

	  drmModeFreeConnector(connector);
   }

   if (i == resources->count_connectors) {
	  fprintf(stderr, "No currently active connector found.\n");
	  return EGL_FALSE;
   }

   for (i = 0; i < resources->count_encoders; i++) {
	  encoder = drmModeGetEncoder(fd, resources->encoders[i]);

	  if (encoder == NULL)
	 continue;

	  if (encoder->encoder_id == connector->encoder_id)
	 break;

	  drmModeFreeEncoder(encoder);
   }

   kms->connector = connector;
   kms->encoder = encoder;
   kms->mode = connector->modes[0];

   return EGL_TRUE;
}
#endif

#ifdef USE_X11
EGLNativeWindowType createXWindow(unsigned width, unsigned height)
{
	Window root;
	XSetWindowAttributes swa;
	XSetWindowAttributes  xattr;
	Atom wm_state;
	XWMHints hints;
	XEvent xev;
	Window win;

	/*
	 * X11 native display initialization
	 */

	x_display = XOpenDisplay(NULL);
	if ( x_display == NULL )
	{
		return EGL_FALSE;
	}

	root = DefaultRootWindow(x_display);

	swa.event_mask  =  ExposureMask;
	win = XCreateWindow(
			   x_display, root,
			   0, 0, width, height, 0,
			   CopyFromParent, InputOutput,
			   CopyFromParent, CWEventMask,
			   &swa );

	xattr.override_redirect = 0;
	XChangeWindowAttributes ( x_display, win, CWOverrideRedirect, &xattr );

	hints.input = 1;
	hints.flags = InputHint;
	XSetWMHints(x_display, win, &hints);

	// make the window visible on the screen
	XMapWindow (x_display, win);
	XStoreName (x_display, win, "title");

	// get identifiers for the provided atom name strings
	wm_state = XInternAtom (x_display, "_NET_WM_STATE", 0);

	memset ( &xev, 0, sizeof(xev) );
	xev.type                 = ClientMessage;
	xev.xclient.window       = win;
	xev.xclient.message_type = wm_state;
	xev.xclient.format       = 32;
	xev.xclient.data.l[0]    = 1;
	xev.xclient.data.l[1]    = 0;
	XSendEvent (
	   x_display,
	   DefaultRootWindow ( x_display ),
	   0,
	   SubstructureNotifyMask,
	   &xev );

	return (EGLNativeWindowType) win;
}
#endif

static const char* eglGetErrorStr()
{
	switch(eglGetError())
	{
	case EGL_SUCCESS: return "The last function succeeded without error.";
	case EGL_NOT_INITIALIZED: return "EGL is not initialized, or could not be initialized, for the specified EGL display connection.";
	case EGL_BAD_ACCESS: return "EGL cannot access a requested resource (for example a context is bound in another thread).";
	case EGL_BAD_ALLOC: return "EGL failed to allocate resources for the requested operation.";
	case EGL_BAD_ATTRIBUTE: return "An unrecognized attribute or attribute value was passed in the attribute list.";
	case EGL_BAD_CONTEXT: return "An EGLContext argument does not name a valid EGL rendering context.";
	case EGL_BAD_CONFIG: return "An EGLConfig argument does not name a valid EGL frame buffer configuration.";
	case EGL_BAD_CURRENT_SURFACE: return "The current surface of the calling thread is a window, pixel buffer or pixmap that is no longer valid.";
	case EGL_BAD_DISPLAY: return "An EGLDisplay argument does not name a valid EGL display connection.";
	case EGL_BAD_SURFACE: return "An EGLSurface argument does not name a valid surface (window, pixel buffer or pixmap) configured for GL rendering.";
	case EGL_BAD_MATCH: return "Arguments are inconsistent (for example, a valid context requires buffers not supplied by a valid surface).";
	case EGL_BAD_PARAMETER: return "One or more argument values are invalid.";
	case EGL_BAD_NATIVE_PIXMAP: return "A NativePixmapType argument does not refer to a valid native pixmap.";
	case EGL_BAD_NATIVE_WINDOW: return "A NativeWindowType argument does not refer to a valid native window.";
	case EGL_CONTEXT_LOST: return "A power management event has occurred. The application must destroy all contexts and reinitialise OpenGL ES state and objects to continue rendering.";
	default: break;
	}
	return "Unknown EGL error!";
}

static const char* glGetErrorStr()
{
	switch(glGetError())
	{
	case GL_NO_ERROR: return "The last function succeeded without error.";
	case GL_INVALID_ENUM: return "GL invalid enum.";
	case GL_INVALID_VALUE: return "GL invalid value.";
	case GL_INVALID_OPERATION: return "GL invalid operation.";
	case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL invalid framebuffer operation.";
	case GL_OUT_OF_MEMORY: return "GL out of memory.";
	default: break;
	}

	return "Unknown GL error!";
}

static void printConfigInfo(int i, EGLDisplay display, EGLConfig* config)
{
	EGLint val;
	eglGetConfigAttrib(display, *config, EGL_RED_SIZE, &val); fprintf(stderr, "EGL_RED_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_GREEN_SIZE, &val); fprintf(stderr, "EGL_GREEN_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_BLUE_SIZE, &val); fprintf(stderr, "EGL_BLUE_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_ALPHA_SIZE, &val); fprintf(stderr, "EGL_ALPHA_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_DEPTH_SIZE, &val); fprintf(stderr, "EGL_DEPTH_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_CONFIG_CAVEAT, &val); fprintf(stderr, "EGL_CONFIG_CAVEAT: %s\n", val == EGL_NONE ? "EGL_NONE" : val == EGL_SLOW_CONFIG ? "EGL_SLOW_CONFIG" : "");
	eglGetConfigAttrib(display, *config, EGL_SAMPLE_BUFFERS, &val); fprintf(stderr, "EGL_SAMPLE_BUFFERS: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_SAMPLES, &val); fprintf(stderr, "EGL_SAMPLES: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_MAX_PBUFFER_WIDTH, &val); fprintf(stderr, "EGL_MAX_PBUFFER_WIDTH: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_MAX_PBUFFER_HEIGHT, &val); fprintf(stderr, "EGL_MAX_PBUFFER_HEIGHT: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_NATIVE_RENDERABLE, &val); fprintf(stderr, "EGL_NATIVE_RENDERABLE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_SURFACE_TYPE, &val); fprintf(stderr, "EGL_SURFACE_TYPE: %i ", val);
	if(val & EGL_WINDOW_BIT) fprintf(stderr, "EGL_WINDOW_BIT ");
	if(val & EGL_PIXMAP_BIT) fprintf(stderr, "EGL_PIXMAP_BIT ");
	if(val & EGL_PBUFFER_BIT) fprintf(stderr, "EGL_PBUFFER_BIT ");
	eglGetConfigAttrib(display, *config, EGL_RENDERABLE_TYPE, &val); fprintf(stderr, "\nEGL_RENDERABLE_TYPE: ");
	if(val&EGL_OPENGL_BIT){ fprintf(stderr, "OpenGL "); } if(val&EGL_OPENGL_ES2_BIT){ fprintf(stderr, "OpenGLES2 "); } fprintf(stderr, "\n");
	fprintf(stderr, "\n");
}

void printExtensions()
{
	const char* extensions = glGetString(GL_EXTENSIONS);
	while(1)
	{
		const char* nextExtensions = strstr(extensions, " ");
		if(!nextExtensions)
		{
			break;
		}

		fprintf(stderr, "%.*s\n", nextExtensions-extensions, extensions);
		extensions = nextExtensions+1;
	}
	fprintf(stderr, "\n");
}

void printEGLExtensions(EGLDisplay display)
{
	const char* extensions = eglQueryString(display, EGL_EXTENSIONS);
	while(extensions && 1)
	{
		const char* nextExtensions = strstr(extensions, " ");
		if(!nextExtensions)
		{
			break;
		}

		fprintf(stderr, "%.*s\n", nextExtensions-extensions, extensions);
		extensions = nextExtensions+1;
	}
	fprintf(stderr, "\n");
}

void printEGLConfigs(EGLDisplay display, int numConfigs)
{
	EGLConfig* configs = malloc(sizeof(EGLConfig) * numConfigs);
	eglGetConfigs(display, configs, numConfigs, &numConfigs);

	for(int i = 0; i < numConfigs; ++i)
	{
		printConfigInfo(i, display, &configs[i]);
	}

	free(configs);
}

int main(int argv, char** argc)
{
	EGLDisplay display;
	int major, minor;
	GLuint program, vert, frag, vbo1, vbo2, vbo3, vbo4;
	GLint posLoc, colorLoc;

#ifdef USE_SURFACELESS
	int fd = open("/dev/dri/renderD128", O_RDWR); assert(fd > 0);
	struct gbm_device* gbm = gbm_create_device(fd); assert(gbm);
	if((display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_MESA, gbm, NULL)) == EGL_NO_DISPLAY)
#endif
#ifdef USE_KMS
	int fd = open("/dev/dri/card0", O_RDWR); assert(fd > 0);
	struct gbm_device* gbm = gbm_create_device(fd); assert(gbm);
	if((display = eglGetDisplay(gbm)) == EGL_NO_DISPLAY)
#endif
#ifdef USE_X11
	if((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY)
#endif
#ifdef USE_PBUFFER
	if((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY)
#endif
	{
		fprintf(stderr, "Failed to get EGL display! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	if(eglInitialize(display, &major, &minor) == EGL_FALSE)
	{
		fprintf(stderr, "Failed to get EGL version! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}

	fprintf(stderr, "\nInitialized EGL version: %d.%d\n\n", major, minor);

	fprintf(stderr, "EGL Vendor: %s\n", eglQueryString(display, EGL_VENDOR));
	fprintf(stderr, "EGL Version: %s\n\n", eglQueryString(display, EGL_VERSION));

	//printEGLExtensions(display);

#ifdef USE_KMS
	struct kms kms;
	int kms_setup = setup_kms(fd, &kms); assert(kms_setup);
#endif

	EGLint numConfigs;
	eglGetConfigs(display, NULL, 0, &numConfigs);

	fprintf(stderr, "EGL has %i configs: \n\n", numConfigs);

	//printEGLConfigs(display, numConfigs);

	EGLConfig config;
	if(eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) != EGL_TRUE)
	{
		fprintf(stderr, "Failed to get EGL config! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}

	//print out chosen config data
	fprintf(stderr, "Config chosen: \n");
	printConfigInfo(0, display, &config);

#define displayWidth 1920
#define displayHeight 1080

	static const EGLint pbufferAttribs[] = {
		EGL_WIDTH, displayWidth,
		EGL_HEIGHT, displayHeight,
		EGL_NONE,
	};
#ifdef USE_KMS
	struct gbm_surface* gs = gbm_surface_create(gbm, kms.mode.hdisplay, kms.mode.vdisplay,
				   GBM_BO_FORMAT_ARGB8888,
				   GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	EGLSurface surface = eglCreateWindowSurface(display, config, gs, NULL);
#endif
#ifdef USE_PBUFFER
	EGLSurface surface = eglCreatePbufferSurface(display, config, pbufferAttribs);
#endif
#ifdef USE_X11
	EGLNativeWindowType window = createXWindow(displayWidth, displayHeight); assert(window != 0);
	//fprintf(stderr, "%i", window);
	EGLSurface surface = eglCreateWindowSurface(display, config, window, NULL);
#endif
#if !defined(USE_SURFACELESS)
	if(surface == EGL_NO_SURFACE)
	{
		fprintf(stderr, "Failed to create EGL surface! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}
#endif

	//EGLBoolean res = eglBindAPI(EGL_OPENGL_API);
	EGLBoolean res = eglBindAPI(EGL_OPENGL_ES_API);
	if(res != EGL_TRUE)
	{
		fprintf(stderr, "Failed to bind GL API to EGL! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
	if(context == EGL_NO_CONTEXT)
	{
		fprintf(stderr, "Failed to create EGL context! Error: %s\n", eglGetErrorStr());
#ifndef USE_SURFACELESS
		eglDestroySurface(display, surface);
#endif
		eglTerminate(display);
		return EGL_FALSE;
	}

#ifdef USE_SURFACELESS
	if(!eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
#else
	if(!eglMakeCurrent(display, surface, surface, context))
#endif
	{
		fprintf(stderr, "Failed to make current! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	////////////////////////////
	//GL Context live from here
	////////////////////////////
	fprintf(stderr, "Vendor: %s\n", glGetString(GL_VENDOR));
	fprintf(stderr, "Renderer: %s\n", glGetString(GL_RENDERER));
	fprintf(stderr, "Version: %s\n", glGetString(GL_VERSION));
	fprintf(stderr, "Shading language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	//	GL_EXT_blend_minmax
	//	GL_EXT_multi_draw_arrays
	//	GL_EXT_texture_format_BGRA8888
	//	GL_OES_compressed_ETC1_RGB8_texture
	//	GL_OES_depth24
	//	GL_OES_element_index_uint
	//	GL_OES_fbo_render_mipmap
	//	GL_OES_mapbuffer
	//	GL_OES_rgb8_rgba8
	//	GL_OES_stencil8
	//	GL_OES_texture_3D
	//	GL_OES_texture_npot
	//	GL_OES_vertex_half_float
	//	GL_OES_EGL_image
	//	GL_OES_depth_texture
	//	GL_OES_packed_depth_stencil
	//	GL_OES_get_program_binary
	//	GL_APPLE_texture_max_level
	//	GL_EXT_discard_framebuffer
	//	GL_EXT_read_format_bgra
	//	GL_EXT_frag_depth
	//	GL_NV_fbo_color_attachments
	//	GL_OES_EGL_image_external
	//	GL_OES_EGL_sync
	//	GL_OES_vertex_array_object
	//	GL_EXT_occlusion_query_boolean
	//	GL_EXT_unpack_subimage
	//	GL_NV_draw_buffers
	//	GL_NV_read_buffer
	//	GL_NV_read_depth
	//	GL_NV_read_depth_stencil
	//	GL_NV_read_stencil
	//	GL_EXT_draw_buffers
	//	GL_EXT_map_buffer_range
	//	GL_KHR_debug
	//	GL_KHR_texture_compression_astc_ldr
	//	GL_OES_required_internalformat
	//	GL_OES_surfaceless_context
	//	GL_EXT_separate_shader_objects
	//	GL_EXT_compressed_ETC1_RGB8_sub_texture
	//	GL_EXT_draw_elements_base_vertex
	//	GL_EXT_texture_border_clamp
	//	GL_KHR_context_flush_control
	//	GL_OES_draw_elements_base_vertex
	//	GL_OES_texture_border_clamp
	//	GL_KHR_no_error
	//	GL_KHR_texture_compression_astc_sliced_3d
	//	GL_KHR_parallel_shader_compile
	//	GL_MESA_tile_raster_order
	//printExtensions();

	//void* lib = dlopen("libGLESv2.so", RTLD_NOW);
	//glInvalidateFramebuffer = (GLINVALIDATEFRAMEBUFFER)dlsym(lib, "glInvalidateFramebuffer");
	//fprintf(stderr, "lib: %p\n", lib);
	//fprintf(stderr, "func: %p\n", glInvalidateFramebuffer);

	// Set GL Viewport size, always needed!
	glViewport(300, 50, 144, 108);
	glScissor(300, 50, 144, 108);
	//glScissor(0, 0, displayWidth, displayHeight);

	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(-2, -1);

	// Get GL Viewport size and test if it is correct.
	// NOTE! DO NOT UPDATE EGL LIBRARY ON RASPBERRY PI AS IT WILL INSTALL FAKE EGL!
	// If you have fake/faulty EGL library, the glViewport and glGetIntegerv won't work!
	// The following piece of code checks if the gl functions are working as intended!
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	// viewport[2] and viewport[3] are viewport width and height respectively
	fprintf(stderr, "GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

	// Clear whole screen (front buffer)
	glClearColor(0.4f, 0.6f, 0.9f, 1.0f);
	//glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

#ifdef USE_SURFACELESS
	GLuint tex = 0;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, displayWidth, displayHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	glBindTexture(GL_TEXTURE_2D, 0);

	GLuint fbo = 0;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
#endif

	//=======================================================================
	//=======================================================================
	//=======================================================================
	//=======================================================================
	//=======================================================================
	//=======================================================================
	enum vertexShaderType vertexShader = SingleTextureClippingPlaneVS;
	enum fragmentShaderType fragmentShader = SingleTextureClippingPlaneFS;
	enum alphaTestType alphaTest = AlphaTestGE80;
	unsigned depthStencilWriteNeeded = 1;
	//-1 to disable
	unsigned dstBlendMode = GL_ONE_MINUS_SRC_ALPHA;
	unsigned srcBlendMode = GL_SRC_ALPHA;
	//=======================================================================
	//=======================================================================
	//=======================================================================
	//=======================================================================
	//=======================================================================

	const char* vertexShaderCode = vertexShaders[vertexShader];
	const char* fragmentShaderCode = fragShaders[fragmentShader][alphaTest];

	// Create a shader program
	// NO ERRRO CHECKING IS DONE! (for the purpose of this example)
	// Read an OpenGL tutorial to properly implement shader creation
	program = glCreateProgram();
	vert = glCreateShader(GL_VERTEX_SHADER);
	unsigned vertlen = strlen(vertexShaderCode);
	glShaderSource(vert, 1, &vertexShaderCode, &vertlen);
	glCompileShader(vert);
	GLint success = 0;
	glGetShaderiv(vert, GL_COMPILE_STATUS, &success);
	if(!success)
	{
		GLint maxLength = 0;
		glGetShaderiv(vert, GL_INFO_LOG_LENGTH, &maxLength);
		char* errorLog = malloc(maxLength);
		glGetShaderInfoLog(vert, maxLength, &maxLength, &errorLog[0]);
		fprintf(stderr, "%s\n", errorLog);
		free(errorLog);
	}
	frag = glCreateShader(GL_FRAGMENT_SHADER);
	unsigned fraglen = strlen(fragmentShaderCode);
	glShaderSource(frag, 1, &fragmentShaderCode, &fraglen);
	glCompileShader(frag);
	glGetShaderiv(frag, GL_COMPILE_STATUS, &success);
	if(!success)
	{
		GLint maxLength = 0;
		glGetShaderiv(frag, GL_INFO_LOG_LENGTH, &maxLength);
		char* errorLog = malloc(maxLength);
		glGetShaderInfoLog(frag, maxLength, &maxLength, &errorLog[0]);
		fprintf(stderr, "%s\n", errorLog);
		free(errorLog);
	}
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);
	GLint isLinked = 0;
	glGetProgramiv(program, GL_LINK_STATUS, (int *)&isLinked);
	if (isLinked == GL_FALSE)
	{
		GLint maxLength = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &maxLength);
		char* errorLog = malloc(maxLength);
		glGetShaderInfoLog(program, maxLength, &maxLength, &errorLog[0]);
		fprintf(stderr, "%s\n", errorLog);
		free(errorLog);
	}
	glUseProgram(program);

	// Create Vertex Buffer Object
	// Again, NO ERRRO CHECKING IS DONE! (for the purpose of this example)
	glGenBuffers(1, &vbo1);
	glBindBuffer(GL_ARRAY_BUFFER, vbo1);
	glBufferData(GL_ARRAY_BUFFER, 3 * 3 * sizeof(float), vertices1, GL_STATIC_DRAW);

	glGenBuffers(1, &vbo2);
	glBindBuffer(GL_ARRAY_BUFFER, vbo2);
	glBufferData(GL_ARRAY_BUFFER, 3 * 4 * sizeof(float), vertices2, GL_STATIC_DRAW);

	glGenBuffers(1, &vbo3);
	glBindBuffer(GL_ARRAY_BUFFER, vbo3);
	glBufferData(GL_ARRAY_BUFFER, 3 * 2 * sizeof(float), vertices3, GL_STATIC_DRAW);

	glGenBuffers(1, &vbo4);
	glBindBuffer(GL_ARRAY_BUFFER, vbo4);
	glBufferData(GL_ARRAY_BUFFER, 3 * 2 * sizeof(float), vertices3, GL_STATIC_DRAW);

	float vertices4[6 * (4 + 1 + 2)];
	GLuint vbo5;
	glGenBuffers(1, &vbo5);
	glBindBuffer(GL_ARRAY_BUFFER, vbo5);
	glBufferData(GL_ARRAY_BUFFER, 6 * (4 + 1 + 2) * sizeof(float), vertices4, GL_STATIC_DRAW);

	uint16_t iboData[3];
	GLuint ibo;
	glGenBuffers(1, &ibo);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, 3 * sizeof(uint16_t), iboData, GL_STATIC_DRAW);

	GLuint tex;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	//glGenerateMipmap(GL_TEXTURE_2D);

	fprintf(stderr, "%s\n", glGetErrorStr());

	int counter = 0;
	//for(; counter < 300;)
	{
		glClearDepthf(1.0f);
		glClearStencil(0xff);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

		glDisable(GL_CULL_FACE);
		glDisable(GL_BLEND);
		//glDepthMask(GL_FALSE);

		if(depthStencilWriteNeeded)
		{
			glEnable(GL_DEPTH_TEST);
			glEnable(GL_STENCIL_TEST);
		}
		else
		{
			glDisable(GL_STENCIL_TEST);
			glDisable(GL_DEPTH_TEST);
		}

		if(srcBlendMode != -1 || dstBlendMode != -1)
		{
			glEnable(GL_BLEND);
			glBlendFuncSeparate(srcBlendMode, //srcRGB
								dstBlendMode, //dstRGB
								srcBlendMode, //srcAlpha
								dstBlendMode //dstAlpha
								);

			glBlendEquationSeparate(GL_FUNC_ADD, //modeRGB
									GL_FUNC_ADD //modeAlpha
									);
		}
		else
		{
			glDisable(GL_BLEND);
		}

		// Render a triangle consisting of 3 vertices:

		glBindBuffer(GL_ARRAY_BUFFER, vbo5);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0);

		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof(float), 3*7*4);

		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, 3*7*4);

//		glEnableVertexAttribArray(3);
//		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, sizeof(float)*2, 3*7*4);

//		glBindBuffer(GL_ARRAY_BUFFER, vbo4);
//		glEnableVertexAttribArray(3);
//		glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);

		float mat[16] = {
			0.00104, 0.00, 0.00, -1.00,
			0.00, 0.00185, 0.00, -1.00,
			0.00, 0.00, 1.00, 0.00,
			0.00, 0.00, 0.00, 1.00};
		glUniformMatrix4fv(glGetUniformLocation(program, "mvp"), 1, 0, mat);
		//glUniformMatrix4fv(glGetUniformLocation(program, "clip_space_xform"), 1, 0, mat);
		//glUniformMatrix4fv(glGetUniformLocation(program, "eye_space_xform"), 1, 0, mat);
		//glUniform4fv(glGetUniformLocation(program, "clipping_plane"), 1, mat);

		//glDrawArrays(GL_TRIANGLES, 0, 3);
		glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0);

		//GLenum attachments[] = { GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT };
		//glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, attachments );

		counter++;

		/*glFinish();
		glFlush();
		unsigned char* buffer = (unsigned char*)malloc(displayWidth * displayHeight * 3);
		glReadPixels(0, 0, displayWidth, displayHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer);
		FILE* output = fopen("triangle.ppm", "wb");
		if(output)
		{
			fprintf(stderr, "writing triangle.ppm\n");
		}
		fprintf(output, "P6\n%d %d\n255\n", displayWidth, displayHeight);
		fwrite(buffer, 1, displayWidth * displayHeight * 3, output);
		fclose(output);
		free(buffer);*/

#ifndef USE_SURFACELESS
		eglSwapBuffers(display, surface);
#endif
	}


	// Cleanup
#ifndef USE_SURFACELESS
	eglDestroySurface(display, surface);
#endif
	eglMakeCurrent( display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT );
	eglDestroyContext(display, context);
	eglTerminate(display);

	return EGL_TRUE;
}
