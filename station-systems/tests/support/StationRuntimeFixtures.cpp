#include "StationRuntimeFixtures.h"
#include "station/Persistence.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
namespace station_test {
ReferenceFixture const& bundledStationReference()
{
    static auto const fixture = [] {
        auto const path = std::filesystem::path(PF_SMOKE_SHARED_SOURCE_ROOT)
            / "resources/station-systems/reference-station.yaml";
        std::ifstream file(path, std::ios::binary);
        if (!file) throw std::runtime_error("Missing Station fixture: " + path.string());
        std::string text{std::istreambuf_iterator<char>(file), {}};
        if (file.bad()) throw std::runtime_error("Could not read Station fixture: " + path.string());
        ReferenceFixture parsed;
        std::string diagnostic;
        if (!station::parseStationConfiguration(text, parsed.settings, &diagnostic))
            throw std::runtime_error(diagnostic);
        return parsed;
    }();
    return fixture;
}
}
