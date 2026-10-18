# Old 3DS full-speed experiments

The target is 100% emulated game speed with continuous audio, then the largest
stable, evenly spaced picture rate that fits. These experiments are local and
opt-in; they are not release defaults. Full speed has not been achieved in all
six benchmark scenes.

## Measurement conditions

Old 3DS, GPU renderer, protected slot2 states, stock PS1 clock and 30% syscore
allowance. Both consoles were rebooted without BootNTR on 2026-10-18. Passive
UDP silence was verified before the fresh runs; merely pausing the viewer was
not sufficient. The tables below identify their condition. Earlier measurements followed the
NTR_OFF_VERIFIED marker. At approximately09:11UTC the user started BootNTR on
Old; that marker is invalid for subsequent Old runs. A fresh passive receiver
replaced the ended release observer. Later A/B uses fixed quality30/128KiB/s
streaming and records the condition on each TSV row. Each batch holds the shared console lock, checks its build identity and
updates if needed. Screenshots are downloaded after sampling.

Game speed counts emulated vblanks, including all four calls in ordinary FF.
FF uses fixed skip3 and mutes frontend audio; it diagnoses headroom but does not
validate normal audio. PMU brackets every emulation call and counts main-thread
CPU cycles. GPU, worker and frontend times overlap or have different brackets;
do not add nested phase times. A draw bucket means the renderer emitted work,
not necessarily that a new visible picture was admitted: GT2 retains required
offscreen work during skipped pictures.

Distinct picture decision rates are normalized from admitted GP1 flips and the
measured vblank populations. They are not raw NULL callbacks, LCD compositions,
or texture draw batches. For 30Hz games a 1:1 drawn/skipped *picture* cadence is
15 pictures/s at full speed; 30 pictures/s requires admitting every picture.

## Measured frontend savings

Same-binary eager presentation/float resampling versus held presentation/integer
resampling, with frameskip off:

| Game | Before fps | After fps | Difference | Present ms/vblank before→after |
|---|---:|---:|---:|---|
| Crash |42.38|45.45|+7.25%|1.903→0.275|
| CTR |28.31|29.97|+5.87%|1.797→0.283|

The separate original release baseline measured Crash45.40 and CTR30.05fps.
Thus these same-binary savings do not establish a cross-binary net gain: layout
and the combined source changes matter on the Old 3DS instruction cache.

Crash's whole-window controller measured100.07% speed at about12.66 raw flip
counts/s versus100.04% and11.07 for the original EWMA in that build, with zero
audio underruns. This14.4% density gain needs qualification across games and
layouts. Fixed nominal20 and22.5-picture cadences reached only91.49% and88.03%
game speed, respectively, and therefore fail the primary target.

## Aggressive-skip ceiling

Build09,5s warm-up and15s sampling, ordinary FF fixed skip3:

| Game | Game speed% | Draw Mcycles | Skip Mcycles | Idle Mcycles | Run ms/vblank |
|---|---:|---:|---:|---:|---:|
| CTR |71.63|10.235|5.270|5.262|23.005|
| GT2 |70.91|6.293|not observed|6.150|23.266|
| Silent Hill |94.44|10.440|4.151|3.617|17.424|
| R4 |123.63|7.530|5.280|1.216|13.259|

CTR cap4→cap5 only improves70.58→71.28% speed while reducing normalized
pictures5.24→4.70/s; audio underruns1.40→1.56/s. The actual-cost controller
with cap5 is similar71.22%/4.70pictures/s. Cap4 stays the default.


## Submission hardware A/B

Build12, all three named submission switches old→new in the same binary:

| Silent Hill mode | Old | New |
|---|---:|---:|
| Frameskip off fps |34.723|36.311|
| Ordinary FF speed% |93.59|95.30|
| Normal-audio window cap4 speed% |90.60|92.03|
| Normal-audio window cap5 speed% |92.28|94.21|

The4.57% off-mode gain reduces PICA CPU12.282→11.119ms/vblank and drawn
work10.846→10.198M cycles. Cap5 still underruns audio0.13/s and shows only
5.22distinct pictures/s, so it does not meet the primary target. Four off/FF
and four cap screenshots show the expected street/player scene; this is a
sparse corruption check, not a flicker audit or speaker listening test.

The hardware result supports retaining the named opt-in submission experiments;
GT2 off27.502→28.194fps (+2.52%), FF71.08→71.84%. Crash off45.995→46.041fps
is flat. No release default changes.

## Primitive hardware A/B

Build13 same-binary Crash old44.953→primsfast46.149fps (+2.66%). PICA CPU
9.111→8.498ms/vblank saves0.613ms; drawn8.166→7.875M cycles. Primitives plus
submission46.344fps; combined FF129.87% is muted. Four stills show expected
Crash scene content. This validates a modest hardware gain, not fullspeed
without frameskip. Other games and the complete texture stack remain pending.

## Renderer profile and limits

Crash sampled phase profiling costs3.07% throughput itself:46.405fps with it
disabled versus44.979 enabled. Inclusive per-vblank estimates are list8.701ms,
primitive8.324, texture1.997 including decode0.635, state0.072, batch submission
and mask0.336, readback0. The primitive residual is the largest target; deleting
state/submission cannot recover several milliseconds in Crash.

Uninstrumented Crash drawn/idle work is7.648/3.276M cycles, a10.924M pair.
A268MHz CPU has8.94M cycles per two60Hz vblanks. Reaching30 distinct Crash
pictures/s therefore requires roughly18% fewer emulation-thread cycles plus
frontend savings. The earlier5.6M average mixed drawn and idle vblanks and
underestimated the cost of a drawn picture.

Submission and vertex/coarse fast paths remain behind runtime selectors until
hardware A/B completes. Native full-VRAM hashes and rendering regressions
validate modeled output, not hardware speed or speaker sound.

Raw evidence, immutable tested artifacts, per-run requests, PMU populations and
remote stills are in the ignored devtools/o3ds60 directory. Continuous notebook:
devtools/o3ds60/results.md. No protected state was written.

## BootNTR and streaming control

Build13 defaults, Crash GPU slot2, frameskip off,5s warm-up/15s sampling in
one unchanged CIA. A fresh passive receiver verified over30s silence before
requesting either stream. New received no NTR request.

| Condition | Emulated fps | Difference vs loaded/no-stream |
|---|---:|---:|
| BootNTR loaded, no stream requested/observed |44.731|—|
| Active requested quality30/128KiB/s |43.659|−2.40%|
| Active requested quality60/1MiB/s |43.676|−2.36%|

The two active-stream results differ0.04%; the test does not establish a
bandwidth-dependent cost in this scene. It also does not measure BootNTR-loaded
overhead relative to an identical reboot without BootNTR. Earlier release45-run
stream-free data remains valid. A phase run interrupted by BootNTR startup is
invalid; its subsequent repeat has unverified stream state during sampling and
is labeled accordingly.

## Renderer-only hardware counters

Build13, BootNTR absent, randomized1/16 command-list sampling through the kernel
performance-counter API. Probeoff44.853fps vs on44.680 (−0.38% perturbation).
Renderer mean2.447Mcycles/vblank and1.082Minstructions (separate counter pair),
15.825k instruction-cache misses (~50.2% of whole-frame31.493k),2.361k data-read
misses (~20.1% of11.736k),2.187k data-write misses. Instruction-starved stall
cycles627.2k (~25.5% of renderer cycles); data-hazard484.4k (~19.7%). These events
can overlap and include other causes: they cannot be summed or attributed
wholly to cache misses. Command loop,emit_tris andtexture_for total14.7KiB but
span~26KiB in a16KiB instruction cache. No new code-layout gain is claimed.

## Texture and combined renderer hardware A/B

Build14, fixed BootNTR quality30/128KiB/s active stream, GPU slot2,
5s warm-up and15s sampling. Each game uses the same executable for all rows.

| Game | All old fps | Primitive only | Texture only | Full renderer stack | PICA ms old→stack |
|---|---:|---:|---:|---:|---|
| Crash |42.312|43.330|45.458|47.029|9.545→7.355|
| Silent Hill |33.046|34.017|33.983|36.748|13.020→10.111|

Crash texture mask+memo alone gains7.43%; the combined submission, primitive,
texture and guarded skip-gate stack gains11.15%. Drawn work falls8.483→7.308M
cycles while idle remains3.476→3.462M. Silent Hill's combined gain is11.20%,
with drawn11.402→9.821M and idle4.290→4.237M. These are emulated vblank rates,
not distinct pictures/s or full-speed claims. Off-mode Silent Hill underruns
remain1.186/s; Crash stack reported zero but ran at78.38% game speed.

A further coarse-hit shortcut adds only0.83% to Crash and is excluded from the
selected stack by the1% small-run threshold. Four Silent Hill and five Crash
stills show expected scene content; this does not establish temporal accuracy
or speaker quality. Native old/texture/stack full-VRAM hashes match in all six
games: five traces×10 frames plus60 SotN fight frames. Earlier texture eviction,
growth, collision/reset and held-picture regressions also pass.

R4 in the same build14 condition measured52.725→56.307fps with primitives,
53.590 with textures only,56.832 with the full stack (+7.79%). PICA4.675→3.558ms;
drawn7.964→7.386M cycles, idle1.495→1.505M. Four stills show the stationary car,
track and HUD correctly. Full stack off-mode reports zero underruns but94.72%
game speed, so this is not full-speed acceptance.

CTR build14 old28.590→fullstack30.464fps (+6.55%), PICA12.674→10.512ms;
drawn10.463→9.321M cycles. Idle5.867→5.883M is unchanged and exceeds the
4.47M cycles available per60Hz vblank. Reducing drawing alone therefore cannot
hold full speed in this scene. Off-mode underruns remain5.762/s. Four stills
show the expected kart/Aku Aku scene. Separate no-BootNTR New sanity on build17
passed Crash59.986fps and SotN dense100.003% speed, zero underruns in both;
New was then restored and screenshot-verified in the gate2 library.

GT2 build14 fullstack26.101→29.219fps (about+11.9%), PICA16.019→12.160ms;
drawn13.048→10.919M, idle7.031→6.991M. Required work remains even when a visible
picture is skipped, so renderer work and picture admission must be distinguished.
Four stills show the expected car/track/HUD. Off-mode remains48.70% game speed
with4.986underruns/s; aggressive-skip gate results are reported separately.

SotN fight build14 old51.185→stack54.659fps (about+6.8%), PICA10.923→9.557ms.
Primitives alone53.890fps, texture pair51.803. Stack off reports zero underruns
but91.10% game speed. Ordinary fixed-skip3 FF reaches153.04% speed (muted);
this establishes headroom, not continuous-audio acceptance. Five stills show
Richter/Dracula and the existing tall HUD elements; native full-VRAM equality
passes. The released curated Software preset remains the proven picture choice.

### Guarded skip-gate hardware A/B

Build14, fixed low NTR, GT2 ordinary fixed-skip3 FF with all other renderer
switches new: gateold71.42→gatenew73.41% game speed (+2.79% throughput). PICA
3.549→3.147ms/vblank. The new gate exposes a pure-skipped population4.169M
cycles, versus drawn7.327M and idle6.267M; old gate retained renderer work in
all picture-selected skips and had no pure-skip population. Normal picture
content still needs the guarded exception. Sparse stills show correct car/HUD.

Silent Hill stack FF92.90%, normal-audio model cap5 only91.74%/5.23pictures/s,
with0.268underruns/s. A higher skip cap does not solve its remaining emulation
floor; this result is below the earlier no-BootNTR build12 figures and must not
be called a cross-condition regression. The per-population probe will identify
where the skipped-minus-idle gap remains.

## Additional idle-loop prototype hardware result

Build15 same-binary low-NTR CTR pointer-poll extension off/on:28.579→28.596fps
(+0.06%); ordinary FF67.80→68.16% speed (+0.52%). Adding the countdown switch
is flat68.14%. Idle population cost falls5.778→5.577M in off mode, but drawn
cost increases10.545→10.728M and cancels the saving. All five stills retain the
expected scene. The gain is below the1% threshold; do not include idleptr or
idledelay in the selected final stack or recommend them as v0.3.1 defaults.

Idle-pointer no-regression checks in build15: GT225.889→25.923 (+0.13%),
R452.604→52.583 (−0.04%), Crash43.135→42.966 (−0.39%). All six corresponding
stills reviewed with expected game content. These short samples show no useful
gain; the prototype remains opt-in with no selected final-game enablement.

## Silent Hill skipped-frame attribution

Build16, low NTR, kernel PMU8/0 (instructions/I-cache misses) and opt-in
per-population timers. Same-CIA FF probeoff90.822→probeon90.681% speed
(−0.16% perturbation). With the renderer stack, FF94.740%; normal-audio model
cap5 only93.564%,0.299underruns/s. Layout differs from earlier binaries, so
use these numbers for attribution rather than a cross-binary speed claim.

| Full-stack FF population | Mcycles | Instructions M | I-cache misses k | DMA ms inclusive | PICA ms inside DMA | SPU ms | SPU wait ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| Drawn |9.403|3.735|71.028|20.449|19.426|0.232|0.022|
| Skipped |4.275|1.398|41.042|1.278|0.312|0.220|0.019|
| Idle |3.739|1.165|33.794|0|0|0.203|0.018|

The skipped-minus-idle0.536M cycles is about2.00ms at268MHz. Inclusive DMA-chain
handling accounts for1.278ms, about64%;0.312ms PICA is nested inside that number.
Approximately0.97ms DMA work lies outside PICA, such as command/state walking
and copying. SPU/wait differences are tiny; FF frontend audio is zero. In normal
mode the gap is0.639M/2.38ms, DMA1.286ms, PICA0.316ms nested, audio0.616vs0.582ms.
Frontend presentation lies outside these PMU buckets. This identifies a remaining
command-chain cost; it does not establish that all of it can safely be skipped.

## Renderer code-layout screen

Fixed low NTR, prebuilt layout CIAs on the same implementation base, Crash
slot2/off, two10s runs after3s warm-up per binary, renderer PMU probe on:

| Binary | Mean fps | Whole Mcycles/vblank | Renderer Mcycles | Renderer I-cache misses k | Renderer D-read misses k |
|---|---:|---:|---:|---:|---:|
| Baseline1 |42.061|6.009|2.734|17.501|2.669|
| Split |44.092|5.736|2.453|12.440|2.708|
| Baseline2 |42.205|5.990|2.734|17.267|2.719|

Split gains4.65% versus the mean of both baselines; baseline drift0.34%.
Renderer cycles fall10.3% and instruction misses about28.4%, with data-read
misses essentially unchanged. Six stills show expected Crash content. This
supports extending the hardware screen across six games with old and full
prototype switches; it is not yet a six-game/default integration decision.

## Dense rational cadence (opt-in)

Build17 same-binary Crash full renderer stack, low NTR,10s warm-up/15s sampling:

| Controller | Game speed % | Distinct pictures/s | Audio underruns/s |
|---|---:|---:|---:|
| Window |100.09|11.60|0|
| Actual-cost coarse model |100.00|13.67|0|
| Dense rational accumulator |99.70|15.40|0|

Dense fills intermediate fractions in a60-slot accumulator while maintaining
adjacent gap lengths and the unchanged cap4. It uses measured draw/skip/idle
costs with a2% work reserve; the default remains off. This sample gains12.7%
pictures versus the coarse model and32.8% versus the window controller. The
99.70% average is near target, not an always-full-speed guarantee or30-picture
result. User now prioritizes raw FPS and does not accept very sparse high-cap
frameskip as a solution: no cap9/11/15 experiment or build was made.

Host controller density/cap/recovery/overload tests and ASan/UBSan pass; the
CIA and remote host suites build/pass. No temporal speaker test was possible.

## Layout six-game result and SotN exception

Same fixed low NTR, pmurender on, warm5/sample15. Each binary tests old and fullstack switches.

|Game|Old baseline fps|Old split fps|Old gain%|Stack baseline fps|Stack split fps|Stack gain%|
|---|---:|---:|---:|---:|---:|---:|
|Crash Bandicoot|42.133|44.149|4.784|46.994|48.293|2.764|
|Silent Hill|32.965|33.438|1.436|36.767|37.549|2.129|
|Ridge Racer Type 4|52.687|52.831|0.274|56.955|57.518|0.990|
|Crash Team Racing|28.536|28.404|-0.463|30.414|30.519|0.347|
|Gran Turismo 2|26.057|26.165|0.412|29.143|29.499|1.219|
|Castlevania - Symphony of the Night|51.055|50.046|-1.977|54.484|53.659|-1.514|

|Stack renderer|Baseline Mcycles|Split Mcycles|I-cache k baseline→split|D-read k baseline→split|PICA ms baseline→split|
|---|---:|---:|---:|---:|---:|
|Crash Bandicoot|2.029|1.849|10.281→7.482|2.455→2.593|7.386→6.867|
|Silent Hill|2.920|2.717|17.409→13.726|4.063→3.891|10.115→9.588|
|Ridge Racer Type 4|0.975|0.948|5.340→4.800|1.120→1.144|3.566→3.449|
|Crash Team Racing|2.025|1.979|9.578→9.348|3.323→3.506|10.532→10.407|
|Gran Turismo 2|3.340|3.200|17.033→14.474|3.702→3.708|12.256→11.847|
|Castlevania - Symphony of the Night|1.988|2.126|12.871→14.750|3.603→3.825|9.588→9.920|

All24 end stills were reviewed for expected game content. Split is a real Crash
win and a smaller SH/GT2 gain on the full switch stack. R4/CTR differences are
small. SotN is slower, so the six-game screen does not justify unconditional
split-layout adoption.

A subsequent fullstack SotN A/B/A uses two10s samples after3s warm-up per CIA:

| Layout | Mean fps |
|---|---:|
| baseline1 | 54.787 |
| split | 53.671 |
| baseline2 | 54.765 |

Split is2.02% slower than the bracketing baseline mean; baseline drift−0.04%.
This confirms the exception. The user accepts this GPU trade-off because SotN's Old preset uses Software:
recommend split as the v0.3.1 layout default. Final all-six GPU records use
the integrated split binary so its SotN trade-off remains visible. Keep SotN's Software
preset in either case. Section/Os were not hardware accepted.

The layout was merged through real core sources at133b73d; patch0002 regenerated
and reverse-checks exactly. Local CIA split build passes. Final hardware uses
the exact archived integrated CIA7472f3b45722eb3b4be6f81c7b690c96d8093d430a2a1d9e5d4a74e730d33500;
different link artifacts must be remeasured even with identical renderer objects.

## Vertex-shader conversion hardware screen

The pica-vsh-1 candidate (de217b3, CIA
bb4939dba305b1a5141c55342f31d98600cf0481c65de0cc905496cf95084266)
was tested in one same-binary Crash off/on/off session, protected slot2,
full named renderer stack, skipgateold, PMU0,b plus renderer/class probes,
fixed low NTR,3s warm-up and10s sampling per row.

| Switch | FPS | Renderer Mcycles | Renderer I-cache k | PICA ms | Underruns/s |
|---|---:|---:|---:|---:|---:|
|off|49.402|1.996|9.078|7.229|0.000|
|on|49.188|1.987|9.411|7.194|0.202|
|off|48.884|1.986|8.733|7.311|0.000|

On versus the mean of both off rows is+0.09%; off-to-off drift-1.05%.
This does not clear noise. The simulator's expected gain was not established
on hardware; no six-game expansion or source integration was performed.
Keep pica_vsh off/not included in the recommended stack. Three stills show
expected Crash beach content; they do not prove precision/flicker equivalence.

## Final report

The completed matched gate2 versus full-stack/split comparison, six-game cadence
results, released-build purple Crash evidence, recommended v0.3.1 defaults and
verified final restoration are in [the final report](old-3ds-performance-final.md).
