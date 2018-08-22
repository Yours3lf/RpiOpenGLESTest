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

#ifdef USE_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
static Display *x_display = NULL;
#endif

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

	//EGL_DEPTH_SIZE, 24,
	//EGL_ALPHA_SIZE, 8,
	EGL_BLUE_SIZE, 8,
	EGL_GREEN_SIZE, 8,
	EGL_RED_SIZE, 8,

	// Uncomment the following to enable stencil buffer
	//EGL_STENCIL_SIZE, 8,

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
static const GLfloat vertices[] =
{
	-1.0f, -1.0f, 0.0f,
	1.0f, -1.0f, 0.0f,
	0.0f,  1.0f, 0.0f,
};

// The following are GLSL shaders for rendering a triangle on the screen
#define STRINGIFY(x) #x
static const char* vertexShaderCode = STRINGIFY(
			attribute vec3 pos;
		void main(){
		gl_Position = vec4(pos, 1.0);
		}
		);

static const char* fragmentShaderCode = STRINGIFY(
			uniform vec4 color;
		void main() {
		gl_FragColor = vec4(color);
		}
		);

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
	return "Unknown error!";
}

static void printConfigInfo(int i, EGLDisplay display, EGLConfig* config)
{
	EGLint val;
	eglGetConfigAttrib(display, *config, EGL_RED_SIZE, &val); printf("EGL_RED_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_GREEN_SIZE, &val); printf("EGL_GREEN_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_BLUE_SIZE, &val); printf("EGL_BLUE_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_ALPHA_SIZE, &val); printf("EGL_ALPHA_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_DEPTH_SIZE, &val); printf("EGL_DEPTH_SIZE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_CONFIG_CAVEAT, &val); printf("EGL_CONFIG_CAVEAT: %s\n", val == EGL_NONE ? "EGL_NONE" : val == EGL_SLOW_CONFIG ? "EGL_SLOW_CONFIG" : "");
	eglGetConfigAttrib(display, *config, EGL_SAMPLE_BUFFERS, &val); printf("EGL_SAMPLE_BUFFERS: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_SAMPLES, &val); printf("EGL_SAMPLES: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_MAX_PBUFFER_WIDTH, &val); printf("EGL_MAX_PBUFFER_WIDTH: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_MAX_PBUFFER_HEIGHT, &val); printf("EGL_MAX_PBUFFER_HEIGHT: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_NATIVE_RENDERABLE, &val); printf("EGL_NATIVE_RENDERABLE: %i\n", val);
	eglGetConfigAttrib(display, *config, EGL_SURFACE_TYPE, &val); printf("EGL_SURFACE_TYPE: %i ", val);
	if(val & EGL_WINDOW_BIT) printf("EGL_WINDOW_BIT ");
	if(val & EGL_PIXMAP_BIT) printf("EGL_PIXMAP_BIT ");
	if(val & EGL_PBUFFER_BIT) printf("EGL_PBUFFER_BIT ");
	eglGetConfigAttrib(display, *config, EGL_RENDERABLE_TYPE, &val); printf("\nEGL_RENDERABLE_TYPE: ");
	if(val&EGL_OPENGL_BIT){ printf("OpenGL "); } if(val&EGL_OPENGL_ES2_BIT){ printf("OpenGLES2 "); } printf("\n");
	printf("\n");
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

		printf("%.*s\n", nextExtensions-extensions, extensions);
		extensions = nextExtensions+1;
	}
	printf("\n");
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

		printf("%.*s\n", nextExtensions-extensions, extensions);
		extensions = nextExtensions+1;
	}
	printf("\n");
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
	GLuint program, vert, frag, vbo;
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
		printf("Failed to get EGL display! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	if(eglInitialize(display, &major, &minor) == EGL_FALSE)
	{
		printf("Failed to get EGL version! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}

	printf("\nInitialized EGL version: %d.%d\n\n", major, minor);

	printf("EGL Vendor: %s\n", eglQueryString(display, EGL_VENDOR));
	printf("EGL Version: %s\n\n", eglQueryString(display, EGL_VERSION));

	//printEGLExtensions(display);

#ifdef USE_KMS
	struct kms kms;
	int kms_setup = setup_kms(fd, &kms); assert(kms_setup);
#endif

	EGLint numConfigs;
	eglGetConfigs(display, NULL, 0, &numConfigs);

	printf("EGL has %i configs: \n\n", numConfigs);

	//printEGLConfigs(display, numConfigs);

	EGLConfig config;
	if(eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) != EGL_TRUE)
	{
		printf("Failed to get EGL config! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}

	//print out chosen config data
	printf("Config chosen: \n");
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
	//printf("%i", window);
	EGLSurface surface = eglCreateWindowSurface(display, config, window, NULL);
#endif
#if !defined(USE_SURFACELESS)
	if(surface == EGL_NO_SURFACE)
	{
		printf("Failed to create EGL surface! Error: %s\n", eglGetErrorStr());
		eglTerminate(display);
		return EGL_FALSE;
	}
#endif

	//EGLBoolean res = eglBindAPI(EGL_OPENGL_API);
	EGLBoolean res = eglBindAPI(EGL_OPENGL_ES_API);
	if(res != EGL_TRUE)
	{
		printf("Failed to bind GL API to EGL! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttribs);
	if(context == EGL_NO_CONTEXT)
	{
		printf("Failed to create EGL context! Error: %s\n", eglGetErrorStr());
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
		printf("Failed to make current! Error: %s\n", eglGetErrorStr());
		return EGL_FALSE;
	}

	////////////////////////////
	//GL Context live from here
	////////////////////////////
	printf("Vendor: %s\n", glGetString(GL_VENDOR));
	printf("Renderer: %s\n", glGetString(GL_RENDERER));
	printf("Version: %s\n", glGetString(GL_VERSION));
	printf("Shading language version: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	//printExtensions();

	// Set GL Viewport size, always needed!
	glViewport(0, 0, displayWidth, displayHeight);

	// Get GL Viewport size and test if it is correct.
	// NOTE! DO NOT UPDATE EGL LIBRARY ON RASPBERRY PI AS IT WILL INSTALL FAKE EGL!
	// If you have fake/faulty EGL library, the glViewport and glGetIntegerv won't work!
	// The following piece of code checks if the gl functions are working as intended!
	GLint viewport[4];
	glGetIntegerv(GL_VIEWPORT, viewport);

	// viewport[2] and viewport[3] are viewport width and height respectively
	printf("GL Viewport size: %dx%d\n", viewport[2], viewport[3]);

	// Clear whole screen (front buffer)
	glClearColor(0.8f, 0.2f, 0.5f, 1.0f);
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

	/*
	// Create a shader program
	// NO ERRRO CHECKING IS DONE! (for the purpose of this example)
	// Read an OpenGL tutorial to properly implement shader creation
	program = glCreateProgram();
	glUseProgram(program);
	vert = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vert, 1, &vertexShaderCode, NULL);
	glCompileShader(vert);
	frag = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(frag, 1, &fragmentShaderCode, NULL);
	glCompileShader(frag);
	glAttachShader(program, frag);
	glAttachShader(program, vert);
	glLinkProgram(program);
	glUseProgram(program);

	// Create Vertex Buffer Object
	// Again, NO ERRRO CHECKING IS DONE! (for the purpose of this example)
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, 9 * sizeof(float), vertices, GL_STATIC_DRAW);

	// Get vertex attribute and uniform locations
	posLoc = glGetAttribLocation(program, "pos");
	colorLoc = glGetUniformLocation(program, "color");

	// Set the desired color of the triangle to pink
	// 100% red, 0% green, 50% blue, 100% alpha
	glUniform4f(colorLoc, 1.0, 0.0f, 0.5, 1.0);

	// Set our vertex data
	glEnableVertexAttribArray(posLoc);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glVertexAttribPointer(posLoc, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
	*/

	int counter = 0;
	{
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		// Render a triangle consisting of 3 vertices:
		//glDrawArrays(GL_TRIANGLES, 0, 3);

		counter++;

		glFinish();
		glFlush();
		unsigned char* buffer = (unsigned char*)malloc(displayWidth * displayHeight * 3);
		glReadPixels(0, 0, displayWidth, displayHeight, GL_RGB, GL_UNSIGNED_BYTE, buffer);
		FILE* output = fopen("triangle.ppm", "wb");
		if(output)
		{
			printf("writing triangle.ppm\n");
		}
		fprintf(output, "P6\n%d %d\n255\n", displayWidth, displayHeight);
		fwrite(buffer, 1, displayWidth * displayHeight * 3, output);
		fclose(output);
		free(buffer);
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
