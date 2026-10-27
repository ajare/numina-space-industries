#pragma once

#include "station/StationSystems.h"
#include <memory>
#include <string>

namespace station
{
	class StationSystemsRuntime;

	// Self-contained YAML: effective settings, base quantum and the existing
	// command journal/elapsed-step codec. Clients own storage. Lua source is
	// deliberately not persisted; the caller selects it at reconstruction.
	std::string serializeStationReplay(StationSystemsRuntime const& runtime);
	// Transactional: malformed data, refused commands or source/drive failures
	// return false with a diagnostic and leave the destination unchanged.
	// Monitor observations restart on load, as with embedded World replay.
	bool parseStationReplay(std::string const& text,
		std::unique_ptr<StationSystemsRuntime>& runtime,
		std::string* diagnostic = nullptr,
		StationSystemsSource source = std::nullopt);
}
