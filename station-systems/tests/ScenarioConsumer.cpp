#include "Smoke.h"
#include <vector>
void registerStationInvarianceChecks(std::vector<smoke::Check>&);
void registerStationPhysicalFidelityChecks(std::vector<smoke::Check>&);
void registerStationPowerChecks(std::vector<smoke::Check>&);
void registerStationProtectionChecks(std::vector<smoke::Check>&);
void registerStationRuntimeRegressionChecks(std::vector<smoke::Check>&);
void registerStationSharedSupplyChecks(std::vector<smoke::Check>&);
void registerStationTransferSupplyChecks(std::vector<smoke::Check>&);
void registerStationSolarAuthoringChecks(std::vector<smoke::Check>&);
int main(int argc, char** argv) {
 std::vector<smoke::Check> checks;
 registerStationInvarianceChecks(checks);
 registerStationPhysicalFidelityChecks(checks);
 registerStationPowerChecks(checks);
 registerStationProtectionChecks(checks);
 registerStationRuntimeRegressionChecks(checks);
 registerStationSharedSupplyChecks(checks);
 registerStationTransferSupplyChecks(checks);
 registerStationSolarAuthoringChecks(checks);
 return smoke::main("station-library-scenarios", checks, argc, argv);
}
