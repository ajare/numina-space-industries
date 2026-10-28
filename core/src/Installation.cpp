#include <nsi/core/Installation.h>

#include <serialization/YamlSerializer.h>

namespace nsi::core {

std::string Installation::serialize() const
{
    auto output = serialization::YamlSerializer::toString();
    output->beginMap("");
    output->writeQuotedString("name", name_);
    output->endMap();
    output->serialize();
    return output->getSerializedString();
}

void Installation::deserialize(const std::string& yaml)
{
    auto input = serialization::YamlSerializer::fromString(yaml);
    input->deserialize();
    input->beginMap("");
    auto name = input->readString("name");
    input->endMap();
    name_ = std::move(name);
}

} // namespace nsi::core
