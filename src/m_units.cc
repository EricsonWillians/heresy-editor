//------------------------------------------------------------------------
//  REAL-WORLD MEASUREMENT UNITS
//------------------------------------------------------------------------

#include "m_units.h"

#include "m_config.h"

#include <cmath>

namespace config
{

int measure_system = 1;  // units::System::metric
int measure_units_per_meter = 32;  // classic Doom convention

} // namespace config

namespace units
{

static constexpr double kFeetPerMeter = 3.28084;
static constexpr double kInchesPerFoot = 12.0;
static constexpr double kFeetPerMile = 5280.0;

SString FormatLength(double mapUnits, int unitsPerMeter, System sys)
{
	if (sys == System::off || unitsPerMeter <= 0)
		return {};

	const double meters = mapUnits / unitsPerMeter;

	if (sys == System::metric)
	{
		const double absMeters = std::fabs(meters);

		if (absMeters < 1.0)
			return SString::printf("%.0f cm", meters * 100.0);
		if (absMeters < 1000.0)
			return SString::printf("%.1f m", meters);
		return SString::printf("%.2f km", meters / 1000.0);
	}

	// imperial
	const double feet = meters * kFeetPerMeter;
	const double absFeet = std::fabs(feet);

	if (absFeet < 1.0)
		return SString::printf("%.0f in", feet * kInchesPerFoot);
	if (absFeet < kFeetPerMile)
		return SString::printf("%.1f ft", feet);
	return SString::printf("%.2f mi", feet / kFeetPerMile);
}

bool Enabled() noexcept
{
	return config::measure_system != static_cast<int>(System::off) &&
			config::measure_units_per_meter > 0;
}

SString FormatLength(double mapUnits)
{
	return FormatLength(mapUnits, config::measure_units_per_meter,
			static_cast<System>(config::measure_system));
}

} // namespace units
