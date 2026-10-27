#pragma once
#include "station/Configuration.h"
#include <string>
#include <vector>

// Shared connection identity and semantics. The solver uses this catalogue to
// bind allocation budgets; graph layout remains strictly editor-only.
namespace station::station_connections
{
	enum class LinkKind { Material, Power, Heat, Control };
	enum class Medium { Unspecified, Water, Oxygen, CarbonDioxide, Hydrogen, Methane,
		Carbon, Food, OrganicWaste, Urine, Nutrients, Consumables, Fuel, Atmosphere,
		Heat, Cooling, Electricity, Control, Light };
	struct Node
	{
		std::string type{}, id{}, detail{};
		int lane{};
		bool unresolved{};
	};
	struct Link
	{
		size_t from{}, to{};
		std::string label{};
		LinkKind kind{LinkKind::Material};
		Medium medium{Medium::Unspecified};
		station::StationConnectionKey designKey{};
		bool scheduledTransfer{false};
	};
	struct Graph { std::vector<Node> nodes; std::vector<Link> links; };
	Graph build(station::StationSystemsSettings const& draft);
	station::StationPhysicalConnection automaticConnectionType(Graph const&, Link const&);
	station::StationConnectionProperties connectionProperties(station::StationSystemsSettings const&, Link const&);
	station::StationPhysicalConnection connectionType(Graph const&, Link const&, station::StationSystemsSettings const&);
	std::string connectionThroughputUnit(Graph const&, Link const&, station::StationSystemsSettings const&);
}
