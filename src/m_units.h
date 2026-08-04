//------------------------------------------------------------------------
//  REAL-WORLD MEASUREMENT UNITS
//------------------------------------------------------------------------

#ifndef __EUREKA_M_UNITS_H__
#define __EUREKA_M_UNITS_H__

#include "m_strings.h"

namespace units
{

enum class System
{
	off = 0,
	metric = 1,
	imperial = 2
};

//  Formats a length given in Doom map units as a real-world measurement,
//  auto-scaling the unit for readability. `unitsPerMeter` is the map's
//  conversion scale (classic Doom convention: 32 map units ~ 1 meter, so a
//  56-unit player is ~1.75 m tall). Examples at 32 units/m:
//    metric:   "85 cm", "16.0 m", "0.72 km"
//    imperial: "10 in", "52.5 ft", "0.82 mi"
//  Returns an empty string when the system is off or the scale is invalid,
//  so callers can simply append the result unconditionally.
SString FormatLength(double mapUnits, int unitsPerMeter, System sys);

//  Convenience wrapper reading the live configuration
//  (config::measure_system / config::measure_units_per_meter).
SString FormatLength(double mapUnits);

//  True when measurement readouts are enabled and usable.
bool Enabled() noexcept;

} // namespace units

#endif  /* __EUREKA_M_UNITS_H__ */
