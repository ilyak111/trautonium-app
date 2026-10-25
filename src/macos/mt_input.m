/*
 * mt_input.m — мультитач и сила нажатия тачпада MacBook через приватный
 * фреймворк MultitouchSupport (тот же, что используют OpenMultitouchSupport,
 * TrackWeight, HapticKey и др.).
 *
 * Фреймворк загружается через dlopen: если Apple что-то поменяет, программа
 * не упадёт, а просто перейдёт на мышь / стандартные события SDL.
 *
 * Касания приходят в отдельном потоке примерно 90–125 раз в секунду.
 * Последний кадр хранится под os_unfair_lock; аудиопоток забирает его
 * через try-lock, так что он никогда не ждёт.
 */
#import <Cocoa/Cocoa.h>
#include <dlfcn.h>
#include <stdio.h>
#include <os/lock.h>
#include <pthread.h>
#include <string.h>
#include "mt_input.h"

/* ---- раскладка структуры касания (96 байт), см. OpenMultitouchSupport ---- */
typedef struct { float x, y; } MTPoint_;
typedef struct { MTPoint_ pos, vel; } MTVector_;

typedef struct {
    int       frame;
    double    timestamp;
    int       identifier;
    int       state;          /* 3 — касание началось, 4 — палец на тачпаде */
    int       fingerId;
    int       handId;
    MTVector_ normalized;     /* 0..1, y — снизу вверх */
    float     total;          /* суммарная ёмкость */
    float     pressure;       /* сила (Force Touch), ≈ граммы */
    float     angle;
    float     majorAxis;
    float     minorAxis;
    MTVector_ absolute;       /* миллиметры */
    int       field14;
    int       field15;
    float     density;
} MTTouch_;

_Static_assert(sizeof(MTTouch_) == 96, "MTTouch layout changed");

typedef void *MTDeviceRef;
typedef void (*MTFrameCallback)(MTDeviceRef, MTTouch_ *, int, double, int);

static CFArrayRef  (*pCreateList)(void);
static MTDeviceRef (*pCreateDefault)(void);
static void        (*pRelease)(MTDeviceRef);
static bool        (*pIsBuiltIn)(MTDeviceRef);
static bool        (*pSupportsForce)(MTDeviceRef);
static void        (*pRegister)(MTDeviceRef, MTFrameCallback);
static void        (*pUnregister)(MTDeviceRef, MTFrameCallback);
static int32_t     (*pStart)(MTDeviceRef, int);
static int32_t     (*pStop)(MTDeviceRef);
static int32_t     (*pGetDeviceID)(MTDeviceRef, uint64_t *);
static CFTypeRef   (*pActCreate)(uint64_t);
static int32_t     (*pActOpen)(CFTypeRef);
static int32_t     (*pActClose)(CFTypeRef);
static int32_t     (*pActActuate)(CFTypeRef, int32_t, uint32_t, float, float);

static MTDeviceRef       g_dev;
static CFTypeRef         g_act;
static int               g_flags;
static os_unfair_lock    g_lock = OS_UNFAIR_LOCK_INIT;
static MTFrame           g_frame;
static unsigned          g_seq;
static dispatch_queue_t  g_hq;
static dispatch_semaphore_t g_ready;
static CFRunLoopRef      g_loop;

double mt_now(void)
{
    return CFAbsoluteTimeGetCurrent();
}

static void frame_cb(MTDeviceRef dev, MTTouch_ *t, int n, double ts, int frame)
{
    (void)dev; (void)ts; (void)frame;
    MTFrame f;
    f.count = 0;
    for (int i = 0; i < n && f.count < MT_MAX_TOUCHES; i++) {
        if (t[i].state != 3 && t[i].state != 4) continue;   /* наведение/отрыв — пропускаем */
        MTPoint *p = &f.t[f.count++];
        p->id       = t[i].identifier;
        p->x        = t[i].normalized.pos.x;
        p->y        = 1.0f - t[i].normalized.pos.y;           /* делаем y сверху вниз */
        p->pressure = t[i].pressure;
        p->area     = t[i].total;
    }
    f.time = mt_now();

    os_unfair_lock_lock(&g_lock);
    g_frame = f;
    g_seq++;
    os_unfair_lock_unlock(&g_lock);
}

int mt_read(MTFrame *out, int try_only)
{
    if (!(g_flags & MT_OK)) return 0;
    if (try_only) {
        if (!os_unfair_lock_trylock(&g_lock)) return 0;
    } else {
        os_unfair_lock_lock(&g_lock);
    }
    *out = g_frame;
    os_unfair_lock_unlock(&g_lock);
    return 1;
}

static void *load(void *lib, const char *name)
{
    return dlsym(lib, name);
}

static MTDeviceRef pick_device(void)
{
    MTDeviceRef found = NULL;
    if (pCreateList) {
        CFArrayRef list = pCreateList();
        if (list) {
            CFIndex n = CFArrayGetCount(list);
            /* сначала встроенный тачпад, иначе первое устройство */
            for (CFIndex i = 0; i < n && !found; i++) {
                MTDeviceRef d = (MTDeviceRef)CFArrayGetValueAtIndex(list, i);
                if (pIsBuiltIn && pIsBuiltIn(d)) found = d;
            }
            if (!found && n > 0) found = (MTDeviceRef)CFArrayGetValueAtIndex(list, 0);
            if (found) CFRetain(found);
            CFRelease(list);
        }
    }
    if (!found && pCreateDefault) found = pCreateDefault();
    return found;
}

static void noop_timer(CFRunLoopTimerRef t, void *info) { (void)t; (void)info; }

/* поток со своим run loop: так касания приходят сразу, а не раз в кадр экрана */
static void *mt_thread(void *arg)
{
    (void)arg;
    @autoreleasepool {
        g_dev = pick_device();
        if (g_dev) {
            pRegister(g_dev, frame_cb);
            int32_t rc = pStart(g_dev, 0);
            if (rc != 0) fprintf(stderr, "MTDeviceStart вернул %d — пробуем всё равно\n", (int)rc);
            g_flags |= MT_OK;
            /* если функции проверки нет — считаем, что Force Touch есть (все Mac с 2015 г.) */
            if (!pSupportsForce || pSupportsForce(g_dev)) g_flags |= MT_FORCE;
        }
        g_loop = CFRunLoopGetCurrent();
        CFRetain(g_loop);
    }
    dispatch_semaphore_signal(g_ready);

    /* держим run loop живым, даже если фреймворк на нём ничего не повесил */
    CFRunLoopTimerRef keep = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1e9,
                                                  1e9, 0, 0, noop_timer, NULL);
    CFRunLoopAddTimer(CFRunLoopGetCurrent(), keep, kCFRunLoopDefaultMode);
    CFRunLoopRun();
    CFRelease(keep);
    return NULL;
}

static void open_actuator(void)
{
    if (!g_dev || !pGetDeviceID || !pActCreate || !pActOpen || !pActActuate) return;
    uint64_t id = 0;
    if (pGetDeviceID(g_dev, &id) != 0 || id == 0) return;
    CFTypeRef a = pActCreate(id);
    if (!a) return;
    if (pActOpen(a) != 0) { CFRelease(a); return; }
    g_act = a;
    g_flags |= MT_HAPTIC;
}

/* закрыть и заново открыть тачпад — в его собственном потоке */
static void mt_restart(void)
{
    if (!g_loop) return;
    CFRunLoopPerformBlock(g_loop, kCFRunLoopDefaultMode, ^{
        if (g_dev) {
            if (pUnregister) pUnregister(g_dev, frame_cb);
            if (pStop) pStop(g_dev);
            if (pRelease) pRelease(g_dev);
            g_dev = NULL;
        }
        g_dev = pick_device();
        if (g_dev) {
            pRegister(g_dev, frame_cb);
            pStart(g_dev, 0);
        }
        /* хаптику тоже открываем заново, в её очереди — чтобы не мешать щелчкам */
        if (g_hq) dispatch_async(g_hq, ^{
            if (g_act) {
                if (pActClose) pActClose(g_act);
                CFRelease(g_act);
                g_act = NULL;
                g_flags &= ~MT_HAPTIC;
            }
            open_actuator();
        });
    });
    CFRunLoopWakeUp(g_loop);
}

int mt_start(void)
{
    void *lib = dlopen("/System/Library/PrivateFrameworks/MultitouchSupport.framework/MultitouchSupport",
                       RTLD_NOW);
    if (!lib) return 0;

    pCreateList    = load(lib, "MTDeviceCreateList");
    pCreateDefault = load(lib, "MTDeviceCreateDefault");
    pRelease       = load(lib, "MTDeviceRelease");
    pIsBuiltIn     = load(lib, "MTDeviceIsBuiltIn");
    pSupportsForce = load(lib, "MTDeviceSupportsForce");
    pRegister      = load(lib, "MTRegisterContactFrameCallback");
    pUnregister    = load(lib, "MTUnregisterContactFrameCallback");
    pStart         = load(lib, "MTDeviceStart");
    pStop          = load(lib, "MTDeviceStop");
    pGetDeviceID   = load(lib, "MTDeviceGetDeviceID");
    pActCreate     = load(lib, "MTActuatorCreateFromDeviceID");
    pActOpen       = load(lib, "MTActuatorOpen");
    pActClose      = load(lib, "MTActuatorClose");
    pActActuate    = load(lib, "MTActuatorActuate");

    if (!pRegister || !pStart || (!pCreateList && !pCreateDefault)) return 0;

    g_ready = dispatch_semaphore_create(0);
    pthread_t th;
    if (pthread_create(&th, NULL, mt_thread, NULL) != 0) return 0;
    pthread_detach(th);
    dispatch_semaphore_wait(g_ready, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));

    if (g_flags & MT_OK) {
        open_actuator();
        g_hq = dispatch_queue_create("trautonium.haptic", DISPATCH_QUEUE_SERIAL);
        /* после сна Mac тачпад нужно открыть заново, иначе касания перестают приходить */
        [[[NSWorkspace sharedWorkspace] notificationCenter]
            addObserverForName:NSWorkspaceDidWakeNotification object:nil queue:nil
                    usingBlock:^(NSNotification *note) { (void)note; mt_restart(); }];
    }
    return g_flags;
}

void mt_stop(void)
{
    if (g_dev && (g_flags & MT_OK)) {
        if (pUnregister) pUnregister(g_dev, frame_cb);
        if (pStop) pStop(g_dev);
    }
    if (g_act) {
        if (pActClose) pActClose(g_act);
        CFRelease(g_act);
        g_act = NULL;
    }
    if (g_loop) {
        CFRunLoopStop(g_loop);
        CFRelease(g_loop);
        g_loop = NULL;
    }
    g_flags = 0;
}

void mt_haptic(int strength)
{
    if (strength < 1) strength = 1;
    if (strength > 6) strength = 6;

    if (g_act && g_hq) {
        int s = strength;
        dispatch_async(g_hq, ^{
            if (g_act) pActActuate(g_act, s, 0, 1.0f, 2.0f);
        });
        return;
    }

    /* запасной вариант — публичный API (работает, пока палец на тачпаде) */
    @autoreleasepool {
        [[NSHapticFeedbackManager defaultPerformer]
            performFeedbackPattern:(strength >= 3 ? NSHapticFeedbackPatternLevelChange
                                                  : NSHapticFeedbackPatternAlignment)
                   performanceTime:NSHapticFeedbackPerformanceTimeNow];
    }
}
