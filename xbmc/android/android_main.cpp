#include <android_native_app_glue.h>
#include <android/log.h>
#include <jni.h>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>

#include "xbmc.h"

static int android_printf(const char *format, ...)
{
  va_list args;
  va_start(args, format);
  int result = __android_log_vprint(ANDROID_LOG_VERBOSE, "XBMC", format, args);
  va_end(args);
  return result;
}

void* tryopen(const char *path)
{
  void *handle;
  handle = dlopen(path, RTLD_NOW);
  if (!handle)
  {
    __android_log_print(ANDROID_LOG_VERBOSE, "XBMC", "could not load %s. Error: %s",path, dlerror());
    exit(1);
  }
  __android_log_print(ANDROID_LOG_VERBOSE, "XBMC", "loaded lib: %s",path);
  return handle;
}

void preload()
{
  tryopen("/data/data/org.xbmc/lib/libsqlite.so");
  tryopen("/data/data/org.xbmc/lib/libyajl.so");
  tryopen("/data/data/org.xbmc/lib/libtiff.so");
  tryopen("/data/data/org.xbmc/lib/libjpeg.so");
  tryopen("/data/data/org.xbmc/lib/libgpg-error.so");
  tryopen("/data/data/org.xbmc/lib/libgcrypt.so");
  tryopen("/data/data/org.xbmc/lib/libiconv.so");
  tryopen("/data/data/org.xbmc/lib/libfreetype.so");
  tryopen("/data/data/org.xbmc/lib/libfontconfig.so");
  tryopen("/data/data/org.xbmc/lib/libfribidi.so");
  tryopen("/data/data/org.xbmc/lib/libsqlite3.so");
  tryopen("/data/data/org.xbmc/lib/libpng12.so");
  tryopen("/data/data/org.xbmc/lib/libpcre.so");
  tryopen("/data/data/org.xbmc/lib/libsamplerate.so");
  tryopen("/data/data/org.xbmc/lib/libpython2.6.so");
  tryopen("/data/data/org.xbmc/lib/libpcrecpp.so");
  tryopen("/data/data/org.xbmc/lib/libzip.so");

  // gdb sleeps before attaching for some reason. let it attach before we try
  // to load libxbmc.
  sleep(1);
}

void setup_env(struct android_app* state)
{
  JavaVM* vm  = state->activity->vm;
  JNIEnv* env = state->activity->env;

  vm->AttachCurrentThread(&env, NULL);
  jclass activityClass = env->GetObjectClass(state->activity->clazz);

  char cstr[1024];
  jmethodID getPackageResourcePath = env->GetMethodID(activityClass, "getPackageResourcePath", "()Ljava/lang/String;");
  jstring jpath = (jstring)env->CallObjectMethod(state->activity->clazz, getPackageResourcePath);
  const char* apkPath = env->GetStringUTFChars(jpath, NULL);
  strcpy(cstr, apkPath);
  env->ReleaseStringUTFChars(jpath, apkPath);
  strcat(cstr, "/assets");
  setenv("XBMC_BIN_HOME", cstr, 0);
  setenv("XBMC_HOME"    , cstr, 0);

  jmethodID getCacheDir = env->GetMethodID(activityClass, "getCacheDir", "()Ljava/io/File;");
  jobject file = env->CallObjectMethod(state->activity->clazz, getCacheDir);
  jclass fileClass = env->FindClass("java/io/File");
  jmethodID getAbsolutePath = env->GetMethodID(fileClass, "getAbsolutePath", "()Ljava/lang/String;");
  jpath = (jstring)env->CallObjectMethod(file, getAbsolutePath);
  const char* cachePath = env->GetStringUTFChars(jpath, NULL);
  setenv("HOME", cachePath, 0);
  env->ReleaseStringUTFChars(jpath, cachePath);
}

extern void android_main(struct android_app* state)
{
  // make sure that the linker doesn't strip out our glue
  app_dummy();

  preload();
  setup_env(state);

  void* soHandle;
  soHandle = tryopen("/data/data/org.xbmc/lib/libxbmc.so");

  // fetch xbmc entry points
  XBMC_Run_t XBMC_Run;
  XBMC_Initialize_t XBMC_Initialize;

  XBMC_Run = (XBMC_Run_t)dlsym(soHandle, "XBMC_Run");
  XBMC_Initialize = (XBMC_Initialize_t)dlsym(soHandle, "XBMC_Initialize");
  if (XBMC_Run == NULL || XBMC_Initialize == NULL)
  {
    android_printf("could not find functions. Error: %s\n", dlerror());
    exit(1);
  }

  // XBMC_PLATFORM MUST exist for app lifetime.
  static XBMC_PLATFORM platform = {XBMCRunNull, 0};
  // stardard platform stuff
  platform.flags  = XBMCRunAsApp;
  platform.log_name = "XBMC";
  // android specific
  if(state && state->window)
  {
    platform.width  = ANativeWindow_getWidth(state->window);
    platform.height = ANativeWindow_getHeight(state->window);
    platform.format = ANativeWindow_getFormat(state->window);
  }
  platform.window_type = state->window;
  // callbacks into android
  platform.android_printf = &android_printf;
  platform.android_setBuffersGeometry = &ANativeWindow_setBuffersGeometry;

  int status = 0;
  status = XBMC_Initialize(&platform, NULL, NULL);
  if (!status)
  {
    try
    {
      status = XBMC_Run();
    }
    catch(...)
    {
      android_printf("ERROR: Exception caught on main loop. Exiting\n");
    }
  }
  else
    android_printf("ERROR: XBMC_Initialize failed. Exiting\n");

  if (status == 0)
    android_printf("DEBUG: Exiting gracefully.\n");

  state->activity->vm->DetachCurrentThread();
  return;
}
