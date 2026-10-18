/* ARMv6/VFP check for finite clipping deltas, including exact half ties.
 * arm-linux-gnueabihf-gcc -O2 -march=armv6k -mfpu=vfp -marm \
 *   -frounding-math clip-round-arm.c -static -lm -o clip-round-arm
 * Run under the project's qemu-arm; no console is required.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fenv.h>

static __attribute__((noinline)) int fast(float x)
{
    float bits;
    int result;
    __asm__("vcvtr.s32.f32 %0, %1" : "=t" (bits) : "t" (x));
    memcpy(&result, &bits, sizeof(result));
    return result;
}
static __attribute__((noinline)) int reference(float x) { return lrintf(x); }

static int check(float x)
{
    int actual = fast(x), expected = reference(x);
    if (actual == expected) return 0;
    fprintf(stderr, "rounding mismatch %.9g: %d != %d\n", x, actual, expected);
    return 1;
}

int main(void)
{
    int modes[] = {FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO};
    uint32_t seed = 1;
    unsigned tested = 0;
    for (unsigned m = 0; m < sizeof(modes) / sizeof(*modes); m++) {
        if (fesetround(modes[m])) return 2;
        for (int i = -8192; i <= 8192; i++) {
            if (check(i * 0.5f)) return 1;
            tested++;
        }
        for (int i = 0; i < 1000000; i++) {
            seed = seed * 1664525u + 1013904223u;
            int denominator = 1 + (seed % 4095);
            seed = seed * 1664525u + 1013904223u;
            int numerator = seed % (denominator + 1);
            seed = seed * 1664525u + 1013904223u;
            int delta = (int)(seed % 8191) - 4095;
            volatile float fraction = (float)numerator / denominator;
            if (check(delta * fraction)) return 1;
            tested++;
        }
    }
    printf("PASS %u bounded clipping values across four rounding modes\n", tested);
    return 0;
}
