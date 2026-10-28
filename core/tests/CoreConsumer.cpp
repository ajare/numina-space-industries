#include <nsi/core/Installation.h>
#include <nsi/core/Core.h>

#include <concepts>

namespace core = nsi::core;
static_assert(std::same_as<int, int>); // C++20 is supplied by nsi::core.

int main()
{
    core::Installation installation;
    if (!installation.getName().empty()) {
        return 1;
    }

    installation.setName("Alpha Station");
    const auto& constInstallation = installation;
    if (constInstallation.getName() != "Alpha Station") {
        return 2;
    }

    std::string name = "Beta Station";
    installation.setName(name);
    name = "Changed externally";
    if (installation.getName() != "Beta Station") {
        return 3;
    }

    installation.setName("");
    if (!installation.getName().empty()) {
        return 4;
    }

    return 0;
}
