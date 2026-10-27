#pragma once
#include "station/StationSystems.h"
#include <type_traits>

namespace pf::smoke
{
// A biology test arrangement authors two ordinary components together. Only
// the population base crosses the production configuration seam; lighting is
// always installed separately as a real, connected fixture.
template<class Population> struct PopulationWithFixture : Population
{
	::station::LightingFixtureSettings lighting;
	PopulationWithFixture() { lighting.fixtureType = ::station::LegacyLampFixtureType; }
};
using CropWithFixture = PopulationWithFixture<::station::CropPopulationSettings>;
using AlgaeWithFixture = PopulationWithFixture<::station::AlgaeCultureSettings>;

inline ::station::LightingFixtureSettings populationFixture(::station::CropPopulationSettings const& p, double kw)
{
	::station::LightingFixtureSettings f;
	f.id = "crop-light-" + p.id; f.atmosphereId = p.atmosphereId;
	f.cropPopulationId = p.id; f.fixtureType = ::station::LegacyLampFixtureType;
	f.fixtureType.ratedKW = kw; f.operatingProfile.constantLevel = 1;
	f.supplyBusIds = p.supplyBusIds; f.supplyMode = p.supplyMode;
	return f;
}
inline ::station::LightingFixtureSettings populationFixture(::station::AlgaeCultureSettings const& p, double kw)
{
	::station::CropPopulationSettings base;
	base.id = p.id; base.atmosphereId = p.atmosphereId;
	base.supplyBusIds = p.supplyBusIds; base.supplyMode = p.supplyMode;
	auto f = populationFixture(base, kw);
	// Crop and algae ids occupy separate namespaces, but these fixtures
	// share one: include the population kind to avoid cross-kind collisions.
	f.id = "algae-light-" + p.id;
	f.cropPopulationId.clear(); f.algaeCultureId = p.id;
	return f;
}
template<class Population>
void addPopulationFixtures(::station::StationSystemsSettings& s,
	std::vector<PopulationWithFixture<Population>> const& populations)
{
	for (auto const& p : populations)
	{
		auto f = populationFixture(static_cast<Population const&>(p), p.lighting.fixtureType.ratedKW);
		f.operatingProfile = p.lighting.operatingProfile;
		s.lightingFixtures.push_back(std::move(f));
	}
}
}
