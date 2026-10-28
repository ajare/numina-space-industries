#include <nsi/core/Installation.h>
#include <serialization/SerializationException.h>
#include <serialization/YamlSerializer.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition)
{
    if (!condition) {
        throw std::runtime_error("Installation serialization contract failed");
    }
}
}

int main()
{
    try {
        for (const std::string name : {"", "Alpha Station", "null", "true",
                 "Station: #1\nDeck \\\"A\\\"", "Station Ω"}) {
            nsi::core::Installation original;
            original.setName(name);
            const auto yaml = original.serialize();
            auto reader = serialization::YamlSerializer::fromString(yaml);
            reader->deserialize();
            reader->beginMap("");
            require(reader->readString("name") == name);
            reader->endMap();

            nsi::core::Installation restored;
            restored.setName("old name");
            restored.deserialize(yaml);
            require(restored.getName() == name);
        }

        nsi::core::Installation installation;
        installation.deserialize("name: null");
        require(installation.getName() == "null"); // Serializer string conversion.
        installation.deserialize("name: External\nextra: ignored\n");
        require(installation.getName() == "External");
        for (const std::string invalid : {"", "[", "{}",
                 "name: []", "name: {}", "- name: Station", "scalar"}) {
            bool rejected = false;
            try {
                installation.deserialize(invalid);
            } catch (const serialization::SerializationException&) {
                rejected = true;
            }
            if (!rejected) {
                throw std::runtime_error("Accepted invalid YAML: " + invalid);
            }
            require(installation.getName() == "External");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
