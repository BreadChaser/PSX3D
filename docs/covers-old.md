# Old 3DS library covers

Worktree `covers-old`, branch `t3/covers-old`, base main `9417930`.
Implementation commits `5a9c580`, `ca90e78` (16-bit compatibility), and
`fdc670f` (release Old decoded pixels before game allocations). No push/publication.

## Root causes and reproduction

RC2's remote mode suppresses `covers_get` as well as the workers, so even valid
cached box art appears as placeholder letters. `ui library` is the only opt-in;
load/batch/update remove it again. A locked RC2 Old session reproduced the
placeholder screen, then showed real box art after `ui library`. The screenshot
sequence is in ignored `devtools/covers-old/rc2-{before,optin-0,optin-1,optin-2}.png`.
RC2 has no cover telemetry, so that sequence alone cannot distinguish a cache
hit from a new HTTPS download.

The previous Old-only priority starvation is already fixed in main (core-0
workers share UI priority). This branch retains it. Two additional reliability
problems were found in code: transient download failures became terminal for
that in-memory slot, and the 15-second network backoff slept inside an active
cover load without checking pause. A stop waited only three seconds before
letting gameplay/update proceed anyway. Worker claims also raced the stop's
busy check. These are fixed and covered by host lifecycle tests.

The original PNG path also has avoidable memory pressure: a typical 512x512
cover requests 1,048,576 contiguous RGBA bytes, in addition to curl's 512 KiB
compressed buffer and its 64 KiB output. RC2's reported free heap dropped from
1,892,880 bytes before opt-in to 519,112 bytes afterward. **That status is
`mallinfo().fordblks`, not a full allocatable-capacity measurement**: it can
omit heap capacity not yet assigned to malloc. It therefore does not prove an
allocation failed on the console. The smaller decoder's allocation requirement
and output are verified by host tests; historical Old OOM/TLS failure remains
unproven. The remote gate is the reproduced root cause.

## Change

- Download/display covers automatically in an idle library, including remote
  mode. `nocovers` and `nonet` retain their existing meanings.
- Load/batch/update pause workers and close curl connections. Library entry
  resumes automatically; pending loads, batches, updates, gameplay and APT
  suspension prevent resume. APT preserves the prior gameplay pause, including
  overlapping sleep/suspend notifications.
- Poll SOC with timeout **zero** and sleep 10 ms outside the socket service.
  Keep TLS certificate validation and session/DNS reuse. Curl's progress
  callback also checks cancellation. No HOME/input test is performed.
- Count a worker busy before releasing the slot lock, and wait for actual
  quiescence before gameplay/update. Network backoff now schedules a retry
  instead of occupying a worker. Transient failures retry after 15 seconds;
  HTTP 404 remains a missing image. Allow 15 seconds for connection/TLS setup.
- Stage compressed PNGs in the cover directory's `download.tmp`, then decode
  one scanline at a time. A 512-wide image needs a 2 KiB RGBA scanline plus the
  64 KiB output, rather than a full RGBA image. Existing cache files remain
  compatible; interlaced and 16-bit user PNGs retain the original full-image colour
  conversion. Old decoded pixels are released at game entry; the GPU-ready
  SD cache reloads on return, so library covers do not eat its gameplay heap.
- Developer-only `covers` reports transfers, cache counts and stop latency.
  `covers refresh=<exact_title_or_index>` forces HTTPS without deleting the SD
  cache first. There is no product setting or legacy gate for this net benefit.

## Validation

3DSX/CIA builds pass (title counter **3076**, remote identity
`covers-old-4-split`). Developer control/transport/update ASan/UBSan suites and
frontend host suite pass. The cover lifecycle suite checks remote enablement,
Old priority fallback, immediate backoff, transient/404 distinction, cancellation,
zero-timeout curl polling and nested APT pause semantics over a real local HTTP
server. This is a host HTTP transport test, not a console TLS result.

PNG ASan/UBSan tests compare every output pixel with the original simplified
libpng decoder: generated dimensions 1x1, 37x91, 128x128, 500x500, 512x512 and
535x512; grayscale/RGB/palette/gray-alpha/RGBA, palette transparency, Adam7,
gamma-tagged images, 16-bit colour conversion, truncation, malformed and
missing files. The six actual
benchmark box-art PNGs also match exactly. Decoder tests reject every owned
allocation above 128 KiB, including a deliberately failed old-size 1 MiB
allocation, while the new decoder still succeeds. Logs/fixtures are in
`devtools/covers-old/`. No core or game-rendering code changes, so gameplay draw
hashes are not the relevant image oracle for this library-only fix.

New 3DS: one earlier candidate (`5a9c580`, `covers-old-2-split`,
`artifacts-final/`, SHA256 `099a8886…`) self-update attempt at about 03:42:53 UTC received the
host's CIA HTTP request but its control response timed out after 60 seconds.
One follow-up status request also timed out after 60 seconds. Per the common
policy, `fastloop/NEW_UNAVAILABLE` was written at 03:44:53 UTC, its lock released,
and all further New contact stopped. No retry, HOME, reboot, FTP or input was
attempted. The final installed state is **unverified**; RC2 restoration and the
New regression check are pending recovery. The last verified pre-attempt state
was RC2/library, same session `63b15a730`; evidence in `new-1/` and `new-1.log`.
Do not interpret this report's host checks as hardware FPS acceptance.

## Old candidate hardware status: pending

After the successful RC2 reproduction, other workers occupied the Old. This
worker acquired the lock once during another active RC2 batch, observed
`mode=playing` / `bench.active=true`, released it without mutation, and waited
about five minutes before retrying. At 03:59:52 UTC the maintainer created
`fastloop/OLD_UNAVAILABLE` after a user-confirmed crash during the **gpu-stereo**
candidate batch. The queued cover driver acquired the released lock, saw the
marker and exited **before any console contact**. This worker never installed
its candidate on the Old; it did not cause or investigate that crash.

Consequently fresh HTTPS success for all six Old games, real console stop
latency, remote load/batch during downloads, and the final candidate screenshots
are **not verified**. Old's current installed build/RC2 restoration belongs to
the interrupted stereo session and is unverified here. Do not contact it until
the maintainer clears the marker. New restoration remains unverified following
this test's one failed install; the maintainer may subsequently recover it.
No claim that either console was restored by this test is made.

`devtools/covers-old/PENDING-HARDWARE.md`, `hardware.py` and `perf-six.txt` contain
the remaining exact RC2 → candidate → RC2 plan and package. Its future session
must start with a fresh locked RC2/library/idle handoff, not assume recovery.
The gallery contains the real RC2 before/opt-in evidence only; no candidate
images are fabricated. This branch is hardware-ready, **not hardware-accepted**.

## Artifacts and remaining risks

Immutable package: `devtools/covers-old/artifacts-final3/PSX3D.{cia,3dsx,elf}`;
CIA SHA256 `3c4c6a38061d034b901d650cddf0a6d6069f28ee5f22b5b307b575a75a68871e`.
Build logs, exact driver and screenshots are under `devtools/covers-old/`;
gallery `devtools/covers-old/index.html`.

Synchronous DNS, a single CPU-heavy TLS step, SD operations and PNG decode can
outlast a 10 ms network-loop check. Quiescence is mandatory; there is no unsafe
three-second fall-through. Hardware stop-latency measurements must establish
whether the requested roughly 50 ms target is met in the tested phase. The
interlaced/16-bit user-PNG fallback still needs a full-image allocation and can fail
on low heap. Cache write failure can leave an invalid `.bin` (detected and
redownloaded), as before. The final ELF has text +5,712 bytes and BSS +320 bytes versus RC2.
Broad Old performance/layout acceptance is separate
from proving that downloads cease during gameplay.

Restoring exact RC2 also restores its remote-mode cover gate: the Old will show
placeholders again while remote is enabled until this fix ships, even though
its cached images remain on SD. This is expected.
