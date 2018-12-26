/*
 * Copyright (C) 2018 Robin Gareus <robin@gareus.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define PROPEX_URI "http://gareus.org/oss/lv2/property_example#"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lv2/lv2plug.in/ns/ext/patch/patch.h"
#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/log/logger.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/lv2core/lv2.h>

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_URID;
	LV2_URID atom_Float;
	LV2_URID atom_Bool;
	LV2_URID patch_Set;
	LV2_URID patch_property;
	LV2_URID patch_value;
	LV2_URID m_param_polarity;
	LV2_URID m_param_gain;
} PropExURIs;

typedef struct {
	/* ports */
	const LV2_Atom_Sequence* control;

	float const* p_in;
	float*       p_out;

	PropExURIs uris;

	/* parameters */
	float target_gain;
	bool  polarity;

	/* internal state */
	float gain;
	float lpf;

	/* LV2 Output */
	LV2_Log_Log*   log;
	LV2_Log_Logger logger;
} PropEx;

static void
map_uris (LV2_URID_Map* map, PropExURIs* uris)
{
	uris->atom_Blank       = map->map (map->handle, LV2_ATOM__Blank);
	uris->atom_Object      = map->map (map->handle, LV2_ATOM__Object);
	uris->atom_URID        = map->map (map->handle, LV2_ATOM__URID);
	uris->atom_Float       = map->map (map->handle, LV2_ATOM__Float);
	uris->atom_Bool        = map->map (map->handle, LV2_ATOM__Bool);
	uris->patch_Set        = map->map (map->handle, LV2_PATCH__Set);
	uris->patch_property   = map->map (map->handle, LV2_PATCH__property);
	uris->patch_value      = map->map (map->handle, LV2_PATCH__value);
	uris->m_param_gain     = map->map (map->handle, PROPEX_URI "gain");
	uris->m_param_polarity = map->map (map->handle, PROPEX_URI "polarity");
}

static LV2_Handle
instantiate (const LV2_Descriptor*     descriptor,
             double                    rate,
             const char*               bundle_path,
             const LV2_Feature* const* features)
{
	PropEx*       self = (PropEx*)calloc (1, sizeof (PropEx));
	LV2_URID_Map* map  = NULL;

	int i;
	for (i = 0; features[i]; ++i) {
		if (!strcmp (features[i]->URI, LV2_URID__map)) {
			map = (LV2_URID_Map*)features[i]->data;
		} else if (!strcmp (features[i]->URI, LV2_LOG__log)) {
			self->log = (LV2_Log_Log*)features[i]->data;
		}
	}

	/* Initialise logger (if map is unavailable, will fallback to printf) */
	lv2_log_logger_init (&self->logger, map, self->log);

	if (!map) {
		lv2_log_error (&self->logger, "PropEx.lv2: Host does not support urid:map\n");
		free (self);
		return NULL;
	}

	map_uris (map, &self->uris);

	self->gain        = 0.f;
	self->target_gain = 1.f;
	self->polarity    = false;
	self->lpf         = 990.f / rate;

	return (LV2_Handle)self;
}

static void
connect_port (LV2_Handle instance,
              uint32_t   port,
              void*      data)
{
	PropEx* self = (PropEx*)instance;

	switch (port) {
		case 0:
			self->control = (const LV2_Atom_Sequence*)data;
			break;
		case 1:
			self->p_in = (const float*)data;
			break;
		case 2:
			self->p_out = (float*)data;
			break;
		default:
			break;
	}
}

static bool
parse_property (PropEx* self, const LV2_Atom_Object* obj)
{
	const LV2_Atom* property = NULL;
	lv2_atom_object_get (obj, self->uris.patch_property, &property, 0);

	/* Get property URI.
	 *
	 * Note: Real world code would only call
	 *  if (!property || property->type != self->uris.atom_URID) { return; }
	 * However this is example and test code, so..
	 */
	if (!property) {
		lv2_log_error (&self->logger, "PropEx.lv2: Malformed set message has no body.\n");
		return false;
	} else if (property->type != self->uris.atom_URID) {
		lv2_log_error (&self->logger, "PropEx.lv2: Malformed set message has non-URID property.\n");
		return false;
	}

	/* Get value */
	const LV2_Atom* val = NULL;
	lv2_atom_object_get (obj, self->uris.patch_value, &val, 0);
	if (!val) {
		lv2_log_error (&self->logger, "PropEx.lv2: Malformed set message has no value.\n");
		return false;
	}

	/* NOTE: This code errs towards the verbose side
	 *  - the type is usually implicit and does not need to be checked.
	 *  - consolidate code e.g.
	 *
	 *    const LV2_URID urid = (LV2_Atom_URID*)property)->body
	 *    PropExURIs* urid    = self->uris;
	 *
	 *  - no need to lv2_log warnings or errors
	 */

	if (((LV2_Atom_URID*)property)->body == self->uris.m_param_gain) {
		if (val->type != self->uris.atom_Float) {
			lv2_log_error (&self->logger, "PropEx.lv2: Invalid property type, expected 'float'.\n");
			return false;
		}
		float f = *((float*)(val + 1));
		lv2_log_note (&self->logger, "PropEx.lv2: Received gain = %f\n", f);
		self->target_gain = powf (10.f, .05f * f); // dB to coeff
	} else if (((LV2_Atom_URID*)property)->body == self->uris.m_param_polarity) {
		if (val->type != self->uris.atom_Bool) {
			lv2_log_error (&self->logger, "PropEx.lv2: Invalid property type, expected 'bool'.\n");
			return false;
		}
		bool b = *((bool*)(val + 1));
		lv2_log_note (&self->logger, "PropEx.lv2: Received polarity = %d\n", b);
		self->polarity = b;
	} else {
		lv2_log_error (&self->logger, "PropEx.lv2: Set message for unknown property.\n");
		return false;
	}
	return true;
}

static void
run (LV2_Handle instance, uint32_t n_samples)
{
	PropEx* self = (PropEx*)instance;
	if (!self->control) {
		return;
	}

	/* process control events */
	LV2_ATOM_SEQUENCE_FOREACH (self->control, ev) {
		if (ev->body.type != self->uris.atom_Object) {
			continue;
		}
		const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
		if (obj->body.otype == self->uris.patch_Set) {
			/* NOTE, events are timestamped. There could be more than
			 * one value for the same parameter in the same cycle.
			 *
			 * ev->time.frames is the sample offset in current cycle:
			 *
			 *    0 <= ev->time.frames < n_samples
			 *
			 * In this example we ignore the timestamp, in
			 * particular since no host currently supports
			 * timestamped automation.
			 *  - Ardour always uses (n_samples - 1).
			 *  - jalv uses (n_samples) -- incorrectly.
			 */
			parse_property (self, obj);

			/* Eventually we should process audio from 0 until
			 * ev->time.frames, then apply the parameter change and
			 * process audio until the next event or end of cycle.
			 */
		}
	}

	/* localize variables */
	const float* in  = self->p_in;
	float*       out = self->p_out;

	float       gain   = self->gain;
	const float target = self->polarity ? -self->target_gain : self->target_gain;

	if (fabsf (gain - target) < 0.01) {
		/* constant gain factor */
		for (uint32_t i = 0; i < n_samples; ++i) {
			out[i] = in[i] * target;
		}
		self->gain = target;
		return;
	}

	/* low pass filter gain-coefficient */
	const float lpf    = self->lpf;
	uint32_t    remain = n_samples;

	while (remain > 0) {
		uint32_t n_proc = remain > 16 ? 16 : remain;
		gain += lpf * (target - gain);
		for (uint32_t i = 0; i < n_proc; ++i) {
			*out++ = *in++ * gain;
		}
		remain -= n_proc;
	}
	self->gain = gain;
}

static void
cleanup (LV2_Handle instance)
{
	free (instance);
}

static const void*
extension_data (const char* uri)
{
	return NULL;
}

static const LV2_Descriptor descriptor = {
	PROPEX_URI "mono",
	instantiate,
	connect_port,
	NULL,
	run,
	NULL,
	cleanup,
	extension_data
};

/* clang-format off */
#undef LV2_SYMBOL_EXPORT
#ifdef _WIN32
# define LV2_SYMBOL_EXPORT __declspec(dllexport)
#else
# define LV2_SYMBOL_EXPORT __attribute__ ((visibility ("default")))
#endif
/* clang-format on */
LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor (uint32_t index)
{
	switch (index) {
		case 0:
			return &descriptor;
		default:
			return NULL;
	}
}
