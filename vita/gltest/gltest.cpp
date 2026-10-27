// G0 of the renderer (docs/PLANO_RENDER_VITA.md): does the runtime shader
// compiler work, and does vitaGL draw and read back?
//
// 1. vitaShaRK directly: shark_init and shark_init_simple (the Vita3K mode),
//    compile a small Cg vertex and fragment shader.
// 2. vitaGL: init, compile GLSL shaders, draw a quad to the screen and to a
//    framebuffer object, read the pixels back.
//
// Log: ux0:data/storm2port/gltest.log (one open/append/close per line, so the
// emulator writes it even if the app is killed). Screen: orange quad if the
// GLSL path works. Press any button to quit (or it quits after 20 s).

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <pthread.h>

#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <vitaGL.h>
#include <vitashark.h>

extern "C" GLboolean is_shark_online;

namespace {

constexpr const char* kLogPath = "ux0:data/storm2port/gltest.log";

void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void Log(const char* fmt, ...) {
  char line[1024];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);
  if (n <= 0) {
    return;
  }
  if (n >= int(sizeof(line))) {
    n = int(sizeof(line)) - 1;
  }
  SceUID fd = sceIoOpen(kLogPath, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0666);
  if (fd >= 0) {
    sceIoWrite(fd, line, n);
    sceIoClose(fd);
  }
}

void LogMemory(const char* when) {
  SceKernelFreeMemorySizeInfo mem{};
  mem.size = sizeof(mem);
  if (sceKernelGetFreeMemorySize(&mem) >= 0) {
    Log("%s: free user %d KiB, cdram %d KiB, phycont %d KiB\n", when, mem.size_user / 1024,
        mem.size_cdram / 1024, mem.size_phycont / 1024);
  }
}

const char* kCgVertex =
    "void main(float2 aPos, float4 out oPos : POSITION) { oPos = float4(aPos, 0.0f, 1.0f); }";
const char* kCgFragment = "float4 main() : COLOR { return float4(1.0f, 0.5f, 0.0f, 1.0f); }";

void SharkTest(const char* label, int (*init)(const char*)) {
  int r = init(nullptr);
  Log("shark %s: init returned %d\n", label, r);
  if (r < 0) {
    return;
  }
  for (int i = 0; i < 2; ++i) {
    uint32_t size = 0;
    SceGxmProgram* program =
        shark_compile_shader(i == 0 ? kCgVertex : kCgFragment, &size,
                             i == 0 ? SHARK_VERTEX_SHADER : SHARK_FRAGMENT_SHADER);
    Log("shark %s: %s shader -> %s, %u bytes\n", label, i == 0 ? "vertex" : "fragment",
        program ? "compiled" : "FAILED", size);
    const SceShaccCgCompileOutput* out = shark_get_internal_compile_output();
    if (out) {
      for (int k = 0; k < out->diagnosticCount; ++k) {
        Log("  diag: %s\n", out->diagnostics[k].message ? out->diagnostics[k].message : "?");
      }
    }
    shark_clear_output();
    if (program) {
      free(program);
    }
  }
  shark_end();
}

const char* kGlslVertex =
    "#version 100\n"
    "attribute vec2 aPos;\n"
    "void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";
const char* kGlslFragment =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform vec4 uColor;\n"
    "void main() { gl_FragColor = uColor; }\n";

GLuint Compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  char info[512] = {};
  GLsizei len = 0;
  glGetShaderInfoLog(s, sizeof(info) - 1, &len, info);
  Log("vitaGL: %s shader compile status %d%s%s\n",
      type == GL_VERTEX_SHADER ? "vertex" : "fragment", ok, len ? ", log: " : "",
      len ? info : "");
  return ok ? s : 0;
}

}  // namespace

static bool g_vgl_inited = false;

static int RunTest() {
  sceIoMkdir("ux0:data", 0777);
  sceIoMkdir("ux0:data/storm2port", 0777);
  sceIoRemove(kLogPath);
  Log("vitaGL / vitaShaRK test (G0)\n");
  LogMemory("start");
  {
    SceIoStat st{};
    Log("ur0:data/external/libshacccg.suprx %s\n",
        sceIoGetstat("ur0:data/external/libshacccg.suprx", &st) >= 0 ? "present" : "absent");
  }

  // (Direct vitaShaRK calls removed: vitaGL owns the compiler.)
  (void)SharkTest;

  // Same initialization as the Storm 2 app (gles/gl_api.cpp), done here unless
  // main already did it on the main thread.
  GLboolean ok = GL_FALSE;
  if (!g_vgl_inited) {
    vglSetupRuntimeShaderCompiler(SHARK_OPT_DEFAULT, SHARK_ENABLE, SHARK_DISABLE, SHARK_DISABLE);
    Log("calling vglInitWithCustomSizes\n");
    ok = vglInitWithCustomSizes(0, 960, 544, 24 * 1024 * 1024, 80 * 1024 * 1024, 0, 0,
                                SCE_GXM_MULTISAMPLE_NONE);
  }
  // vitaGL returns GL_TRUE only when it lowered the resolution; not a success flag.
  Log("vglInit returned %d (resolution lowered), shader compiler online: %d\n", int(ok),
      int(is_shark_online));
  LogMemory("after vglInit");
  Log("GL_VENDOR %s, GL_RENDERER %s, GL_VERSION %s\n",
      reinterpret_cast<const char*>(glGetString(GL_VENDOR)),
      reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
      reinterpret_cast<const char*>(glGetString(GL_VERSION)));

  GLuint vs = Compile(GL_VERTEX_SHADER, kGlslVertex);
  GLuint fs = Compile(GL_FRAGMENT_SHADER, kGlslFragment);
  GLuint program = 0;
  GLint color_loc = -1;
  if (vs && fs) {
    program = glCreateProgram();
    Log("step: created program\n");
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    Log("step: attached\n");
    glBindAttribLocation(program, 0, "aPos");
    Log("step: bound attribute\n");
    glLinkProgram(program);
    Log("step: linked\n");
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    color_loc = glGetUniformLocation(program, "uColor");
    Log("vitaGL: program link status %d, uColor location %d\n", linked, color_loc);
    if (!linked) {
      program = 0;
    }
  }

  const float quad[] = {-0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
  Log("step: before buffers\n");
  GLuint vbo = 0;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

  // Render to texture (framebuffer object), as a Xenos render target would.
  GLuint rt_tex = 0, fbo = 0;
  glGenTextures(1, &rt_tex);
  glBindTexture(GL_TEXTURE_2D, rt_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  Log("step: before framebuffer\n");
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt_tex, 0);
  Log("vitaGL: framebuffer status 0x%X\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
  glViewport(0, 0, 64, 64);
  glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
  Log("step: before FBO clear\n");
  glClear(GL_COLOR_BUFFER_BIT);
  Log("step: FBO cleared\n");
  if (program) {
    glUseProgram(program);
    glUniform4f(color_loc, 0.0f, 1.0f, 0.0f, 1.0f);
    Log("step: uniform set\n");
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  }
  unsigned char px[2][4] = {};
  glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[0]);
  glReadPixels(2, 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[1]);
  Log("vitaGL: FBO pixel centre %u,%u,%u,%u (expect 0,255,0,255), corner %u,%u,%u,%u (expect "
      "0,0,255,255)\n",
      px[0][0], px[0][1], px[0][2], px[0][3], px[1][0], px[1][1], px[1][2], px[1][3]);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  SceCtrlData pad{};
  sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
  const uint64_t start = sceKernelGetProcessTimeWide();
  int frame = 0;
  for (;;) {
    glViewport(0, 0, 960, 544);
    glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (program) {
      glUseProgram(program);
      glUniform4f(color_loc, 1.0f, 0.5f, 0.0f, 1.0f);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
      glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }
    if (frame == 10) {
      unsigned char p[4] = {};
      glReadPixels(480, 272, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p);
      Log("vitaGL: screen pixel centre %u,%u,%u,%u (expect 255,127/128,0,255)\n", p[0], p[1],
          p[2], p[3]);
    }
    vglSwapBuffers(GL_FALSE);
    ++frame;
    sceCtrlPeekBufferPositive(0, &pad, 1);
    const uint64_t now = sceKernelGetProcessTimeWide();
    if ((pad.buttons && now - start > 1000000) || now - start > 20000000) {
      break;
    }
  }
  Log("frames drawn: %d in %.1f s\n", frame,
      double(sceKernelGetProcessTimeWide() - start) / 1e6);
  LogMemory("end");
  Log("done\n");
  sceKernelExitProcess(0);
  return 0;
}

// Like the Storm 2 GPU command thread: the whole test runs on a secondary
// pthread with a 16 MiB stack (GLTEST_MAIN_THREAD runs it on the main thread).
static void* TestThread(void*) {
  RunTest();
  return nullptr;
}

int main() {
#ifdef GLTEST_MAIN_THREAD
  return RunTest();
#else
  // vitaGL initialized on the main thread, used from the secondary one.
  vglSetupRuntimeShaderCompiler(SHARK_OPT_DEFAULT, SHARK_ENABLE, SHARK_DISABLE, SHARK_DISABLE);
  vglInitWithCustomSizes(0, 960, 544, 24 * 1024 * 1024, 80 * 1024 * 1024, 0, 0,
                         SCE_GXM_MULTISAMPLE_NONE);
  g_vgl_inited = true;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 512 * 1024);
  pthread_t thread;
  if (pthread_create(&thread, &attr, TestThread, nullptr) != 0) {
    return 1;
  }
  pthread_join(thread, nullptr);
  return 0;
#endif
}