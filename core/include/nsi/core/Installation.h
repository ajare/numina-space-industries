#pragma once

#include <string>
#include <utility>

namespace nsi::core {

class Installation {
public:
    [[nodiscard]] const std::string& getName() const noexcept
    {
        return name_;
    }

    void setName(std::string name)
    {
        name_ = std::move(name);
    }

    [[nodiscard]] std::string serialize() const;
    // Throws serialization::SerializationException on invalid input.
    // The current name is unchanged if deserialization fails.
    void deserialize(const std::string& yaml);

private:
    std::string name_;
};

} // namespace nsi::core
