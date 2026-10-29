#include "StationSystemsPhysics.h"
namespace station
{
	double twoBodyConductedHeatKJ(long double ca, long double cb,
		long double temperatureAC, long double temperatureBC,
		long double conductance, long double seconds)
	{
		// Q = Ca Cb/(Ca + Cb) (Ta - Tb) (1 - exp(-G (1/Ca + 1/Cb) dt)).
		// Evaluate the reduced-capacity equilibrium transfer independently
		// of the relaxation factor: even enormous conductance tends to
		// equilibrium, not an indeterminate form. expm1 preserves tiny steps.
		// Keep extended precision until the one shared ledger booking value.
		auto const relaxed = -std::expm1(-conductance * (1.0L / ca + 1.0L / cb)
			* seconds);
		return static_cast<double>(ca * cb / (ca + cb)
			* (temperatureAC - temperatureBC) * relaxed);
	}
	// One body exchanging heat with a shared reservoir through a passive
	// conductance. The weight is the implicit step's coupling coefficient:
	// the heat this body gains is its weight times the gap between its own
	// tick-start temperature and the coupled end-of-tick reservoir
	// temperature the solver computes for the whole star.
	double solvePassiveExchange(std::vector<PassiveExchangeLeaf>& leaves,
		double reservoirCapacityKJPerK, double reservoirTemperatureC, double dt)
	{
		double weightedTemperatureKJ{0.0};
		double weightKJPerK{0.0};
		for (auto& leaf : leaves)
		{
			leaf.weightKJPerK = 0.0;
			// A dead conductance or a body with no heat capacity exchanges
			// nothing, and contributes nothing to the coupled temperature.
			if (leaf.conductanceKWPerK <= 0.0 || leaf.capacityKJPerK <= 0.0)
				continue;
			auto const coupling = dt * leaf.conductanceKWPerK / leaf.capacityKJPerK;
			leaf.weightKJPerK = dt * leaf.conductanceKWPerK / (1.0 + coupling);
			weightedTemperatureKJ += leaf.weightKJPerK * leaf.temperatureC;
			weightKJPerK += leaf.weightKJPerK;
		}
		// The reservoir's own capacity is always positive, so the weighted
		// average is always defined; with no live leaves it is just the
		// reservoir's own starting temperature and nothing moves.
		return (reservoirCapacityKJPerK * reservoirTemperatureC + weightedTemperatureKJ)
			/ (reservoirCapacityKJPerK + weightKJPerK);
	}
	// The heat a leaf exchanges over the tick once the coupled end-of-tick
	// reservoir temperature is known. Kept as its own expression so the
	// host and the sandbox book the identical arithmetic.
	double passiveExchangeHeatKJ(PassiveExchangeLeaf const& leaf, double coupledReservoirTemperatureC)
	{
		return leaf.weightKJPerK * (coupledReservoirTemperatureC - leaf.temperatureC);
	}
	// Lossy, input-capped passive exchange is still one implicit star solve.
	// F(T) is monotone. Its sign at each leaf and at its cap threshold
	// identifies the exact linear segment containing the common solution.
	// Relative-temperature sums avoid subtracting nearly equal temperatures
	// after a stiff zero-efficiency interface has pinned the hub temperature.
	void solveTransportExchange(std::vector<PassiveExchangeLeaf> const& leaves,
		std::vector<TransportExchange>& ports, double hubCapacity, double hubTemperature,
		double dt, StationConnectionLedger& connections)
	{
		struct Term { double forwardWeight{}, backwardWeight{}, forwardCap{}, backwardCap{}, forwardEta{}, backwardEta{}, slope{}, constant{}; bool forward{}, saturated{}; };
		std::vector<Term> terms(leaves.size());
		for (size_t i = 0; i < leaves.size(); ++i)
		{
			auto const& leaf = leaves[i]; auto& term = terms[i];
			auto const kd = dt * leaf.conductanceKWPerK;
			term.forwardEta = connections.efficiency(ports[i].forward);
			term.backwardEta = connections.efficiency(ports[i].backward);
			term.forwardCap = connections.remaining(ports[i].forward) * dt;
			term.backwardCap = connections.remaining(ports[i].backward) * dt;
			auto const weight = [&](double eta) {
				if (kd <= 0.0 || leaf.capacityKJPerK <= 0.0) return 0.0;
				if (eta == 0.0) return kd;
				return kd <= leaf.capacityKJPerK ? kd / (1.0 + eta * (kd / leaf.capacityKJPerK))
					: leaf.capacityKJPerK / (eta + leaf.capacityKJPerK / kd); };
			term.forwardWeight = weight(term.forwardEta);
			term.backwardWeight = weight(1.0);
		}
		auto const residual = [&](double origin, double offset) {
			double result = hubCapacity * ((origin - hubTemperature) + offset);
			for (size_t j = 0; j < leaves.size(); ++j)
			{
				auto const gap = (origin - leaves[j].temperatureC) + offset;
				auto const& term = terms[j];
				result += gap >= 0.0 ? std::min(term.forwardCap, term.forwardWeight * gap)
					: -term.backwardEta * std::min(term.backwardCap, term.backwardWeight * -gap);
			}
			return result;
		};
		double normalization = hubCapacity;
		for (size_t i = 0; i < leaves.size(); ++i)
		{
			auto& term = terms[i];
			term.forward = residual(leaves[i].temperatureC, 0.0) <= 0.0;
			auto const weight = term.forward ? term.forwardWeight : term.backwardWeight;
			auto const cap = term.forward ? term.forwardCap : term.backwardCap;
			auto const atCap = weight > 0.0 ? residual(leaves[i].temperatureC,
				(term.forward ? 1.0 : -1.0) * cap / weight) : 0.0;
			term.saturated = weight > 0.0 && (term.forward ? atCap <= 0.0 : atCap >= 0.0);
			term.slope = term.saturated ? 0.0 : term.forward ? weight : weight * term.backwardEta;
			term.constant = term.saturated ? (term.forward ? -cap : cap * term.backwardEta) : 0.0;
			normalization = std::max(normalization, term.slope);
		}
		double denominator = hubCapacity / normalization, constants = 0.0;
		for (auto const& term : terms) { denominator += term.slope / normalization; constants += term.constant / normalization; }
		for (size_t i = 0; i < leaves.size(); ++i)
		{
			auto const& term = terms[i];
			double gapNumerator = (hubCapacity / normalization) * (hubTemperature - leaves[i].temperatureC) + constants;
			for (size_t j = 0; j < leaves.size(); ++j)
				gapNumerator += (terms[j].slope / normalization) * (leaves[j].temperatureC - leaves[i].temperatureC);
			auto const cap = term.forward ? term.forwardCap : term.backwardCap;
			auto const energy = term.saturated ? cap : std::clamp(
				(term.forward ? term.forwardWeight : -term.backwardWeight) * (gapNumerator / denominator), 0.0, cap);
			auto const port = term.forward ? ports[i].forward : ports[i].backward;
			connections.direction(port, term.forward && ports[i].forward == ports[i].backward);
			auto const flow = connections.send(port, energy / dt);
			ports[i].leafKJ = (term.forward ? flow.delivered : -flow.input) * dt;
			ports[i].hubKJ = (term.forward ? -flow.input : flow.delivered) * dt;
		}
	}
	// The compartment's temperature and its water vapour inventory are
	// one coupled state (#564): the saturation the air can hold depends
	// on the temperature, and the condensation that constraint forces
	// releases latent heat back into that same temperature. Integrating
	// the two in sequence - condensing against the tick-start
	// temperature, then moving the temperature - leaves the gas
	// supersaturated by however far the temperature fell during the
	// tick, a phase change forever one tick behind its own constraint.
	// Solving them together removes the lag. With C the compartment heat
	// capacity, T0 its tick-start temperature, Q the tick's constant
	// external heat rate (occupant sensible heat plus the exchanger
	// exchange), L the latent heat of vaporization and m0 the vapour
	// mass entering the solve, the end-of-tick equilibrium is either
	//   unsaturated - no phase change, T = T0 + Q*dt/C, or
	//   saturated   - the condensate x satisfies the fixed point
	//       x = m0 - m_sat(T0 + (Q*dt + L*x)/C),
	// where the latent term is the feedback: condensing warms the air,
	// which raises the saturation capacity, which condenses less. The
	// residual F(x) = m0 - m_sat(T0 + (Q*dt + L*x)/C) - x is strictly
	// decreasing - m_sat never decreases with temperature and the
	// temperature grows with x, and it is strictly decreasing in x
	// wherever the saturation is flat because of the explicit -x term -
	// so a fixed-count bisection lands on the unique root
	// deterministically, and the published condensate is the bracket
	// end where F <= 0: the remaining vapour never exceeds what the
	// temperature that condensation itself produced can hold. The
	// temperature is then booked from the condensate,
	// C*(T - T0) = Q*dt + L*x, so the thermal ledger closes over
	// exactly the latent heat that left the gas phase rather than a
	// clamped display value hiding an unsatisfied constraint.
	VapourThermalCoupling solveVapourThermalCoupling(double capacityKJPerK,
		double startTemperatureC, double externalHeatKW, double vapourKg,
		double volumeM3, double dt)
	{
		auto const saturatedMassAt = [volumeM3](double temperatureC)
		{
			return stationSaturatedVapourMassKg(
				stationSaturationVapourPressureKPa(temperatureC), volumeM3, temperatureC);
		};
		auto const freeTemperatureC = startTemperatureC + externalHeatKW * dt / capacityKJPerK;
		// Air that still holds no more than saturation at the end of a
		// phase-change-free tick never reaches the phase boundary: with a
		// constant heat rate the temperature is monotone across the
		// tick, so the end is its tightest point.
		if (!(vapourKg > saturatedMassAt(freeTemperatureC)))
			return {freeTemperatureC, 0.0};
		// Saturated branch: bisect the condensate fixed point on [0, m0].
		// F(0) > 0 because the unsaturated branch just failed, and
		// F(m0) = -m_sat(...) <= 0, so the root is bracketed.
		auto low = 0.0;
		auto high = vapourKg;
		for (int iteration = 0; iteration < 100; ++iteration)
		{
			auto const mid = 0.5 * (low + high);
			// Adjacent double endpoints cannot advance the bracket further.
			if (mid == low || mid == high) break;
			auto const midTemperatureC = freeTemperatureC
				+ StationLatentHeatVaporizationKJPerKg * mid / capacityKJPerK;
			if (vapourKg - saturatedMassAt(midTemperatureC) - mid > 0.0) low = mid;
			else high = mid;
		}
		// `high` is the bracket end where F <= 0: condensing it leaves
		// the vapour at or below the saturation of the temperature the
		// condensation itself produced.
		auto const condensedKg = high;
		return {freeTemperatureC
			+ StationLatentHeatVaporizationKJPerKg * condensedKg / capacityKJPerK,
			condensedKg};
	}
	// Outage windows that have opened at or before the given simulation
	// time (#413). A window k starts at firstOccurrenceSeconds + k x
	// recurrenceSeconds (or just firstOccurrenceSeconds for a one-off) and
	// runs for durationSeconds. The count is monotonic in elapsed time and
	// uses the same boundary tolerance as the delivery schedule rule, so a
	// window that opens exactly on a tick boundary is never missed. With a
	// recurrence no shorter than the duration the windows are disjoint, so
	// at most one window of a schedule is open at any instant.
	uint64_t stationOutageOccurrencesStarted(double firstOccurrenceSeconds,
		double /*durationSeconds*/, double recurrenceSeconds, double elapsedSeconds)
	{
		if (elapsedSeconds + StationScheduleTolerance < firstOccurrenceSeconds) return 0;
		if (recurrenceSeconds <= 0.0) return 1;
		auto const steps = std::floor((elapsedSeconds + StationScheduleTolerance
			- firstOccurrenceSeconds) / recurrenceSeconds);
		if (!(steps > 0.0)) return 1;
		return static_cast<uint64_t>(steps) + 1;
	}
	// Outage windows that have closed (been restored) at or before the
	// given simulation time (#413). Window k closes at its start plus the
	// duration; the count is monotonic in elapsed time and uses the same
	// tolerance, so a restoration landing exactly on a tick boundary is
	// never missed and never counted twice. A window is restored only once
	// its full duration has elapsed, so the installation keeps its reduced
	// availability for exactly the authored window.
	uint64_t stationOutageOccurrencesCompleted(double firstOccurrenceSeconds,
		double durationSeconds, double recurrenceSeconds, double elapsedSeconds)
	{
		auto const firstRestore = firstOccurrenceSeconds + durationSeconds;
		if (elapsedSeconds + StationScheduleTolerance < firstRestore) return 0;
		if (recurrenceSeconds <= 0.0) return 1;
		auto const steps = std::floor((elapsedSeconds + StationScheduleTolerance
			- firstRestore) / recurrenceSeconds);
		if (!(steps > 0.0)) return 1;
		return static_cast<uint64_t>(steps) + 1;
	}
}
