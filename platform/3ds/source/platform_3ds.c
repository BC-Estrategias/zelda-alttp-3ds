#include "platform_3ds.h"

#include <3ds.h>
#include <citro2d.h>
#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "assets.h"
#include "config.h"
#include "setup_audio_assets.h"
#include "features.h"
#include "setup_selector_assets.h"
#include "types.h"
#include "util.h"
#include "zelda_rtl.h"

extern void SecondScreenSDL_OpenDeveloperOverlay(void);

static const char kStorageDirectory[] = "sdmc:/3ds/Zelda 3DS";
static const char kProfilesDirectory[] = "profiles";
static const char kSelectedRomFile[] = "selected_rom.ini";
static const char kForceSelectorFile[] = "select-rom.flag";
static const char kAssetsFilename[] = "zelda3_assets.dat";
static const char kTemporaryAssetsFilename[] = "zelda3_assets.tmp";
static const char kBundledPatch[] = "romfs:/zelda3_assets.bps";
static const char kBundledConfig[] = "romfs:/zelda3.ini";

static enum Platform3DSDisplayMode g_display_mode =
  kPlatform3DSDisplayOriginal;
static enum Platform3DSWideEdgeMode g_wide_edge_mode =
  kPlatform3DSWideEdgeStandard;
static int g_wide_zoom_index;
static bool g_display_mode_auto = true;
static bool g_wide_edge_mode_auto = true;
static bool g_display_mode_legacy_stretch;
static bool g_runtime_wide_edge_seen;
static enum Platform3DSCStickMode g_cstick_mode = kPlatform3DSCStickTurbo;
static int g_turbo_multiplier = 5;
static bool g_quick_dump_requested;
static bool g_quick_save_requested;
static bool g_quick_load_requested;
static bool g_rom_selection_requested;
static aptHookCookie g_apt_hook_cookie;
static bool g_apt_hook_registered;
static volatile bool g_system_exit_requested;
static volatile bool g_system_suspended;
static char g_active_save_directory[512] = "saves";
static bool g_is_new_3ds;
static bool g_model_detected;
static bool g_irrst_initialized;
static bool g_core1_time_enabled;
static int g_core1_time_limit_percent;
static uint64_t g_frame_timing_samples;
static uint64_t g_top_work_total_us;
static uint64_t g_total_work_total_us;
static uint64_t g_logic_work_total_us;
static uint64_t g_top_draw_total_us;
static uint64_t g_ppu_draw_total_us;
static uint64_t g_capture_total_us;
static uint64_t g_present_total_us;
static uint64_t g_bottom_work_total_us;
static uint64_t g_top_frames_over_budget;
static uint64_t g_total_frames_over_budget;
static uint32_t g_logic_work_max_us;
static uint32_t g_top_draw_max_us;
static uint32_t g_ppu_draw_max_us;
static uint32_t g_capture_max_us;
static uint32_t g_present_max_us;
static uint32_t g_bottom_work_max_us;
static uint32_t g_top_work_max_us;
static uint32_t g_total_work_max_us;
static uint64_t g_render_interval_samples;
static uint64_t g_render_interval_total_us;
static uint64_t g_scheduled_logic_frames;
static uint64_t g_timed_scheduled_logic_frames;
static uint64_t g_executed_logic_frames;
static uint64_t g_catchup_presentations;
static uint32_t g_max_scheduled_logic_frames;
static bool g_gpu_presenter_initialized;
static bool g_gpu_frame_active;
static bool g_setup_console_active;
static C3D_RenderTarget *g_top_target;
static C3D_RenderTarget *g_bottom_target;
static C3D_Tex g_top_texture;
static C3D_Tex g_bottom_texture;
static Tex3DS_SubTexture g_top_subtexture;
static Tex3DS_SubTexture g_bottom_subtexture;
static uint16_t g_setup_top_pixels[400 * 240];
static uint16_t g_setup_bottom_pixels[320 * 240];
static bool g_setup_audio_initialized;
static int16_t *g_setup_music_buffer;
static int16_t *g_setup_move_buffer;
static ndspWaveBuf g_setup_music_wavebuf;
static ndspWaveBuf g_setup_move_wavebuf;

enum {
  kTopTextureWidth = 512,
  kTopTextureHeight = 256,
};

static bool WriteBlob(const char *path, const void *data, size_t size);
static bool EnsureDirectory(const char *path);
static void MakeTimestamp(char *stamp, size_t stamp_size);
static bool RomFileShouldBeIgnored(const char *name);
static uint32 ReadU32LE(const uint8 *data);

static void Platform3DS_DetectModel(void) {
  if (g_model_detected)
    return;
  bool is_new_3ds = false;
  if (R_SUCCEEDED(APT_CheckNew3DS(&is_new_3ds)))
    g_is_new_3ds = is_new_3ds;
  else
    g_is_new_3ds = false;
  g_model_detected = true;
}

static void Platform3DS_ApplyAutoDisplayDefaults(void) {
  if (g_display_mode_legacy_stretch && !g_runtime_wide_edge_seen) {
    g_display_mode_auto = true;
    g_wide_edge_mode_auto = true;
  }
  if (g_display_mode_auto)
    g_display_mode = g_is_new_3ds ? kPlatform3DSDisplayUltraWideMod :
                                    kPlatform3DSDisplayOriginal;
  if (g_wide_edge_mode_auto)
    g_wide_edge_mode = g_is_new_3ds ? kPlatform3DSWideEdgeFixedCamera :
                                      kPlatform3DSWideEdgeStandard;
}

static void LogSetup(const char *format, ...) {
  FILE *log = fopen("setup-progress.txt", "ab");
  if (!log)
    return;
  va_list arguments;
  va_start(arguments, format);
  vfprintf(log, format, arguments);
  va_end(arguments);
  fputc('\n', log);
  fclose(log);
}

void Platform3DS_LogRuntime(const char *format, ...) {
  FILE *log = fopen("runtime.log", "ab");
  if (!log)
    return;
  va_list arguments;
  va_start(arguments, format);
  vfprintf(log, format, arguments);
  va_end(arguments);
  fputc('\n', log);
  fclose(log);
}

static void Platform3DS_AptHook(APT_HookType hook, void *param) {
  (void)param;
  switch (hook) {
  case APTHOOK_ONSUSPEND:
  case APTHOOK_ONSLEEP:
    g_system_suspended = true;
    break;
  case APTHOOK_ONRESTORE:
  case APTHOOK_ONWAKEUP:
    g_system_suspended = false;
    break;
  case APTHOOK_ONEXIT:
    g_system_exit_requested = true;
    break;
  default:
    break;
  }
}

static void Platform3DS_RegisterAptHook(void) {
  if (g_apt_hook_registered)
    return;
  aptHook(&g_apt_hook_cookie, Platform3DS_AptHook, NULL);
  g_apt_hook_registered = true;
}

static bool CStickIsHeld(u32 keys) {
  if (keys & (KEY_CSTICK_UP | KEY_CSTICK_DOWN |
              KEY_CSTICK_LEFT | KEY_CSTICK_RIGHT))
    return true;

  if (g_irrst_initialized) {
    circlePosition cstick = {0};
    hidCstickRead(&cstick);
    return abs((int)cstick.dx) > 24 || abs((int)cstick.dy) > 24;
  }
  return false;
}

uint16_t Platform3DS_ReadInput(bool *turbo_held, int *turbo_multiplier) {
  hidScanInput();
  u32 keys = hidKeysHeld();
  u32 keys_up = hidKeysUp();
  static uint64_t old_3ds_x_hold_start_ms;
  static bool old_3ds_x_turbo_was_active;
  bool old_3ds_x_turbo = false;
  bool old_3ds_x_tap = false;
  if (!g_is_new_3ds && (keys & KEY_X)) {
    uint64_t now_ms = osGetTime();
    if (old_3ds_x_hold_start_ms == 0)
      old_3ds_x_hold_start_ms = now_ms;
    old_3ds_x_turbo = now_ms - old_3ds_x_hold_start_ms >= 1000;
    if (old_3ds_x_turbo)
      old_3ds_x_turbo_was_active = true;
  } else {
    if (!g_is_new_3ds && (keys_up & KEY_X) &&
        old_3ds_x_hold_start_ms != 0 &&
        !old_3ds_x_turbo_was_active &&
        osGetTime() - old_3ds_x_hold_start_ms < 1000)
      old_3ds_x_tap = true;
    old_3ds_x_hold_start_ms = 0;
    old_3ds_x_turbo_was_active = false;
  }

  static bool quick_dump_combo_was_held;
  static bool version_combo_was_held;
  static bool quick_save_combo_was_held;
  static bool quick_load_combo_was_held;

  bool quick_save_combo =
    (keys & (KEY_L | KEY_ZL)) == (KEY_L | KEY_ZL);
  if (quick_save_combo && !quick_save_combo_was_held)
    g_quick_save_requested = true;
  quick_save_combo_was_held = quick_save_combo;

  bool quick_load_combo =
    (keys & (KEY_R | KEY_ZR)) == (KEY_R | KEY_ZR);
  if (quick_load_combo && !quick_load_combo_was_held)
    g_quick_load_requested = true;
  quick_load_combo_was_held = quick_load_combo;

  bool quick_dump_combo =
    (keys & (KEY_L | KEY_R | KEY_A)) == (KEY_L | KEY_R | KEY_A);
  if (quick_dump_combo && !quick_dump_combo_was_held)
    g_quick_dump_requested = true;
  quick_dump_combo_was_held = quick_dump_combo;

  bool version_combo =
    (keys & (KEY_L | KEY_R | KEY_B)) == (KEY_L | KEY_R | KEY_B);
  if (version_combo && !version_combo_was_held)
    SecondScreenSDL_OpenDeveloperOverlay();
  version_combo_was_held = version_combo;

  circlePosition circle;
  hidCircleRead(&circle);

  uint16_t input = 0;
  if ((keys & KEY_DUP) || circle.dy > 40) input |= 1u << 4;
  if ((keys & KEY_DDOWN) || circle.dy < -40) input |= 1u << 5;
  if ((keys & KEY_DLEFT) || circle.dx < -40) input |= 1u << 6;
  if ((keys & KEY_DRIGHT) || circle.dx > 40) input |= 1u << 7;
  if (keys & KEY_SELECT) input |= 1u << 2;
  if (keys & KEY_START) input |= 1u << 3;
  if ((keys & KEY_A) && !quick_dump_combo) input |= 1u << 8;
  if ((keys & KEY_B) && !version_combo) input |= 1u << 0;
  if (g_is_new_3ds ? (keys & KEY_X) : old_3ds_x_tap)
    input |= 1u << 9;
  if (keys & KEY_Y) input |= 1u << 1;
  if ((keys & KEY_L) && !version_combo && !quick_save_combo)
    input |= 1u << 10;
  if ((keys & KEY_R) && !version_combo && !quick_load_combo)
    input |= 1u << 11;

  bool trigger_turbo =
    ((keys & KEY_ZL) && !quick_save_combo) ||
    ((keys & KEY_ZR) && !quick_load_combo);
  *turbo_held = g_turbo_multiplier > 0 &&
                (trigger_turbo || CStickIsHeld(keys) || old_3ds_x_turbo);
  *turbo_multiplier = g_turbo_multiplier > 0 ? g_turbo_multiplier : 1;
  return input;
}

static char *Trim(char *text) {
  while (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')
    text++;
  char *end = text + strlen(text);
  while (end > text &&
         (end[-1] == ' ' || end[-1] == '\t' ||
          end[-1] == '\r' || end[-1] == '\n')) {
    *--end = 0;
  }
  return text;
}

static void LoadRuntimeSetting(const char *key, const char *value) {
  if (strcasecmp(key, "DisplayMode") == 0) {
    g_display_mode_legacy_stretch = false;
    if (strcasecmp(value, "Auto") == 0) {
      g_display_mode_auto = true;
    } else if (strcasecmp(value, "Original") == 0) {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayOriginal;
    } else if (strcasecmp(value, "Stretch") == 0 ||
               strcasecmp(value, "UltraWideStretch") == 0) {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayStretch;
      g_display_mode_legacy_stretch = true;
    } else {
      g_display_mode_auto = false;
      g_display_mode = kPlatform3DSDisplayUltraWideMod;
    }
  } else if (strcasecmp(key, "WideEdgeMode") == 0) {
    g_runtime_wide_edge_seen = true;
    if (strcasecmp(value, "Auto") == 0) {
      g_wide_edge_mode_auto = true;
    } else if (strcasecmp(value, "FixedCamera") == 0) {
      g_wide_edge_mode_auto = false;
      g_wide_edge_mode = kPlatform3DSWideEdgeFixedCamera;
    } else {
      g_wide_edge_mode_auto = false;
      g_wide_edge_mode = kPlatform3DSWideEdgeStandard;
    }
  } else if (strcasecmp(key, "WideZoom") == 0) {
    int zoom_index = 0;
    if (strcasecmp(value, "1.2") == 0 || strcasecmp(value, "1.2x") == 0)
      zoom_index = 1;
    else if (strcasecmp(value, "1.5") == 0 || strcasecmp(value, "1.5x") == 0)
      zoom_index = 2;
    else if (strcasecmp(value, "2") == 0 || strcasecmp(value, "2x") == 0)
      zoom_index = 3;
    else if (strcasecmp(value, "2.5") == 0 || strcasecmp(value, "2.5x") == 0)
      zoom_index = 4;
    g_wide_zoom_index = zoom_index;
  } else if (strcasecmp(key, "CStickMode") == 0) {
    if (strcasecmp(value, "Disabled") == 0 ||
        strcasecmp(value, "Off") == 0) {
      g_cstick_mode = kPlatform3DSCStickDisabled;
      g_turbo_multiplier = 0;
    } else {
      g_cstick_mode = kPlatform3DSCStickTurbo;
    }
  } else if (strcasecmp(key, "CStickTurboMultiplier") == 0) {
    if (strcasecmp(value, "Off") == 0 || strcasecmp(value, "Disabled") == 0) {
      g_turbo_multiplier = 0;
      return;
    }
    int multiplier = atoi(value);
    if (multiplier <= 0)
      multiplier = 0;
    else if (multiplier < 2)
      multiplier = 2;
    if (multiplier > 5)
      multiplier = 5;
    g_turbo_multiplier = multiplier;
  }
}

void Platform3DS_LoadRuntimeSettings(void) {
  g_display_mode_auto = true;
  g_wide_edge_mode_auto = true;
  g_display_mode_legacy_stretch = false;
  g_runtime_wide_edge_seen = false;
  FILE *file = fopen("zelda3.ini", "rb");
  if (!file) {
    Platform3DS_ApplyAutoDisplayDefaults();
    return;
  }
  bool in_general = false;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    char *text = Trim(line);
    if (text[0] == 0 || text[0] == '#' || text[0] == ';')
      continue;
    if (text[0] == '[') {
      in_general = strcasecmp(text, "[General]") == 0;
      continue;
    }
    if (!in_general)
      continue;
    char *equals = strchr(text, '=');
    if (!equals)
      continue;
    *equals = 0;
    LoadRuntimeSetting(Trim(text), Trim(equals + 1));
  }
  fclose(file);
  Platform3DS_ApplyAutoDisplayDefaults();
}

enum Platform3DSDisplayMode Platform3DS_GetDisplayMode(void) {
  return g_display_mode;
}

void Platform3DS_SetDisplayMode(enum Platform3DSDisplayMode mode) {
  if (mode > kPlatform3DSDisplayStretch)
    mode = kPlatform3DSDisplayOriginal;
  g_display_mode_auto = false;
  g_display_mode = mode;
  Platform3DS_LogRuntime("Display mode set: %d", (int)g_display_mode);
}

enum Platform3DSWideEdgeMode Platform3DS_GetWideEdgeMode(void) {
  return g_wide_edge_mode;
}

void Platform3DS_SetWideEdgeMode(enum Platform3DSWideEdgeMode mode) {
  if (mode > kPlatform3DSWideEdgeFixedCamera)
    mode = kPlatform3DSWideEdgeStandard;
  g_wide_edge_mode_auto = false;
  g_wide_edge_mode = mode;
  ZeldaSetWidescreenEdgeMode((int)g_wide_edge_mode);
  Platform3DS_LogRuntime("Wide edge mode set: %d", (int)g_wide_edge_mode);
}

int Platform3DS_GetWideZoomIndex(void) {
  return g_wide_zoom_index;
}

void Platform3DS_SetWideZoomIndex(int zoom_index) {
  if (zoom_index < 0)
    zoom_index = 0;
  if (zoom_index > 4)
    zoom_index = 4;
  g_wide_zoom_index = zoom_index;
  Platform3DS_LogRuntime("Wide zoom set: %d", g_wide_zoom_index);
}

enum Platform3DSCStickMode Platform3DS_GetCStickMode(void) {
  return g_cstick_mode;
}

void Platform3DS_SetCStickMode(enum Platform3DSCStickMode mode) {
  if (mode > kPlatform3DSCStickDisabled)
    mode = kPlatform3DSCStickTurbo;
  g_cstick_mode = mode;
  Platform3DS_LogRuntime("C-stick mode set: %d", (int)g_cstick_mode);
}

int Platform3DS_GetTurboMultiplier(void) {
  return g_turbo_multiplier;
}

bool Platform3DS_TakeQuickSaveRequest(void) {
  bool requested = g_quick_save_requested;
  g_quick_save_requested = false;
  return requested;
}

bool Platform3DS_TakeQuickLoadRequest(void) {
  bool requested = g_quick_load_requested;
  g_quick_load_requested = false;
  return requested;
}

bool Platform3DS_TakeQuickDumpRequest(void) {
  bool requested = g_quick_dump_requested;
  g_quick_dump_requested = false;
  return requested;
}

void Platform3DS_RequestRomSelection(void) {
  Platform3DS_BlankScreens();
  FILE *file = fopen("sdmc:/3ds/Zelda 3DS/select-rom.flag", "wb");
  if (file) {
    fputs("1\n", file);
    fclose(file);
  }
  g_rom_selection_requested = true;
  g_system_exit_requested = true;
  Platform3DS_LogRuntime("ROM selector requested from settings");
}

bool Platform3DS_TakeRomSelectionRequest(void) {
  bool requested = g_rom_selection_requested;
  g_rom_selection_requested = false;
  if (requested)
    g_system_exit_requested = false;
  return requested;
}

bool Platform3DS_ShouldExit(void) {
  if (g_system_exit_requested || aptShouldClose())
    return true;
  if (!aptMainLoop()) {
    g_system_exit_requested = true;
    return true;
  }
  if (g_system_suspended || !aptIsActive() || aptShouldJumpToHome()) {
    Platform3DS_EndFrame();
    while (!aptShouldClose() && aptMainLoop() &&
           (g_system_suspended || !aptIsActive() || aptShouldJumpToHome())) {
      aptHandleSleep();
      gspWaitForVBlank();
    }
    if (aptShouldClose()) {
      g_system_exit_requested = true;
      return true;
    }
  }
  return false;
}

void Platform3DS_BlankScreens(void) {
  if (!g_gpu_presenter_initialized)
    return;
  if (g_gpu_frame_active) {
    C3D_FrameEnd(0);
    g_gpu_frame_active = false;
  }
  for (int i = 0; i < 3; i++) {
    if (!C3D_FrameBegin(0))
      return;
    C2D_TargetClear(g_top_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(g_top_target);
    C2D_TargetClear(g_bottom_target, C2D_Color32(0, 0, 0, 255));
    C2D_SceneBegin(g_bottom_target);
    C3D_FrameEnd(0);
    gspWaitForVBlank();
  }
}

bool Platform3DS_IsSystemClosing(void) {
  return g_system_exit_requested || aptShouldClose();
}

bool Platform3DS_IsNew3DS(void) {
  Platform3DS_DetectModel();
  return g_is_new_3ds;
}

bool Platform3DS_CanUseCore1PpuWorker(void) {
  return g_core1_time_enabled && g_core1_time_limit_percent > 0;
}

bool Platform3DS_IsVersionOverlayVisible(void) {
  return false;
}

void Platform3DS_SetTurboMultiplier(int multiplier) {
  if (multiplier <= 0)
    multiplier = 0;
  else if (multiplier < 2)
    multiplier = 2;
  if (multiplier > 5)
    multiplier = 5;
  g_turbo_multiplier = multiplier;
  Platform3DS_LogRuntime("Turbo multiplier set: %d", g_turbo_multiplier);
}

bool Platform3DS_InitTopPresenter(void) {
  return true;
}

void Platform3DS_ShutdownTopPresenter(void) {
}

void Platform3DS_PresentTopFrame(const uint8_t *pixels, int pitch,
                                 int width, int height,
                                 int focus_x, int focus_y) {
  (void)pixels;
  (void)pitch;
  (void)width;
  (void)height;
  (void)focus_x;
  (void)focus_y;
}

void Platform3DS_PresentBottomFrame(const uint8_t *pixels, int pitch,
                                    int width, int height) {
  (void)pixels;
  (void)pitch;
  (void)width;
  (void)height;
}

void Platform3DS_EndFrame(void) {
  if (g_gpu_frame_active) {
    C3D_FrameEnd(0);
    g_gpu_frame_active = false;
  }
}

uint32_t Platform3DS_WaitForVBlank(void) {
  gspWaitForVBlank();
  return osGetTime();
}

void Platform3DS_RecordFrameTiming(uint32_t logic_work_us,
                                   uint32_t top_draw_us,
                                   uint32_t ppu_draw_us,
                                   uint32_t capture_us,
                                   uint32_t present_us,
                                   uint32_t top_work_us,
                                   uint32_t bottom_work_us,
                                   uint32_t total_work_us,
                                   uint32_t render_interval_us,
                                   int scheduled_logic_frames,
                                   int executed_logic_frames) {
  (void)logic_work_us;
  (void)top_draw_us;
  (void)ppu_draw_us;
  (void)capture_us;
  (void)present_us;
  (void)top_work_us;
  (void)bottom_work_us;
  (void)total_work_us;
  (void)render_interval_us;
  (void)scheduled_logic_frames;
  (void)executed_logic_frames;
}

bool Platform3DS_CreateDumpDirectory(char *out, size_t out_size) {
  (void)out;
  (void)out_size;
  return false;
}

bool Platform3DS_SaveARGB8888Bmp(const char *path, const uint8_t *pixels,
                                 int pitch, int width, int height) {
  (void)path;
  (void)pixels;
  (void)pitch;
  (void)width;
  (void)height;
  return false;
}

bool Platform3DS_SaveRGB565Bmp(const char *path, const uint8_t *pixels,
                               int pitch, int width, int height) {
  (void)path;
  (void)pixels;
  (void)pitch;
  (void)width;
  (void)height;
  return false;
}

bool Platform3DS_DumpMemory(const char *directory,
                            const uint8_t *ram, size_t ram_size,
                            const uint8_t *sram, size_t sram_size,
                            const uint16_t *vram, size_t vram_words) {
  (void)directory;
  (void)ram;
  (void)ram_size;
  (void)sram;
  (void)sram_size;
  (void)vram;
  (void)vram_words;
  return false;
}
