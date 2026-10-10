# framepace

A small Vulkan implicit layer (C11, meson) that replaces MangoHud for one game:

- a frame limiter whose cap can change at runtime, with no config reload
  (same behaviour as MangoHud's `fps_limit_method=early`)
- a minimal in-frame overlay (fps, frame time, cap, frame-time graph) drawn
  into the presented swapchain image, so the compositor can still direct-scan-out
  the game

Layer name `VK_LAYER_FRAMEPACE_x86_64`, library `libVkLayer_framepace.so`, 64-bit only.

![overlay](docs/overlay-panel.png)

Full window: [docs/overlay.png](docs/overlay.png)

## Build and install

    meson setup build
    ninja -C build
    sudo meson install -C build

Needs `vulkan-headers`, `glslang` (for `glslangValidator`), meson and ninja.
Installs:

- `/usr/lib/framepace/libVkLayer_framepace.so`
- `/usr/share/vulkan/implicit_layer.d/framepace.x86_64.json` (absolute
  `library_path`, same format as MangoHud's manifest, so Steam's
  pressure-vessel imports it and remaps the host path)

Enable per process with `FRAMEPACE=1`; `DISABLE_FRAMEPACE=1` turns it off.
`FRAMEPACE_DEBUG=1` logs one-line messages to stderr (layer loaded, state
file path, swapchain created, overlay enabled/disabled and why).

Try it before installing:

    VK_ADD_IMPLICIT_LAYER_PATH=$PWD/build/dev FRAMEPACE=1 FRAMEPACE_DEBUG=1 vkcube --wsi wayland

## Shared state file

`$HOME/.local/state/framepace/state`, created (with its directory) by the
layer, `ftruncate`d to the struct size and mapped `MAP_SHARED`. If it cannot
be opened the layer is a pure passthrough. Little-endian, packed, version 1,
4128 bytes:

```c
struct fp_frame { uint64_t present_ns; uint32_t frame_ns; uint32_t work_ns; };  // 16 bytes
struct fp_state {
    uint32_t magic;      // 0x43415046 ('F','P','A','C')
    uint32_t version;    // 1
    int32_t  cap;        // fps cap; 0 = unlimited. Written by external tools, read every frame.
    uint32_t flags;      // bit0 overlay visible; bit1 adaptive (display only); bit2 manual (display only)
    uint64_t seq;        // total frames presented; written by the layer after each ring write
    uint32_t head;       // index of the most recent ring entry; layer-written
    uint32_t pid;        // pid of the process writing frames; layer-written
    struct fp_frame ring[256];
};
```

| offset | field |
|-------:|-------|
| 0 | magic |
| 4 | version |
| 8 | cap (int32) |
| 12 | flags |
| 16 | seq (u64) |
| 24 | head |
| 28 | pid |
| 32 + 16*i | ring[i]: present_ns (u64), frame_ns (u32), work_ns (u32) |

- A fresh file (wrong magic/version) is initialised by the layer: cap 0, flags 1.
- The layer reads `cap` and `flags` with relaxed atomic loads each frame.
  It writes the ring entry, then `head`, then `seq`, each with release stores.
  Readers should load `seq` with acquire semantics (plain loads are fine on x86).
- `present_ns`: CLOCK_MONOTONIC when the frame was handed down to the driver.
  `frame_ns`: interval since the previous present (0 for the first frame).
  `work_ns`: previous present returned -> frame rendered, i.e. CPU plus GPU
  time for the frame without the limiter's wait, and the best headroom measure
  available. When the layer did not wait for the GPU (cap 0, or a present that
  could not be relayed) it is previous present returned -> this
  vkQueuePresentKHR call instead.
- `seq` is not reset when a new process attaches; use `pid` and `present_ns` to
  detect restarts.

`tools/fpstate.py` mmaps the file:

    tools/fpstate.py get
    tools/fpstate.py set-cap 90        # 0 = unlimited; takes effect next frame
    tools/fpstate.py overlay on|off
    tools/fpstate.py mode adaptive|manual|none   # display flags only
    tools/fpstate.py frames 30
    tools/fpstate.py stress 2000 200   # hammer cap + overlay while a game runs

## Limiter

The GPU usually finishes a frame some variable time after the app's
`vkQueuePresentKHR`, so presenting exactly at the deadline makes the frame
reach the display at deadline + GPU time. To avoid that, with `cap > 0` the
layer relays the present: it submits an (optionally overlay-drawing) batch
on the present queue that waits on the app's present semaphores and signals
a per-image semaphore and fence, waits for that fence on the CPU (100 ms
timeout, then carries on), and only then sleeps/spins to the deadline and
presents on its already-signalled semaphore. The deadline grid is as in
MangoHud's `early` method: it advances by `1e9/cap` per frame; if the frame
is more than one interval late the grid resyncs to now; otherwise
`clock_nanosleep` to `deadline - 300us`, then spin. A cap change restarts the
grid from the last present time, so there is no jump. With `cap <= 0` there
is no wait at all (a relay submit is only made when the overlay is drawn).

## Overlay

Drawn only when `flags` bit0 is set. `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` is
added at swapchain creation (if the surface supports it). Per swapchain the
layer creates a render pass (LOAD/STORE, PRESENT_SRC to PRESENT_SRC), an
alpha-blended pipeline, and per image a framebuffer, command buffer, fence,
semaphore and a host-visible vertex buffer. Each present submits the overlay
as part of the relay submit described above. Any failure disables the overlay for that swapchain only.
Text is an embedded 5x7 font, one quad per lit pixel, scaled to about
`height/480` px per font pixel.

## Known limitations

- Relay and overlay apply only to presents with a single swapchain (others
  pass through untouched, with no limiter relay); the overlay also needs a
  queue family with GRAPHICS.
- While limiting, the present thread blocks on the GPU for each frame, so
  the CPU cannot run ahead of the GPU by the usual frame(s).
- Overlay resources are created on the first present of a swapchain (the
  present queue is not known at swapchain creation), so the first frame
  after (re)creation pays a one-off setup cost.
- SRGB swapchains get a plain 2.2 pre-linearisation of colours; there is no
  HDR handling.
- 64-bit only.
