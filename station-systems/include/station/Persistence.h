#pragma once

#include "station/Configuration.h"
#include <cstdint>
#include <string>

namespace serialization { class Serializer; }

namespace station
{
	// Historical shared Station/World wire versions, not a separate format.
	inline constexpr std::uint32_t kStationSchemaVersion = 139;
	std::uint32_t stationConfigurationSchemaVersion(StationSystemsSettings const& settings);

	// Embed the existing stationSystems child map in the current scope.
	// Reading is transactional; malformed input throws SerializationException.
	// Clients loading drafts may defer physical validation explicitly.
	void writeStationSystemsSettingsMap(serialization::Serializer&, StationSystemsSettings const&);
	void readStationSystemsSettingsMap(serialization::Serializer&, std::uint32_t version,
		StationSystemsSettings&, bool validate = true);

	// YAML text, with schemaVersion and stationSystems at the document root.
	// No storage or presentation metadata is owned here. Parsing validates a
	// complete configuration and leaves the destination unchanged on refusal.
	// Serialization throws on invalid settings; parsing returns a diagnostic.
	std::string serializeStationConfiguration(StationSystemsSettings const& settings);
	bool parseStationConfiguration(std::string const& text, StationSystemsSettings& settings,
		std::string* diagnostic = nullptr);
}
