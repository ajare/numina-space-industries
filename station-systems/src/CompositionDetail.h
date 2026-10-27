#pragma once
#include "station/Configuration.h"
#include <cmath>
namespace station::configuration_detail
{
	inline std::string declaredUnitsList()
	{
		std::string list;
		for (auto const* unit : StationStoreUnits)
		{
			if (!list.empty()) list += ", ";
			list += unit;
		}
		return list;
	}
	inline std::string declaredThermalClasses()
	{
		std::string list;
		for (auto const* thermalClass : StationThermalServiceClasses)
		{
			if (!list.empty()) list += ", ";
			list += thermalClass;
		}
		return list;
	}
	// Compound Resource materials are mass-denominated (#391); their
	// constituents are accounted in kg and the energy term in kJ.
	inline bool isMassUnit(std::string const& unit)
	{
		return unit == "g" || unit == "kg";
	}
	inline double massToKg(std::string const& unit, double mass)
	{
		return unit == "g" ? mass * 0.001 : mass;
	}
	// Inverse of massToKg: a magnitude expressed in kilograms rendered
	// in a store's own declared mass unit (#402, where the recovery
	// rates are authored in kg/s against stores that may be grams).
	inline double kgToMass(std::string const& unit, double kg)
	{
		return unit == "g" ? kg * 1000.0 : kg;
	}
	inline bool compositionsClose(ResourceCompositionSettings const& left,
		ResourceCompositionSettings const& right)
	{
		auto const close = [](double a, double b)
		{ return std::abs(a - b) <= StationBalanceTolerance; };
		return close(left.moisture, right.moisture)
			&& close(left.nutrientNitrogen, right.nutrientNitrogen)
			&& close(left.atmosphericNitrogen, right.atmosphericNitrogen)
			&& close(left.phosphorus, right.phosphorus)
			&& close(left.potassium, right.potassium)
			&& close(left.otherMinerals, right.otherMinerals)
			&& close(left.energyKJPerKgDry, right.energyKJPerKgDry);
	}
	// Biological uptake role purity (#572). A crop or culture uptake
	// spends the drawn mass exactly as the role's own material:
	// irrigation/culture water is spent as water, and each tracked
	// nutrient draw is spent as that nutrient. A plain store with no
	// declared composition is accepted as the role material by
	// convention; a compound store must declare itself to BE that
	// material - pure in the role constituent, with no moisture for
	// the nutrient roles and no other constituent riding along,
	// because the uptake owns no residual stream to account for
	// anything else the drawn mass carries. A P-only compound can
	// never fund nitrogen-containing biomass, atmospheric nitrogen is
	// never a match for the bound-nitrogen role, and one tracked
	// nutrient never substitutes for another: crossing those
	// boundaries is a conversion the uptake contract does not own,
	// so it is refused at configuration. Roles: 0 water, 1 nitrogen,
	// 2 phosphorus, 3 potassium, 4 minerals.
	inline bool isPureUptakeComposition(ResourceCompositionSettings const& composition,
		size_t role)
	{
		auto const close = [](double a, double b)
		{ return std::abs(a - b) <= StationBalanceTolerance; };
		double const fractions[5]{composition.moisture, composition.nutrientNitrogen,
			composition.phosphorus, composition.potassium, composition.otherMinerals};
		if (!close(fractions[role], 1.0)) return false;
		if (role == 0)
		{
			for (size_t field = 1; field < 5; ++field)
				if (!close(fractions[field], 0.0)) return false;
		}
		else
		{
			if (!close(fractions[0], 0.0)) return false;
			for (size_t field = 1; field < 5; ++field)
				if (field != role && !close(fractions[field], 0.0)) return false;
		}
		return true;
	}
	using ConstituentLedger = std::map<std::string, double>;
	// Add the constituent amounts of a compound wet mass to a ledger.
	// The seven mass constituents partition the wet mass exactly: water
	// is the moisture term and dryOrganics is the dry-mass balance, so
	// constituent accounting closes over the whole inventory. Energy is
	// the declared chemical energy term, in kJ.
	inline void addComposition(ConstituentLedger& ledger,
		ResourceCompositionSettings const& composition, double wetMassKg, double sign)
	{
		auto const dry = wetMassKg * (1.0 - composition.moisture);
		ledger["water"] += sign * wetMassKg * composition.moisture;
		ledger["nutrientNitrogen"] += sign * dry * composition.nutrientNitrogen;
		ledger["atmosphericNitrogen"] += sign * dry * composition.atmosphericNitrogen;
		ledger["phosphorus"] += sign * dry * composition.phosphorus;
		ledger["potassium"] += sign * dry * composition.potassium;
		ledger["otherMinerals"] += sign * dry * composition.otherMinerals;
		ledger["dryOrganics"] += sign * dry * (1.0 - (composition.nutrientNitrogen
			+ composition.atmosphericNitrogen + composition.phosphorus
			+ composition.potassium + composition.otherMinerals));
		ledger["energy"] += sign * dry * composition.energyKJPerKgDry;
	}
	// The internal alias for the published biological constituent
	// inventory (#571): the actual dry-mass amounts of respirable
	// organic matter and bound nutrients a biological population
	// carries, routes and loses.
	using BiologicalConstituents = StationBiologicalConstituents;
	// The declared organic fraction of a dry biological composition:
	// the share of dry mass that is respirable organic matter.
	inline double organicFractionOf(ResourceCompositionSettings const& composition)
	{
		return 1.0 - (composition.nutrientNitrogen + composition.phosphorus
			+ composition.potassium + composition.otherMinerals);
	}
	// The chemical-energy basis for the organic material (#571): the
	// declared dry-biomass chemical energy is carried wholly by the
	// organic fraction, because the bound nutrients are not fuel. So
	// a kilogram of organic matter carries the dry-biomass energy
	// divided by the organic fraction, which makes the energy fixed
	// during growth equal to the energy later respired plus the energy
	// still carried in the living and harvested organic matter. A
	// composition with no organic matter carries no chemical energy.
	inline double chemicalEnergyKJPerKgOrganic(ResourceCompositionSettings const& composition)
	{
		auto const organicFraction = organicFractionOf(composition);
		if (organicFraction <= 0.0) return 0.0;
		return composition.energyKJPerKgDry / organicFraction;
	}
	// A share of an inventory, for splitting a routed mass across the
	// constituents it actually carries rather than a declared mix.
	inline BiologicalConstituents scaledConstituents(BiologicalConstituents const& inventory,
		double share)
	{
		auto result = BiologicalConstituents{};
		result.organicKg = inventory.organicKg * share;
		result.nutrientNitrogenKg = inventory.nutrientNitrogenKg * share;
		result.phosphorusKg = inventory.phosphorusKg * share;
		result.potassiumKg = inventory.potassiumKg * share;
		result.mineralsKg = inventory.mineralsKg * share;
		return result;
	}
	// Keep the living inventory consistent with the biomass ledger
	// (#571): the organic share is whatever dry mass the population
	// holds beyond its tracked nutrients, so the inventory always
	// sums to the living biomass exactly and the nutrients are never
	// silently resized by unit roundoff.
	inline void settleLivingOrganic(BiologicalConstituents& inventory, double livingBiomassKg)
	{
		inventory.organicKg = livingBiomassKg - (inventory.nutrientNitrogenKg
			+ inventory.phosphorusKg + inventory.potassiumKg + inventory.mineralsKg);
	}
	// Take a share of the living inventory, returning what came off
	// (#571). The nutrients come off as real tracked amounts and the
	// organic share is re-closed against the remaining biomass ledger,
	// so the inventory keeps summing to the living biomass exactly.
	inline BiologicalConstituents takeConstituents(BiologicalConstituents& inventory, double share,
		double remainingBiomassKg)
	{
		auto const removed = scaledConstituents(inventory, share);
		inventory.nutrientNitrogenKg -= removed.nutrientNitrogenKg;
		inventory.phosphorusKg -= removed.phosphorusKg;
		inventory.potassiumKg -= removed.potassiumKg;
		inventory.mineralsKg -= removed.mineralsKg;
		settleLivingOrganic(inventory, remainingBiomassKg);
		return removed;
	}
	// The constituent inventory a declared dry biomass carries at a
	// declared composition (#571). Nothing new is authored: the
	// inventory is derived wholly from settings that already exist, so
	// reset and persistence behaviour is untouched.
	inline BiologicalConstituents constituentsOf(double dryMassKg,
		ResourceCompositionSettings const& composition)
	{
		auto inventory = BiologicalConstituents{};
		inventory.nutrientNitrogenKg = dryMassKg * composition.nutrientNitrogen;
		inventory.phosphorusKg = dryMassKg * composition.phosphorus;
		inventory.potassiumKg = dryMassKg * composition.potassium;
		inventory.mineralsKg = dryMassKg * composition.otherMinerals;
		settleLivingOrganic(inventory, dryMassKg);
		return inventory;
	}
	// Add one ledger into another (#601).
	inline void addLedger(ConstituentLedger& target, ConstituentLedger const& source,
		double sign)
	{
		for (auto const& entry : source)
			target[entry.first] += sign * entry.second;
	}
	// Fixed-yield processes must not turn a changed feed into unfunded
	// constituents (or energy). Empty means the recipe is feasible.
	inline std::string unfundedConstituent(ConstituentLedger const& funded,
		ConstituentLedger const& required)
	{
		for (auto const& [key, amount] : required)
		{
			auto const available = funded.count(key) ? funded.at(key) : 0.0;
			if (amount > available + StationBalanceTolerance * std::max(1.0, std::abs(available)))
				return key;
		}
		return {};
	}
	// The constituent ledger a declared composition carries at a wet
	// mass, returned instead of added in place (#601).
	inline ConstituentLedger compositionLedgerOf(ResourceCompositionSettings const& composition,
		double wetMassKg, double sign)
	{
		auto ledger = ConstituentLedger{};
		addComposition(ledger, composition, wetMassKg, sign);
		return ledger;
	}
	// The constituent ledger a routed biological mass actually carries
	// (#601): the population's tracked dry constituents plus the
	// chemical energy the organic share carries on the organic basis.
	// Biological residue is dry material, so it brings no water.
	inline ConstituentLedger biologicalLedgerOf(BiologicalConstituents const& inventory,
		double energyKJPerKgOrganic)
	{
		auto ledger = ConstituentLedger{};
		ledger["water"] += 0.0;
		ledger["nutrientNitrogen"] += inventory.nutrientNitrogenKg;
		ledger["atmosphericNitrogen"] += 0.0;
		ledger["phosphorus"] += inventory.phosphorusKg;
		ledger["potassium"] += inventory.potassiumKg;
		ledger["otherMinerals"] += inventory.mineralsKg;
		ledger["dryOrganics"] += inventory.organicKg;
		ledger["energy"] += inventory.organicKg * energyKJPerKgOrganic;
		return ledger;
	}
	// The composition a compound store's material actually has (#601):
	// the authored declaration plus the drift of everything the store
	// has really received, renormalised to the store's current mass.
	// With no drift this is exactly the authored composition; with
	// drift the fractions describe the blend that is actually in the
	// store, so a process sizing its yields from the store's material
	// tracks the actual constituents instead of the original
	// declaration. The ledger is the source of truth: every fraction
	// is the actual constituent over the actual dry (or wet) mass.
	inline ResourceCompositionSettings effectiveCompositionOf(
		ResourceCompositionSettings const& authored, double totalKg,
		ConstituentLedger const& drift)
	{
		if (drift.empty() || totalKg <= 0.0) return authored;
		auto const actual = compositionLedgerOf(authored, totalKg, 1.0);
		auto blended = actual;
		for (auto const& entry : drift)
			blended[entry.first] += entry.second;
		auto result = authored;
		result.moisture = std::max(0.0, std::min(1.0, blended["water"] / totalKg));
		auto const dry = std::max(0.0, totalKg * (1.0 - result.moisture));
		if (dry > 0.0)
		{
			result.nutrientNitrogen = std::max(0.0, blended["nutrientNitrogen"] / dry);
			result.atmosphericNitrogen = std::max(0.0, blended["atmosphericNitrogen"] / dry);
			result.phosphorus = std::max(0.0, blended["phosphorus"] / dry);
			result.potassium = std::max(0.0, blended["potassium"] / dry);
			result.otherMinerals = std::max(0.0, blended["otherMinerals"] / dry);
			result.energyKJPerKgDry = std::max(0.0, blended["energy"] / dry);
		}
		else
		{
			result.nutrientNitrogen = 0.0;
			result.atmosphericNitrogen = 0.0;
			result.phosphorus = 0.0;
			result.potassium = 0.0;
			result.otherMinerals = 0.0;
			result.energyKJPerKgDry = 0.0;
		}
		return result;
	}
}
