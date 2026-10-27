#include "station/Persistence.h"
#include "serialization/YamlSerializer.h"

namespace station
{
	std::string serializeStationConfiguration(StationSystemsSettings const& settings)
	{
		std::string diagnostic;
		if (!validateStationSystemsSettings(settings, diagnostic))
			throw serialization::SerializationException(diagnostic);
		auto output = serialization::YamlSerializer::toString();
		output->beginMap("stationConfiguration");
		output->writeUint32("schemaVersion", stationConfigurationSchemaVersion(settings));
		writeStationSystemsSettingsMap(*output, settings);
		output->endMap();
		output->serialize();
		return output->getSerializedString();
	}

	bool parseStationConfiguration(std::string const& text, StationSystemsSettings& settings,
		std::string* diagnostic)
	{
		try
		{
			auto input = serialization::YamlSerializer::fromString(text);
			input->deserialize();
			if (!input->hasField("stationSystems"))
				throw serialization::SerializationException("Missing stationSystems map");
			input->beginMap("stationConfiguration");
			auto const version = input->readUint32("schemaVersion");
			if (version < 65 || version > kStationSchemaVersion)
				throw serialization::SerializationException("Unsupported Station configuration schemaVersion");
			StationSystemsSettings candidate;
			readStationSystemsSettingsMap(*input, version, candidate);
			input->endMap();
			settings = std::move(candidate);
			if (diagnostic) diagnostic->clear();
			return true;
		}
		catch (serialization::SerializationException const& error)
		{
			if (diagnostic) *diagnostic = error.what();
			return false;
		}
	}
}
