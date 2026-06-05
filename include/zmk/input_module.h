#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct zmk_input_module_capabilities {
	uint32_t flags;
	bool kscan;
	bool encoder;
	bool adc;
	bool spi;
	bool i2c;
};

struct zmk_input_module_profile {
	uint32_t id;
	const char *name;
	uint32_t capabilities;
	bool available;
};

uint32_t zmk_input_module_selected_get(void);
uint32_t zmk_input_module_applied_get(void);
bool zmk_input_module_is_applied(void);
size_t zmk_input_module_profile_count(void);
int zmk_input_module_profile_get(size_t index, struct zmk_input_module_profile *profile);
const char *zmk_input_module_profile_name(uint32_t profile_id);
uint32_t zmk_input_module_profile_flags(uint32_t profile_id);
bool zmk_input_module_profile_available(uint32_t profile_id);
uint32_t zmk_input_module_available_profile_mask(void);
struct zmk_input_module_capabilities zmk_input_module_profile_capabilities(uint32_t profile_id);
struct zmk_input_module_capabilities zmk_input_module_selected_capabilities(void);
int zmk_input_module_select_set(uint32_t profile_id);
int zmk_input_module_apply(uint32_t profile_id);
