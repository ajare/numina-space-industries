#include "station/Persistence.h"
#include "serialization/BinarySerializer.h"
#include "serialization/YamlSerializer.h"
#include "ConfigurationScenarios.h"
#include "SolarArrayAuthoringScenarios.h"
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, char const* message)
{
    if (!condition) throw std::runtime_error(message);
}
template<class Backend>
void checkEmbedding()
{
    auto const settings = station_test::pathwaySettings();
    auto output = Backend::toString();
    output->beginMap("clientDocument");
    output->writeUint32("schemaVersion", station::kStationSchemaVersion);
    station::writeStationSystemsSettingsMap(*output, settings);
    output->writeString("clientMetadata", "kept outside Station");
    output->endMap();
    output->serialize();
    auto input = Backend::fromString(output->getSerializedString());
    input->deserialize();
    input->beginMap("clientDocument");
    auto const version = input->readUint32("schemaVersion");
    station::StationSystemsSettings restored;
    station::readStationSystemsSettingsMap(*input, version, restored);
    require(restored == settings, "embedded round trip changed settings");
    require(input->readString("clientMetadata") == "kept outside Station", "client metadata lost");
    input->endMap();
}
}
int main()
{
    try {
        checkEmbedding<serialization::YamlSerializer>();
        checkEmbedding<serialization::BinarySerializer>();
        station_test::solar::roundTrip<serialization::YamlSerializer>();
        station_test::solar::roundTrip<serialization::BinarySerializer>();
        station_test::solar::featureAbsentKeepsItsBytes<serialization::YamlSerializer>("yaml");
        station_test::solar::featureAbsentKeepsItsBytes<serialization::BinarySerializer>("bin");
        station_test::solar::newTierOnlyWhenPresent();
        station_test::solar::invalidFieldsRefused();
    }
    catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
