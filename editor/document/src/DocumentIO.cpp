#include "station/editor/DocumentIO.h"

#include "serialization/YamlSerializer.h"

#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace station::editor {
namespace {

void require(bool condition, const std::string& diagnostic) {
    if (!condition) throw std::runtime_error(diagnostic);
}

void requireMap(const YAML::Node& node, const std::string& location) {
    require(node.IsMap(), location + " must be a map");
    std::set<std::string> keys;
    for (const auto& item : node) {
        require(item.first.IsScalar(), location + " has a non-scalar key");
        const auto key = item.first.as<std::string>();
        require(keys.insert(key).second, location + " has duplicate field: " + key);
    }
}

void requireNumber(const YAML::Node& node, unsigned expected,
                   const std::string& diagnostic) {
    require(node.IsScalar(), diagnostic);
    try {
        require(node.as<unsigned>() == expected, diagnostic);
    } catch (const YAML::Exception&) {
        throw std::runtime_error(diagnostic);
    }
}

void requireSupported(const SpaceStation& station) {
    require(station.deckCount() == 1, "Expected exactly one Deck");
    require(station.deck(0).width() == 16 && station.deck(0).height() == 16,
            "Expected a 16x16 Deck");
}

}  // namespace

SpaceStation YamlDocumentFiles::read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.is_open(), "Could not open file for reading: " + path.string());
    const std::string text{std::istreambuf_iterator<char>(input), {}};
    require(!input.bad(), "Could not read file: " + path.string());
    // LoadAll prevents accepting a valid first document and silently ignoring
    // trailing YAML. Inspect node types and duplicates before conversion.
    const auto documents = YAML::LoadAll(text);
    require(documents.size() == 1, "Expected exactly one YAML document");
    const auto root = documents.front();
    requireMap(root, "Station document");
    requireNumber(root["version"], 1, "version must be the supported schema version 1");
    const auto decks = root["decks"];
    require(decks.IsSequence() && decks.size() == 1,
            "decks must contain exactly one Deck");
    const auto deck = decks[0];
    requireMap(deck, "decks[0]");
    requireNumber(deck["width"], 16, "decks[0].width must be 16");
    requireNumber(deck["height"], 16, "decks[0].height must be 16");
    const auto rows = deck["rows"];
    require(rows.IsSequence() && rows.size() == 16,
            "decks[0].rows must contain exactly 16 row strings");
    Deck loaded(16, 16, CellState::Void);
    for (std::uint32_t y = 0; y < 16; ++y) {
        const auto location = "decks[0].rows[" + std::to_string(y) + "]";
        require(rows[y].IsScalar(), location + " must be a row string");
        const auto row = rows[y].as<std::string>();
        require(row.size() == 16, location + " must contain exactly 16 symbols");
        for (std::uint32_t x = 0; x < 16; ++x) {
            require(row[x] == '#' || row[x] == '.',
                    location + " contains an invalid symbol at column " + std::to_string(x));
            loaded.setCell(x, y, row[x] == '#' ? CellState::Filled : CellState::Void);
        }
    }
    return SpaceStation(std::vector<Deck>{std::move(loaded)});
}

void YamlDocumentFiles::write(const std::filesystem::path& path,
                             const SpaceStation& station) {
    requireSupported(station);
    // Reuse the existing serializer and its transactional file writer. The
    // YAML emitter quotes comment-like strings rather than emitting raw rows.
    auto output = serialization::YamlSerializer::toFile(path.string());
    output->beginMap("");
    output->writeUint32("version", 1);
    output->beginArray("decks");
    output->beginMap("");
    output->writeUint32("width", 16);
    output->writeUint32("height", 16);
    output->beginArray("rows");
    for (std::uint32_t y = 0; y < 16; ++y) {
        std::string row;
        for (std::uint32_t x = 0; x < 16; ++x) {
            row += station.deck(0).cell(x, y) == CellState::Filled ? '#' : '.';
        }
        output->writeQuotedString("", row);
    }
    output->endArray();
    output->endMap();
    output->endArray();
    output->endMap();
    output->serialize();
}

}  // namespace station::editor
