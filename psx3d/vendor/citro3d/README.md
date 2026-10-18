This directory contains `internal.h` and the zlib license from
[citro3d 1.7.1](https://github.com/devkitPro/citro3d/tree/v1.7.1).
`../../source/renderqueue.c` is an altered version of that release's
`source/renderqueue.c`.

The replacement keeps citro3d's drawing context and render-target API, but adds
two command-list regions and two GX queues. Only one GX queue runs at a time.
The private `VIDEO_FRAME_OVERLAP` flag permits recording the next frame while
the previous queue runs. A sticky completion event is signaled after the GPU
callback has finished screen swaps. Menus and synchronous readbacks continue
to wait before reusing shared buffers. The original command allocation is
restored before citro3d frees it.

This uses citro3d's private context layout. It was built and tested against
citro3d 1.7.1 (devkitPro package 1.7.1-2) and libctru 2.7.0-1. Review the context
layout and renderqueue changes when upgrading either library; the static link
must select this renderqueue object instead of the library's renderqueue.o.
