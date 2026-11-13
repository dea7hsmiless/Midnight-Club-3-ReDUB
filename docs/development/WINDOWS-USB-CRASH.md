# Remix Windows USB completion crash

User-reported unexpected exit, dump `mcdub3remix.exe.28992.dmp`:

- Windows exception `0xC0000005`, reading host address `0x8000FFF8`.
- Fault RVA `0x1B895B9`, mapped against the installed executable to
  `sub_004464D4`, the guest USB completed-transfer handler.
- The failing instruction reads the next descriptor's `+8` field.
  Its guest address is `0x7FFFFFF8`, obtained from physical `0xFFFFFFF0`
  plus the driver's `0x80000000` translation base (32-bit arithmetic).
- Native caller chain maps to `sub_004465C8`, `sub_00446674`,
  `sub_0044692D`, and `kernel_run_dpc`. This establishes the USB DPC path;
  it does not identify every possible source of the invalid descriptor.
- The application log has no normal quit marker.

The USB controller worker's descriptor walker and done-list publisher ran
outside the guest execution lock. Only its subsequent ISR took that lock.
This allowed descriptor next/head writes to overlap the guest's completion
handler, which can unlink, recycle and free those descriptors. The controller
now takes the guest execution lock for its short DMA/frame/done-publication
pass, releasing before IRQ holdoff/dispatch and any sleep. Control/interrupt
enable registers are re-read after acquiring the lock, preventing stale list
enable bits from walking a list disabled while the worker waited.

This addresses a concrete race consistent with the dump. It requires sustained
controller/gameplay testing before concluding that this was the sole cause.
No exception is swallowed, no invalid-pointer read is replaced with fake data,
and controller input remains enabled.

Test logs: `work-remix/logs/usb_serial_test_stderr.log` and corresponding stdout.
If another crash occurs, compare its new dump and executable together before
changing descriptor handling further. Keep radio HUD/menu visual changes
separate until this regression is verified.

## Second fault: cached Port Options list after a frontend transition

The next run, dump `mcdub3remix.exe.31940.dmp`, faults at RVA `0x15C041B`.
This is a different path: `sub_00348950` (clear frontend list), reached from
`port_build`, through a native menu transition involving `port_title` and
`sub_0018D4A0`. The cached list address is `0x82F7CE50`; its page index is
`0x004D0020` instead of 0..2. The resulting list-count write is outside mapped
guest RAM at guest `0x842BD108`. This establishes that cached frontend state
was being used after it ceased to be a valid list.

The patch verifies the current frontend manager and list against the captured
owner, vtable, page range, capacity and column count before rebuilding, updating
labels, changing a title or returning to the parent page. Invalidated owners
clear all cached Port Options state. Reentrant page building is guarded, title
movie calls are removed from the rebuild path, and row/cell/text buffers are
range checked with room for their terminating character.

An isolated regression check compiles the actual context-validation functions
and verifies rejection of the exact crash page, manager/list replacement,
object reuse with a different vtable, and complete state invalidation.
Live validation logs: `work-remix/logs/menu_lifetime_test_stderr.log`.

The real Options list has four columns; requiring exactly three hid the entry.
The corrected validation accepts 3..32 columns, including the observed layout.
User confirmed the row appeared again and live logs show successful entry.

## Input freeze while rendering continued

After B returned from Port Options, FPS logs kept advancing but menu input
stopped. The custom Back handler cleared actor `+0x44` bit 0 on both B press
and release. `sub_00160B90` explicitly tests that bit before handling any menu
input: it is the native menu-enable flag, not a Back-button latch.

Both flag writes are removed. A held B is swallowed by omitting native updates
only until release; the parent stays enabled throughout. Confirm actions are
latched until A is released to avoid repeated settings writes/side effects.
The regression check compiles the actual update handler and verifies that Back
preserves enable state, consumes held B, and resumes native input on the next
frame after release. Live logs: `work-remix/logs/menu_input_fix_stderr.log`.

## Render fence wait with missed completion notification

A later hang stopped frame progress with the guest main thread inside
`sub_004165A0`, waiting indefinitely on D3D device `+0x1DCC`. A per-thread
kernel wait recheck is registered only around this D3D fence function. It
reads the actual GPU semaphore and accepts completion only when the signed
wrapping counter comparison reaches the requested target. A pending GPU fence
continues waiting; unrelated events have no recheck registered. The callback
runs outside the kernel dispatcher lock and the previous callback is restored
on return. Tests cover pending/completed fences, unrelated waits and wraparound.
Live logs record verified completed fences whose event notification was missed.

The separate startup vblank spin in `sub_0041B250_gen` now invokes the runtime's
existing `xbox_Nv2aVblankTaken()` acknowledgement before entering the original
handler. This clears the emulated interrupt hold and pending bits.

The user reported that driving and repeated pause/resume worked after these
changes. This is runtime evidence for these paths, not proof that all possible
game crashes have been eliminated.

## Mid-race fence behind an apparently empty command queue

The next captured freeze had GPU fence `C5537`, requested fence `C554F`, and
DMA PUT/GET both `60C420`. A saved command-ring snapshot still contained later
semaphore releases through `C5593`. This differs from a lost event after an
already-completed fence: allowing the guest to continue would be premature.

The decoder previously kept a fixed PUT snapshot while the producer could
continue writing and wrapping the ring. At its safety limit it silently
discarded the walk and set GET to that old snapshot. The polling thread also
published PUT as consumed unconditionally. The candidate correction follows
live PUT, returns the actual execution position, and publishes only that GET.
A bounded pass retains its position and is retried even if PUT has not changed.
Decoder aborts now preserve header/flow diagnostics in the log.

Replay of the captured ring from `6BC5C8` with stale endpoint `5FC890` and live
endpoint `60C420` sends the old decoder to its 4,194,305-word cutoff. The updated
decoder processes 343,698 words and 46 semaphore releases, reaches `60C420` and
`C5593`, and reports zero unknown headers. A separate bounded-loop check verifies
that unexecuted data is never reported consumed. Live race verification is
still required; no timeout or fabricated fence completion was introduced.
