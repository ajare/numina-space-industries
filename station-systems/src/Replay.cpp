#include "station/Replay.h"
#include "station/StationSystemsRuntime.h"
#include "serialization/YamlSerializer.h"

namespace station
{
	std::string serializeStationReplay(StationSystemsRuntime const& runtime)
	{
		auto output = serialization::YamlSerializer::toString();
		output->beginMap("stationReplay");
		output->writeUint32("schemaVersion", kStationSchemaVersion);
		auto const snapshot = runtime.snapshot();
		output->writeFloat("baseStepSeconds", static_cast<float>(
			snapshot.systemsTimestepSeconds / snapshot.systemsStepWorldTicks));
		writeStationSystemsSettingsMap(*output, runtime.effectiveSettings());
		runtime.writeFaultReplay(*output);
		output->endMap();
		output->serialize();
		return output->getSerializedString();
	}

	bool parseStationReplay(std::string const& text,
		std::unique_ptr<StationSystemsRuntime>& runtime,
		std::string* diagnostic, StationSystemsSource source)
	{
		try
		{
			auto input = serialization::YamlSerializer::fromString(text);
			input->deserialize();
			input->beginMap("stationReplay");
			auto const version = input->readUint32("schemaVersion");
			if (version < 65 || version > kStationSchemaVersion)
				throw serialization::SerializationException("Unsupported Station replay schemaVersion");
			auto const quantum = input->readFloat("baseStepSeconds");
			StationSystemsSettings settings;
			readStationSystemsSettingsMap(*input, version, settings);
			auto candidate = StationSystemsRuntime::readFaultReplay(*input,
				settings, quantum, version, std::move(source));
			input->endMap();
			runtime = std::move(candidate);
			if (diagnostic) diagnostic->clear();
			return true;
		}
		catch (std::exception const& error)
		{
			if (diagnostic) *diagnostic = error.what();
			return false;
		}
	}
}
