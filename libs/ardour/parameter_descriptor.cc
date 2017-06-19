/*
    Copyright (C) 2014 Paul Davis
    Author: David Robillard

    This program is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the Free
    Software Foundation; either version 2 of the License, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <boost/algorithm/string.hpp>

#include "pbd/control_math.h"

#include "ardour/amp.h"
#include "ardour/dB.h"
#include "ardour/parameter_descriptor.h"
#include "ardour/rc_configuration.h"
#include "ardour/types.h"
#include "ardour/utils.h"

#include "pbd/i18n.h"

namespace ARDOUR {

ParameterDescriptor::ParameterDescriptor(const Evoral::Parameter& parameter)
	: Evoral::ParameterDescriptor()
	, key((uint32_t)-1)
	, datatype(Variant::NOTHING)
	, type((AutomationType)parameter.type())
	, unit(NONE)
	, step(0)
	, smallstep(0)
	, largestep(0)
	, integer_step(parameter.type() >= MidiCCAutomation &&
	               parameter.type() <= MidiChannelPressureAutomation)
	, sr_dependent(false)
	, enumeration(false)
{
	ScalePoints sp;

	/* Note: defaults in Evoral::ParameterDescriptor */

	switch((AutomationType)parameter.type()) {
	case GainAutomation:
	case BusSendLevel:
		upper  = Config->get_max_gain();
		normal = 1.0f;
		break;
	case BusSendEnable:
		normal = 1.0f;
		toggled = true;
		break;
	case TrimAutomation:
		upper  = 10; // +20dB
		lower  = .1; // -20dB
		normal = 1.0f;
		logarithmic = true;
		break;
	case PanAzimuthAutomation:
		normal = 0.5f; // there really is no _normal but this works for stereo, sort of
		upper  = 1.0f;
		break;
	case PanWidthAutomation:
		lower  = -1.0;
		upper  = 1.0;
		normal = 0.0f;
		break;
	case RecEnableAutomation:
	case RecSafeAutomation:
		lower  = 0.0;
		upper  = 1.0;
		toggled = true;
		break;
	case FadeInAutomation:
	case FadeOutAutomation:
	case EnvelopeAutomation:
		upper  = 2.0f;
		normal = 1.0f;
		break;
	case SoloAutomation:
	case MuteAutomation:
		upper  = 1.0f;
		normal = 0.0f;
		toggled = true;
		break;
	case MidiCCAutomation:
	case MidiPgmChangeAutomation:
	case MidiChannelPressureAutomation:
	case MidiNotePressureAutomation:
		lower  = 0.0;
		normal = 0.0;
		upper  = 127.0;
		break;
	case MidiPitchBenderAutomation:
		lower  = 0.0;
		normal = 8192.0;
		upper  = 16383.0;
		break;
	case PhaseAutomation:
		toggled = true;
		break;
	case MonitoringAutomation:
		enumeration = true;
		integer_step = true;
		lower = MonitorAuto;
		upper = MonitorDisk; /* XXX bump when we add MonitorCue */
		break;
	case SoloIsolateAutomation:
	case SoloSafeAutomation:
		toggled = true;
		break;
	default:
		break;
	}

	update_steps();
}

ParameterDescriptor::ParameterDescriptor()
	: Evoral::ParameterDescriptor()
	, key((uint32_t)-1)
	, datatype(Variant::NOTHING)
	, type(NullAutomation)
	, unit(NONE)
	, step(0)
	, smallstep(0)
	, largestep(0)
	, integer_step(false)
	, sr_dependent(false)
	, enumeration(false)
{}

void
ParameterDescriptor::update_steps()
{
	if (unit == ParameterDescriptor::MIDI_NOTE) {
		step      = smallstep = 1;  // semitone
		largestep = 12;             // octave
	} else if (type == GainAutomation || type == TrimAutomation) {
		/* dB_coeff_step gives a step normalized for [0, max_gain].  This is
		   like "slider position", so we convert from "slider position" to gain
		   to have the correct unit here. */
		largestep = position_to_gain (dB_coeff_step(upper));
		step      = position_to_gain (largestep / 10.0);
		smallstep = step;
	} else {
		/* note that LV2Plugin::get_parameter_descriptor ()
		 * overrides this is lv2:rangeStep is set for a port.
		 */
		const float delta = upper - lower;

		/* 30 happens to be the total number of steps for a fader with default
		   max gain of 2.0 (6 dB), so we use 30 here too for consistency. */
		step      = smallstep = (delta / 300.0f);
		largestep = (delta / 30.0f);

		if (logarithmic) {
			/* Steps are linear, but we map them with pow like values (in
			   internal_to_interface).  Thus, they are applied exponentially,
			   which means too few steps.  So, divide to get roughly the
			   desired number of steps (30).  This is not mathematically
			   precise but seems to be about right for the controls I tried.
			   If you're reading this, you've probably found a case where that
			   isn't true, and somebody needs to sit down with a piece of paper
			   and actually do the math. */
			smallstep = smallstep / logf(30.0f);
			step      = step      / logf(30.0f);
			largestep = largestep / logf(30.0f);
		} else if (integer_step) {
			smallstep = 1.0;
			step      = std::max(1.f, rintf (step));
			largestep = std::max(1.f, rintf (largestep));
		}
	}
}

std::string
ParameterDescriptor::midi_note_name (const uint8_t b, bool translate)
{
	char buf[16];
	if (b > 127) {
		snprintf(buf, sizeof(buf), "%d", b);
		return buf;
	}

	static const char* en_notes[] = {
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};

	static const char* notes[] = {
		S_("Note|C"),
		S_("Note|C#"),
		S_("Note|D"),
		S_("Note|D#"),
		S_("Note|E"),
		S_("Note|F"),
		S_("Note|F#"),
		S_("Note|G"),
		S_("Note|G#"),
		S_("Note|A"),
		S_("Note|A#"),
		S_("Note|B")
	};

	/* MIDI note 0 is in octave -1 (in scientific pitch notation) */
	const int octave = b / 12 - 1;
	const size_t p = b % 12;
	snprintf (buf, sizeof (buf), "%s%d", translate ? notes[p] : en_notes[p], octave);
	return buf;
}

std::string
ParameterDescriptor::normalize_note_name(const std::string& name)
{
	// Remove whitespaces and convert to lower case for a more resilient parser
	return boost::to_lower_copy(boost::erase_all_copy(name, " "));
};

ParameterDescriptor::NameNumMap
ParameterDescriptor::build_midi_name2num()
{
	NameNumMap name2num;
	for (uint8_t num = 0; num < 128; num++) {
		name2num[normalize_note_name(midi_note_name(num))] = num;
	}
	return name2num;
}

uint8_t
ParameterDescriptor::midi_note_num (const std::string& name)
{
	static NameNumMap name2num = build_midi_name2num();

	uint8_t num = -1;			// -1 (or 255) is returned in case of failure

	NameNumMap::const_iterator it = name2num.find(normalize_note_name(name));
	if (it != name2num.end())
		num = it->second;

	return num;
}

float
ParameterDescriptor::to_interface (float val) const
{
	val = std::min (upper, std::max (lower, val));
	switch(type) {
		case GainAutomation:
		case BusSendLevel:
			val = gain_to_slider_position_with_max (val, upper);
			break;
		case TrimAutomation:
			{
				const float lower_db = accurate_coefficient_to_dB (lower);
				const float range_db = accurate_coefficient_to_dB (upper) - lower_db;
				val = (accurate_coefficient_to_dB (val) - lower_db) / range_db;
			}
			break;
		case PanAzimuthAutomation:
		case PanElevationAutomation:
			val = 1.f - val;
			break;
		case PanWidthAutomation:
			val = .5f + val * .5f;
			break;
		default:
			if (logarithmic) {
				if (rangesteps > 1) {
					val = logscale_to_position_with_steps (val, lower, upper, rangesteps);
				} else {
					val = logscale_to_position (val, lower, upper);
				}
			} else if (toggled) {
				return (val - lower) / (upper - lower) >= 0.5f ? 1.f : 0.f;
			} else if (integer_step) {
				/* evenly-divide steps. lower,upper inclusive
				 * e.g. 5 integers 0,1,2,3,4 are mapped to a fader
				 * [0.0 ... 0.2 | 0.2 ... 0.4 | 0.4 ... 0.6 | 0.6 ... 0.8 | 0.8 ... 1.0]
				 *       0             1             2             3             4
				 *      0.1           0.3           0.5           0.7           0.9
				 */
				val = (val + .5f - lower) / (1.f + upper - lower);
			} else {
				val = (val - lower) / (upper - lower);
			}
			break;
	}
	val = std::max (0.f, std::min (1.f, val));
	return val;
}

float
ParameterDescriptor::from_interface (float val) const
{
	assert (val >= 0 && val <= 1.f);
	val = std::max (0.f, std::min (1.f, val));

	switch(type) {
		case GainAutomation:
		case BusSendLevel:
			val = slider_position_to_gain_with_max (val, upper);
			break;
		case TrimAutomation:
			{
				const float lower_db = accurate_coefficient_to_dB (lower);
				const float range_db = accurate_coefficient_to_dB (upper) - lower_db;
				val = dB_to_coefficient (lower_db + val * range_db);
			}
			break;
		case PanAzimuthAutomation:
		case PanElevationAutomation:
			 val = 1.f - val;
			break;
		case PanWidthAutomation:
			val = 2.f * val - 1.f;
			break;
		default:
			if (logarithmic) {
				assert (!toggled && !integer_step); // update_steps() should prevent that.
				if (rangesteps > 1) {
					val = position_to_logscale_with_steps (val, lower, upper, rangesteps);
				} else {
					val = position_to_logscale (val, lower, upper);
				}
			} else if (toggled) {
				val = val > 0 ? upper : lower;
			} else if (integer_step) {
				/* upper and lower are inclusive. use evenly-divided steps
				 * e.g. 5 integers 0,1,2,3,4 are mapped to a fader
				 * [0.0 .. 0.2 | 0.2 .. 0.4 | 0.4 .. 0.6 | 0.6 .. 0.8 | 0.8 .. 1.0]
				 */
				val =  round (lower + val * (1.f + upper - lower) - .5f);
			} else if (rangesteps > 1) {
				/* similar to above, but for float controls */
				val = floor (val * (rangesteps - 1.f)) / (rangesteps - 1.f); // XXX
				val = val * (upper - lower) + lower;
			} else {
				val = val * (upper - lower) + lower;
			}
			break;
	}
	val = std::min (upper, std::max (lower, val));
	return val;
}

} // namespace ARDOUR
