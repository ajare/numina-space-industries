#include "station/editor/DocumentIO.h"
#include <serialization/YamlSerializer.h>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace station::editor {
namespace {
// Use the shared serializer's transactional file replacement rather than
// truncating the user's document before a write has succeeded.
void emit(serialization::YamlSerializer& out, const std::string& key, const YAML::Node& node) {
    if (node.IsMap()) {
        out.beginMap(key);
        for (const auto& entry:node) emit(out,entry.first.as<std::string>(),entry.second);
        out.endMap();
    } else if (node.IsSequence()) {
        out.beginArray(key);
        for (const auto& entry:node) emit(out,"",entry);
        out.endArray();
    } else out.writeQuotedString(key,node.as<std::string>());
}
}
Installation YamlDocumentFiles::read(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    if (!input) throw std::runtime_error("Could not open file: "+path.string());
    std::string text{std::istreambuf_iterator<char>(input),{}};
    if (input.bad()) throw std::runtime_error("Could not read file: "+path.string());
    Installation installation;
    installation.deserialize(text);
    return installation;
}
void YamlDocumentFiles::write(const std::filesystem::path& path,const Installation& installation) {
    auto output=serialization::YamlSerializer::toFile(path.string());
    emit(*output,"",YAML::Load(installation.serialize()));
    output->serialize();
}
} // namespace station::editor
