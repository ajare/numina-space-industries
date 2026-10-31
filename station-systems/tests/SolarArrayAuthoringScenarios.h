#pragma once
// #60: authoring and persistence checks through the public library only.
#include "station/Configuration.h"
#include "station/Persistence.h"

#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

namespace station_test::solar {

inline void require(bool condition, char const* message)
{
	if (!condition) throw std::runtime_error(message);
}

inline station::StationSystemsSettings oneArraySettings(station::SolarArraySettings const& array)
{
	station::StationSystemsSettings settings;
	settings.enabled = true;
	settings.solarArrays = {array};
	return settings;
}

inline station::SolarArraySettings plainArray()
{
	station::SolarArraySettings array;
	array.id = "sun";
	array.capacityKW = 20.0;
	return array;
}

inline void catalogue()
{
	struct Expected
	{
		station::SolarCellTechnology technology;
		char const* name;
		double efficiency;
		double packingFactor;
	};
	constexpr Expected expected[]{
		{station::SolarCellTechnology::TripleJunctionGaAs, "triple-junction-gaas", 0.32, 0.85},
		{station::SolarCellTechnology::SingleJunctionGaAs, "single-junction-gaas", 0.19, 0.85},
		{station::SolarCellTechnology::SpaceSilicon, "space-silicon", 0.14, 0.80},
	};
	require(station::SolarCellTechnologyNames.size() == std::size(expected),
		"cell technology catalogue is not the three decided entries");
	for (auto const& entry : expected)
	{
		require(std::string(station::SolarCellTechnologyNames.at(static_cast<size_t>(entry.technology)))
			== entry.name, "cell technology name does not match its catalogue slot");
		auto const spec = station::solarCellSpec(entry.technology);
		require(spec.efficiency == entry.efficiency, "incorrect cell technology conversion efficiency");
		require(spec.packingFactor == entry.packingFactor, "incorrect cell technology packing factor");
	}
}

inline void unknownTechnologyRefused()
{
	for (auto const value : {3, 99, 255})
	{
		auto bad = plainArray();
		bad.cellTechnology = static_cast<station::SolarCellTechnology>(value);
		bool refused = false;
		try { (void)station::solarCellSpec(bad.cellTechnology); }
		catch (std::invalid_argument const&) { refused = true; }
		require(refused, "unknown cell technology was not refused at selection");
		std::string diagnostic;
		require(!station::validateStationSystemsSettings(oneArraySettings(bad), diagnostic)
			&& diagnostic.find("cell technology") != std::string::npos
			&& diagnostic.find("sun") != std::string::npos,
			"unknown cell technology validation did not name the array and field");
	}
}

inline void settingsCarryTheFields()
{
	auto array = plainArray();
	require(!array.areaM2 && array.enabled
		&& array.cellTechnology == station::SolarCellTechnology::TripleJunctionGaAs,
		"legacy array defaults changed");
	array.areaM2 = 61.5;
	array.cellTechnology = station::SolarCellTechnology::SpaceSilicon;
	array.enabled = false;
	auto const settings = oneArraySettings(array);
	std::string diagnostic;
	auto const valid = station::validateStationSystemsSettings(settings, diagnostic);
	require(valid, diagnostic.c_str());
	require(settings.solarArrays.front() == array, "settings lost authored solar array fields");
}

inline void blanketAreaBounds()
{
	std::string diagnostic;
	for (double value : {-1.0, -0.001, std::numeric_limits<double>::infinity(),
			-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
	{
		auto bad = plainArray();
		bad.areaM2 = value;
		require(!station::validateStationSystemsSettings(oneArraySettings(bad), diagnostic),
			"invalid blanket area accepted");
		require(diagnostic.find("blanket area") != std::string::npos
			&& diagnostic.find("sun") != std::string::npos,
			"blanket area refusal did not name the array and the field");
		bool refused = false;
		try { (void)station::serializeStationConfiguration(oneArraySettings(bad)); }
		catch (std::exception const&) { refused = true; }
		require(refused, "invalid blanket area serialized");
	}
	for (auto const area : {std::optional<double>{}, std::optional<double>{0.0},
			std::optional<double>{61.5}})
	{
		auto array = plainArray();
		array.areaM2 = area;
		require(station::validateStationSystemsSettings(oneArraySettings(array), diagnostic),
			"absent, zero or positive blanket area refused");
	}
}

template<class Backend>
std::string embeddedDocument(station::StationSystemsSettings const& settings)
{
	auto output = Backend::toString();
	output->beginMap("clientDocument");
	output->writeUint32("schemaVersion", station::stationConfigurationSchemaVersion(settings));
	station::writeStationSystemsSettingsMap(*output, settings);
	output->endMap();
	output->serialize();
	return output->getSerializedString();
}

// All combinations, including absent versus zero area and mixed legacy/new arrays.
template<class Backend>
void roundTrip()
{
	for (auto const area : {std::optional<double>{}, std::optional<double>{0.0},
			std::optional<double>{61.5}})
	for (auto const technology : {station::SolarCellTechnology::TripleJunctionGaAs,
			station::SolarCellTechnology::SingleJunctionGaAs, station::SolarCellTechnology::SpaceSilicon})
	for (bool enabled : {false, true})
	{
		auto array = plainArray();
		array.areaM2 = area;
		array.cellTechnology = technology;
		array.enabled = enabled;
		auto settings = oneArraySettings(array);
		auto legacy = plainArray();
		legacy.id = "legacy";
		settings.solarArrays.push_back(legacy);
		auto input = Backend::fromString(embeddedDocument<Backend>(settings));
		input->deserialize();
		input->beginMap("clientDocument");
		auto const version = input->readUint32("schemaVersion");
		station::StationSystemsSettings restored;
		station::readStationSystemsSettingsMap(*input, version, restored);
		input->endMap();
		require(restored == settings, "embedded round trip changed solar array settings");
		std::string diagnostic;
		auto const text = station::serializeStationConfiguration(settings);
		auto const parsed = station::parseStationConfiguration(text, restored, &diagnostic);
		require(parsed, diagnostic.c_str());
		require(restored == settings, "standalone round trip changed solar array settings");
	}
}

#ifdef STATION_FIXTURE_ROOT
// Frozen output from the pre-#60 codec, not output generated by the codec under test.
template<class Backend>
void featureAbsentKeepsItsBytes(char const* extension)
{
	auto const settings = oneArraySettings(plainArray());
	require(station::stationConfigurationSchemaVersion(settings) == 137,
		"feature-absent solar array document changed its schema stamp");
	std::ifstream file(std::string(STATION_FIXTURE_ROOT)
		+ "/station-systems/tests/fixtures/legacy-solar-array." + extension, std::ios::binary);
	require(file.is_open(), "missing pre-#60 solar array fixture");
	std::string const original{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
	auto input = Backend::fromString(original);
	input->deserialize();
	input->beginMap("clientDocument");
	auto const version = input->readUint32("schemaVersion");
	station::StationSystemsSettings restored;
	station::readStationSystemsSettingsMap(*input, version, restored);
	input->endMap();
	require(restored == settings, "legacy fixture did not receive compatible defaults");
	require(embeddedDocument<Backend>(restored) == original,
		"feature-absent solar array document changed bytes");
}
#endif

inline void newTierOnlyWhenPresent()
{
	auto const check = [](station::SolarArraySettings const& array, std::uint32_t version) {
		auto const settings = oneArraySettings(array);
		require(station::stationConfigurationSchemaVersion(settings) == version,
			"schema tier does not match fields actually emitted");
		auto const text = station::serializeStationConfiguration(settings);
		require(text.find("schemaVersion: " + std::to_string(version)) != std::string::npos,
			"serialized schema tier does not match selected tier");
		require((text.find("areaM2:") != std::string::npos) == array.areaM2.has_value(),
			"blanket area emission does not match presence");
		require((text.find("cellTechnology:") != std::string::npos)
			== (array.cellTechnology != station::SolarCellTechnology::TripleJunctionGaAs),
			"default technology must be omitted; non-default technology must be emitted");
		// The root systems map has its own enabled field; inspect only the array entry.
		auto const entry = text.substr(text.find("solarArrays:"));
		require((entry.find("enabled:") != std::string::npos) == !array.enabled,
			"default enabled flag must be omitted; disabled flag must be emitted");
	};
	check(plainArray(), 137);
	check([](auto a) { a.areaM2 = 0.0; return a; }(plainArray()), 141);
	check([](auto a) { a.areaM2 = 61.5; return a; }(plainArray()), 141);
	check([](auto a) {
		a.cellTechnology = station::SolarCellTechnology::SingleJunctionGaAs;
		return a;
	}(plainArray()), 141);
	check([](auto a) { a.enabled = false; return a; }(plainArray()), 141);
}

inline void invalidFieldsRefused()
{
	auto const document = [](char const* field, char const* version = "141") {
		auto text = station::serializeStationConfiguration(oneArraySettings(plainArray()));
		text.replace(text.find("137"), 3, version);
		text += std::string("\n      ") + field;
		return text;
	};
	// Test each schema gate separately, even for an explicitly default value.
	for (auto const field : {"areaM2: 0", "cellTechnology: triple-junction-gaas", "enabled: true"})
	{
		station::StationSystemsSettings restored;
		std::string diagnostic;
		auto const parsed = station::parseStationConfiguration(document(field), restored, &diagnostic);
		require(parsed, diagnostic.c_str());
		auto const original = restored;
		require(!station::parseStationConfiguration(document(field, "140"), restored, &diagnostic),
			"a pre-141 schema context accepted new solar array authoring fields");
		require(diagnostic.find("schema 141") != std::string::npos && restored == original,
			"schema refusal mutated settings or lost the required tier diagnostic");
	}
	for (auto const field : {"cellTechnology: unobtainium", "areaM2: -1", "areaM2: .nan",
			"areaM2: .inf", "areaM2: -.inf"})
	{
		auto const text = document(field);
		auto restored = oneArraySettings(plainArray());
		auto const original = restored;
		std::string diagnostic;
		require(!station::parseStationConfiguration(text, restored, &diagnostic),
			"invalid solar array authoring field was accepted");
		auto const expected = std::string(field).starts_with("cellTechnology")
			? "cell technology" : "blanket area";
		require(diagnostic.find(expected) != std::string::npos
			&& diagnostic.find("sun") != std::string::npos && restored == original,
			"invalid field refusal mutated settings or failed to identify the array and field");
	}
}

} // namespace station_test::solar
