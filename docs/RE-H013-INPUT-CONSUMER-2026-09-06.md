# H-013: the title-screen input consumer

Static investigation, 2026-09-06. Source checkout `9baceb5`; target Steam
`PreyDll.dll` SHA-256
`7d6e322f61b28331095a400f0ba5f09bd9a39c57bf993602285dd6acb05311a7`.
Addresses below are RVAs unless explicitly described as fields. Ghidra base
is `0x180000000`.

## Answer

The title screen's raw input consumer is **ArkLauncherMenu::OnInputEvent at
`0x138A930`**, reached through **CGame's exclusive input listener**, not by
finding an ActiveUserManager entry in the normal listener list.

Its entire acceptance predicate is:

```cpp
// this is the IInputEventListener subobject at launcher + 0x40.
bool LauncherOnInputEvent(void* listener, const PreyInputEvent* event) {
    if (uint32_t(event->deviceType) <= 1 && event->state == 1) {
        SetMainMenuMode((char*)listener - 0x40, nullptr); // 0x138BDA0
        return true;
    }
    return false;
}
```

**A pressed keyboard event already supplies everything this handler requires.**
It does not read key ID, key name, value, modifiers, input character, `pSymbol`
or device index. CGame's keyboard forwarding branch does not require those
fields either. A fabricated symbol or another blind key variant is therefore
not the next experiment.

The remaining live question is whether the failed event actually traversed
the exclusive-listener/override chain below. This static investigation locates
and checks that chain; it does **not** claim which pointer was absent or which
earlier listener consumed the events in the previous run.

## Exact routing and object identities

```text
IInput::PostInputEvent                         0x9D6D30, inherited R-080/R-089
  SendEventToListeners                         0x9D7430, inherited R-090
    console listeners first
    *(input + 0x48)                            exclusive listener
      normally game + 0x18
      vtable 0x1E76AA8, slot +0x08              CGame::OnInputEvent 0x1701540
        *(game + 0x148)                        input-listener override
          in attract mode: launcher + 0x40
          vtable 0x1E2C298, slot +0x08          launcher handler 0x138A930
            launcher = listener - 0x40
            SetMainMenuMode                    0x138BDA0
```

The `CGame*` global is the qword at **`PreyDll + 0x2C16840`**. This is not
`gEnv`. The game constructor `0x16F23C0` installs CGame's input-interface vtable
at object `+0x18` and publishes that global.

The launcher factory/constructor `0x1388A90` allocates `0x98` bytes and places
the input-interface vtable at launcher `+0x40`. Other interfaces use different
adjustments: `IUIGameEventSystem` is `+0x20`; the blocking-action interface is
`+0x18`. Using a complete-object pointer as a listener pointer would be wrong.

### Registration is exclusive, not normal-list insertion

`AddEventListener` is **`0x9D5D20`, IInput slot 1 (`+0x08`)**. It inserts into
`input+0x28`, skips duplicate listener pointers and sorts by descending
`GetPriority()` using comparator `0x9D7C80`. Nodes hold next `+0`, previous `+8`,
listener `+0x10`; `input+0x30` is the list count.

But CGame's initialization at `0x16FE550` does this at **`0x16FEE32..0x16FEE45`**:

```asm
mov  rcx,[PreyDll+0x224D9D8]  ; gEnv's pInput global slot
test rcx,rcx
jz   skip
mov  rax,[rcx]
lea  rdx,[rdi+0x18]          ; CGame IInputEventListener subobject
call qword ptr [rax+0x38]    ; IInput::SetExclusiveListener, slot 7
```

That slot targets **`0x9843B0`**, whose whole body is
`mov [rcx+0x48],rdx; ret`. CGame's destructor clears the same slot.

Consequently, walking normal listeners cannot establish that the title
consumer was called. Also, direct callers of `AddEventListener` do not
enumerate registrations made through virtual calls. In this database the
direct xrefs are data references; that is not evidence of no registrations.

### The override is armed and cleared by menu state transitions

`0x138B360`, matched as `ArkLauncherMenu::OnReturnToMainMenu`, has an attract
transition that writes **`game+0x148 = launcher+0x40`** at `0x138B3F2`, calls
ClearActiveUser/RemoveExclusiveController, invokes `attractOpenPage`, and sets
**launcher mode `+0x80 = 2`**. Other attract-entry paths, including the action
handler, contain the same assignment.

`SetMainMenuMode` **`0x138BDA0`** exits attract mode, clears **`game+0x148`** at
`0x138BDF0`, constructs the actual menu via `0x138C190`, enables the menu action
map and writes **mode `+0x80 = 3`** at `0x138BE75`. Its UI element is at
**launcher `+0x78`**. It also calls `SetListening(true)` at the end.

This gives a better success receipt than posted counts or the background level
load: the same launcher transitions **2 -> 3**, its override is cleared by the
native code, and the image changes from the attract prompt to the actual menu.
If already in mode 3, the raw handler can return true without another visible
transition; capture identity and initial mode therefore matter.

## What CGame's handler checks

**`0x1701540`** receives `RCX = game+0x18`, `RDX = event`.

- For **device 0 or 1**, it calls IInput slot `+0x80` with index `-1`, then
  forwards the unchanged event to the override. This slot is
  `0x9D6480`, the force-feedback device-index selector; it is not a keyboard
  rejection or an active-user lookup.
- For **device 3**, it has real exclusive-controller checks. The relevant
  complete-CGame fields are `+0xD9` (exclusive-controller state), `+0xDA`
  (connection state), `+0xDB` (reassignment path), and `+0xE8` (device ID).
  A mismatched device index can return true early when an exclusive controller
  is established and reassignment cannot be resolved. This is a gamepad branch,
  not an explanation for a pressed keyboard event.
- At **`0x17016D5`**, it loads `[listenerThis+0x130]`, which is
  **`game+0x148`**. If nonnull it calls override vtable `+0x08` at
  **`0x17016E9`** and returns that result. If null it returns false.
- There is no `pSymbol` read in this function. The keyboard route needs neither
  a key-name bind nor a native input-symbol object to enter the launcher.

`RemoveExclusiveController` **`0x1703370`** clears the exclusive-controller
flags and selects force-feedback index `-1`. It does **not** remove CGame as
IInput's exclusive listener or clear `game+0x148`. The similar names describe
different objects and must not be conflated.

## UI-state and gamepad events take different paths

For state **`0x10`**, input dispatch calls listener slot `+0x10`.
CGame's **`OnInputEventUI`, `0x1701710`**, forwards to override slot `+0x10`.
The launcher slot points to **`0x16D2100`, an unconditional false-return leaf**.
The recorded UI-state negative is therefore expected on the title route.

The launcher's raw handler rejects device 3. Gamepad acceptance instead has
the **blocking action handler `0x138A4A0`**, called with `this = launcher+0x18`.
In attract mode it recognizes **`menu_confirm`** and calls SetMainMenuMode.
In menu mode it routes `menu_up/down/left/right`, `menu_confirm`, `menu_back`
and `menu_exit` to the Flash menu.

These names are confirmed from this binary's GameActions constructor
`0x1706DA0`: member `+0x668` binds string `menu_confirm` at `0x1E79270`;
`+0x5C8` is `menu_up`; `+0x670` is `menu_back`; `+0x680` is `menu_exit`.
This identifies the native semantic consumer. **It does not prove that the
current profile binds `xi_a` to `menu_confirm`.** That remains the separate
action-map check already identified in R-089.

| Recorded attempt | Static expectation once the correct title route is reached |
| --- | --- |
| `xi_a`, device 3, pressed | Raw launcher handler declines; active gamepad and `menu_confirm` binding matter. |
| Space, device 0, pressed | Accepted by raw launcher handler. |
| Space, device 0, pressed, force | Same handler predicate; force does not change it. |
| Space, device 0, UI state 16 | Different launcher method, always returns false. |
| Enter, device 0, pressed | Accepted by raw launcher handler; key ID is not examined. |

## ActiveUserManager is not the missing input listener

The string anchor `[ActiveUserManagerBase] SetListening(%s)` resolves to
**`0x1324A60`**. It stores its flag at manager **`+0x20`** and, when enabled,
calls manager vtable `+0x18` with the game's device ID. It does not insert an
IInput listener or examine an SInputEvent.

CGame constructs this **base** manager at `0x13249A0` and stores it at
**`game+0x448`**. Its vtable is **`0x1E24F10`**. In this PC binary,
RegisterActiveUser, ClearActiveUser and EnsureActiveUserValid target the no-op
leaf **`0x1706520`**; IsActiveUserLoggedIn targets **`0xACF540`**, which returns
true. This is an on-disk fact for this constructor/table, not an assumption
transferred from the reference headers.

`SetListening(true)` also executes after the main menu opens. The log line
alone therefore proves neither that the title owns input nor that it is waiting
for a platform user to become valid.

## What can and cannot be concluded about the failed run

The static chain proves a native SInputEvent route exists for the title; a
Windows-message-only explanation is false for this route. It also rules out
missing `pSymbol` and device index as requirements of the keyboard title handler.

There is an important consistency test: if a device-0/state-1 event reaches
CGame with the correct nonnull launcher override, the launcher calls the menu
transition and returns true. **The dispatcher then stops before normal
listeners.** A receipt saying that this same event instead reached the normal
walk cannot simultaneously establish that the exclusive chain was intact and
accepted it.

The prior `inputPosted` counter increments after returning from the void native
post. It is not an exclusive-listener hit count. R-090's static permissiveness
and a counter do not supply the missing per-consumer receipt. This does not
require re-running the posting-gate experiments: observe the newly located
consumer and its actual pointers instead.

Nor does a title failure prove locomotion is blocked for the same reason.
Gameplay axes go through action maps and player handlers; title keyboard input
is deliberately consumed earlier while its override owns the screen.

## Bounded proof for the owning live lane

**Hypothesis:** the existing keyboard-Pressed event satisfies the consumer,
but a required routing pointer or observed consumer identity differs in the
failed run. **Control:** one unchanged attract frame. **Variable:** one queued
keyboard tap already supported by the mod. **Decision:** classify the earliest
missing call/pointer, or observe native mode 2 -> 3 and the corresponding image.

Before posting, on the owning native thread, record this passive snapshot:

```text
input pointer used by InputPost
*(input+0x48) and its vtable/OnInputEvent target
game = *(PreyDll+0x2C16840)
expected exclusive = game+0x18; expected vtable RVA 0x1E76AA8
override = *(game+0x148)
if override has vtable RVA 0x1E2C298:
    launcher = override-0x40
    launcher mode at +0x80 and UI element at +0x78
manager = *(game+0x448); flags +0x20/+0x21/+0x22, for context only
```

Resolve and validate readable pointers; never dereference a fabricated layout
just because a field is nonnull. Bound any list census and identify the same
game PID, DLL hash, XR state and capture run throughout.

For one tap, gather bounded entry/return receipts at **`0x1701540`** and
**`0x138A930`** (and the menu-transition entry if needed), including thread,
listener identity and the event's two predicate fields. Preserve native
behavior. Do not alter the game/override pointers or fabricate a symbol.

- **CGame is not called:** inspect actual exclusive registration and the
  existing console-listener stage; do not start another event-ABI search.
- **CGame is called but launcher is not:** its event device, return branch and
  current override identify the failure. A null/wrong override is actionable.
- **Launcher is called with device <= 1/state 1:** the recorded bytes require
  the SetMainMenuMode call. If absent, verify the deployed target/receipt.
- **Mode changes and fresh image changes:** title acceptance is proven. Only
  then proceed to `menu_confirm`/navigation binds and an existing save.
- **Mode changes but image does not:** investigate the UI element and image
  producer; additional input fields are not a justified remedy.

The current queue drains in `CSystem::Render`, with one event per call. That
is newer than the previous audit's worker-thread implementation. Record its
thread and cadence during the bounded proof; do not inherit either the old
worker-thread criticism or the new main-thread comment as runtime evidence.

## Evidence and deliverables

- String anchors, vtable contents, constructors, decompiler and assembly were
  read from `/Prey/PreyDll.dll` in the preflighted Ghidra instance. The project
  graph was queried first; its September 3 snapshot predates this work.
- Created the previously undefined blocking-query function at `0x9D7790` in
  Ghidra as requested. Its result is a bool in AL; the decompiler's inherited
  upper RAX bits are not a nonzero bool result.
- [Offline verifier](../tools/re/verify_h013_input_consumer.py): **24 checks
  passed** against the exact installed DLL, including the full launcher
  handler, its relative call target and the relevant input/manager vtables.
  Run with `C:\Python314\python.exe tools/re/verify_h013_input_consumer.py`.
- Raw static receipts are in ignored local capture storage:
  `captures/traces/2026-09-06-h013-static-receipts.json`.

No live attach, input post, game launch, injection, runtime source change,
deployment, save operation or headset experiment was performed by this lane.
The consumer question is statically answered; the exact failure in the previous
live run remains subject to the bounded routing proof above.
