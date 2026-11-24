/*
 * Draw one triangle with Mesa lima on drm-shim and let LIMA_DEBUG=dump record the
 * GP/PP jobs, which mali400 replays on the real GPU (see gen_job.py).
 * build: cc tri.c -o tri -lEGL -lGLESv2
 */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <stdlib.h>

#define W 720
#define H 1280

static GLuint shader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512];

		glGetShaderInfoLog(s, sizeof(log), NULL, log);
		fprintf(stderr, "shader: %s\n", log);
		exit(1);
	}
	return s;
}

int main(void)
{
	static const EGLint ctx_attr[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
	static const GLfloat v[] = { 0, 0.8f, -0.8f, -0.8f, 0.8f, -0.8f };
	static const GLfloat c[] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	PFNEGLGETPLATFORMDISPLAYEXTPROC gpd =
		(void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
	EGLDisplay d = gpd(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLConfig cfg;
	EGLint n;
	GLuint fbo, rb, p;

	if (!eglInitialize(d, NULL, NULL) || !eglBindAPI(EGL_OPENGL_ES_API))
		return 1;
	eglChooseConfig(d, (EGLint[]){ EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_NONE }, &cfg, 1, &n);
	if (!eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE,
			    eglCreateContext(d, cfg, EGL_NO_CONTEXT, ctx_attr)))
		return 2;
	printf("renderer: %s\n", glGetString(GL_RENDERER));

	glGenRenderbuffers(1, &rb);
	glBindRenderbuffer(GL_RENDERBUFFER, rb);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8_OES, W, H);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		return 3;

	p = glCreateProgram();
	glAttachShader(p, shader(GL_VERTEX_SHADER,
		"attribute vec4 pos; attribute vec3 col; varying vec3 vc;\n"
		"void main() { gl_Position = pos; vc = col; }"));
	glAttachShader(p, shader(GL_FRAGMENT_SHADER,
		"precision mediump float; varying vec3 vc;\n"
		"void main() { gl_FragColor = vec4(vc, 1.0); }"));
	glBindAttribLocation(p, 0, "pos");
	glBindAttribLocation(p, 1, "col");
	glLinkProgram(p);
	glUseProgram(p);

	glViewport(0, 0, W, H);
	glClearColor(0.1f, 0.1f, 0.2f, 1);
	glClear(GL_COLOR_BUFFER_BIT);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, v);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, c);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	glFinish();
	puts("done");
	return 0;
}
