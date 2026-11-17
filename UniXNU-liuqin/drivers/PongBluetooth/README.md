# PongBluetooth / liuqin

QCA6490/WCN6855 UART20 transport bring-up for Xiaomi Pad6Pro, SM8475/22H374.
This is not an Apple controller/HID success stub. The first image requires
real HCI Reset/version/address responses and explicitly marks pairing/HID SKIP.

Board evidence: local mainline/stock-derived `sm8475-xiaomi-liuqin.dts` UART20
`0x894000`, GPIO76CTS/77RTS/78TX/79RX andBT_EN81; QUP2 wrapper8c0000,
GCC SE5 source2e618 andvote62010 bits9/2/1 from pinnedLinux7.0.14.
Do not use sm8450-hdk's reversedTX/RX overrides. GPIO80/204, SPI touch/pen,
GPU/Metal, audio and device identity are not modified.

FIFO 8N1,115200 with32/16 oversampling selected from actualwrapper version;
resident GENI UART firmware is required. Missing prerequisites SKIP before
dependent work. This trial waits for WiFi67's real sharedrails, does not lower
their votes, and restores UARTclock/pin ownership after bounded transactions.
No Bluetooth firmware is guessed or embedded before actualcontroller identity.

Build integration is optional `PONG_BLUETOOTH=1`, bootargument `bt=1`.
Host protocol tests are not hardware or HID results. Keep all originalinputs
and functioning fallbacks; no imagehash/audit/readback or benchmark required.

services76 corrects the services75 power observer: valid mapped channels are
accepted with EE0 preferred rather than mandatory. Each rail must still have
the actual expected type/subtype, voltage minimum, enabled/status bits, with
bounded complete-sample retry on transient observer refusal. No PMIC voltage
or enable value is written. The75device log showed powerSKIP while WiFi had
verified the same five rails. Owner76 subsequently verified all five mapped
channels on owner4, UART firmware204/wrapper30080000, FIFO16/32 and real HCI
Reset/version/address responses (HCI/LMP11, manufacturer1d, subversion38e6).
This proves the bounded physical controller transport, not pairing/HID. The
UART firmware field is GENI microcode, not the Bluetooth ROM/patch identity.

services77 adds validated modern/legacy QCA EDL identity, real LMP capability
gates, bounded classical Inquiry and a10-second passive LE advertising scan.
Global HCI and LE event masks must succeed before dependent discovery; reset
defaults are not sufficient for LE Meta-Events. A lost scan-enable reply still
gets a bounded disable attempt before GPIO/clock restoration. Status, inquiry
completion and scan-stop results remain distinct from device report counts.
Only peripheral keyboard-class/HID UUID1812 report counts are logged, not
names/addresses. Counts are not unique-device identities or pairing evidence;
a zero HID UUID count does not exclude a keyboard whose advertisement omits it.
No unknown device is automatically connected or unqualified firmware loaded.

Host H4/discovery and actual recording-transport tests do not open hardware.
The pre-mask77 body has a reproduced negative mask-aware host fixture; the
corrected20-case transport tests retain feature/mask rejection, enable timeout,
stop failure, rail/UART error and HID SKIP behavior. Settings visibility and
real keyboard input still require persistent native controller/pairing/HID
integration, not forced UI capability flags.

Owner77 reported shared rails PASS but clock FAIL before UART/HCI/discovery;
Settings still had no Bluetooth entry. No failed clock register was captured,
so the exact hardware failure branch is not localized. Independent76 real
HCI success remains valid. No77 discovery result can be inferred from that log.

services78 fixes a reproduced branch-contract defect: real AHB votes plus
HWCG bit1 allow idle HALT, matching pinned clk-branch.c and existing native56
input checks. SE5 has no HWCG exemption and still requires actual branch2 ON.
Read-only parent decoding/lock wait and per-phase GCC snapshots separate
PLL, DFS, UPDATE, MND, votes, branches and mapping failures. Unknown fixed
600/300MHz parent still fails; shared GPLL0 rate/divider/votes are never written.
Only this UART's clock vote/source fields are changed/restored, with bounded
restore acknowledgements rather than an unconditional success claim.
36 actual recording-transport cases and the isolated77 HWCG negative are host
tests, not proof that78 hardware discovery/pairing or Settings works.

Owner78 verified actual clock/UART/HCI and RF scanning. Classic Inquiry
completed with zero reports; the LE scan produced1162 repeated reports with
102 HID-service reports and no malformed records. These are not102 keyboards
or target-device/pairing/HID proof. Clock restoration also acknowledged; QCA
EDL identity remained SKIP because no valid parsed version reply was captured.

services79 adds a real controller-backed UART.SK handshake path for the
original22H374 daemon. Same-build native strings confirm the control name and
channel specification/UUID operations. The pinned QcaBluetooth component
supplies only the hardware-independent APSL pipe; SM8550 DMA/firmware and27
binary adapters are not imported. Signed/entitled caller and PID binding,
real registration/address/node gates, H4 ACL/SCO/ISO framing and backpressure
retain errors instead of fabricating successful controller replies. Unknown
vendor firmware/NVM requests are refused before TX. The native packet queue
covers the actual FIFO burst, and no input payload/address/name is logged.
The owner remains functional while connected, restores this UART on close or
idle, and reports native ring traffic separately from pairing/HID completion.
OriginalBlueTool/bluetoothd/root43 stay unchanged; their bootstrap/controller
selection may still require a matching18 adapter. Menu visibility is not proof
of pairing, and no Apple controller class/capability flag is spoofed.

## services80 recovery and owner evidence

79 is withdrawn: its `OSString` DT `device_type` caused native byte-reader
exceptions and UI process restart loops, before any native pipe traffic.
80 uses the qualified bounded transport with native publication disabled,
fixes every DT value to byte data and rejects the non-unique controller address.
The modern QCA `0/19/0c` envelope contains a12-byte version payload length.

The owner's80 log shows one SpringBoard/backboardd lifetime of about623seconds
until normal shutdown, with no79 type exception/fatal UI restart loop. It also
reports product`0x13`, SoC`0x400c0210`, ROM`0x0201`, patch`0x38e6`, and881 repeated
LE reports including5 HID-service reports. These are owner log observations,
not a host boot, visual desktop confirmation, unique keyboards or pairing/HID.
80 remains the recovery fallback.

## services81 matched22H374 userspace candidate

`PONG_BLUETOOTH_NATIVE=1` and `PONG_BLUETOOTH_USERSPACE=1` add the matching
runtime supplied through `PONG_BLUETOOTH_RUNTIME`, with a private owner input
selected by `PONG_BLUETOOTH_ADDRESS`. Do not commit binaries or address values.
The installed root43, Data seed, GPU65/Metal63 and native56 SPI touch/pen stay.

- `userland/bluetooth_bootstrap18.c` serves the actual `com.apple.BlueTool`
  request keys/scripts. Boot/init use real owner-worker power, boot-state and
  firmware results. Deepsleep requests a real owner-transport stop and checks
  its stopped state; shared WLAN rails are not removed. Denied, pending,
  failed, malformed and unsupported requests remain errors.
- `scripts/bluetooth_native18.py` guards the original ordinary-arm64 UUID,
  instructions and executable padding. It adapts the physical-controller
  selector, type0 factory/registration/vendor storage and generic callbacks,
  without importing27 offsets, forging capabilities or bypassing key errors.
  This build's explicit local-address/key-state diagnostic emissions are
  suppressed; the unexpected-address fault keeps a value-free message.
- The kernel exec hook installs only the embedded, signed versioned executables
  into `/private/var/tmp/pong-bt81-r2` before PID1 runs original launchd. Original
  OS files/jobs and daemon entitlements are preserved. Root-owned0711 permits
  the original mobile jobs to traverse the directory, not list/write it;
  binaries are0555. Wrong ownership/modes/types/links/bytes, raced or partial
  files and I/O failures keep the original executable paths and block readiness.
- Native callers need verified executable path/bytes, original process name
  and control entitlement, not merely the development platform-binary flag.
  Publication waits for installed runtime, actual QCA identity and a real
  owner-address0xfc14 write/0x1009 readback match. The address is never printed.

Host checks cover324 native/ASLR instruction cases at four slides, six negative
guards,36 recording-VFS cases,20 real-XPC/recording-sysctl cases,16 private
address/recording-HCI cases and the existing protocol/Skywalk/DT checks. Darwin24
target compilation uses the selected source's actual private VFS declarations,
not invented structures or APIs. None proves installed runtime or HID input.

First81 kernel candidate was withheld for native address logging; the privacy
revision uses a separate Data directory and a fresh kernel-r2 build. Pairing,
real HID typing, native optional commands/capability publication/key storage,
physical low-power/sleep-wake and Wi-Fi association/DHCP/web stability still
need device evidence. Do not turn a visible page or ring/advertising counts
into a keyboard/input success claim.

## services82: registered bootstrap interfaces

Owner81 reports a continuously searching Bluetooth UI with no devices. Its
log confirms runtime installation, actual QCA identity, owner-address write/
readback and node publication, but native power/boot/control/channel and HCI
traffic remain zero until idle-stop. The daemon later hits its memory limit.
This is not a zero-device RF scan or HID result.

The actual Darwin24 exported `SYSCTL_OID` macro defines storage only;81 never
registered its four `kern.qbt_*` records. Direct-handler host checks missed
that interface defect.82 explicitly calls the genuine `sysctl_register_oid`
for boot, firmware, power-on and power-off after valid worker binding, outside
the transport lock, once. The revised host check uses name lookup: actual81
source reproduces ENOENT,82 routes names to real handlers/results. The81 log
does not directly record that errno, so do not overstate device localization.

82 installs separate `/private/var/tmp/pong-bt82` files without deleting81 or
resetting Data. Authentication compares the actual executable vnode with the
canonical installed path and verifies all bytes/metadata; a legitimate display
path alias is accepted, a copied image is not. Name/control-entitlement checks
remain mandatory. Limited fixed-label exec/auth/BlueTool traces and a same-size
bluetoothd/BlueTool log filter distinguish further activation/permission/pipe
failures. No memory-limit override or fabricated initialization result is used.

Target compilation, name-based MIB/Skywalk regression,38 VFS cases and existing
bootstrap/address/routing checks pass on the host. Actual82 native scanning,
pairing/HID and Wi-Fi still need owner evidence;80 remains the recovery fallback.

## services83: preserve initialization errors, repair cold-session residue

Owner82 verifies actual BlueTool boot/init, installed-vnode authentication,
UART.SK channel/termios and25 real command/response ring packets per attempt.
The daemon then returns1 and restarts, explaining grey/searching transitions.
The exact failing native step/status was not exposed by82's text log; do not
claim that a memory limit or a particular optional command caused that exit.

Retry logs also show Reset timeout and old parser errors. `Parser::reset()`
correctly retains oversized-frame discard inside a live stream, but the old
owner also retained it across physical controller reset.83 adds a distinct
`beginSession`, drains only UART20's old RX while BT_EN81 is verified low, and
clears old native queues/partial frames before assertion. Cumulative failures,
normal oversize discard, shared rails/PLL/SPI and genuine HCI status remain.
The actual82-source stale-session negative and repaired repeated-controller
recording cases pass separately from device retry observations.

The ordinary-arm64 matched18 patch adds result-preserving diagnostics for a
nonzero init-chain status/internal step and the common main return1. Darwin
stack variadic arguments, original registers/SP/LR/prologue/status and return1
are tested at four ASLR slides (356cases/seven guards). No failure is rewritten
to success. The owner emits at most96 HCI command/status metadata records, no
payload/address/key data.83 uses separate `pong-bt83` storage, retains all prior
Data/runtime files and80 fallback. Owner83 subsequently localized the first
failure below. Later Reset timeouts still occurred, so the cold-session repair
has not been established as sufficient on the device.

## services84: explicitly unsupported optional0c5b

Owner83 records real `0x0c5b` (Write Default Erroneous Data Reporting) returning
HCI Unknown Command(1), followed by `initCmdWriteErroneousDataReportingCB`
status701 and daemon exit1. Host Buffer Size/Flow Control`0x0c33/0x0c31` succeed;
the native stack already continues after`0x0c6d` status17. Those paths need no
failure override. Pinned Linux gates erroneous-data reporting on supported
command byte18/bit3 and LMP feature support; it is an optional SCO/speech feature.

84 adapts only this exact22H374 optional callback's701 result. It logs SKIP and
advances initialization without enabling the unsupported feature or changing
the actual HCI response. Success and every other error use the original native
body; Unknown Command on mandatory callbacks still fails. No command-support
or capability bits, keychain results, firmware or global error handling change.

The actual signed83 callback's fatal701 path is reproduced at four ASLR slides.
84 passes392 instruction cases/eight guards, including whole-callback native
advance/failure, register/stack preservation and mandatory-error negatives.
Target compilation and fresh kernel/shim/source-link receipts pass. The runtime
uses separate `/private/var/tmp/pong-bt84` files, retaining old Data and80 fallback.
Actual84 initialization beyond this step, scanning, pairing/HID, repeated reset
and Wi-Fi connectivity remain device tests; host checks are not keyboard proof.

84-r2 additionally addresses the second independent83 observation: the first
Reset after each powered cycle occasionally saw one invalid byte (`bad=1`) and
timed out, while a retry succeeded. After BT_EN81 is verified high, the owner
now observes at most200ms for two consecutive quiet UART milliseconds, discards
only a bounded, counted number of pre-command bytes (max4096), then sends the
real Reset. It never parses those bytes as a packet or fabricates a response.
Over-depth/abnormally active RX fails before command TX. The actual83 source
cannot complete a new recorded Reset behind such a post-assert stale partial
event;84-r2's recording test drains4bytes, requires the genuine Reset/version/
address, and additionally covers the over-depth failure and a clean cycle.
84 (non-r2) is retained only for comparison and is superseded by84-r2.

Owner84-r2 confirms the optional0c5b SKIP is effective: real initialization then
advances through more than20 further commands (including Host Buffer Size/Flow
Control, LE parameters and extended-scan setup). It next stops at legacy`0x2007`
LE Read Advertising Channel TX Power, which the controller rejects with Command
Disallowed(12); the daemon exits1. That is exactly the legacy/extended
advertising conflict described above, so85 takes the build's own existing
"Skip Reading Legacy controller LE Advertising TX Power, will wait" branch and
does not issue the read. No TX power value or capability is fabricated; a later
real extended-advertising/vendor value may still be used if the native stack
obtains one.

HID itself does not require a new kernel driver: the stock 22H374 daemon creates
`IOHIDUserDevice`/`KeyboardInputDevice` after a real pairing, and its original
HID manager/user-client entitlements are preserved. The remaining work is
initialization completion, discovery, actual pairing and report delivery, not a
custom HID transport.85 therefore targets the current initializer blocker before
HID validation; pairing/typing still needs device evidence.

Owner85 verifies the legacy0x2007 skip and reaches LE scanning; the next blocker
is legacy`0x200b` LE Set Scan Parameters returning Command Disallowed(12).86
keeps the prior fixes and raises bounded HCI opcode/length/status logging to512
entries so the full advertising/scan sequence and the0x200b context are visible;
no payload/address/key bytes are printed and nothing is fabricated.

## Services87 evidence and88 standard scan selection

Owner87 reports a successful full `0x1002` response with supported-command
byte37=`ff`, including both standard extended scan commands. Nevertheless the
daemon sends legacy `0x200b`, receives Command Disallowed(12), and exits1.
Repeated first-Reset timeouts remain a separate, unresolved observation.

87's scan-parameter diagnostic was off by one: `Packet::data` excludes the H4
type, so parameters begin at3, not4. Its printed type=`e0` is not the scan type;
the final filter=`d` reads a stale byte outside the valid frame. The actual scan
type was not printed.88 fixes the offsets and requires the exact declared
command shape, and a full successful64-byte command table for the capability
diagnostic. A recording test executes the actual owner logger and reproduces
87's defect. Do not infer malformed controller parameters from that old line.
Also, `0x204a` is Read Periodic Advertiser List Size, not Set Privacy Mode.

The exact22H374 native descriptors `0xd7/0xd8` map to `0x2041/0x2042`, with
byte37 masks `0x20/0x40`. In this build the direct standard-command selector is
`ExtendedScanning` (upper cache `0x1009db058`, lower `0x1009d8a06`), **not** the
distinct `ExtendedScanHCI` multicore backend (`0x1009d8a05`). The naming/layout
must not be transplanted from27. The model's0x848 getter rejects type0 by its
product table even though the real controller reports these commands.

88 publishes only this direct standard selector at the native query callback
boundary `0x1001c6b48`: status must be zero, the native parser must have validated
and copied all64 bytes, and both actual command bits must be present. Failed or
short queries clear the two caches without consulting stale bitmap data. The
type0 model getter uses that real-result cache; nonzero product paths, the actual
bitmap/replies/statuses and the unsupported multicore backend remain unchanged.
No HCI command or completion is translated or manufactured. Existing native
scan parameter/disable/enable paths select their original standard descriptors.

Host checks pass400 routing/privacy/optional-command cases,14 target guards,
1424 additional signed-native scan cases and four ASLR slides, plus14 actual
logger cases, signed87/logger baselines, VFS/bootstrap/address/pipe/owner tests
and Darwin24 target compilation. These record parser and dispatcher boundaries,
not controller TX, scanning or pairing success.

The fresh88 kernel/shim is published as
`shim-boot-liuqin-bluetooth88-userspace18-gpu65-va36-native56-quiet.img`, with a
new `/private/var/tmp/pong-bt88` runtime directory. Root43, Data, prior runtime
files, GPU65/Metal63, native56 and80 fallback are retained; instructions are in
chat only. Actual scanning, genuine pairing, `IOHIDUserDevice` creation and
physical typing remain owner tests before Wi-Fi password/association/DHCP/web.

## Owner88 result and89 scan lifecycle

Owner88 confirms the matched runtime installs/authenticates and the corrected
scan log reports passive0, interval`01e0`, window`0030`, random address type1,
filter0. However, no standard`0x2041/0x2042` TX is observed: legacy`0x200b`
still returns12 and the daemon exits1.88 therefore did **not** demonstrate an
effective extended scan selection, scanning, pairing or HID. First-Reset
timeouts remain separate. The exact internal cache/store timing was not logged.

The signed88 host regression now composes the getter, genuine-format complete
query parser and original core-stack setter: a config snapshot taken before the
query can overwrite the query-published lower selector with0. Conversely, an
old true snapshot can re-enable it after a short/failed query.88's independent
parser/cache/dispatcher tests did not cover this ordering; they were not a
device-success gate.

89 makes the type0 lower selector query-owned: the existing core-config setter
at`0x1002585fc` cannot overwrite it, while nonzero products retain the original
setter. At the native HCI initialization/table-reset boundary`0x1001c22c0`, it
clears old type0 scan results. Only the existing successful complete current
query can subsequently select standard scanning. Actual bitmap, command/status
callbacks, parameters and multicore backend stay unchanged; no unconditional
support, HCI reply or keychain success is introduced.

480 additional four-ASLR cases execute initialization/query/getter/setter/scan
dispatcher and original descriptor lookup in one recording machine, including
before/after snapshots, short/error/absent queries, missing bits, passive owner88
parameter shape and nonzero products. Signed88 ordering failures are reproduced.
The prior400+1424 cases,16 target guards and existing host/target tests pass too.
89 uses a new `/private/var/tmp/pong-bt89` runtime; root43/Data/GPU/input/80 are
retained. Exact device store timing and actual89 scanning/pairing/HID still
require owner evidence.

## Owner89 and90 execution-point evidence

Owner89 still records legacy`0x200b`/status12/daemon exit1 and no standard scan
TX. The owner reports intermittent peer discovery as a computer, with connection
attempts failing. That feedback is not pairing/HID proof; fresh discovery, cached
classification and connection events are not individually established by the
command log. The repeated startup/exit is consistent with a short visibility
window, but classification alone does not establish the failure cause.

Native initial-table descriptor/decoder execution confirms`0x90/0x1002` selects
`0x1001c6acc`, and selected compiler-object instructions match89's signed input.
Neither establishes the actual device callback/type/selector values.90 retains
89's policy instead of guessing another fix, and records bounded actual state:

| Phase | Observation | `arg` |
| --- | --- | --- |
|0 | Before native initialization clears old scan results | unused0 |
|1 | After supported-command result handling | native status |
|2 | Before the core-config setter/type gate | supplied snapshot |
|3 | Before the model getter/type gate | model product type |

Lines include actual registered chipset, complete-result flag, native command
byte37 and upper/lower selector bytes. Only the query phase's complete flag
means a validated64-byte result pointer exists; other phases may show old table
contents, not support/success. Integer registers0..18, LR and NZCV are preserved.
Two lines per phase/process and four exclusive-store retries are enforced. The
16-byte counters use guarded loader zero-fill tail padding after every `__DATA`
section, not an invented native field or writable-code patch.

PID1 prints explicit runtime version `pong-bt90`. Kernel metadata logs exact-shape
ClassOfDevice/ScanEnable scalars without changing identity or exposing names,
addresses or keys.90 passes400+1424+480+112+20 native cases/18 guards/four ASLR,
19 actual logger cases and affected host/target/source-link checks, not scanning
or HID tests. Published90 uses a new `pong-bt90` runtime and
`shim-boot-liuqin-bluetooth90-userspace18-gpu65-va36-native56-quiet.img`.
Root43, Data, GPU65/Metal63/native56, power/quiet/OOBE and80 remain. This is an
execution-evidence candidate, not a new functional scan/identity/audio fix.

## Owner90 localization and91 stable-model routing

Actual90 confirms `pong-bt90` is installed. Before initialization the registration
is0; at the complete successful`0x1002` callback it is`ffff`, with command37=`ff`
but upper/lower selection both0. The registration-based type0 guard therefore
skips a real supported-command result. This explains the ineffective adapter
selection, not a missing query or wrong initial decoder. The exact writer/origin
of the registration transition has not been established and is not patched.
The logged ClassOfDevice major1/minor4 confirms computer/handheld classification;
no identity change or connection/HID success follows from that classification.

91 gates query/config/init adaptation by the same native stable model identity
used by the original singleton/getter: pointer `0x1009dae90`, product offset
`0x2e0`. Both native getter instructions and the product load are guarded.
A missing model or nonzero product does not receive type0 adaptation. The
registration, including`ffff`, is untouched. For model0 only a successful full
current64-byte query with both real command bits publishes standard scanning;
short/error/missing-bit results cannot fabricate support. Existing query-owned
setter/session clearing, actual HCI replies/statuses/bitmap and vendor backend
policies remain. No registration is rewritten to make a failure look successful.

1012 additional four-ASLR cases separate stable product from volatile
registration, replay the actual90 transition, and compose init/query/setter/
standard-or-legacy dispatcher boundaries. They include null/foreign models,
short/error/missing-bit results and the signed90 gate failure. The transition is
replayed as owner-observed input, not claimed to execute a proven native reset
writer. Existing2436 cases,20 target guards and actual host/target gates pass.
The fresh91 runtime uses `pong-bt91`; root43/Data/GPU/input/80 are retained.
Real91 standard scanning, keyboard pairing and physical HID input still await
owner evidence before Wi-Fi password/association/DHCP/web testing.

## Owner91 SIGSEGV and92 complete scan encoding

The latest91 log confirms the stable-model gate works: registration remains
`ffff`, while the complete successful query publishes upper/lower1/1. The daemon
then repeatedly dies with SIGSEGV, and launchd delays its respawn. The owner says
one earlier search worked but has no log from it; that stays owner observation,
not evidence that the current log scanned or paired successfully. No fault PC is
present and the owner cannot currently export the crash report.

Complete signed91 native dispatcher/encoder execution exposes a separate real
packet bug omitted by prior dispatcher-boundary tests: `0xd7/0x2041` declares9
parameter bytes for an actual8-byte1M scan payload. The original local queue
stores a12-byte H4 frame, but its declared shape requires13; the actual H4 parser
cannot complete it. The two-PHY shape has the same extra byte.92 changes only the
encoder's fixed length from4 to3, yielding `3+5*popcount(scanning_phys)` with the
original parameter/callback/queue/status behavior. It does not add a fake byte,
translate a reply or declare the device SIGSEGV cause proven.

28 new four-ASLR tests execute the full native generic dispatcher, variable
encoder, allocation failure and original queue store. Actual emitted frames then
run through the real H4 parser/native TX gate; signed91 mismatches are reproduced.
Existing3448 native cases/21 guards and affected Darwin24/source-link checks pass,
but neither confirms that SIGSEGV, stable opening or physical HID is repaired.

92 uses new `pong-bt92` Data files, retains91's model gate and root43/Data/GPU/
input/80, and replaces BlueTool with ReportCrash in the user-log filter to try
to capture crash context. Kernel bootstrap traces remain; args are1015 bytes
includingNUL, with no ABI change. The owner conditionally permits Data clearing
if necessary, but none was performed: current evidence points to code/encoding,
not corrupted saved data. Real opening/scanning/pairing/HID remain owner tests.

## Owner92 and93 standard scan submit operations

Actual92 keeps the stable-model gate and the corrected packet length, yet still
never emits`0x2041`: after`0x2005` Set Random Address the daemon sends`0x0c03`
Reset, disconnects, and SIGSEGVs about3.7s after spawn. ReportCrash starts, but
this log has no fault PC and the crash report cannot currently be exported. The
owner's single earlier successful search has no log and remains observation only.

The type0 registration path explains the missing command. To keep the vendor
backend out of type0 it zeroes the whole0x948-byte operation table at
`0x1009cde70`. That table also holds the *standard* scan submit callbacks used by
extended scanning at`+0x100`/`+0x108`; while they are zero the extended-scan
wrapper returns E11 and nothing reaches HCI. Signed92 host execution reproduces
that E11/no-command behavior, consistent with the device trace. This is not
claimed to be the unique SIGSEGV cause, and no crash PC was recovered.

93 still clears every vendor slot, then restores only those two callbacks from
the build's own static table (`0x1009632a0+0x100/+0x108`, functions
`0x10006df9c`/`0x1000854c4`). No vendor firmware path, capability bit, HCI
reply or identity value is enabled or invented. The real-query gate, corrected
packet length and actual status/bitmap/keys remain unchanged.

36 new four-ASLR cases execute the patched registration site (zero product
clears+restores; nonzero products are untouched), assert every vendor slot stays
clear, then run the real extended-scan submit into the original queue and require
a complete`0x2041` frame (8-byte1M,13-byte2PHY). Signed92's cleared-table
E11/no-command result is retained as a negative. Prior3476 cases/22 target guards
and affected Darwin24/source-link gates pass. These are not controller, opening
or HID results;93 waits for the owner's next log.

## Owner93 result and94 fault-PC diagnostic

Actual93 installs and keeps the query-published selectors`1/1`, yet still sends
no`0x2041`/`0x2042`, and bluetoothd continues to SIGSEGV roughly every3.2s. The
log has no fault PC and the crash report is still not exportable. Restoring the
two standard submit callbacks was therefore necessary-looking but not sufficient:
the extended path is still not reached or still fails before the transport.

94 adds no scan-command behavior. It registers `pong_bt_user_fault`, called from
the XNU user-abort path, which logs `pc/far/esr/code/type` only while the installed
`bluetoothd` fault is being delivered and only for eight occurrences. Signal
delivery, the exception itself and all process behavior are unchanged; the log
contains no address, name or key data. This is intended to identify the exact
crashing instruction so the next repair is evidence-based instead of inferred.

The previous3476 native cases/22 guards, data-only gate reports and selected
Darwin24 compilation remain valid; the new kernel patch is additionally compiled
by the target preflight. Real opening, scanning, pairing and HID are still
unverified and Data remains uncleared.

## Owner94 fault site and95 caller evidence

Owner94 confirms `pong-bt94` and prints, every launch,
`bluetoothd user fault pc=0 lr=... far=0 esr=82000005 code=5 type=5`.
ESR`0x82000005` is an instruction abort from EL0 with a level-1 translation
fault at address0, and `fault_type` includes execute: the daemon branches to a
**null function pointer**. Signal delivery is unchanged; the bounded logger
itself only reports the fault site.

This explains why restoring only the two scan submit callbacks did not help: the
crash is a null call on the scan path, not a length or selector problem.95 keeps
behavior identical and adds the caller LR to the same bounded record so the exact
native call site can be identified before changing any op table again. No op,
capability, reply or identity is fabricated, and Data stays uncleared.

## Owner95 fault LR and96 optional-report skip

Owner95 reports, on every launch, `pc=0 lr=100xxxx7b8 far=0 esr=82000005`.
All eight records share the same low twelve bits, so ASLR aside the caller is a
single site: `0x1003127b8`, the return address of an unguarded virtual call
`[singleton 0x1009daee0]->vtable[0x78]()` (the other branch uses slot0x80).
The singleton exists but that optional reporting hook is null in type0, so the
daemon branches to zero. This is why scan never reached HCI and why restoring
scan submit ops did not help.

96 takes that build's own fall-through path at the two optional report calls,
skipping only an absent hook; any present hook still runs. Nothing is fabricated:
no HCI command/reply, status, capability, identity or scan state is invented, and
the corrected packet length and model/query gating stay. The bound is in-place
(the trampoline padding is full), so the change replaces exactly those two
`blr x8` instructions with a branch to their original continuation.

Host checks remain400 routing/privacy cases with24 site guards, the3476 native
scan/encode/model/lifecycle/trace cases, and target compilation. This is still
not a device scan, pairing or HID result;96 awaits the owner's next log. Data is
uncleared and root43/GPU/input/80 are preserved.

## Owner102 rejected legacy initiating command

The restored root43 UI image (queue48) plus boot97 gives a stable daemon: zero
bluetoothd faults and zero SpringBoard faults, and LE scanning keeps working
(112 `0x2041` and 222 `0x2042` commands, all status0). The keyboard still does
not connect, and the log now shows why: the daemon issues legacy
`0x200d LE Create Connection` twenty-nine times and every attempt is answered
with status12 (`Invalid Parameters`). Extended `0x2043` is never used, and no
classic `0x0405` is issued.

102 is a boot-only diagnostic. The fixed 512-packet trace budget had been
exhausted long before the connect phase, so the connect and capability packets
are now exempt from that budget. New bounded records carry no address, name or
key bytes:

- `LE CreateConn scan=…/… filter=… peerType=… ownType=… interval=…-… latency=… timeout=… ce=…-…`
  for a 25-byte `0x200d`, read from `data[3..]` before any rewriting;
- the same scalars for `0x2043` if it is ever used;
- `LE features`, `LE supported states` and white-list size from their real
  command-complete events;
- `LE meta subevent=… status=…` for connection complete/update/remote-feature
  subevents only (advertising reports are not traced).

The image also contains an opt-in `pong_bt_init=1` normalizer that rewrites only
parameter values the Core specification forbids in legacy `0x200d` (out-of-range
scan interval/window, identity address types2/3, out-of-range interval, latency
or supervision timeout). It is **disabled** in this image, so the transmitted
bytes are unchanged; the log first records the daemon's original values, and an
enabled run would add one `normalized …` line with the replacement scalars.

Runtime stays `pong-bt97`, root stays queue48, and no OS/Data/GPU/input change is
made. This is not a pairing or HID result.
