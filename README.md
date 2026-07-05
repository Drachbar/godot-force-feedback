# godot-force-feedback

Real force feedback for racing wheels in **Godot 4** — a small GDExtension
wrapping [SDL3's haptic subsystem](https://wiki.libsdl.org/SDL3/CategoryHaptic).

Godot's built-in `Input.start_joy_vibration()` only does XInput gamepad
rumble and cannot drive the motor of a DirectInput force feedback wheel
(Thrustmaster, Logitech, Fanatec, Moza, …). This extension fills that gap
until native support lands in the engine
([godot-proposals#8309](https://github.com/godotengine/godot-proposals/issues/8309)).

- **Constant force** — the main torque channel; feed it your tire model's
  self-aligning torque every physics tick.
- **Spring** — centering force (e.g. at standstill).
- **Damper** — velocity-based resistance.
- **Gain / autocenter** control, device enumeration, capability queries.

Only SDL's *haptic* subsystem is initialized — never its joystick/input
subsystems — so the extension coexists with Godot's own reading of the same
physical device (steering axis, pedals, buttons).

Requires Godot **4.5+**. Windows x86_64 binaries included; Linux should work
by building against a system SDL3 (untested, PRs welcome).

## Install

Copy `addons/force_feedback/` into your project. Done — no editor plugin to
enable; the `ForceFeedback` class is registered automatically.

## Usage

```gdscript
var ffb := ForceFeedback.new()

func _ready() -> void:
    print(ffb.list_devices())          # e.g. ["Thrustmaster TX Racing Wheel"]
    if not ffb.open_by_name("thrustmaster"):
        ffb.open(0)
    ffb.set_autocenter(0.0)            # the game supplies all forces
    ffb.set_gain(0.8)
    ffb.set_damper(0.15)

func _physics_process(_delta: float) -> void:
    # -1..1 along the steering axis. Compute this from your tire model's
    # self-aligning torque for realistic feel.
    ffb.set_constant_force(my_normalized_steering_torque)

func _exit_tree() -> void:
    ffb.stop_all()                     # never leave torque on the motor
    ffb.close()
```

**Safety:** a force feedback wheel can spin violently. Always zero the force
and call `stop_all()` when the game pauses, loses focus, or quits.
`close()` (and the destructor) stop all effects as a backstop.

### API

| Method | Description |
| --- | --- |
| `list_devices() -> PackedStringArray` | Names of all haptic devices. |
| `get_device_count() -> int` | Number of haptic devices. |
| `open(index: int) -> bool` | Open a device by enumeration index. |
| `open_by_name(name_substring: String) -> bool` | Open the first device whose name contains the substring (case-insensitive). |
| `is_open() -> bool` / `close()` | Lifecycle. |
| `get_device_name() -> String` | Name of the opened device. |
| `supports_constant/spring/damper() -> bool` | Capability queries. |
| `set_gain(gain: float)` | Master gain 0..1. |
| `set_autocenter(strength: float)` | Driver-side centering 0..1 (set 0 for game-driven FFB). |
| `set_constant_force(value: float)` | Torque -1..1 along the steering axis. NaN-guarded and clamped. Redundant updates are skipped, so calling at 120 Hz is fine. |
| `set_spring(strength: float, saturation := 1.0)` | Centering spring 0..1. |
| `set_damper(strength: float)` | Damping 0..1. |
| `stop_all()` | Stop every effect (they re-arm on the next `set_*`). |

All calls must come from the main thread.

## Building from source

Prerequisites: Python 3 + SCons (`pip install scons`), a C++17 compiler
(MSVC Build Tools on Windows), git.

```sh
git clone --recurse-submodules https://github.com/<you>/godot-force-feedback
cd godot-force-feedback
# Windows: download SDL3-devel-3.x.x-VC.zip from
# https://github.com/libsdl-org/SDL/releases and extract include/ and
# lib/x64/ into thirdparty/SDL3/
scons platform=windows target=template_debug arch=x86_64
scons platform=windows target=template_release arch=x86_64
```

The DLLs land in `addons/force_feedback/bin/`. The first build compiles
godot-cpp and takes a few minutes; incremental builds are fast.

## Notes & caveats

- On Windows SDL3 drives FFB through DirectInput. Set your wheel to PC mode
  and install the vendor driver, or the device won't expose FFB.
- Some drivers dislike being opened from two processes; if `open()` fails
  while the Godot editor is also running the project, try a standalone run.
- The constant-force direction uses `SDL_HAPTIC_STEERING_AXIS` with a
  cartesian fallback for older drivers.

## License

MIT — see [LICENSE](LICENSE). SDL3 is zlib-licensed.
