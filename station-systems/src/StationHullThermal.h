#pragma once
#include "station/StationSystems.h"
#include "StationSystemsPhysics.h"
#include <vector>
#include <algorithm>
#include <cmath>
#include <optional>

namespace station
{
// Integrate stellar input and signed infrared exchange simultaneously. Used by
// the standalone body and the adaptive radiation/conduction splitting pass.
// Evolve the temperature CHANGE directly: recovering it by subtracting absolute
// temperatures loses small radiative exchanges to roundoff. The mesh's local
// energy tolerance shrinks with duration, so that cancellation can otherwise
// cause endless subdivision of a perfectly physical stiff contact (#938).
inline std::optional<long double> hullRadiationTemperatureChangeC(HullSettings const& a,
	long double initialC, double absorbedKW, double dt)
{
	auto const radiation = a.emissivity * a.emittingAreaM2 * StationStefanBoltzmannKWPerM2K4;
	auto const sink = static_cast<long double>(a.sinkTemperatureC) + StationCelsiusOffset;
	auto const equilibrium = std::pow(std::pow(sink, 4) + absorbedKW / radiation, 0.25L);
	auto const initialK = initialC + StationCelsiusOffset;
	long double change = 0, remaining = dt, step = dt;
	auto rate = [&](long double delta) {
		auto const t = initialK + delta;
		return radiation / a.heatCapacityKJPerK
			* (equilibrium - t) * (equilibrium + t) * (equilibrium * equilibrium + t * t);
	};
	auto rk4 = [&](long double t, long double duration) {
		auto const k1 = rate(t), k2 = rate(t + duration * k1 / 2);
		auto const k3 = rate(t + duration * k2 / 2), k4 = rate(t + duration * k3);
		return t + duration * (k1 + 2 * k2 + 2 * k3 + k4) / 6;
	};
	for (unsigned attempts = 0; remaining > 0; ++attempts)
	{
		if (attempts >= 4096 || !std::isfinite(equilibrium) || !std::isfinite(change)) return {};
		auto const temperature = initialK + change;
		if (std::abs(temperature - equilibrium) <= 1e-12 * std::max(1.0L, equilibrium))
		{ change = equilibrium - initialK; break; }
		auto const hottest = std::max(temperature, equilibrium);
		step = std::min({step, remaining, a.heatCapacityKJPerK / (4 * radiation * hottest * hottest * hottest)});
		if (!(step > 0) || remaining - step == remaining) return {};
		auto const whole = rk4(change, step);
		auto const halves = rk4(rk4(change, step / 2), step / 2);
		auto const error = std::abs(whole - halves) / 15;
		auto const tolerance = 1e-12L * std::max(1.0L, std::abs(initialK + halves));
		if (std::isfinite(halves) && error <= tolerance)
		{
			change = halves + (halves - whole) / 15;
			remaining -= step;
		}
		step *= !std::isfinite(error) || error > tolerance ? 0.5 : 2.0;
	}
	return change;
}
inline std::optional<long double> hullRadiationTemperatureC(HullSettings const& a,
	long double initialC, double absorbedKW, double dt)
{
	auto change = hullRadiationTemperatureChangeC(a, initialC, absorbedKW, dt);
	if (!change) return {};
	return initialC + *change;
}
struct HullMeshLink
{
	size_t a, b;
	double conductance;
};
struct HullMeshSolution
{
	std::vector<long double> temperatures;
	std::vector<double> transferred;
	double emittedKJ{};
};
// Radiation and conduction do not commute. Control their splitting error,
// rather than letting the configured Station step decide insulation fidelity.
// Every trial uses only closed-form pair relaxation; rejected trials book nothing.
inline std::optional<HullMeshSolution> solveHullThermalMesh(HullSettings const& hull,
	double absorbedKW, double dt, std::vector<long double> capacities,
	std::vector<long double> temperatures, std::vector<HullMeshLink> const& links)
{
	HullMeshSolution result{std::move(temperatures), std::vector<double>(links.size()), 0};
	auto split = [&](HullMeshSolution& state, double duration) {
		auto radiate = [&]() {
			auto const initial = state.temperatures.back();
			auto const solved = hullRadiationTemperatureChangeC(hull, initial, absorbedKW, duration / 2);
			if (!solved) return false;
			state.emittedKJ += absorbedKW * duration / 2 - hull.heatCapacityKJPerK * *solved;
			state.temperatures.back() += *solved;
			return true;
		};
		if (!radiate()) return false;
		auto conduct = [&](size_t i) {
			auto const& link = links[i];
			auto& ta = state.temperatures[link.a]; auto& tb = state.temperatures[link.b];
			auto const moved = twoBodyConductedHeatKJ(capacities[link.a], capacities[link.b],
				ta, tb, link.conductance, duration / 2);
			ta -= moved / capacities[link.a]; tb += moved / capacities[link.b];
			state.transferred[i] += moved;
		};
		for (size_t i = 0; i < links.size(); ++i) conduct(i);
		for (size_t i = links.size(); i > 0; --i) conduct(i - 1);
		return radiate();
	};
	double remaining = dt, step = dt;
	for (unsigned attempts = 0; remaining > 0; ++attempts)
	{
		if (attempts >= 65536 || !(step > 0) || remaining - step == remaining) return {};
		step = std::min(step, remaining);
		// Compare local exchange increments, not rounded cumulative ledgers.
		HullMeshSolution whole{result.temperatures, std::vector<double>(links.size()), 0};
		auto halves = whole;
		bool valid = split(whole, step) && split(halves, step / 2) && split(halves, step / 2);
		double error = 0;
		for (size_t i = 0; i < capacities.size(); ++i)
		{
			auto const delta = static_cast<double>(std::abs(whole.temperatures[i] - halves.temperatures[i]));
			if (!std::isfinite(delta)) valid = false;
			error = std::max(error, delta / 1e-8);
		}
		for (size_t i = 0; i < links.size(); ++i)
		{
			auto const delta = std::abs(whole.transferred[i] - halves.transferred[i]);
			if (!std::isfinite(delta)) valid = false;
			error = std::max(error, delta / 1e-7);
		}
		auto const emittedError = std::abs(whole.emittedKJ - halves.emittedKJ);
		valid = valid && std::isfinite(emittedError);
		error = std::max(error, emittedError / 1e-7);
		if (valid && error <= step / dt)
		{
			result.temperatures = std::move(halves.temperatures);
			for (size_t i = 0; i < links.size(); ++i) result.transferred[i] += halves.transferred[i];
			result.emittedKJ += halves.emittedKJ;
			remaining -= step;
		}
		// The local Strang defect is cubic, but its allocated tolerance is
		// proportional to duration: error / tolerance therefore scales as h^2.
		// Retain a safety margin instead of doubling after every acceptance
		// (which otherwise wastes roughly half the finite budget on rejection).
		step *= !valid ? 0.5 : error == 0 ? 2.0
			: std::clamp(0.9 * std::sqrt((step / dt) / error), 0.2, 2.0);
	}
	return result;
}
inline bool stationHasHullLinks(StationSystemsSettings const& s)
{
	return std::any_of(s.thermalLinks.begin(), s.thermalLinks.end(), [](auto const& link) {
		return link.aKind == ThermalNodeKind::Hull || link.bKind == ThermalNodeKind::Hull;
	});
}
}
