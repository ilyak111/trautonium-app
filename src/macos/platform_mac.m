/*
 * platform_mac.m — части, которые зависят от macOS:
 *   • текст системным шрифтом через CoreText (чётко на Retina, кириллица);
 *   • папка настроек в ~/Library/Application Support;
 *   • перехват системных жестов (эксперимент, нужен «Универсальный доступ»).
 */
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <ApplicationServices/ApplicationServices.h>
#include <math.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "platform.h"

/* ===================================================================== */
/*                                текст                                  */
/* ===================================================================== */
int plat_text_raster(const char *utf8, float size_pt, int weight, float scale, TextBitmap *out)
{
    memset(out, 0, sizeof *out);
    if (!utf8 || !*utf8 || scale <= 0) return 0;

    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:utf8];
        if (!s) return 0;

        NSFontWeight w = weight == FONT_BOLD   ? NSFontWeightSemibold
                       : weight == FONT_MEDIUM ? NSFontWeightMedium
                                               : NSFontWeightRegular;
        /* цифры одинаковой ширины — значения не «прыгают» при изменении */
        NSFont *font = [NSFont monospacedDigitSystemFontOfSize:size_pt * scale weight:w];
        if (!font) font = [NSFont systemFontOfSize:size_pt * scale];

        NSDictionary *attrs = @{
            NSFontAttributeName: font,
            (__bridge NSString *)kCTForegroundColorFromContextAttributeName: @YES,
        };
        NSAttributedString *as = [[NSAttributedString alloc] initWithString:s attributes:attrs];
        CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)as);
        if (!line) return 0;

        CGFloat asc = 0, desc = 0, lead = 0;
        double width = CTLineGetTypographicBounds(line, &asc, &desc, &lead);
        CGFloat fa = font.ascender, fd = -font.descender;
        if (asc > fa) fa = asc;
        if (desc > fd) fd = desc;

        const int pad = 1;
        int pw = (int)ceil(width) + 2 * pad;
        int ph = (int)ceil(fa + fd) + 2 * pad;
        uint8_t *buf = calloc((size_t)pw * ph, 1);
        if (!buf) { CFRelease(line); return 0; }

        CGContextRef ctx = CGBitmapContextCreate(buf, pw, ph, 8, pw, NULL,
                                                 (CGBitmapInfo)kCGImageAlphaOnly);
        if (!ctx) { free(buf); CFRelease(line); return 0; }
        CGContextSetAllowsAntialiasing(ctx, true);
        CGContextSetShouldAntialias(ctx, true);
        CGContextSetAllowsFontSmoothing(ctx, false);
        CGContextSetGrayFillColor(ctx, 1.0, 1.0);
        CGContextSetTextPosition(ctx, pad, pad + fd);
        CTLineDraw(line, ctx);
        CGContextRelease(ctx);
        CFRelease(line);

        out->w = pw;
        out->h = ph;
        out->lw = pw / scale;
        out->lh = ph / scale;
        out->alpha = buf;
        return 1;
    }
}

void plat_text_free(TextBitmap *b)
{
    free(b->alpha);
    b->alpha = NULL;
}

/* ===================================================================== */
/*                                папки                                  */
/* ===================================================================== */
const char *plat_support_dir(void)
{
    static char path[1024];
    if (path[0]) return path;
    @autoreleasepool {
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                                            NSUserDomainMask, YES);
        NSString *base = dirs.count ? dirs[0] : [NSHomeDirectory() stringByAppendingPathComponent:
                                                 @"Library/Application Support"];
        NSString *p = [base stringByAppendingPathComponent:@"Trautonium"];
        [[NSFileManager defaultManager] createDirectoryAtPath:p withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        snprintf(path, sizeof path, "%s", p.fileSystemRepresentation);
    }
    return path;
}

const char *plat_music_dir(void)
{
    static char path[1024];
    if (path[0]) return path;
    @autoreleasepool {
        NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSMusicDirectory, NSUserDomainMask, YES);
        NSString *base = dirs.count ? dirs[0] : [NSHomeDirectory() stringByAppendingPathComponent:@"Music"];
        NSString *p = [base stringByAppendingPathComponent:@"Trautonium"];
        [[NSFileManager defaultManager] createDirectoryAtPath:p withIntermediateDirectories:YES
                                                   attributes:nil error:nil];
        snprintf(path, sizeof path, "%s", p.fileSystemRepresentation);
    }
    return path;
}

void plat_reveal_file(const char *path)
{
    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:path];
        if (!s) return;
        [[NSWorkspace sharedWorkspace] activateFileViewerSelectingURLs:@[[NSURL fileURLWithPath:s]]];
    }
}

void plat_open_url(const char *url)
{
    @autoreleasepool {
        NSString *s = [NSString stringWithUTF8String:url];
        if (!s) return;
        NSURL *u = [s hasPrefix:@"/"] ? [NSURL fileURLWithPath:s] : [NSURL URLWithString:s];
        if (u) [[NSWorkspace sharedWorkspace] openURL:u];
    }
}

/* ===================================================================== */
/*                     перехват системных жестов                          */
/*                                                                       */
/* События жестов (тип 29) и жестов Dock — Mission Control, переключение */
/* рабочих столов (тип 30) — недокументированы. Пока идёт игра, мы их    */
/* просто выбрасываем. Работает не на всех версиях macOS, поэтому это    */
/* эксперимент и по умолчанию выключено.                                 */
/* ===================================================================== */
#define EV_GESTURE      29
#define EV_DOCK_GESTURE 30

static CFMachPortRef g_tap;
static volatile int  g_block;
static int           g_want;
static int           g_failed;
static int           g_reset_done;
static double        g_last_poll;

static CGEventRef tap_cb(CGEventTapProxy proxy, CGEventType type, CGEventRef ev, void *refcon)
{
    (void)proxy; (void)refcon;
    if (type == kCGEventTapDisabledByTimeout || type == kCGEventTapDisabledByUserInput) {
        if (g_tap) CGEventTapEnable(g_tap, true);
        return ev;
    }
    if (g_block && ((int)type == EV_GESTURE || (int)type == EV_DOCK_GESTURE)) return NULL;
    return ev;
}

static void *tap_thread(void *arg)
{
    (void)arg;
    CFRunLoopSourceRef src = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, g_tap, 0);
    CFRunLoopAddSource(CFRunLoopGetCurrent(), src, kCFRunLoopCommonModes);
    CGEventTapEnable(g_tap, true);
    CFRunLoopRun();
    CFRelease(src);
    return NULL;
}

static void create_tap(void)
{
    if (g_tap || g_failed) return;
    CGEventMask mask = (1ULL << EV_GESTURE) | (1ULL << EV_DOCK_GESTURE);
    g_tap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionDefault,
                             mask, tap_cb, NULL);
    if (!g_tap) { g_failed = 1; return; }
    pthread_t th;
    if (pthread_create(&th, NULL, tap_thread, NULL) != 0) {
        CFRelease(g_tap);
        g_tap = NULL;
        g_failed = 1;
        return;
    }
    pthread_detach(th);
}

void plat_gestures_want(int on)
{
    g_want = on;
    if (!on) g_block = 0;
    if (on && AXIsProcessTrusted()) create_tap();
}

/* После пересборки приложение получает новую подпись, и macOS считает его
   новой программой: старая галочка в Настройках остаётся, но к нему уже не
   относится. Поэтому перед запросом сбрасываем старую запись (только свою,
   по идентификатору приложения) — тогда запрос создаёт свежую. */
static void reset_stale_permission(void)
{
    if (g_reset_done) return;
    g_reset_done = 1;
    NSString *bid = [[NSBundle mainBundle] bundleIdentifier];
    if (!bid.length) return;            /* запуск из Терминала: разрешение у Терминала */
    const char *argv[] = {"tccutil", "reset", "Accessibility", bid.UTF8String, NULL};
    pid_t pid;
    if (posix_spawn(&pid, "/usr/bin/tccutil", NULL, NULL, (char *const *)argv, NULL) == 0) {
        int st;
        waitpid(pid, &st, 0);
    }
}

void plat_gestures_request(void)
{
    if (AXIsProcessTrusted()) { create_tap(); return; }
    @autoreleasepool {
        reset_stale_permission();
        NSDictionary *opts = @{ (__bridge NSString *)kAXTrustedCheckOptionPrompt: @YES };
        AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)opts);
    }
}

void plat_gestures_poll(void)
{
    if (!g_want || g_tap || g_failed) return;
    double now = CFAbsoluteTimeGetCurrent();
    if (now - g_last_poll < 1.0) return;
    g_last_poll = now;
    if (AXIsProcessTrusted()) create_tap();
}

void plat_gestures_block(int on)
{
    g_block = on && g_tap && g_want;
}

int plat_gestures_status(void)
{
    if (!g_want)  return GESTURES_OFF;
    if (g_failed) return GESTURES_FAILED;
    if (!g_tap)   return GESTURES_WAITING;
    return g_block ? GESTURES_BLOCKING : GESTURES_READY;
}
