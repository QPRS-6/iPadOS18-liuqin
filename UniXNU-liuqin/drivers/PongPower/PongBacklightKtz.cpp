// SPDX-License-Identifier: BSD-2-Clause-Patent
// Liuqin KTZ8866 LCD backlight. The board's backlight controller is a Kinetic
// KTZ8866 on QUP0 SE1 (0x984000), address 0x11, enable on TLMM gpio6. The
// engine runs the GENI I2C firmware the boot loaders left loaded; this uses
// the same FIFO state machine as the audio/PHY code (GeniI2c.c) and never
// touches the display controller. Register numbers and the 11-bit level
// (LSB bits 2:0, MSB bits 7:0, maximum 2047) follow the mainline ktz8866
// driver; BL_EN bit6 gates all sinks while bits 5:0 select them.
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOUserClient.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include <libkern/c++/OSNumber.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSSymbol.h>
#include <libkern/OSKextLib.h>
#include <mach/kmod.h>
#include <pexpert/pexpert.h>
#include "GeniI2c.h"
#include "PongBacklightRequest.h"

extern "C" vm_offset_t ml_io_map(vm_offset_t, vm_size_t);

extern kmod_info_t g_kernel_kmod_info;
extern "C" void PongBacklightStaticInit(void);
class PongBacklight;

namespace {
PongBacklight *gPublishedBacklight;
const OSMetaClass *gAppleBacklightClass;
/* pong_backlight_publish=0 keeps the KTZ8866 hardware path but never publishes
 * an AppleARMBacklight service, so a bring-up round can tell the hardware work
 * apart from the CoreBrightness/backboardd interaction. */
bool gPublishAllowed = true;
/* pong_backlight_publish is a bit mask so one kernel can be tried in several
 * configurations: 1 = set backlight-control/built-in, 2 = publish
 * IODisplayParameters, 4 = pretend to be AppleARMBacklight.  0 publishes
 * nothing; 8 registers a bare service. Older experimental masks are disabled. */
bool gSpoofClass = true;
bool gPublishParams = true;
bool gPublishProps = true;

enum : uint32_t {
    DeviceId = 0x01, BlCfg1 = 0x02, BlCfg2 = 0x03,
    BlBrtLsb = 0x04, BlBrtMsb = 0x05, BlEn = 0x08, Flag = 0x0f
};
constexpr uint32_t kSinkEnable = 0x3f;      // six current sinks
constexpr uint32_t kAllSinksOn = 1u << 6;   // global BL enable
constexpr uint32_t kGeniCgc = 0x7f;
constexpr uint32_t kGeniIoMux = 0x7f;
constexpr uint32_t kGccQupe = 0x62008;
/* SM8475 GCC: wrap0 s1 = m_ahb bit6, s_ahb bit7, core bit8, core_2x bit9,
 * s1 bit11; the matching halt/status registers. */
constexpr uint32_t kQupeVote = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 11);
const uint32_t kQupeHalt[] = {0x27004, 0x27008, 0x33000, 0x3300c, 0x27140};
/* TLMM: one 0x1000 block per pin; IN_OUT at +4, OUT is bit1, oe bit9. */
constexpr uint32_t kTlmmInOut = 0x4;
constexpr uint32_t kTlmmOut = 1u << 1;
constexpr uint32_t kTlmmConfig = (1u << 9) | (2u << 6);

struct Board {
    uint64_t base;
    uint32_t span, gpio, address, maximum, version, reserved;
};
static_assert(sizeof(Board) == 32, "Pong I2C backlight ADT wire size");

uint64_t nowNs() {
    uint64_t ns = 0;
    absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
    return ns;
}
}

/* A plain user client is all IOConnectSetCFProperties needs: the property
 * path resolves the service from the connection and calls its setProperties,
 * which is where the KTZ8866 level is latched. */
class PongBacklightUserClient : public IOUserClient {
    OSDeclareDefaultStructors(PongBacklightUserClient)
public:
    bool initWithTask(task_t owningTask, void *securityID, UInt32 type,
                      OSDictionary *properties) override;
    void stop(IOService *provider) override;
    IOReturn setProperties(OSObject *properties) override;
    IOReturn clientClose() override;
    IOReturn externalMethod(uint32_t selector, IOExternalMethodArguments *arguments,
        IOExternalMethodDispatch *dispatch = nullptr, OSObject *target = nullptr,
        void *reference = nullptr) override;
};

class PongBacklight : public IOService {
    OSDeclareDefaultStructors(PongBacklight)
    Board board = {};
    vm_offset_t seWindow = 0, gccWindow = 0, pinsWindow = 0;
    volatile uint32_t *se = nullptr, *gcc = nullptr, *pins = nullptr;
    GeniI2c bus = {};
    IOWorkLoop *loop = nullptr;
    IOTimerEventSource *timer = nullptr;
    const OSMetaClass *appleClass = nullptr;
    bool published = false, stopped = false;
    uint32_t level = 0, sending = 0, from = 0, goal = 0;
    uint64_t deadline = 0, classDeadline = 0, fadeStart = 0, fadeDuration = 0;
    uint64_t request = 0;
    uint64_t consumedRequest = 0;
    bool ready = false;
    bool displayOn = true, consumedDisplayOn = true;
    uint32_t userMinimum = 102; // about 5%; zero remains available to the internal off path
    uint32_t requestedFade = 0, requests = 0, writes = 0, reads = 0, failures = 0;
    uint64_t lastErrorLog = 0;

    static uint32_t ioRead(void *ctx, uint32_t reg) {
        return static_cast<PongBacklight *>(ctx)->se[reg / 4];
    }
    static void ioWrite(void *ctx, uint32_t reg, uint32_t value) {
        static_cast<PongBacklight *>(ctx)->se[reg / 4] = value;
        __asm__ volatile("dsb sy" ::: "memory");
    }
    uint32_t rdGcc(uint32_t offset) const { return gcc[offset / 4]; }
    void wrGcc(uint32_t offset, uint32_t value) {
        gcc[offset / 4] = value; __asm__ volatile("dsb sy" ::: "memory");
    }
    uint32_t rdPin(uint32_t offset) const { return pins[offset / 4]; }
    void wrPin(uint32_t offset, uint32_t value) {
        pins[offset / 4] = value; __asm__ volatile("dsb sy" ::: "memory");
    }
    uint32_t clamped(uint32_t value) const {
        return value > board.maximum ? board.maximum : value;
    }

    /* The branch votes are harmless when the boot loader left them set; the
     * halt registers decide whether the SE block is clocked before any of its
     * registers are touched. */
    bool ensureClocks() {
        wrGcc(kGccQupe, rdGcc(kGccQupe) | kQupeVote);
        const uint64_t end = nowNs() + 100000000ULL;
        for (;;) {
            bool running = true;
            for (uint32_t offset : kQupeHalt) {
                const uint32_t value = rdGcc(offset);
                if ((value & 0x80000000u) && !(value & 2u)) running = false;
            }
            if (running) return true;
            if (nowNs() >= end) return false;
            IOSleep(1);
        }
    }
    void enablePin() {
        const uint32_t base = board.gpio * 0x1000;
        const uint32_t before = rdPin(base), io = rdPin(base + kTlmmInOut);
        setProperty("PongEnableGpioBefore", before, 32);
        wrPin(base + kTlmmInOut, io | kTlmmOut);
        wrPin(base, kTlmmConfig);
        wrPin(base + kTlmmInOut, io | kTlmmOut);
        setProperty("PongEnableGpioAfter", rdPin(base), 32);
    }
    /* Same FIFO transport as PongAudio: start, then feed the engine's status
     * until it is done. Bounded to three 20 ms phases; the caller runs on this
     * service's own work loop. */
    int transfer(bool reading, uint8_t reg, uint8_t value, uint8_t *answer) {
        const int started = reading ? geni_i2c_start_read(&bus, reg)
                                    : geni_i2c_start_write(&bus, reg, value);
        if (started) { failures++; return -1; }
        for (unsigned phase = 0; phase < 3; phase++) {
            for (unsigned i = 0; i < 1000; i++) {
                const int result = geni_i2c_poll(&bus);
                if (result == GeniDone) {
                    if (answer) *answer = bus.data;
                    return 0;
                }
                if (result == GeniFailed) { failures++; return -1; }
                IODelay(20);
            }
            if (geni_i2c_timeout(&bus) != GeniPending) break;
        }
        failures++;
        return -1;
    }
    bool readRegister(uint8_t reg, uint8_t &value) {
        if (transfer(true, reg, 0, &value)) return false;
        reads++;
        return true;
    }
    bool writeRegister(uint8_t reg, uint8_t value) {
        if (transfer(false, reg, value, nullptr)) return false;
        writes++;
        return true;
    }
    bool readLevel(uint32_t &value) {
        uint8_t lsb = 0, msb = 0;
        if (!readRegister(BlBrtLsb, lsb) || !readRegister(BlBrtMsb, msb)) return false;
        value = clamped((uint32_t)((msb << 3) | (lsb & 7u)));
        return true;
    }
    /* Mirrors the mainline update_status: gate the sinks, then latch the
     * 11-bit level. A readback distinguishes a NACKed write from a stale one. */
    bool writeLevel(uint32_t value, uint32_t &measured, bool &known) {
        known = false;
        uint8_t enabled = 0;
        if (!readRegister(BlEn, enabled)) {
            known = readLevel(measured);
            return false;
        }
        const uint8_t wanted = value ? (uint8_t)(enabled | kAllSinksOn)
                                     : (uint8_t)(enabled & ~kAllSinksOn);
        bool ok = true;
        if (wanted != enabled) ok = writeRegister(BlEn, wanted);
        ok = writeRegister(BlBrtLsb, (uint8_t)(value & 7u)) && ok;
        ok = writeRegister(BlBrtMsb, (uint8_t)((value >> 3) & 0xffu)) && ok;
        /* Even a partially failed write can change a register. Recover the
         * actual level before rolling back the accepted UI target. */
        known = readLevel(measured);
        if (!known) return false;
        setProperty("PongBrightnessReadback", measured, 32);
        if (!ok) return false;
        if (measured != value) {
            setProperty("PongBacklightError", "level readback differs");
            return false;
        }
        removeProperty("PongBacklightError");
        return true;
    }
    void logFailure(const char *reason) {
        const uint64_t now = nowNs();
        setProperty("PongBacklightError", reason);
        if (now - lastErrorLog > 1000000000ULL) {
            lastErrorLog = now;
            IOLog("PongBacklight: %s; requests=%u writes=%u reads=%u failures=%u fw=%x cfg=%x scl=%x\n",
                  reason, __atomic_load_n(&requests, __ATOMIC_RELAXED), writes, reads, failures,
                  se ? se[GENI_FW_REVISION / 4] : 0, se ? se[GENI_CLK_CFG / 4] : 0,
                  se ? se[GENI_SCL_COUNTERS / 4] : 0);
        }
    }
    void publishLevel() {
        OSDictionary *item = OSDictionary::withCapacity(3);
        OSDictionary *parameters = OSDictionary::withCapacity(1);
        OSNumber *minimum = OSNumber::withNumber(0ULL, 32);
        OSNumber *maximum = OSNumber::withNumber(board.maximum, 32);
        OSNumber *value = OSNumber::withNumber(level, 32);
        if (item && parameters && minimum && maximum && value) {
            item->setObject("min", minimum); item->setObject("max", maximum); item->setObject("value", value);
            parameters->setObject("brightness", item); setProperty("IODisplayParameters", parameters);
        }
        OSSafeReleaseNULL(item); OSSafeReleaseNULL(parameters);
        OSSafeReleaseNULL(minimum); OSSafeReleaseNULL(maximum); OSSafeReleaseNULL(value);
    }
    void publishFirst() {
        // Use the actual AppleARMPlatform metaclass for CoreBrightness's
        // conformance query. The getter below is compiled against Darwin24;
        // no vtable-size assumption, fixed slot, or duplicate class name.
        if (gSpoofClass) {
            gAppleBacklightClass = appleClass;
            __atomic_store_n(&gPublishedBacklight, this, __ATOMIC_RELEASE);
            setName("AppleARMBacklight");
            setProperty("NativeSandboxGrantProcess", "backboardd");
        }
        published = true;
        if (gPublishProps) {
            setProperty("backlight-control", true); setProperty("built-in", true);
        }
        if (gPublishParams) publishLevel();
        registerService();
        IOLog("PongBacklight: KTZ8866 service ready spoof=%u props=%u params=%u level=%u\n",
              gSpoofClass ? 1u : 0u, gPublishProps ? 1u : 0u, gPublishParams ? 1u : 0u, level);
    }
    void service() {
        if (__atomic_load_n(&stopped, __ATOMIC_ACQUIRE)) return;
        if (!published && gPublishAllowed && gSpoofClass && !appleClass) {
            // AppleARMPlatform initializes asynchronously after framebuffer
            // startup. Never register its private class name in the kernel:
            // its later real initializer would panic on the duplicate.
            const OSSymbol *name = OSSymbol::withCString("AppleARMBacklight");
            appleClass = name ? OSMetaClass::getMetaClassWithName(name) : nullptr;
            OSSafeReleaseNULL(name);
            if (!appleClass) {
                if (nowNs() >= classDeadline) {
                    __atomic_store_n(&stopped, true, __ATOMIC_RELEASE);
                    setProperty("PongBacklightError", "native AppleARMBacklight class unavailable");
                    IOLog("PongBacklight: native AppleARMBacklight class unavailable; brightness stays at the boot level\n");
                    return;
                }
                timer->setTimeoutMS(100); return;
            }
            IOLog("PongBacklight: native AppleARMBacklight class ready\n");
        }
        if (!published && gPublishAllowed) { from = goal = level; publishFirst(); }
        const uint64_t latest = __atomic_load_n(&request, __ATOMIC_ACQUIRE);
        const bool on = __atomic_load_n(&displayOn, __ATOMIC_ACQUIRE);
        if (latest != consumedRequest || on != consumedDisplayOn) {
            consumedRequest = latest;
            consumedDisplayOn = on;
            from = level; goal = on ? clamped(pong_bl_target(latest)) : 0;
            fadeStart = nowNs(); fadeDuration = on ? pong_bl_fade(latest) * 1000000ULL : 0;
        }
        const uint64_t elapsed = nowNs() - fadeStart;
        uint32_t next = goal;
        if (fadeDuration && elapsed < fadeDuration)
            next = (uint32_t)((int64_t)from + ((int64_t)goal - from) * (int64_t)elapsed / (int64_t)fadeDuration);
        next = clamped(next);
        if (next != level) {
            sending = next;
            uint32_t measured = level;
            bool known = false;
            if (writeLevel(next, measured, known)) {
                level = measured;
            } else {
                logFailure("I2C brightness write failed");
                if (known) level = measured;
                from = goal = level; fadeDuration = 0;
                uint64_t expected = consumedRequest;
                const uint64_t rollback = pong_bl_next(expected, level, 0);
                if (on && __atomic_compare_exchange_n(&request, &expected, rollback,
                        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                    consumedRequest = rollback;
                /* No trustworthy readback or engine retirement: do not claim
                 * readiness or continue issuing requests to an unknown FIFO. */
                if (!known || bus.phase != GeniIdle) {
                    __atomic_store_n(&ready, false, __ATOMIC_RELEASE);
                    __atomic_store_n(&stopped, true, __ATOMIC_RELEASE);
                }
            }
            if (gPublishParams) publishLevel();
        }
    }
    static void tick(OSObject *owner, IOTimerEventSource *) {
        PongBacklight *self = static_cast<PongBacklight *>(owner);
        self->service();
        /* Only this callback schedules the timer after prepare(). Foreign
         * threads just update the mailbox; no timer generation races. */
        if (!__atomic_load_n(&self->stopped, __ATOMIC_ACQUIRE))
            self->timer->setTimeoutMS(16);
    }
public:
    bool prepare() {
        IOLog("PongBacklight: prepare enter\n");
        IORegistryEntry *chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
        OSData *data = chosen ? OSDynamicCast(OSData, chosen->getProperty("pong-i2c-backlight")) : nullptr;
        if (data && data->getLength() == sizeof(board)) memcpy(&board, data->getBytesNoCopy(), sizeof(board));
        OSSafeReleaseNULL(chosen);
        if (board.version != 1 || board.base != 0x984000 || board.span < 0x4000 ||
            board.address != 0x11 || board.maximum != 2047 || board.gpio > 200) return false;
        PE_parse_boot_argn("pong_backlight_min", &userMinimum, sizeof(userMinimum));
        userMinimum = clamped(userMinimum);
        setProperty("PongBacklightMinimum", userMinimum, 32);
        /* Device windows below the DRAM base are not phystokv-able; use the
         * same uncached IO mapping the touch driver uses for TLMM/GCC. */
        gccWindow = ml_io_map(0x100000, 0xa0000);
        pinsWindow = ml_io_map(0xf100000, 0x200000);
        IOLog("PongBacklight: maps gcc=%#llx pins=%#llx\n",
              (unsigned long long)gccWindow, (unsigned long long)pinsWindow);
        if (!gccWindow || !pinsWindow) return false;
        gcc = reinterpret_cast<volatile uint32_t *>(gccWindow);
        pins = reinterpret_cast<volatile uint32_t *>(pinsWindow);
        IOLog("PongBacklight: clocks before\n");
        if (!ensureClocks()) {
            IOLog("PongBacklight: QUP0 SE1 clock did not come up; keep current panel brightness\n");
            return false;
        }
        IOLog("PongBacklight: clocks ready\n");
        seWindow = ml_io_map((vm_offset_t)board.base, board.span);
        se = seWindow ? reinterpret_cast<volatile uint32_t *>(seWindow) : nullptr;
        if (!se) return false;
        IOLog("PongBacklight: SE mapped, reading revision\n");
        const uint32_t revision = se[GENI_FW_REVISION / 4];
        setProperty("PongGENIRevision", revision, 32);
        setProperty("PongGENIClockConfig", se[GENI_CLK_CFG / 4], 32);
        setProperty("PongGENISclCounters", se[GENI_SCL_COUNTERS / 4], 32);
        if (((revision >> 8) & 0xffu) != GENI_PROTO_I2C || (se[GENI_IF_DISABLE / 4] & 1u)) {
            IOLog("PongBacklight: QUP0 SE1 protocol=%u if_disable=%u; I2C firmware required\n",
                  (revision >> 8) & 0xffu, se[GENI_IF_DISABLE / 4] & 1u);
            return false;
        }
        /* The boot loader normally leaves the engine configured. Only program
         * the serial clock and FIFO when it did not, so a working bus is never
         * reprogrammed underneath the running panel. */
        if (!(se[GENI_CLK_CFG / 4] & 1u) || !se[GENI_SCL_COUNTERS / 4]) {
            se[0x28 / 4] = se[0x28 / 4] | kGeniCgc;
            se[0xe30 / 4] = se[0xe30 / 4] | 15u;
            se[0x24 / 4] = kGeniIoMux;
            se[0x20 / 4] = 1;
            se[0xe18 / 4] = 0;
            se[0x7c / 4] = se[0x7c / 4] & ~7u;       // SE_GENI_CLK_SEL level 0
            se[GENI_CLK_CFG / 4] = (2u << 4) | 1u;   // div 2, serial clock on
            se[GENI_SCL_COUNTERS / 4] = (5u << 20) | (11u << 10) | 22u;
            __asm__ volatile("dsb sy" ::: "memory");
        }
        GeniIo io = {ioRead, ioWrite, this};
        uint32_t config = 0;
        if (geni_i2c_prepare(&bus, &io, (uint8_t)board.address, &config)) {
            IOLog("PongBacklight: QUP0 SE1 not ready for I2C (config=%x)\n", config);
            return false;
        }
        IOLog("PongBacklight: enable pin\n");
        enablePin();
        IOLog("PongBacklight: reading KTZ registers\n");
        uint8_t id = 0, cfg1 = 0, enabled = 0;
        if (!readRegister(DeviceId, id) || !readRegister(BlCfg1, cfg1) || !readRegister(BlEn, enabled)) {
            IOLog("PongBacklight: KTZ8866 at %x did not answer (failures=%u)\n", board.address, failures);
            return false;
        }
        setProperty("PongBacklightDevice", id, 32);
        setProperty("PongBacklightConfig", cfg1, 32);
        setProperty("PongBacklightEnable", enabled, 32);
        if (!readLevel(level)) {
            IOLog("PongBacklight: KTZ8866 brightness read failed\n");
            return false;
        }
        from = goal = level;   /* the stepper works from the live level */
        consumedRequest = (uint64_t)level << 16;
        __atomic_store_n(&request, consumedRequest, __ATOMIC_RELEASE);
        // Keep the sinks enabled even if the boot loader left them off; the
        // level itself is published from the panel, not overwritten here.
        if (!(enabled & kAllSinksOn) && !writeRegister(BlEn, (uint8_t)(enabled | kAllSinksOn)))
            return false;
        loop = IOWorkLoop::workLoop();
        timer = IOTimerEventSource::timerEventSource(this, tick);
        if (!loop || !timer || loop->addEventSource(timer)) return false;
        setProperty("PongPanel", "CSOT NT36532 LCD / KTZ8866 QUP0 SE1");
        classDeadline = nowNs() + 30000000000ULL;
        timer->setTimeoutMS(1);
        __atomic_store_n(&ready, true, __ATOMIC_RELEASE);
        IOLog("PongBacklight: KTZ8866 id=%x cfg1=%x enable=%x level=%u gpio%u=%x\n",
              id, cfg1, enabled, level, board.gpio, rdPin(board.gpio * 0x1000));
        return true;
    }
    IOReturn setProperties(OSObject *object) override {
        OSDictionary *properties = OSDynamicCast(OSDictionary, object);
        if (!properties) return kIOReturnBadArgument;
        IOLog("PongBacklight: setProperties count=%u brightness=%u fade=%u calibrated=%u\n",
              properties->getCount(), properties->getObject("brightness") ? 1u : 0u,
              properties->getObject("FadePeriod") ? 1u : 0u,
              properties->getObject("backlight-level") ? 1u : 0u);
        if (__atomic_load_n(&stopped, __ATOMIC_ACQUIRE)) return kIOReturnNotReady;
        // Darwin24 also sends the optional calibrated current-table level.
        // It is metadata, not another KTZ8866 register write.
        if (properties->getCount() > 3 ||
            properties->getCount() != (properties->getObject("brightness") ? 1u : 0u) +
                                      (properties->getObject("FadePeriod") ? 1u : 0u) +
                                      (properties->getObject("backlight-level") ? 1u : 0u)) return kIOReturnUnsupported;
        OSNumber *brightness = OSDynamicCast(OSNumber, properties->getObject("brightness"));
        OSNumber *fade = OSDynamicCast(OSNumber, properties->getObject("FadePeriod"));
        OSNumber *calibrated = OSDynamicCast(OSNumber, properties->getObject("backlight-level"));
        if ((properties->getObject("brightness") && !brightness) ||
            (properties->getObject("FadePeriod") && !fade) ||
            (properties->getObject("backlight-level") && !calibrated)) return kIOReturnBadArgument;
        const uint64_t fixed = fade ? fade->unsigned64BitValue() : 0;
        const uint64_t ms = fade ? (fixed > 5u * 65536u ? 5000u : (fixed * 1000) >> 16) :
                                  __atomic_load_n(&requestedFade, __ATOMIC_RELAXED);
        __atomic_store_n(&requestedFade, (uint32_t)ms, __ATOMIC_RELAXED);
        if (!brightness) return kIOReturnSuccess;
        const int64_t value = (int64_t)brightness->unsigned64BitValue();
        const uint32_t target = value < 0 ? 0 : clamped(value > board.maximum ? board.maximum : (uint32_t)value);
        __atomic_fetch_add(&requests, 1u, __ATOMIC_RELAXED);
        return requestLevel(target, (uint32_t)ms) >= 0 ? kIOReturnSuccess : kIOReturnNotReady;
    }
    int requestLevel(uint32_t target, uint32_t fadeMs, bool relative = false,
        uint32_t minimum = 0)
    {
        if (!__atomic_load_n(&ready, __ATOMIC_ACQUIRE)) return -1;
        return pong_bl_submit(&request, relative ? (int64_t)(int32_t)target : target,
            fadeMs, board.maximum, relative, minimum);
    }
    int setLevel(int level, int *applied)
    {
        int64_t next = level;
        if (next < userMinimum) next = userMinimum;
        if (next > (int64_t)board.maximum) next = (int64_t)board.maximum;
        int result = requestLevel((uint32_t)next,
            __atomic_load_n(&requestedFade, __ATOMIC_RELAXED));
        if (result >= 0 && applied) *applied = result;
        return result;
    }
    /* Return the accepted target so a fade does not snap the UI backward. */
    int currentLevel() const {
        return __atomic_load_n(&ready, __ATOMIC_ACQUIRE) ?
            (int)pong_bl_target(__atomic_load_n(&request, __ATOMIC_ACQUIRE)) : -1;
    }

    int stepLevel(int delta, int *applied)
    {
        int result = requestLevel((uint32_t)delta, 0, true, userMinimum);
        if (result >= 0 && applied) *applied = result;
        return result;
    }
    void setDisplayPower(bool on) {
        /* Keep the accepted user brightness intact while the physical sink is
         * off. The work loop applies zero and restores the saved target on wake. */
        bool old = __atomic_exchange_n(&displayOn, on, __ATOMIC_ACQ_REL);
        if (old != on) IOLog("PongBacklight: display power %s\n", on ? "on" : "off");
    }
    IOReturn newUserClient(task_t owningTask, void *securityID, UInt32 type,
                           OSDictionary *properties, IOUserClient **handler) override {
        if (!handler) return kIOReturnBadArgument;
        *handler = nullptr;
        if (type != 0) return kIOReturnUnsupported;
        IOLog("PongBacklight: newUserClient type=%u\n", type);
        PongBacklightUserClient *client = OSTypeAlloc(PongBacklightUserClient);
        if (!client) return kIOReturnNoMemory;
        if (!client->initWithTask(owningTask, securityID, type, properties) ||
            !client->attach(this) || !client->start(this)) {
            if (client->getProvider() == this) client->detach(this);
            client->release();
            return kIOReturnError;
        }
        *handler = client;
        return kIOReturnSuccess;
    }
    void free() override {
        if (__atomic_load_n(&gPublishedBacklight, __ATOMIC_ACQUIRE) == this)
            __atomic_store_n(&gPublishedBacklight, (PongBacklight *)nullptr, __ATOMIC_RELEASE);
        if (timer) { timer->cancelTimeout(); if (loop) loop->removeEventSource(timer); }
        OSSafeReleaseNULL(timer); OSSafeReleaseNULL(loop);
        IOService::free();
    }
};

// Expand the ordinary metaclass definition so the conformance getter is
// selected in source, rather than replacing a guessed Darwin17 vtable slot.
PongBacklight::MetaClass PongBacklight::gMetaClass;
const OSMetaClass *const PongBacklight::metaClass = &PongBacklight::gMetaClass;
const OSMetaClass *const PongBacklight::superClass = &IOService::gMetaClass;
PongBacklight::PongBacklight(const OSMetaClass *meta) : IOService(meta) {}
PongBacklight::~PongBacklight() {}
const OSMetaClass *PongBacklight::getMetaClass() const {
    return __atomic_load_n(&gPublishedBacklight, __ATOMIC_ACQUIRE) == this ? gAppleBacklightClass : &gMetaClass;
}
OSMetaClassConstructorInit(PongBacklight, IOService, )
OSDefineDefaultStructors(PongBacklight, IOService)

OSDefineMetaClassAndStructors(PongBacklightUserClient, IOUserClient)

bool
PongBacklightUserClient::initWithTask(task_t owningTask, void *securityID,
    UInt32 type, OSDictionary *properties)
{
    if (!IOUserClient::initWithTask(owningTask, securityID, type, properties)) {
        return false;
    }
    return true;
}

void
PongBacklightUserClient::stop(IOService *provider)
{
    IOUserClient::stop(provider);
}

IOReturn PongBacklightUserClient::setProperties(OSObject *properties)
{
    PongBacklight *provider = OSDynamicCast(PongBacklight, getProvider());
    return provider ? provider->setProperties(properties) : kIOReturnNotAttached;
}

IOReturn PongBacklightUserClient::clientClose()
{
    terminate();
    return kIOReturnSuccess;
}

IOReturn PongBacklightUserClient::externalMethod(uint32_t selector,
    IOExternalMethodArguments *arguments, IOExternalMethodDispatch *, OSObject *, void *)
{
    if (selector != 0) return kIOReturnUnsupported;
    if (!arguments || arguments->scalarInputCount || arguments->structureInputSize ||
        arguments->structureInputDescriptor || arguments->scalarOutputCount != 2 ||
        !arguments->scalarOutput) return kIOReturnBadArgument;
    PongBacklight *provider = OSDynamicCast(PongBacklight, getProvider());
    int target = provider ? provider->currentLevel() : -1;
    if (target < 0) return kIOReturnNotReady;
    arguments->scalarOutput[0] = (uint64_t)target;
    arguments->scalarOutput[1] = 2047;
    return kIOReturnSuccess;
}

static PongBacklight *gBacklight;
static bool gDisplayOn = true;

extern "C" void pong_backlight_power(unsigned state)
{
    if (state > 1) return;
    __atomic_store_n(&gDisplayOn, state != 0, __ATOMIC_RELEASE);
    PongBacklight *device = __atomic_load_n(&gBacklight, __ATOMIC_ACQUIRE);
    if (device) device->setDisplayPower(state != 0);
}

/* Called from the PMIC key driver (bsd/dev/pong_buttons.c): no IOKit service is
 * needed, so brightness works while the CoreBrightness handoff is unresolved. */
extern "C" int
pong_backlight_set(int level, int *applied)
{
    PongBacklight *device = __atomic_load_n(&gBacklight, __ATOMIC_ACQUIRE);
    if (!device) {
        return -1;
    }
    return device->setLevel(level, applied);
}

extern "C" int
pong_backlight_get(int *level)
{
    PongBacklight *device = __atomic_load_n(&gBacklight, __ATOMIC_ACQUIRE);
    if (!device || !level) {
        return -1;
    }
    *level = device->currentLevel();
    return *level >= 0 ? 0 : -1;
}

extern "C" int
pong_backlight_step(int delta, int *applied)
{
    PongBacklight *device = __atomic_load_n(&gBacklight, __ATOMIC_ACQUIRE);
    if (!device) {
        return -1;
    }
    return device->stepLevel(delta, applied);
}

extern "C" void pong_backlight_start(IOService *display) {
    static PongBacklight *backlight;
    static bool registered;
    unsigned enabled = 1;
    PE_parse_boot_argn("pong_backlight", &enabled, sizeof(enabled));
    unsigned publish = 0;
    PE_parse_boot_argn("pong_backlight_publish", &publish, sizeof(publish));
    /* Only the real, private PongBacklight service is allowed. Advertising
     * AppleARMBacklight conformance is not ABI-compatible and broke the UI. */
    unsigned where = 2;
    PE_parse_boot_argn("pong_backlight_parent", &where, sizeof(where));
    IOService *host = (where == 2) ? IOService::getServiceRoot() : display;
    if (publish != 0 && publish != 8)
        IOLog("PongBacklight: unsafe publication mask %u ignored\n", publish);
    gPublishAllowed = publish == 8;
    gSpoofClass = gPublishParams = gPublishProps = false;
    IOLog("PongBacklight: start enabled=%u publish=%u parent=%u display=%p host=%p\n",
          enabled, publish, where, display, host);
    if (!enabled || backlight || !host) return;
    if (!registered) {
        void *handle = OSMetaClass::preModLoad(g_kernel_kmod_info.name);
        if (!handle) { IOLog("PongBacklight: class registration unavailable\n"); return; }
        PongBacklightStaticInit();
        if (OSMetaClass::postModLoad(handle)) { IOLog("PongBacklight: class registration failed\n"); return; }
        registered = true;
    }
    PongBacklight *service = OSTypeAlloc(PongBacklight);
    if (!service) return;
    if (!service->init() || !service->attach(host) || !service->prepare()) {
        IOLog("PongBacklight: KTZ8866 unavailable; keep current panel brightness\n");
        if (service->getProvider()) service->detach(host);
        service->release(); return;
    }
    __atomic_store_n(&gBacklight, service, __ATOMIC_RELEASE);
    service->setDisplayPower(__atomic_load_n(&gDisplayOn, __ATOMIC_ACQUIRE));
    backlight = service; // the service and hardware handoff persist until reboot
}
