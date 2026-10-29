#pragma once
#include "station/StationSystemsRuntime.h"
#include <stdexcept>
namespace station_test::live
{
inline void require(bool ok, char const* message)
{ if (!ok) throw std::runtime_error(message); }
inline void liveSettingsSeam()
{
	auto base = [] {
		station::StationSystemsSettings s; s.enabled = true;
		station::AtmosphereSettings a;
		a.id = "habitat"; a.volumeM3 = a.thermalCapacityKJPerK = 100;
		a.minPressureKPa = 1; a.maxPressureKPa = 200;
		a.startingOxygenKg = 30; a.startingNitrogenKg = 80;
		auto b = a; b.id = "medical";
		b.startingOxygenKg = 2; b.startingNitrogenKg = 2;
		s.atmospheres = {a, b};
		station::AtmospherePathwaySettings hatch;
		hatch.id = "hatch"; hatch.aAtmosphereId = "habitat";
		hatch.bAtmosphereId = "medical"; hatch.areaM2 = 0.5;
		hatch.openFraction = 1;
		s.atmospherePathways = {hatch};
		station::ResourceStoreSettings store;
		store.id = "pantry"; store.unit = "kg"; store.capacity = 1000;
		store.startingQualified = 400;
		station::ResourceStoreSettings cargo;
		cargo.id = "cargo"; cargo.unit = "kg"; cargo.capacity = 1000;
		cargo.startingQualified = 10;
		s.stores = {store, cargo};
		station::ResourceTransferSettings pump;
		pump.id = "pump"; pump.sourceId = "pantry"; pump.destinationId = "cargo";
		pump.unit = "kg"; pump.priority = 10;
		// Validation wants a positive rate, so park it far outside the test
		// window: the transfer exists with a real priority but moves nothing
		// while the seam is exercised, keeping the inventory assertions about
		// "undisturbed live state" honest.
		pump.ratePerSecond = 1.0;
		pump.mode = station::ResourceTransferMode::Scheduled;
		pump.firstOccurrenceSeconds = 1.0e9;
		s.transfers = {pump};
		return s;
	};
	station::StationSystemsRuntime runtime(base(), 1.0);
	runtime.advanceTo(60);
	auto const opening = runtime.snapshot();
	auto const& hatch0 = opening.atmospherePathways.at(0);
	require(hatch0.openFraction == 1.0, "hatch did not start at its authored openness");
	auto const oxygenBefore = hatch0.netTransferredKg[0];
	auto const pantryBefore = opening.stores.at(0).currentQualified;
	require(pantryBefore == 400.0, "store did not start at its authored quantity");

	std::string diagnostic;

	// A parameter change lands: the opening reports the new area, and the
	// live totals carry on from where they were rather than restarting.
	auto wider = base();
	wider.atmospherePathways[0].areaM2 = 25.0;
	require(runtime.updateSettings(wider, &diagnostic),
		("parameter update refused: " + diagnostic).c_str());
	require(runtime.snapshot().atmospherePathways.at(0).netTransferredKg[0]==oxygenBefore,
		"live transfer totals were reset by the update");
	runtime.advanceTo(120);
	auto const hatch1 = runtime.snapshot().atmospherePathways.at(0);
	require(hatch1.areaM2 == 25.0, "area change did not reach the running scenario");
	// Net transfer is signed: thermal pressure reversals can reduce it.
	require(hatch1.netTransferredKg[0]!=0,"live transfer ledger vanished");
	require(runtime.snapshot().stores.at(0).currentQualified == pantryBefore,
		"live store inventory was disturbed by the update");

	// An initial-condition change is refused live. The store keeps the
	// quantity the run actually started with, because the conservation audit
	// derives its baseline from the authored starting values at snapshot
	// time and must not shift under a running scenario.
	auto retuned = wider;
	retuned.stores[0].startingQualified = 900;
	require(runtime.updateSettings(retuned, &diagnostic),
		("update with changed initial conditions refused: " + diagnostic).c_str());
	require(runtime.snapshot().stores.at(0).currentQualified == pantryBefore,
		"startingQualified leaked into the live inventory");
	require(runtime.snapshot().stores.at(0).startingQualified == 400.0,
		"the store's reported starting baseline shifted under a live update");
	require(runtime.effectiveSettings().stores[0].startingQualified == 400.0,
		"effective settings reported the refused starting quantity");
	// The parameter half of the same update still lands.
	require(runtime.effectiveSettings().atmospherePathways[0].areaM2 == 25.0,
		"parameter half of a mixed update was lost");

	// Topology is frozen: adding, removing or re-pointing is refused, and a
	// refusal leaves the scenario untouched.
	auto added = retuned;
	added.atmospherePathways.push_back(retuned.atmospherePathways[0]);
	added.atmospherePathways.back().id = "second";
	require(!runtime.updateSettings(added, &diagnostic), "adding a pathway was accepted");
	require(!diagnostic.empty(), "refusal carried no explanation");

	auto removed = retuned;
	removed.atmospherePathways.clear();
	require(!runtime.updateSettings(removed, &diagnostic), "removing a pathway was accepted");

	auto repointed = retuned;
	repointed.atmospherePathways[0].bAtmosphereId = "airlock";
	require(!runtime.updateSettings(repointed, &diagnostic),
		"re-pointing a pathway endpoint was accepted");
	require(diagnostic.find("topology") != std::string::npos,
		("endpoint refusal did not name topology: " + diagnostic).c_str());

	// Every refusal above left the running scenario exactly as it was.
	auto const hatch3 = runtime.snapshot().atmospherePathways.at(0);
	require(hatch3.areaM2 == 25.0 && hatch3.openFraction == 1.0,
		"a refused update mutated the running scenario");
	require(runtime.snapshot().atmospherePathways.size() == 1,
		"a refused update changed the pathway set");

	// A targeted single-node edit reaches the running scenario without the
	// caller owning the document, and refuses a preserved field by name.
	require(runtime.updateNode<station::AtmospherePathwaySettings>("hatch",
		[](station::AtmospherePathwaySettings& hatch) { hatch.areaM2 = 7.5; }, &diagnostic),
		("targeted edit refused: " + diagnostic).c_str());
	require(runtime.effectiveSettings().atmospherePathways[0].areaM2 == 7.5,
		"targeted edit did not reach the effective settings");
	runtime.advanceTo(180);
	require(runtime.snapshot().atmospherePathways.at(0).areaM2 == 7.5,
		"targeted edit did not reach the running snapshot");
	require(runtime.snapshot().stores.at(0).currentQualified == pantryBefore,
		"targeted edit disturbed unrelated live state");

	// A preserved field is refused whole, with the rule named, and the node
	// keeps the value the run began with.
	require(!runtime.updateNode<station::ResourceStoreSettings>("pantry",
		[](station::ResourceStoreSettings& store) { store.startingQualified = 777.0; },
		&diagnostic),
		"a preserved initial-condition field was accepted live");
	require(diagnostic.find("initial-condition") != std::string::npos,
		("refusal did not name the rule: " + diagnostic).c_str());
	require(runtime.effectiveSettings().stores[0].startingQualified == 400.0,
		"a refused targeted edit still moved the initial condition");
	require(runtime.snapshot().stores.at(0).currentQualified == pantryBefore,
		"a refused targeted edit moved the live inventory");

	// A mixed edit - one live field and one preserved field - is refused whole,
	// so the live field does not sneak through with the preserved one.
	require(!runtime.updateNode<station::ResourceStoreSettings>("pantry",
		[](station::ResourceStoreSettings& store) {
			store.capacity = 4321.0; store.startingQualified = 555.0;
		}, &diagnostic),
		"a mixed live/preserved edit was partially applied");
	require(runtime.effectiveSettings().stores[0].capacity == 1000.0,
		"the live half of a refused mixed edit still landed");

	// Openness is a live parameter, not an initial condition: sealing the
	// opening takes effect immediately and stops the exchange dead.
	require(runtime.updateNode<station::AtmospherePathwaySettings>("hatch",
		[](station::AtmospherePathwaySettings& hatch) { hatch.openFraction = 0; }, &diagnostic),
		("live seal refused: " + diagnostic).c_str());
	require(runtime.effectiveSettings().atmospherePathways[0].openFraction == 0,
		"live seal did not reach the effective settings");
	auto const sealedNet = runtime.snapshot().atmospherePathways.at(0).netTransferredKg[0];
	runtime.advanceTo(420);
	require(runtime.snapshot().atmospherePathways.at(0).netTransferredKg[0] == sealedNet,
		"a sealed opening kept exchanging gas");
	require(runtime.updateNode<station::AtmospherePathwaySettings>("hatch",
		[](station::AtmospherePathwaySettings& hatch) { hatch.openFraction = 0.5; }, &diagnostic),
		("live reopen refused: " + diagnostic).c_str());
	runtime.advanceTo(480);
	require(runtime.snapshot().atmospherePathways.at(0).netTransferredKg[0] != sealedNet,
		"reopening the hatch did not resume the exchange");

	// A negative opening must never be able to invent gas. With a negative
	// area the relaxation exponent flips sign: on an opening to Exterior
	// vacuum the loss term goes negative, so the compartment gains mass it
	// never had while the loss ledger runs backwards. This needs a fresh
	// scenario still riding a live gradient - the long-running hatch above
	// has already equilibrated, so a sign flip there moves nothing and
	// proves nothing. It also has to be reached through the live path, which
	// runs no validation pass; a draft would simply be refused at Apply.
	{
		auto venting = base();
		venting.atmospherePathways[0].bAtmosphereId = station::StationExteriorAtmosphereId;
		venting.atmospherePathways[0].areaM2 = 5.0;
		station::StationSystemsRuntime station(venting, 1.0);
		station.advanceTo(10);
		auto const before = station.snapshot().atmospheres.at(0);
		require(before.oxygenKg < before.startingOxygenKg - 1e-6,
			"the venting fixture moved no gas, so it cannot prove anything");
		require(station.updateNode<station::AtmospherePathwaySettings>("hatch",
			[](station::AtmospherePathwaySettings& hatch) { hatch.areaM2 = -50.0; }, &diagnostic),
			("negative-area live edit refused: " + diagnostic).c_str());
		station.advanceTo(120);
		auto const after = station.snapshot().atmospheres.at(0);
		require(after.oxygenKg <= before.oxygenKg + 1e-9,
			"a negative opening manufactured oxygen out of nothing");
		require(after.exteriorLostKg[0] >= before.exteriorLostKg[0] - 1e-9,
			"a negative opening ran the exterior loss ledger backwards");
		require(station.snapshot().healthy, "a negative opening failed the conservation audit");
	}

	// An unknown id is refused, and the node-type overload catches a caller
	// that pairs the wrong type string with the settings type.
	require(!runtime.updateNode<station::AtmospherePathwaySettings>("ghost",
		[](station::AtmospherePathwaySettings&) {}, &diagnostic),
		"an unknown node id was accepted");
	require(!runtime.updateNode<station::AtmospherePathwaySettings>("Store", "hatch",
		[](station::AtmospherePathwaySettings&) {}, &diagnostic),
		"a mismatched node type/settings pair was accepted");

	// A no-op edit is not an error and changes nothing.
	auto const sameArea = runtime.effectiveSettings().atmospherePathways[0].areaM2;
	require(runtime.updateNode<station::AtmospherePathwaySettings>("hatch",
		[sameArea](station::AtmospherePathwaySettings& hatch) { hatch.areaM2 = sameArea; },
		&diagnostic),
		("no-op targeted edit failed: " + diagnostic).c_str());

	// A priority change through the targeted path is picked up by the
	// ordering, which lives in its own index vector.
	require(runtime.updateNode<station::ResourceTransferSettings>("pump",
		[](station::ResourceTransferSettings& t) { t.priority = 40; }, &diagnostic),
		("transfer priority edit refused: " + diagnostic).c_str());
	require(runtime.effectiveSettings().transfers[0].priority == 40,
		"targeted priority change did not land");
	require(runtime.updateNode<station::ResourceTransferSettings>("pump",
		[](station::ResourceTransferSettings& t) { t.priority = 5; }, &diagnostic),
		("transfer priority revert refused: " + diagnostic).c_str());
	require(runtime.effectiveSettings().transfers[0].priority == 5,
		"targeted priority revert did not land");

}

}
