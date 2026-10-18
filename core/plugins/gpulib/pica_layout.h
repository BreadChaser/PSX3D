/* Build-only renderer layout experiments; no runtime or rendering state. */
#ifndef PICA_LAYOUT_H
#define PICA_LAYOUT_H
#ifdef PICA_LAYOUT_SPLIT
#define PICA_INLINE_HOT(name) PICA_HOT(name)
#define PICA_COLD __attribute__((__cold__, __noinline__))
#define PICA_UNLIKELY(x) __builtin_expect(!!(x), 0)
#ifdef PICA_LAYOUT_SECTION
#define PICA_HOT(name) __attribute__((__noinline__, __section__(".pica_hot." #name)))
#else
#define PICA_HOT(name) __attribute__((__noinline__))
#endif
#else
#define PICA_INLINE_HOT(name) inline
#define PICA_COLD
#define PICA_HOT(name)
#define PICA_UNLIKELY(x) (x)
#endif
#endif
