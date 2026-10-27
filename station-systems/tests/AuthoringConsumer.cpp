#include "PrefabScenarios.h"
#include "PortScenarios.h"
#include <filesystem>
#include <iostream>

struct Context
{
 std::filesystem::path fixture(char const* path) const { return std::filesystem::path(STATION_FIXTURE_ROOT) / path; }
};
int main()
{
 try
 {
 Context ctx;
 port_scenarios::linkSeam(ctx);
 port_scenarios::growablePorts(ctx);
 prefab_scenarios::prefabTemplateParse(ctx);
 prefab_scenarios::prefabTemplateRefusals(ctx);
 prefab_scenarios::prefabExpansion(ctx);
 prefab_scenarios::prefabOverrides(ctx);
 prefab_scenarios::prefabDialEdit(ctx, [](auto const&, auto const&) {});
 prefab_scenarios::prefabCapRefusal(ctx);
 prefab_scenarios::prefabCapRefusalAllCappedTypes(ctx);
 prefab_scenarios::prefabDeterminism(ctx);
 prefab_scenarios::prefabExposedPinResolution(ctx);
 prefab_scenarios::prefabLinkThroughComposite(ctx);
 prefab_scenarios::prefabInstanceRenameCascade(ctx);
 prefab_scenarios::prefabInstanceRenameRefusals(ctx);
 prefab_scenarios::prefabInstanceCascadeDelete(ctx);
 prefab_scenarios::prefabTemplateLoadValidation(ctx);
 prefab_scenarios::prefabCatalogueParse(ctx);
 prefab_scenarios::prefabCatalogueRefusals(ctx);
 prefab_scenarios::prefabInstanceAllocation(ctx);
 prefab_scenarios::prefabEntryOrderIsFixed(ctx);
 }
 catch (std::exception const& e) { std::cerr << e.what() << "\n"; return 1; }
}
