#ifndef FORCE_FEEDBACK_H
#define FORCE_FEEDBACK_H

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <SDL3/SDL.h>

namespace godot {

/// Force feedback for racing wheels (and other haptic devices) via SDL3's
/// haptic subsystem. On Windows this drives DirectInput FFB, on Linux the
/// kernel ff API — anything SDL3 supports.
///
/// Only the haptic subsystem is initialized, never SDL's joystick/input
/// subsystems, so this coexists with the host engine's own input handling
/// of the same physical device.
///
/// Not thread-safe: call everything from the main thread.
class ForceFeedback : public RefCounted {
	GDCLASS(ForceFeedback, RefCounted)

	SDL_Haptic *haptic = nullptr;
	int constant_id = -1;
	int spring_id = -1;
	int damper_id = -1;
	bool constant_running = false;
	// Set when the driver rejects effect creation, so we don't retry (and
	// spam warnings) every tick — some wheels advertise features they can't
	// actually create.
	bool spring_failed = false;
	bool damper_failed = false;
	// Cached last-sent values so redundant driver updates are skipped.
	float last_constant = 999.0f;
	float last_spring = -1.0f;
	float last_spring_saturation = -1.0f;
	float last_damper = -1.0f;

	static bool ensure_sdl();
	bool create_constant_effect();
	bool update_condition_effect(int &effect_id, Uint16 effect_type, float strength, float saturation, float &last_strength, bool &failed);

protected:
	static void _bind_methods();

public:
	~ForceFeedback() override;

	// Device enumeration — usable before open().
	PackedStringArray list_devices();
	int get_device_count();

	// Lifecycle.
	bool open(int p_index);
	bool open_by_name(const String &p_name_substring);
	bool is_open() const;
	void close();
	String get_device_name() const;

	// Capabilities of the opened device.
	bool supports_constant() const;
	bool supports_spring() const;
	bool supports_damper() const;

	// Control. All strengths are normalized: gain/autocenter/spring/damper
	// 0..1, constant force -1..1 (sign = direction along the steering axis).
	void set_gain(float p_gain);
	void set_autocenter(float p_strength);
	void set_constant_force(float p_value);
	void set_spring(float p_strength, float p_saturation);
	void set_damper(float p_strength);
	void keep_alive();
	void stop_all();
};

} // namespace godot

#endif // FORCE_FEEDBACK_H
