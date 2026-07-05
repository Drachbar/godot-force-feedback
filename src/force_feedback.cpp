#include "force_feedback.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cmath>
#include <cstring>

using namespace godot;

// One update step on a 16-bit effect level; smaller changes are skipped.
static const float LEVEL_EPSILON = 1.0f / 32767.0f;

ForceFeedback::~ForceFeedback() {
	close();
}

bool ForceFeedback::ensure_sdl() {
	// Haptics only — SDL's joystick subsystem stays untouched so the host
	// engine keeps exclusive ownership of input.
	if (SDL_WasInit(SDL_INIT_HAPTIC)) {
		return true;
	}
	if (!SDL_InitSubSystem(SDL_INIT_HAPTIC)) {
		UtilityFunctions::push_warning("ForceFeedback: SDL haptic init failed: ", SDL_GetError());
		return false;
	}
	return true;
}

PackedStringArray ForceFeedback::list_devices() {
	PackedStringArray names;
	if (!ensure_sdl()) {
		return names;
	}
	int count = 0;
	SDL_HapticID *ids = SDL_GetHaptics(&count);
	for (int i = 0; i < count; i++) {
		const char *name = SDL_GetHapticNameForID(ids[i]);
		names.push_back(name != nullptr ? String::utf8(name) : String("(unknown)"));
	}
	SDL_free(ids);
	return names;
}

int ForceFeedback::get_device_count() {
	if (!ensure_sdl()) {
		return 0;
	}
	int count = 0;
	SDL_free(SDL_GetHaptics(&count));
	return count;
}

bool ForceFeedback::open(int p_index) {
	close();
	if (!ensure_sdl()) {
		return false;
	}
	int count = 0;
	SDL_HapticID *ids = SDL_GetHaptics(&count);
	if (p_index < 0 || p_index >= count) {
		SDL_free(ids);
		UtilityFunctions::push_warning("ForceFeedback: device index ", p_index, " out of range (", count, " devices)");
		return false;
	}
	haptic = SDL_OpenHaptic(ids[p_index]);
	SDL_free(ids);
	if (haptic == nullptr) {
		UtilityFunctions::push_warning("ForceFeedback: open failed: ", SDL_GetError());
		return false;
	}
	// The game supplies all forces; the driver's own centering would fight it.
	SDL_SetHapticAutocenter(haptic, 0);
	SDL_SetHapticGain(haptic, 100);
	if (!create_constant_effect()) {
		close();
		return false;
	}
	return true;
}

bool ForceFeedback::open_by_name(const String &p_name_substring) {
	if (!ensure_sdl()) {
		return false;
	}
	const String needle = p_name_substring.to_lower();
	int count = 0;
	SDL_HapticID *ids = SDL_GetHaptics(&count);
	int found = -1;
	for (int i = 0; i < count; i++) {
		const char *name = SDL_GetHapticNameForID(ids[i]);
		if (name != nullptr && String::utf8(name).to_lower().contains(needle)) {
			found = i;
			break;
		}
	}
	SDL_free(ids);
	if (found < 0) {
		return false;
	}
	return open(found);
}

bool ForceFeedback::is_open() const {
	return haptic != nullptr;
}

String ForceFeedback::get_device_name() const {
	if (haptic == nullptr) {
		return String();
	}
	const char *name = SDL_GetHapticName(haptic);
	return name != nullptr ? String::utf8(name) : String();
}

void ForceFeedback::close() {
	if (haptic == nullptr) {
		return;
	}
	// Kill all motor output before letting go of the device.
	SDL_StopHapticEffects(haptic);
	if (constant_id >= 0) {
		SDL_DestroyHapticEffect(haptic, constant_id);
	}
	if (spring_id >= 0) {
		SDL_DestroyHapticEffect(haptic, spring_id);
	}
	if (damper_id >= 0) {
		SDL_DestroyHapticEffect(haptic, damper_id);
	}
	SDL_CloseHaptic(haptic);
	haptic = nullptr;
	constant_id = -1;
	spring_id = -1;
	damper_id = -1;
	constant_running = false;
	spring_failed = false;
	damper_failed = false;
	last_constant = 999.0f;
	last_spring = -1.0f;
	last_spring_saturation = -1.0f;
	last_damper = -1.0f;
}

bool ForceFeedback::supports_constant() const {
	return haptic != nullptr && (SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_CONSTANT) != 0;
}

bool ForceFeedback::supports_spring() const {
	return haptic != nullptr && (SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_SPRING) != 0;
}

bool ForceFeedback::supports_damper() const {
	return haptic != nullptr && (SDL_GetHapticFeatures(haptic) & SDL_HAPTIC_DAMPER) != 0;
}

bool ForceFeedback::create_constant_effect() {
	if (!supports_constant()) {
		UtilityFunctions::push_warning("ForceFeedback: device has no constant-force support");
		return false;
	}
	SDL_HapticEffect effect;
	std::memset(&effect, 0, sizeof(effect));
	effect.type = SDL_HAPTIC_CONSTANT;
	effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
	effect.constant.length = SDL_HAPTIC_INFINITY;
	effect.constant.level = 0;
	constant_id = SDL_CreateHapticEffect(haptic, &effect);
	if (constant_id < 0) {
		// Older drivers may not accept the steering-axis direction; fall back
		// to a cartesian X-axis direction, which maps to the wheel on most rigs.
		effect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
		effect.constant.direction.dir[0] = 1;
		constant_id = SDL_CreateHapticEffect(haptic, &effect);
	}
	if (constant_id < 0) {
		UtilityFunctions::push_warning("ForceFeedback: creating constant effect failed: ", SDL_GetError());
		return false;
	}
	if (!SDL_RunHapticEffect(haptic, constant_id, 1)) {
		UtilityFunctions::push_warning("ForceFeedback: starting constant effect failed: ", SDL_GetError());
		return false;
	}
	constant_running = true;
	return true;
}

void ForceFeedback::set_gain(float p_gain) {
	if (haptic == nullptr) {
		return;
	}
	p_gain = std::isfinite(p_gain) ? p_gain : 0.0f;
	SDL_SetHapticGain(haptic, (int)(CLAMP(p_gain, 0.0f, 1.0f) * 100.0f));
}

void ForceFeedback::set_autocenter(float p_strength) {
	if (haptic == nullptr) {
		return;
	}
	p_strength = std::isfinite(p_strength) ? p_strength : 0.0f;
	SDL_SetHapticAutocenter(haptic, (int)(CLAMP(p_strength, 0.0f, 1.0f) * 100.0f));
}

void ForceFeedback::set_constant_force(float p_value) {
	if (haptic == nullptr || constant_id < 0) {
		return;
	}
	// A NaN from game physics must never reach the motor.
	if (!std::isfinite(p_value)) {
		p_value = 0.0f;
	}
	p_value = CLAMP(p_value, -1.0f, 1.0f);
	if (std::fabs(p_value - last_constant) < LEVEL_EPSILON) {
		return;
	}
	last_constant = p_value;
	SDL_HapticEffect effect;
	std::memset(&effect, 0, sizeof(effect));
	effect.type = SDL_HAPTIC_CONSTANT;
	effect.constant.direction.type = SDL_HAPTIC_STEERING_AXIS;
	effect.constant.length = SDL_HAPTIC_INFINITY;
	effect.constant.level = (Sint16)(p_value * 32767.0f);
	if (!SDL_UpdateHapticEffect(haptic, constant_id, &effect)) {
		UtilityFunctions::push_warning("ForceFeedback: updating constant force failed: ", SDL_GetError());
		return;
	}
	// stop_all() halts the effect; updates alone don't restart it.
	if (!constant_running) {
		constant_running = SDL_RunHapticEffect(haptic, constant_id, 1);
	}
}

bool ForceFeedback::update_condition_effect(int &effect_id, Uint16 effect_type, float strength, float saturation, float &last_strength, bool &failed) {
	if (haptic == nullptr || failed) {
		return false;
	}
	strength = std::isfinite(strength) ? CLAMP(strength, 0.0f, 1.0f) : 0.0f;
	saturation = std::isfinite(saturation) ? CLAMP(saturation, 0.0f, 1.0f) : 1.0f;
	if (strength == last_strength) {
		return true;
	}
	if (strength <= 0.0f) {
		if (effect_id >= 0) {
			SDL_StopHapticEffect(haptic, effect_id);
		}
		last_strength = 0.0f;
		return true;
	}
	SDL_HapticEffect effect;
	std::memset(&effect, 0, sizeof(effect));
	effect.type = effect_type;
	effect.condition.direction.type = SDL_HAPTIC_STEERING_AXIS;
	effect.condition.length = SDL_HAPTIC_INFINITY;
	const Sint16 coeff = (Sint16)(strength * 32767.0f);
	const Uint16 sat = (Uint16)(saturation * 65535.0f);
	for (int axis = 0; axis < 3; axis++) {
		effect.condition.right_coeff[axis] = coeff;
		effect.condition.left_coeff[axis] = coeff;
		effect.condition.right_sat[axis] = sat;
		effect.condition.left_sat[axis] = sat;
	}
	bool was_stopped = last_strength <= 0.0f;
	if (effect_id < 0) {
		effect_id = SDL_CreateHapticEffect(haptic, &effect);
		if (effect_id < 0) {
			// Retry with a cartesian direction — some drivers reject the
			// steering-axis direction type.
			effect.condition.direction.type = SDL_HAPTIC_CARTESIAN;
			effect.condition.direction.dir[0] = 1;
			effect_id = SDL_CreateHapticEffect(haptic, &effect);
		}
		if (effect_id < 0) {
			// Give up for good: the device advertises the feature but can't
			// create the effect. Warn once instead of every tick.
			failed = true;
			UtilityFunctions::push_warning("ForceFeedback: creating condition effect failed (disabled): ", SDL_GetError());
			return false;
		}
		was_stopped = true;
	} else if (!SDL_UpdateHapticEffect(haptic, effect_id, &effect)) {
		UtilityFunctions::push_warning("ForceFeedback: updating condition effect failed: ", SDL_GetError());
		return false;
	}
	last_strength = strength;
	if (was_stopped) {
		return SDL_RunHapticEffect(haptic, effect_id, 1);
	}
	return true;
}

void ForceFeedback::set_spring(float p_strength, float p_saturation) {
	if (!supports_spring()) {
		return;
	}
	// Force re-send when saturation changes even if strength is unchanged.
	if (p_saturation != last_spring_saturation) {
		last_spring = -1.0f;
		last_spring_saturation = p_saturation;
	}
	update_condition_effect(spring_id, SDL_HAPTIC_SPRING, p_strength, p_saturation, last_spring, spring_failed);
}

void ForceFeedback::set_damper(float p_strength) {
	if (!supports_damper()) {
		return;
	}
	update_condition_effect(damper_id, SDL_HAPTIC_DAMPER, p_strength, 1.0f, last_damper, damper_failed);
}

void ForceFeedback::keep_alive() {
	if (haptic == nullptr) {
		return;
	}
	if (constant_id >= 0) {
		constant_running = SDL_RunHapticEffect(haptic, constant_id, 1);
	}
	if (spring_id >= 0 && last_spring > 0.0f) {
		SDL_RunHapticEffect(haptic, spring_id, 1);
	}
	if (damper_id >= 0 && last_damper > 0.0f) {
		SDL_RunHapticEffect(haptic, damper_id, 1);
	}
}

void ForceFeedback::stop_all() {
	if (haptic == nullptr) {
		return;
	}
	SDL_StopHapticEffects(haptic);
	// Effects are stopped, not destroyed: the next set_* call re-arms them.
	constant_running = false;
	last_constant = 999.0f;
	last_spring = -1.0f;
	last_damper = -1.0f;
}

void ForceFeedback::_bind_methods() {
	ClassDB::bind_method(D_METHOD("list_devices"), &ForceFeedback::list_devices);
	ClassDB::bind_method(D_METHOD("get_device_count"), &ForceFeedback::get_device_count);
	ClassDB::bind_method(D_METHOD("open", "index"), &ForceFeedback::open);
	ClassDB::bind_method(D_METHOD("open_by_name", "name_substring"), &ForceFeedback::open_by_name);
	ClassDB::bind_method(D_METHOD("is_open"), &ForceFeedback::is_open);
	ClassDB::bind_method(D_METHOD("close"), &ForceFeedback::close);
	ClassDB::bind_method(D_METHOD("get_device_name"), &ForceFeedback::get_device_name);
	ClassDB::bind_method(D_METHOD("supports_constant"), &ForceFeedback::supports_constant);
	ClassDB::bind_method(D_METHOD("supports_spring"), &ForceFeedback::supports_spring);
	ClassDB::bind_method(D_METHOD("supports_damper"), &ForceFeedback::supports_damper);
	ClassDB::bind_method(D_METHOD("set_gain", "gain"), &ForceFeedback::set_gain);
	ClassDB::bind_method(D_METHOD("set_autocenter", "strength"), &ForceFeedback::set_autocenter);
	ClassDB::bind_method(D_METHOD("set_constant_force", "value"), &ForceFeedback::set_constant_force);
	ClassDB::bind_method(D_METHOD("set_spring", "strength", "saturation"), &ForceFeedback::set_spring, DEFVAL(1.0f));
	ClassDB::bind_method(D_METHOD("set_damper", "strength"), &ForceFeedback::set_damper);
	ClassDB::bind_method(D_METHOD("keep_alive"), &ForceFeedback::keep_alive);
	ClassDB::bind_method(D_METHOD("stop_all"), &ForceFeedback::stop_all);
}
