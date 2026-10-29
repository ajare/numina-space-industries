#pragma once
#include "station/Configuration.h"
#include "station/AtmosphereMetrics.h"
#include "station/support/ScriptExecution.h"
namespace station
{
	struct AlertSink;
	// ADR-0031: what an Alert *does* is code, never authored data. Each alert
	// type binds this pair; the handler receives the Alert instance and the
	// sink it may write.
	struct AlertCallbacks
	{
		void (*onFire)(AlertSink&, StationAlertSnapshot const&) = nullptr;
		void (*onClear)(AlertSink&, StationAlertSnapshot const&) = nullptr;
	};
	// #868: the alert-callback registry. The alert type is the Monitor Metric
	// that raised the Alert, the same identity #866 introduced. Every fire and
	// clear resolves through here, so a developer adds an Alert behaviour by
	// binding a pair for a type and never touches the authored schema. An
	// unbound type still resolves to the v1 ledger-and-stream handler, so
	// widening the Metric catalogue can never drop an Alert on the floor.
	AlertCallbacks const& stationAlertCallbacksFor(std::string_view alertType);
	std::string_view stationAlertClearReasonTag(AlertClearReason reason);
	// Read-only view of one Resource store: the authored capacity, protected
	// reserve and starting inventory beside the runtime's current
	// qualified/unqualified contents and last-tick transfer flows.
	struct StationSystemsStoreSnapshot
	{
		std::string id;
		std::string unit;
		double capacity{0.0};
		double startingQualified{0.0};
		double startingUnqualified{0.0};
		double protectedReserve{0.0};
		double currentQualified{0.0};
		double currentUnqualified{0.0};
		double lastTickIn{0.0};
		double lastTickOut{0.0};
		// The declared composition when the store holds a compound Resource
		// material (#391); absent for plain single-quantity stores.
		std::optional<ResourceCompositionSettings> composition;
		// The store's actual constituent inventory in kg (and kJ for the
		// energy term) (#601): the authored composition at the current
		// mass plus the drift of every material that really entered the
		// store. For a compound store this is what the store actually
		// holds - which diverges from the declared composition whenever
		// deposited material (a harvest yield, a dead residue) carried a
		// different makeup - and every draw spends this blend. Empty for
		// plain, uncharacterised stores, which carry no constituent
		// inventory.
		std::map<std::string, double> actualConstituents;
		// Live kg constituent per kg stream (energy excluded). Published for
		// compound stores and treatment water ports; empty inventories read 0.
		std::map<std::string, double> concentrations;
		bool operator==(StationSystemsStoreSnapshot const&) const = default;
	};

	// The endurance basis of one resource (#419): what the observed net
	// rate over the declared observation interval says about the drawable
	// inventory. This is an explanation of the current drawable supply, not
	// a survival countdown: it assumes the net rate observed over the last
	// completed observation window continues unchanged (a static
	// current-rate assumption) and forecasts no future profiles, deliveries
	// or consumption changes.
	enum class StationEnduranceBasis : std::uint8_t
	{
		Unknown,     // no completed observation window yet: nothing is known
		Depleted,    // no drawable qualified inventory above the protected reserve
		NoNetChange, // observed net rate within the deadband: no depletion countdown
		Growing,     // drawable inventory increasing: no depletion countdown
		Depleting,   // drawable inventory decreasing: bounded endurance estimate
	};
	inline constexpr std::array<char const*, 5> StationEnduranceBasisNames{
		"unknown", "depleted", "no-net-change", "growing", "depleting"};
	// How consistently the net rate held across consecutive observation
	// windows. A single completed window is one data point: it carries a
	// bounded estimate but no stability claim (Unknown). A rate that flipped
	// sign or moved materially between consecutive windows is Unstable, so
	// the endurance is reported as a bound, never as a precise countdown.
	enum class StationRateStability : std::uint8_t
	{
		Unknown,
		Stable,
		Unstable,
	};
	inline constexpr std::array<char const*, 3> StationRateStabilityNames{
		"unknown", "stable", "unstable"};
	// Documented deadband for near-zero net rates (#419): a net rate within
	// this tolerance, scaled by the drawable inventory at the window start
	// (rate_deadband = tolerance x max(1, drawable at window start)), is
	// reported as no-net-change rather than as an endurance number large
	// enough to read as infinite. Reuses the boundary tolerance idiom.
	inline constexpr double StationEnduranceDeadbandTolerance{StationBalanceTolerance};
	// Documented stability threshold (#419): a net rate that moved by more
	// than this fraction of the larger of the two consecutive window rates
	// (or flipped sign) is an unstable rate; the endurance is then a bound.
	inline constexpr double StationEnduranceStabilityFraction{0.5};
	// The endurance explanation for one resource (#419): the inventory
	// split (physical total = qualified + unqualified; drawable = the
	// qualified portion above the protected reserve), the net rate observed
	// over the declared observation interval, the basis and stability of
	// the estimate, and the soonest authored next delivery into the same
	// store as context. Estimates are derived observability sampled at the
	// existing observation cadence - no second clock, no per-tick history -
	// and rest on the explicit static current-rate assumption: the observed
	// net rate is assumed to continue; no future profile, delivery timing
	// or consumption change is forecast. For a battery the same shape
	// applies to energy: physical = nameplate, usable = the cyclable slice,
	// reserve = nameplate - usable, drawable = stored above the reserve
	// floor; the discharge power rating is reported as a constraint on how
	// fast the endurance can be drawn and is never used as the endurance
	// rate itself.
	struct StationEnduranceSnapshot
	{
		std::string kind; // "store" or "battery"
		std::string id;
		std::string unit; // the store's declared unit; "kWh" for batteries
		double physicalTotal{0.0};     // qualified + unqualified (battery: nameplate)
		double usableQualified{0.0};   // qualified contents (battery: cyclable slice)
		double protectedReserve{0.0};  // withheld from discretionary use
		double drawableUsable{0.0};   // usable above the protected reserve
		StationEnduranceBasis basis{StationEnduranceBasis::Unknown};
		StationRateStability stability{StationRateStability::Unknown};
		double observedNetRatePerSecond{0.0}; // drawable delta per simulated second
		double observationWindowSeconds{0.0}; // actual sim time of the observed window
		double observedUntilSeconds{0.0};     // sim time the estimate is as of
		double estimateSeconds{0.0};         // static current-rate estimate (conservative bound when unstable)
		double boundMinSeconds{0.0};        // fastest observed drain bound
		double boundMaxSeconds{0.0};        // slowest observed drain bound; 0 = no upper bound claimed
		// Battery-only power constraint: the max discharge power rating in
		// kW. Energy and power are distinct: the endurance estimate uses the
		// observed energy rate, never this rating. 0 = not applicable (stores).
		double maxDrawPowerKW{0.0};
		// Soonest authored delivery into this store, as context (#392).
		bool hasNextDelivery{false};
		double nextDeliverySeconds{0.0};
		double nextDeliveryQuantity{0.0};
		bool nextDeliveryArrivesQualified{true};
		bool operator==(StationEnduranceSnapshot const&) const = default;
	};

	// The projection outcome of one Resource store under the scenario
	// forecast (#420). Where the endurance estimate (#419) extends the
	// OBSERVED current rate, the forecast extends the estimate to the
	// CONFIGURED demand/production profiles and the authored future
	// delivery schedule under explicitly bounded model assumptions: a
	// fixed simulated-time horizon, a fixed integration segment cap, and
	// the declared rates continuing exactly as authored. "Sustained"
	// means the projected drawable inventory stays above zero through the
	// whole projected window; "shortfall" means the projection reaches
	// drawable-zero inside the window - the store runs out before any
	// accepted arrival can prevent it - with the onset time and the
	// limiting service reported beside it. "Incomplete" (#604) means the
	// bounded projection stopped before reaching the horizon: no shortfall
	// was found inside the complete-through range, but the status beyond
	// StationForecastSnapshot::completeThroughSeconds is unknown and must
	// never be read as sustained supply.
	enum class StationForecastBasis : std::uint8_t
	{
		Unknown,    // no forecast has been computed for this store
		Sustained,  // projected drawable stays above zero through the window
		Shortfall,  // projected drawable reaches zero inside the window
		Incomplete, // budget-truncated: no shortfall inside the complete
		            // range, status beyond it unknown
	};
	inline constexpr std::array<char const*, 4> StationForecastBasisNames{
		"unknown", "sustained", "shortfall", "incomplete"};
	// Documented forecast bounds (#420): the projection runs over a fixed
	// simulated-time horizon and integrates at most this many event-driven
	// segments. A schedule dense enough to need more segments truncates at
	// the cap and reports it; the forecast is never an unbounded
	// computation. The forecast mutates no live inventory, advances no
	// World clock and consumes no deterministic stream: it is a pure
	// bounded projection of the committed state forward.
	inline constexpr double StationForecastHorizonSeconds{86400.0};
	inline constexpr std::uint32_t StationForecastMaxSegments{4096};
	// One projected resupply arrival of one store (#420): what the
	// schedule brings, what the store's projected headroom actually
	// accepts (accepted delivery quantity - what cannot fit is refused at
	// the boundary, never overflowed), the projected drawable inventory
	// at the moment of arrival, and whether the arrival is timely against
	// the projected trajectory (#595): rescuesStore is true when no
	// shortfall is projected strictly before the arrival instant - which
	// includes the exactly-timely arrival that lands drawable-zero as the
	// store would otherwise run dry, because arrivals precede that tick's
	// drains and the delivery prevents the shortage - and false only once
	// the store's projected shortfall onset has passed, even if a prior
	// delivery has restored a positive drawable since.
	struct StationForecastArrival
	{
		std::string deliveryId;
		std::string storeId;
		double arrivalSeconds{0.0};
		double scheduledQuantity{0.0};
		double acceptedQuantity{0.0};
		double rejectedQuantity{0.0};
		double projectedDrawableBefore{0.0};
		double projectedPhysicalBefore{0.0};
		bool arrivesQualified{true};
		bool rescuesStore{false}; // no shortfall projected strictly before this arrival
		bool operator==(StationForecastArrival const&) const = default;
	};
	// The scenario forecast of one Resource store (#420): the projected
	// trajectory summary, the shortfall (if any) with its onset and the
	// limiting service that drives it, the projected arrivals with
	// capacity-capped accepted quantities, and the uncertainty flags for
	// recovery the forecast deliberately does NOT count: biology-derived
	// recovery (crop, algae, microbial) is a provisional model
	// assumption and is never presented as guaranteed recovery, and
	// process-chain production (treatment, electrolysis, conversion,
	// capture, fuel-cell product) is outside the projected scope.
	struct StationForecastEntry
	{
		std::string storeId;
		std::string unit; // the store's declared unit; every quantity is in it
		double asOfSeconds{0.0};
		double drawableAtStart{0.0};
		double physicalAtStart{0.0};
		StationForecastBasis basis{StationForecastBasis::Unknown};
		double netRateAtStart{0.0}; // projected net rate over the first segment
		bool hasShortfall{false};
		double shortfallOnsetSeconds{0.0}; // when drawable reaches zero; 0 = none
		// Drawable at the end of the complete projected range: the horizon
		// value only when the forecast is not truncated; otherwise the
		// value at StationForecastSnapshot::completeThroughSeconds, which
		// is not a horizon projection (#604).
		double projectedDrawableAtHorizon{0.0};
		// The binding outflow at the projected depletion: the largest
		// modelled outflow of the store in the segment that drives it to
		// zero. Empty when no depletion is projected.
		std::string limitingServiceId;
		std::string limitingServiceKind; // "transfer" / "crew" / "equipment"
		// Total outflow the projection could not cover inside the window:
		// draws blocked at drawable-zero plus arrivals refused for lack of
		// headroom never disappear - they are reported here as explicit
		// uncovered demand, never silently dropped.
		double unmetOutflowTotal{0.0};
		std::vector<StationForecastArrival> arrivals;
		// Uncertainty labels: recovery pathways that could replenish this
		// store but are NOT counted in the guaranteed projection.
		bool biologyRecoveryPossible{false};
		bool processProductionPossible{false};
		// #594: consuming pathways that could drain this store but are
		// NOT counted in the projection. A Sustained basis beside one of
		// these flags is not a promise of no shortfall: the omitted draw
		// can make the live store run short earlier than projected. A
		// manually commanded generator's fuel draw is projected as
		// deterministic demand; under Automatic dispatch the draw
		// follows the unmodelled net load, so the generator's fuel store
		// is flagged here instead of projected. processConsumptionPossible
		// covers process feeds and consumables (fuel-cell reactants,
		// treatment source/consumables, electrolysis feed
		// water, capture consumables, Sabatier and methane feeds, authored
		// conversion inputs, consumed maintenance spares) plus Automatic-
		// dispatch generator fuel; biologyConsumptionPossible covers
		// crop/algae water and nutrient uptake and microbial feed,
		// oxygen, process water, culture medium and replacement stock.
		bool processConsumptionPossible{false};
		bool biologyConsumptionPossible{false};
		// Conservation of the projected exchanges: the residual of
		// (projected physical) - (start physical + accepted arrivals +
		// inflow moved - outflow moved). It must stay within the declared
		// balance tolerance scaled by the store size; a larger residual is
		// a forecast defect, never silent creation or loss of material.
		double conservationResidual{0.0};
		std::string unavailableReason; // nonempty means no projected range for this store
		bool operator==(StationForecastEntry const&) const = default;
	};
	// The station-wide scenario forecast (#420): every Resource store's
	// projection under one declared scenario, with the scenario and
	// uncertainty labels carried so no projection can be read as a
	// guarantee, and the segment/truncation accounting so the boundedness
	// of the computation is itself observable.
	struct StationForecastSnapshot
	{
		bool available{false};
		// Derived explanation when disabled or outside the projection's supported scope.
		std::string unavailableReason;
		double asOfSeconds{0.0};
		double horizonSeconds{0.0};
		// #604: the time through which the projection is complete: every
		// authored event and profile breakpoint strictly before it was
		// modelled. Equals asOfSeconds + horizonSeconds when the whole
		// horizon was projected; when the event/segment budget truncated
		// the run it is the earliest unmodelled event (or the point the
		// segment cap stopped the integration), and the range beyond it is
		// explicitly unavailable - no result there may be read as covering
		// the full horizon.
		// An unavailable projection completes no range beyond asOfSeconds.
		double completeThroughSeconds{0.0};
		std::uint32_t segmentsComputed{0};
		bool truncated{false};
		std::string scenarioLabel;
		std::string uncertaintyLabel;
		std::vector<StationForecastEntry> stores;
		bool operator==(StationForecastSnapshot const&) const = default;
	};

	// Per-transfer observable outcome: what was requested, what moved, and the
	// unmet remainder. Unmet material is never deleted; it simply did not move.
	struct StationTransferSnapshot
	{
		std::string id;
		std::string sourceId;
		std::string destinationId;
		std::string unit;
		double ratePerSecond{0.0};
		int priority{0};
		double lastRequested{0.0};
		double lastCompleted{0.0};
		double lastUnmet{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		ResourceTransferMode mode{ResourceTransferMode::Continuous};
		double firstOccurrenceSeconds{0.0};
		uint64_t occurrencesFired{0};
		uint64_t lastFiredTick{0};
		std::string limitingReason;
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		// Runtime-only occurrence totals, independent of bounded event history.
		double cumulativeRequested{0.0};
		double cumulativeMoved{0.0};
		double cumulativeUnmet{0.0};
		bool operator==(StationTransferSnapshot const&) const = default;
	};

	// Per-unit boundary balance: everything of one unit that the station owns.
	// With no external exchanges the residual must stay within tolerance;
	// anything larger is a conservation violation, not a shortage.
	struct StationBalanceSnapshot
	{
		std::string unit;
		double totalStarting{0.0};
		double totalCurrent{0.0};
		// Cumulative mass of this unit removed by declared conversion
		// losses (#391). The residual closes over it: current + lost must
		// equal starting within tolerance.
		double lost{0.0};
		// Cumulative mass of this unit admitted from outside the station by
		// accepted deliveries (#392). It is the inflow side of the same
		// boundary: current + lost - delivered must equal starting within
		// tolerance, so delivered material is accounted, never implicit.
		double delivered{0.0};
		// Cumulative mass of this unit recovered out of a compartment
		// atmosphere by a water collector (#402) or a CO2 capture machine
		// (#404). It is inflow from outside the store boundary just like a
		// delivery, so the residual closes as current + lost - delivered -
		// collected against starting: the condensate and the captured gas
		// are accounted, never implicit.
		double collected{0.0};
		// Cumulative mass of this unit admitted into stores by crop harvest
		// and dead-residue routing (#410). Living biomass lives outside the
		// store network, so harvested material arriving in the food and
		// waste stores is inflow on the same boundary as a delivery: the
		// residual closes as current + lost - delivered - collected -
		// harvested against starting, so harvest never reads as creation
		// of mass.
		double harvested{0.0};
		double residual{0.0};
		bool operator==(StationBalanceSnapshot const&) const = default;
	};

	// One authored conversion flow as of the most recent tick: what the
	// declared full throughput requested and what actually moved at the
	// tick's completed scale.
	struct StationConversionFlowSnapshot
	{
		std::string storeId;
		std::string unit;
		double requested{0.0};
		double completed{0.0};
		// The remainder that did not move (#554 for equipment material
		// exchanges): explicit unmet material, never deleted.
		double unmet{0.0};
		bool operator==(StationConversionFlowSnapshot const&) const = default;
	};
	// Per-conversion observable outcome. The completed scale is the fraction
	// of declared full throughput that ran this tick: 1.0 full, (0,1)
	// partial under shortage or output headroom limits, 0 rejected.
	struct StationConversionSnapshot
	{
		std::string id;
		int priority{0};
		double lastScale{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		std::vector<StationConversionFlowSnapshot> inputs;
		std::vector<StationConversionFlowSnapshot> outputs;
		bool operator==(StationConversionSnapshot const&) const = default;
	};
	// Per-constituent boundary ledger (#391): everything of one tracked
	// constituent that the compound stores own, plus the cumulative declared
	// conversion losses. With no external exchanges current + lost must
	// equal starting within tolerance; anything larger is a conservation
	// violation, never silent creation or deletion of material.
	struct StationConstituentBalanceSnapshot
	{
		std::string constituent;
		std::string unit; // "kg" for mass constituents, "kJ" for energy
		double starting{0.0};
		double current{0.0};
		double lost{0.0};
		// Cumulative constituent admitted by accepted deliveries (#392).
		double delivered{0.0};
		// Cumulative constituent admitted into compound stores by crop
		// harvest and dead-residue routing (#410), carrying the actual
		// transferred composition and remaining chemical energy (#601).
		// The residual closes as current + lost - delivered -
		// harvested against starting: the constituents a harvest carries
		// into the food and waste streams are accounted, never implicit.
		double harvested{0.0};
		double residual{0.0};
		bool operator==(StationConstituentBalanceSnapshot const&) const = default;
	};
	// One authored resupply delivery as of the most recent tick: what its
	// schedule says, what the last occurrence brought in, what the
	// destination refused, and when the next arrival is due (#392).
	// Rejected material never entered the station, so it is reported here
	// rather than in the station's own ledgers — the refusal is the account.
	struct StationDeliverySnapshot
	{
		std::string id;
		std::string storeId;
		std::string unit;
		double quantity{0.0};
		double firstOccurrenceSeconds{0.0};
		double recurrenceSeconds{0.0}; // 0 = one-off
		bool arrivesQualified{true};
		int priority{0};
		uint64_t occurrencesCompleted{0};
		double lastOccurrenceSeconds{0.0};
		double lastScheduled{0.0};
		double lastAccepted{0.0};
		double lastRejected{0.0};
		double acceptedTotal{0.0};
		double rejectedTotal{0.0};
		bool exhausted{false};            // one-off already delivered
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationDeliverySnapshot const&) const = default;
	};
	// One authored terminal-waste ejection as of the most recent tick:
	// what its schedule says, what the last occurrence actually
	// ejected, what it could not eject because the selected partition
	// held no more eligible material, and when the next ejection is due
	// (#403, #746). Unmet material never left the store — stock in the
	// other partition stays, absent material was never there — so it is reported here
	// rather than as a loss; only what was actually ejected is booked
	// as external loss in the station's boundary ledgers.
	struct StationWasteEjectionSnapshot
	{
		std::string id;
		std::string storeId;
		std::string unit;
		double quantity{0.0};
		double firstOccurrenceSeconds{0.0};
		double recurrenceSeconds{0.0}; // 0 = one-off
		int priority{0};
		uint64_t occurrencesCompleted{0};
		double lastOccurrenceSeconds{0.0};
		double lastScheduled{0.0};
		double lastEjected{0.0};
		double lastUnmet{0.0};       // scheduled minus ejectable material
		double ejectedTotal{0.0};
		double unmetTotal{0.0};
		bool exhausted{false};            // one-off already ejected
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		WasteEjectionEligibility eligibility{WasteEjectionEligibility::QualifiedOnly};
		double lastQualifiedEjected{0.0};
		double lastHeldEjected{0.0};
		double qualifiedEjectedTotal{0.0};
		double heldEjectedTotal{0.0};
		bool operator==(StationWasteEjectionSnapshot const&) const = default;
	};
	// One authored installation outage as of the most recent tick (#413):
	// what its schedule says, whether its window is open right now, how
	// many occurrences have started and completed, and the effective
	// availability the target installation currently holds under every
	// outage aimed at it. The availability is the deepest reduction among
	// the outage windows open this tick (the minimum capacity fraction),
	// 1.0 when none is open; it is a pure function of World time, so the
	// same tick sequence always reproduces the same availability. A
	// restoration returns the installation to its declared availability
	// without touching any inventory, so the snapshot never reports a
	// restored machine as having regained fuel, consumables, buffers or
	// biomass.
	struct StationOutageSnapshot
	{
		std::string id;
		std::string installationId;
		std::string installationKind;
		double capacityFraction{0.0};
		double firstOccurrenceSeconds{0.0};
		double durationSeconds{0.0};
		double recurrenceSeconds{0.0}; // 0 = one-off
		int priority{0};
		bool active{false};                 // an outage window is open this tick
		uint64_t occurrencesStarted{0};
		uint64_t occurrencesCompleted{0};
		double lastStartSeconds{0.0};
		double lastRestoreSeconds{0.0};
		bool exhausted{false};              // one-off already restored
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		double installationAvailability{1.0}; // target's effective factor now
		bool operator==(StationOutageSnapshot const&) const = default;
	};
	// The lifecycle phase of one authored maintenance job (#414).
	// Idle: no occurrence is currently being worked. Blocked: an
	// occurrence is waiting for prerequisites that cannot currently be
	// reserved (missing tools/parts, no waste headroom) - the work is
	// not running and the installation is not isolated. Active: the
	// prerequisites are reserved and the work is progressing; the
	// installation is isolated. Completed: the most recent occurrence
	// finished and spent its resources; the job returns to Idle for its
	// next occurrence or exhausts after a one-off.
	enum class StationMaintenancePhase : std::uint8_t
	{
		Idle,
		Blocked,
		Active,
		Completed,
	};
	inline constexpr std::array<char const*, 4> StationMaintenancePhaseNames{
		"idle", "blocked", "active", "completed"};
	// What started a maintenance job (#842): an authored schedule or a
	// runtime repairFault command. One job type, two starters - the kind
	// distinguishes only the snapshot and event surface, never the work
	// semantics, which are identical for both.
	enum class StationMaintenanceKind : std::uint8_t
	{
		Scheduled,
		Repair,
	};
	inline constexpr std::array<char const*, 2> StationMaintenanceKindNames{
		"scheduled", "repair"};
	// One maintenance requirement as of the most recent tick (#414):
	// what the job needs from which store, whether it is a reusable tool
	// or a consumed part/consumable, whether the reservation currently
	// holds it, and what the last completed occurrence actually spent.
	struct StationMaintenanceRequirementSnapshot
	{
		std::string storeId;
		std::string unit;
		double quantity{0.0};
		bool consumed{false};
		bool reserved{false};   // held by this job's live reservation this tick
		double lastConsumed{0.0}; // spent by the most recent completion
		double consumedTotal{0.0}; // committed spend across completions; observation only
		bool operator==(StationMaintenanceRequirementSnapshot const&) const = default;
	};
	// One authored maintenance job as of the most recent tick (#414):
	// the schedule, the current phase with progress and the blocking
	// reason while Blocked, the per-requirement reservation and spend,
	// the isolation state of the target installation, and the
	// occurrence history. Everything is observable through the
	// snapshot/event seam without any Agent assignment: maintenance is
	// a scheduled station process, not a task handed to anyone.
	struct StationMaintenanceSnapshot
	{
		std::string id;
		std::string installationId;
		std::string installationKind;
		// #842: "scheduled" for authored jobs, "repair" for a
		// runtime-started repair; faultModeId names the Fault a repair
		// job targets and is empty for scheduled work.
		StationMaintenanceKind kind{StationMaintenanceKind::Scheduled};
		std::string faultModeId;
		double firstOccurrenceSeconds{0.0};
		double durationSeconds{0.0};
		double recurrenceSeconds{0.0}; // 0 = one-off
		int priority{0};
		std::string wasteStoreId;
		StationMaintenancePhase phase{StationMaintenancePhase::Idle};
		bool isolated{false};           // target held out of service this tick
		double progressSeconds{0.0};   // work completed toward the occurrence
		double remainingSeconds{0.0};  // duration still owed
		std::string blockReason;        // why Blocked or why last interrupted
		std::vector<StationMaintenanceRequirementSnapshot> requirements;
		uint64_t occurrencesStarted{0};
		uint64_t occurrencesCompleted{0};
		uint64_t occurrencesMissed{0};  // windows that closed unstarted
		double lastStartSeconds{0.0};
		double lastCompleteSeconds{0.0};
		bool exhausted{false};          // one-off already finished
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationMaintenanceSnapshot const&) const = default;
	};

	// One solar array as of the most recent tick: installed capacity
	// and the generation it could deliver under the Station sun. A
	// zero-capacity array is disabled and contributes nothing.
	struct StationSolarSourceSnapshot
	{
		std::string id;
		double capacityKW{0.0};
		double availableKW{0.0};    // capacity * Station sun * availabilityFactor
		bool disabled{false};       // zero installed capacity
		// #845: the availability the array's fault ledger leaves it - the
		// string-loss remainder and the tracking-failure fixed value
		// composed by minimum. 1.0 with no Fault active.
		double availabilityFactor{1.0};
		std::vector<StationFaultSnapshot> activeFaults;
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationSolarSourceSnapshot const&) const = default;
	};
	// One External equipment load group as of the most recent tick: what
	// it requested, what was actually supplied, the unmet remainder, the
	// protected floor and any unmet critical shortfall, the delivered
	// fraction, exported heat or unmet cooling, and its scaled material
	// exchanges. Fraction semantics are explicit (#548): requestedKW is
	// the profile-scaled request (demand * profileLevel), deliveredFraction
	// is the fraction of that profile-scaled request actually supplied,
	// and the full-operation heat and material exchange rates scale by the
	// full-operation delivered fraction (profileLevel * deliveredFraction).
	// Material feasibility is explicit on top (#554): materialFeasibleFraction
	// is the fraction of the full-operation exchanges the declared feed and
	// destination limits sustained this tick, and allocation ran against
	// requestedKW * materialFeasibleFraction — so a starved or backed-up
	// load shows a reduced delivered fraction, reduced heat and reduced
	// outputs, with the shortfall reported as unmet service and
	// per-exchange unmet material. A cooling-infeasible load is not
	// admitted: it draws no power and its cooling need stays visible as
	// unmet cooling.
	// The recovery stage of a purely discretionary External load (#417).
	// Active is normal operation. Shed is a supply refusal held after the
	// load could not be served: it draws nothing and stays put until the
	// declared dwell, hysteresis, cooling and firm-source gate opens. A
	// load with a protected critical floor is never recovery-managed - the
	// protected pass restores it - so it always reports Active.
	enum class StationLoadRecoveryStage : std::uint8_t
	{
		Active,
		Shed,
		Restoring,
	};
	struct StationEquipmentLoadSnapshot
	{
		std::string id;
		int priority{0};
		std::string thermalClass;
		StationCoolingDisposition coolingDisposition{StationCoolingDisposition::Exported};
		double profileLevel{0.0};
		double requestedKW{0.0};
		double suppliedKW{0.0};
		double unmetKW{0.0};
		double criticalFloorKW{0.0};
		double unmetCriticalKW{0.0};
		double deliveredFraction{0.0};
		double materialFeasibleFraction{1.0}; // feed/destination-sustainable operation fraction (#554)
		double heatExportedKW{0.0};   // heat leaving via the declared export boundary
		// Emitted heat routed into the load's declared thermal reservoir
		// instead of direct export (#395). A load's produced heat is
		// booked exactly once: either exported directly or routed into the
		// reservoir, never both. Electrical input is never converted to
		// duplicate local heat - the authored heat output is the emitted
		// heat, and energy stored in batteries or material is not heat.
		double heatRoutedKW{0.0};
		double unmetCoolingKW{0.0};   // cooling need with no feasible service
		bool admitted{false};
		std::string blockReason;
		StationTransferStatus status{StationTransferStatus::Idle};
		// #417 recovery observability: the load's current recovery stage,
		// the startup surge it drew this tick while Restoring, the seconds
		// still owed before the stage advances (dwell while Shed, surge
		// while Restoring), and the reason a Shed load is being held from
		// restarting. A non-recovery-managed load reports Active with these
		// at their zero/empty defaults.
		StationLoadRecoveryStage recoveryStage{StationLoadRecoveryStage::Active};
		double recoverySurgeKW{0.0};
		double recoverySecondsRemaining{0.0};
		std::string recoveryHoldReason;
		std::vector<StationConversionFlowSnapshot> materialExchanges;
		bool operator==(StationEquipmentLoadSnapshot const&) const = default;
	};
	// One battery bank as of the most recent tick: the authored nameplate,
	// usable and derived reserve energies beside the runtime's stored energy
	// and state of charge, the last tick's charge/discharge power, and the
	// cumulative energy ledger that keeps losses visible. The residual
	// (starting - discharged + charged - stored) must stay within the
	// declared tolerance; delivered must never exceed drawn and stored must
	// never exceed what the bus supplied on charge — free energy is a
	// conservation violation, never a silent rounding.
	struct StationBatterySnapshot
	{
		std::string id;
		double nameplateKWh{0.0};
		double usableKWh{0.0};
		double reserveKWh{0.0};          // nameplate - usable: never dischargeable
		double startingStoredKWh{0.0};
		double storedKWh{0.0};
		double stateOfCharge{0.0};      // stored / nameplate
		double maxChargeKW{0.0};
		double maxDischargeKW{0.0};
		double chargeEfficiency{1.0};
		double dischargeEfficiency{1.0};
		double lastChargeKW{0.0};        // battery-terminal input, after charge cable losses
		double lastDischargeKW{0.0};    // battery-terminal output, before discharge cable losses
		double lastDischargeRequestedKW{0.0};   // discharge requested at the battery terminal
		double lastDischargeShortfallKW{0.0};    // requested but not supplied at that terminal
		double chargedKWh{0.0};         // cumulative energy added to storage
		double dischargedKWh{0.0};     // cumulative energy drawn from storage
		double deliveredKWh{0.0};      // cumulative battery-terminal output energy
		double chargeInputKWh{0.0};    // cumulative battery-terminal charging input energy
		double lossesKWh{0.0};         // intrinsic charge/discharge losses, excluding cables
		double residualKWh{0.0};       // starting - discharged + charged - stored
		// #843: the permanent capacity fade and the accumulators that
		// drive it. The effective ratings are the authored values scaled
		// by (1 - wearFraction): what the bank can actually hold, cycle
		// and keep protected now. A battery with no fade model reports
		// zero wear and effective ratings equal to the authored ones.
		double wearFraction{0.0};
		double wearCycles{0.0};
		double wearOperatingHours{0.0};
		double wearCalendarHours{0.0};
		double effectiveNameplateKWh{0.0};
		double effectiveUsableKWh{0.0};
		double effectiveReserveKWh{0.0};
		bool disabled{false};          // zero usable capacity
		bool atReserve{false};         // stored is at the reserve floor
		bool saturated{false};         // stored is at nameplate capacity
		// #845: the bank's active Fault ledger. A thermal-runaway Fault
		// holds the bank out of service (no charge, no discharge) while it
		// is active; the one-shot heat and toxic-species booking it made
		// into its declared compartment is reported on the compartment,
		// not here.
		std::vector<StationFaultSnapshot> activeFaults;
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationBatterySnapshot const&) const = default;
	};
	// The observable state machine of a fuelled generator (#399). Stopped
	// produces nothing and draws nothing; Starting is cranking toward the
	// startup energy the running state requires; Running is producing
	// electrical output and burning fuel with its waste heat routed to the
	// sink; Shutdown is the post-output decay period where electrical
	// output has stopped but the configured shutdown heat and its cooling
	// requirement persist.
	enum class StationGeneratorPhase : std::uint8_t
	{
		Stopped,
		Starting,
		Running,
		Shutdown,
	};
	// One generator as of the most recent tick: the authored contract
	// beside the phase, the electrical output actually produced, the fuel
	// actually drawn, the heat actually routed, the startup progress, the
	// cooling qualification, and the closing fuel ledger. The generator's
	// energy ledger closes over fuel = electrical + waste within the
	// declared tolerance: fuel energy is never silently destroyed, and
	// electrical output never appears without the fuel that produced it.
	struct StationGeneratorSnapshot
	{
		std::string id;
		std::string primeMover{"reciprocating"};
		std::string fuelStoreId;
		std::string thermalClass;
		std::string heatSinkId;
		double ratedOutputKW{0.0};
		double efficiency{1.0};
		double maxRampKWPerSecond{0.0};
		double commandedOutputKW{0.0};
		double startupPowerKW{0.0};
		double startupSeconds{0.0};
		double shutdownHeatKW{0.0};
		double shutdownSeconds{0.0};
		double maxSinkTemperatureC{0.0};
		StationGeneratorPhase phase{StationGeneratorPhase::Stopped};
		double outputKW{0.0};              // instantaneous endpoint output (ramp state)
		double averageOutputKW{0.0};       // mean electrical power actually generated over the Station step
		double fuelDrawKWh{0.0};           // fuel energy drawn this tick
		double wasteHeatKW{0.0};           // waste heat routed this tick
		double startupDrawKW{0.0};         // cranking power actually taken this tick
		double startupUnmetKW{0.0};        // cranking power requested but not taken
		double startupReceivedKJ{0.0};     // cranking energy received this attempt
		double startupRequiredKJ{0.0};     // configured startup energy (power x seconds)
		double decayHeatKW{0.0};           // decay heat routed this tick
		double sinkTemperatureC{0.0};
		bool coolingAvailable{false};      // sink below the maximum sink temperature
		bool coolingRequired{false};       // running, or decay heat booked this tick
		bool disabled{false};              // zero installed capacity
		double availabilityFactor{1.0};   // installed capacity available under outages (#413)
		// #415: the command the dispatch controller issued this tick, and
		// whether the running command came from the dispatcher rather than
		// the authored setpoint. In Manual mode the dispatch command is 0
		// and dispatchCommanded is false.
		double dispatchCommandKW{0.0};
		bool dispatchCommanded{false};
		std::string blockReason;           // why a commanded start is not proceeding
		double combustionFuelKg{0.0}, combustionOxygenKg{0.0};
		double combustionCarbonDioxideKg{0.0}, combustionWaterKg{0.0};
		double combustionAirKg{0.0}, combustionExhaustKg{0.0};
		double fuelEnergyKJ{0.0};          // cumulative fuel energy consumed
		double electricalKJ{0.0};          // cumulative electrical delivered
		double wasteHeatKJ{0.0};           // cumulative waste heat routed
		double startupHeatKJ{0.0};         // cumulative cranking energy dissipated
		double decayHeatKJ{0.0};           // cumulative decay heat routed
		double residualKJ{0.0};            // fuel - electrical - waste
		// #845: the generator's active Fault ledger. A `failed-start`
		// Fault zeroes every start attempt: the crank runs and dissipates
		// its energy as accounted heat, and the machine never reaches
		// Running while the Fault is active.
		std::vector<StationFaultSnapshot> activeFaults;
		StationTransferStatus status{StationTransferStatus::Idle};
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationGeneratorSnapshot const&) const = default;
	};
	// The station-wide electrical picture: generation actually available
	// this tick against total requested service, what was supplied and
	// left unmet, unmet critical floors, untaken surplus and exported
	// heat. Allocated power never exceeds generation plus battery output;
	// shortages are explicit results, never silent clamping. Battery
	// discharge and charge ride the same allocation pool as generation —
	// they are never a second power budget.
	// One protected Critical station service account for the tick (#416):
	// the protected demand the service presented to the shared pool, what
	// the pool actually delivered, the unmet remainder (which stays
	// explicit unmet critical demand and is never reclassified as
	// permissible curtailment) and the reason the protection could not be
	// fully honoured. Kind is one of "containment-circulation" (hazardous
	// coolant loop pump and freeze heater), "biological-circulation"
	// (crop/algae essential circulation pumps), "critical-cooling"
	// (authored critical-service chiller work) or "shutdown-cooling"
	// (a generator's decay-heat obligation, which books unconditionally
	// even when the machine's electrical output has stopped).
	struct StationProtectedServiceSnapshot
	{
		std::string id;
		std::string kind;
		double requestedKW{0.0};
		double suppliedKW{0.0};
		double unmetKW{0.0};
		std::string reason;
		bool operator==(StationProtectedServiceSnapshot const&) const = default;
	};

	// #882: one Transfer-mode load's automatic transfer switch, as of the
	// most recent tick. A Transfer load owns a primary and a backup Power Bus
	// and sits on exactly one of them: the switch never splits the load across
	// both. `servingBusId` is the bus the load drew from, and the event fields
	// record the route and the sim instant of the last time it moved.
	struct StationSupplyTransferSnapshot
	{
		std::string consumerType;
		std::string loadId;
		std::string primaryBusId;
		std::string backupBusId;
		std::string servingBusId;
		bool onBackup{false};
		// The authored bar the primary must hold the load for before it may come
		// back, and how long it has held so far. `primaryCapableSeconds` resets
		// to zero the moment the primary falls short again, which is what stops a
		// marginal primary from flapping the load between buses.
		double returnDwellSeconds{0.0};
		double primaryCapableSeconds{0.0};
		// The most recent transfer this load went through. Both bus ids are
		// empty and the instant is negative until the switch has moved once.
		std::string eventFromBusId, eventToBusId;
		double eventAtSimSeconds{-1.0};
		bool operator==(StationSupplyTransferSnapshot const&) const = default;
	};

	// #883: one bus's part in feeding one Shared-supply load this tick. `spareKW`
	// is what that bus could put on the wire for this load when the split was
	// weighed, `shareKW` the pro-rata slice it was asked to carry, and
	// `suppliedKW` what it actually delivered.
	struct StationSharedSupplyFeedSnapshot
	{
		std::string busId;
		double spareKW{0.0};
		double shareKW{0.0};
		double suppliedKW{0.0};
		bool operator==(StationSharedSupplyFeedSnapshot const&) const = default;
	};

	// #883: how a Shared-supply load's demand was divided across its buses.
	// The counterpart to StationSupplyTransferSnapshot: where a Transfer load
	// names the one bus serving it, a Shared load names them all and the slice
	// each carried, so a reader can see a combined feed and a zero-spare bus
	// without diffing two bus ledgers. Buses appear in declared order.
	struct StationSharedSupplySnapshot
	{
		std::string consumerType;
		std::string loadId;
		std::vector<std::string> busIds;
		std::vector<StationSharedSupplyFeedSnapshot> feeds;
		bool operator==(StationSharedSupplySnapshot const&) const = default;
	};

	struct StationBusPowerSnapshot
	{
		std::string busId;
		double ratedCapacityKW{0.0};
		double loadFraction{0.0};
		double generationKW{0.0};
		double batteryChargeKW{0.0}, batteryDischargeKW{0.0};
		double demandKW{0.0};
		double protectedDemandKW{0.0}, protectedSuppliedKW{0.0}, protectedUnmetKW{0.0};
		double suppliedKW{0.0}, unmetKW{0.0}, surplusKW{0.0};
		bool tripped{false};
		double trippedAtSimSeconds{0.0};
		double throughPowerKW{0.0};
		double unmetCriticalKW{0.0};
		double residualKW{0.0};
		std::vector<StationProtectedServiceSnapshot> protectedServices;
		bool operator==(StationBusPowerSnapshot const&) const = default;
	};

	// #885: stable feeder subjects for the future Monitor catalogue. Source
	// delivery is measured at the bus; load draw is measured leaving the bus
	// (before cable loss). Batteries have one subject in each direction.
	enum class StationPowerBreakerState { Closed, Open, TrippedWithBus };
	struct StationPowerConnectionSnapshot
	{
		StationConnectionKey key;
		std::string busId;
		bool source{false};
		double sourceDeliveredKW{0.0}, loadDrawnKW{0.0};
		StationPowerBreakerState breakerState{StationPowerBreakerState::Closed};
		// Empty if this connection delivered no power in the reported interval.
		// Shared loads can have several serving rows; Transfer can change buses
		// inside an event-split interval. This is flow, not switch position.
		std::string servingBusId;
		// Structurally false in v1: non-charging sources never receive power.
		bool reversePower{false};
		bool operator==(StationPowerConnectionSnapshot const&) const = default;
	};

	struct StationPowerSnapshot
	{
		std::vector<StationPowerConnectionSnapshot> connections;
		// Network-side availability and actual physical connection losses.
		double poolGenerationKW{0.0};
		double connectionLossKW{0.0};
		double poolResidualKW{0.0};
		double availableGenerationKW{0.0};
		double totalRequestedKW{0.0};
		double totalSuppliedKW{0.0};
		double totalUnmetKW{0.0};
		double unmetCriticalKW{0.0};
		double surplusKW{0.0};
		double exportedHeatKW{0.0};
		double batteryDischargeKW{0.0};   // bus power received from battery discharge cables this tick
		double batteryChargeKW{0.0};      // bus power taken by charging this tick
		double batteryShortfallKW{0.0};   // routed to batteries but not delivered
		// #415: the dispatch controller's own account for this tick. The
		// mode is the authored controller setting; dispatchDemandKW is the
		// station-wide electrical request the last committed tick presented
		// to the shared pool; dispatchTargetKW is the net load after this
		// tick's solar that dispatchable sources are commanded against,
		// plus the startup cranking draw a legitimate generator start
		// still owes this tick (#578); dispatchCommittedKW is the capacity
		// actually deliverable this tick that the command booked within the
		// sources' physical limits - a stopped, starting, ramping or shut-
		// down machine contributes nothing it cannot serve (#578);
		// dispatchShortfallKW is the part of
		// the target no available source could take - published, never
		// fabricated. In Manual mode target/committed/shortfall stay zero:
		// the dispatcher is inert. Limit reporting lives in the Alert ledger.
		StationDispatchMode dispatchMode{StationDispatchMode::Manual};
		double dispatchDemandKW{0.0};
		double dispatchTargetKW{0.0};
		double dispatchCommittedKW{0.0};
		double dispatchShortfallKW{0.0};
		// Coolant loop electrical service this tick (#396): pump and
		// freeze-heater power actually drawn from the same allocation
		// pool as external loads — never a second budget.
		double coolantPumpKW{0.0};
		double coolantHeaterKW{0.0};
		// Chiller electrical service this tick (#397): compressor work
		// plus running auxiliaries drawn from the same allocation pool —
		// never a second budget.
		double chillerPowerKW{0.0};
		// Fuelled generator service this tick (#399): electrical output
		// added to the same allocation pool as solar, the startup draws
		// the cranking machines took ahead of load allocation, and the
		// waste and decay heat those generators routed to their sinks.
		double generatorOutputKW{0.0}; // sum of step-mean generator output, not ramp endpoints
		double generatorStartupDrawKW{0.0};
		double waterTreatmentPowerKW{0.0}; // treatment work drawn this tick (#402)
		double electrolyzerPowerKW{0.0};   // electrolysis work drawn this tick (#404)
		double co2CapturePowerKW{0.0};     // CO2 capture work drawn this tick (#404)
		double sabatierPowerKW{0.0};       // Sabatier reactor work drawn this tick (#405)
		double methaneProcessingPowerKW{0.0}; // methane processing work drawn this tick (#405)
		double microbialProcessingPowerKW{0.0}; // microbial digestion work drawn this tick (#407)
		double cropPowerKW{0.0}; // crop lamp and circulation work drawn this tick (#409)
		double algaePowerKW{0.0}; // algae lamp and culture circulation work drawn this tick (#411)
		// Fuel-cell backup service this tick (#406): electrical output
		// added to the same allocation pool as solar and the generators,
		// and the waste heat those cells routed to their sinks.
		double fuelCellOutputKW{0.0};
		double fuelCellWasteHeatKW{0.0};
		// Heat engine service this tick (#804): net electrical output added
		// to the same allocation pool as solar, generators and fuel cells -
		// supply earned from heat, never a load with a refund - and the
		// rejection heat those engines booked into their sinks.
		double heatEngineOutputKW{0.0};
		double heatEngineRejectionKW{0.0};
		double generatorWasteHeatKW{0.0};
		double generatorDecayHeatKW{0.0};
		// #416: the protected critical service admission for this tick.
		// Protected services - containment circulation, biological
		// circulation, authored critical cooling and generator shutdown
		// cooling - draw ahead of every discretionary external load from
		// the actual generation-and-battery pool, never from nameplate
		// totals. protectedDemandKW is the protected demand presented to
		// the pool, protectedSuppliedKW what the pool delivered, and
		// protectedUnmetKW the unmet critical minimum that stays visible
		// with its reason. The corresponding Monitor reports on the Alert ledger.
		double protectedDemandKW{0.0};
		double protectedSuppliedKW{0.0};
		double protectedUnmetKW{0.0};
		std::vector<StationProtectedServiceSnapshot> protectedServices;
		// #417 recovery staging summary. recoveryPending is true while any
		// purely discretionary load is Shed or Restoring - the station is in
		// a recovery stage rather than steady state. recoveringCount counts
		// loads currently Restoring, and recoverySurgeKW is the total startup
		// surge those loads drew this tick. The surge is held out of battery
		// charging so depleted reserves refill only from the surplus the
		// recovery does not need, never overloading a service that is still
		// coming back.
		bool recoveryPending{false};
		int recoveringCount{0};
		double recoverySurgeKW{0.0};
		std::vector<StationSolarSourceSnapshot> sources;
		std::vector<StationEquipmentLoadSnapshot> loads;
		std::vector<StationBatterySnapshot> batteries;
		std::vector<StationGeneratorSnapshot> generators;
		// #879 expand step: the single bus wraps the unchanged pool account.
		// Multi-bus allocation is implemented separately in #880.
		std::vector<StationBusPowerSnapshot> buses;
		// #882: which bus serves each Transfer-mode load. The per-bus accounts
		// above say what a bus carried; this says which side of its automatic
		// transfer switch each load sat on, so a reader can see a failover and a
		// dwell-gated return without diffing two bus ledgers. Authored order.
		std::vector<StationSupplyTransferSnapshot> supplyTransfers;
		// #883: how each Shared-mode load split its demand across its buses this
		// tick. Where supplyTransfers says which one bus served a Transfer load,
		// this says how much of a Shared load every one of its buses carried, so
		// a combined feed and a zero-spare bus are both readable directly.
		// Authored order.
		std::vector<StationSharedSupplySnapshot> sharedSupplies;
		bool operator==(StationPowerSnapshot const&) const = default;
	};
	// One radiator as of the most recent tick: the authored rejection
	// contract beside the heat it actually rejected at the reservoir's
	// emitting temperature (#395).
	struct StationRadiatorSnapshot
	{
		std::string id;
		std::string reservoirId;
		double areaM2{0.0};
		double emissivity{0.9};
		double viewFactor{1.0};
		double sinkTemperatureC{0.0};
		double lastRejectedKW{0.0}; // signed net radiative exchange this tick
		// #844: the panel's finite coolant charge, the fraction of panel
		// area still holding coolant (the rejection cap), the cumulative
		// puncture booking, and the active-Fault ledger. A radiator with no
		// puncture opt-in reports the full charge, full area and no Faults.
		std::vector<StationFaultSnapshot> activeFaults;
		double panelCoolantKg{0.0};
		double panelCoolantCapacityKg{0.0};
		double remainingAreaFraction{1.0};
		double leakedCoolantKg{0.0};
		double leakedCoolantEnergyKJ{0.0};
		double availabilityFactor{1.0};
		bool operator==(StationRadiatorSnapshot const&) const = default;
	};
	// One thermal reservoir as of the most recent tick: the authored
	// capacity and limits beside the integrated temperature, the heat
	// flows and the cumulative energy ledger. Energy is tracked against
	// a 0 degrees C reference: current energy is capacity x temperature,
	// and starting + absorbed + routed - rejected must equal it within
	// the declared tolerance. Above/below-limit flags report crossings of
	// the declared service limits; they never clamp the state.
	struct StationLightingFixtureSnapshot
	{
		std::string id;
		std::string atmosphereId;
		double requestedElectricalKW{}, suppliedElectricalKW{};
		// Profile/standby service intent before outage, maintenance or Fault caps.
		double requestedServiceKW{};
		double deliveredLightKW{}, lumens{};
		double conversionHeatKW{}, reabsorbedLightKW{}, heatIntoCompartmentKW{};
		double requestedElectricalEnergyKJ{}, suppliedElectricalEnergyKJ{};
		double deliveredLightEnergyKJ{}, luminousExposureLmSeconds{};
		double conversionHeatEnergyKJ{}, reabsorbedLightEnergyKJ{}, heatIntoCompartmentEnergyKJ{};
		double availabilityFactor{1.0};
		std::vector<StationFaultSnapshot> activeFaults;
		bool operator==(StationLightingFixtureSnapshot const&) const = default;
	};

	struct StationHeatSourceSnapshot
	{
		std::string id;
		std::string targetReservoirId;
		std::string thermalClass;
		double ratedOutputKW{};
		double deliveredOutputKW{};
		double deliveredEnergyKJ{};
		double decayFraction{};             // permanent shared Wear fraction
		double decayedOutputKW{};
		bool operator==(StationHeatSourceSnapshot const&) const = default;
	};

	struct StationThermalReservoirSnapshot
	{
		std::string id;
		std::string thermalClass;
		double heatCapacityKJPerK{0.0};
		double startingTemperatureC{0.0};
		double temperatureC{0.0};
		double minTemperatureC{0.0};
		double maxTemperatureC{0.0};
		double absorbedHeatKW{0.0};
		double primaryHeatInputKW{};
		double primaryHeatInputEnergyKJ{};
		double lastHeatRoutedKW{0.0};   // equipment heat routed in this tick
		// Net heat this tick from coolant loops coupled to this reservoir
		// (#396): the loop side of the heat-only exchanger exchange, so
		// the reservoir ledger closes over exactly the terms that moved.
		double lastLoopHeatKW{0.0};
		// Condenser heat this tick rejected into this reservoir by the
		// chillers that use it as their hot-side sink (#397): the sink
		// side of the chiller booking, so the reservoir ledger closes
		// over exactly the terms that moved.
		double lastCondenserHeatKW{0.0};
		// Net heat this tick from atmosphere compartments coupled to this
		// reservoir (#400): the reservoir side of the compartment's
		// heat-only exchanger exchange, so the reservoir ledger closes
		// over exactly the terms that moved. Positive is the reservoir
		// gaining from the compartment, negative is the reservoir giving
		// heat to a colder compartment.
		double lastConductedKW{0.0};
		double conductedEnergyKJ{0.0};
		double lastAtmosphereHeatKW{0.0};
		// Generator heat this tick routed into this reservoir by the
		// generators that use it as their heat sink (#399): waste heat
		// from electrical production, startup cranking dissipation and
		// post-shutdown decay heat, booked once at the sink boundary.
		double lastGeneratorHeatKW{0.0};
		// Electrolysis process heat this tick routed into this reservoir
		// by the cells that use it as their heat sink (#404): the part of
		// the electrical dose the split did not retain as chemical energy
		// in the products, booked once at the sink boundary.
		double lastElectrolysisHeatKW{0.0};
		// Fuel-cell waste heat this tick routed into this reservoir by the
		// backup cells that use it as their heat sink (#406): the chemical
		// energy the conversion did not deliver as electricity, booked
		// once at the sink boundary.
		double lastFuelCellHeatKW{0.0};
		// Sabatier heat this tick routed into this reservoir by the
		// reactors that use it as their heat sink (#405): the exothermic
		// reaction heat the products do not retain plus the electrical
		// dose the reactor drew, booked once at the sink boundary.
		double lastSabatierHeatKW{0.0};
		// Methane-processing waste heat this tick routed into this
		// reservoir by the processors that use it as their heat sink
		// (#405): the part of the electrical dose the endothermic split
		// did not absorb as reaction heat, booked once at the sink
		// boundary.
		double lastMethaneProcessingHeatKW{0.0};
		// Microbial digestion heat this tick routed into this reservoir
		// by the processors that use it as their heat sink (#407): the
		// exothermic reaction heat the products do not retain plus the
		// electrical dose the processor drew, booked once at the sink
		// boundary.
		double lastMicrobialHeatKW{0.0};
		double microbialEnergyKJ{0.0}; // cumulative microbial reaction + process heat into the reservoir (#407)
		// Buffer exchange this tick (#398): heat the thermal buffers
		// hosted on this reservoir absorbed out of it (charge) and
		// returned to it (discharge). Both are booked once at the
		// reservoir boundary so the ledger closes over exactly the
		// terms that moved: the charge leaves the reservoir into the
		// buffer's finite store, the discharge re-enters it and must
		// be carried by the rejection path.
		double lastBufferChargeKW{0.0};
		double lastBufferDischargeKW{0.0};
		// Heat engine exchange this tick (#804): heat the engines using this
		// reservoir as their hot source drew out of it, and heat the engines
		// using it as their rejection sink booked into it. Both are booked
		// once at the reservoir boundary so the ledger closes over exactly
		// the terms that moved: the extraction is the engine's intake, the
		// rejection is the intake's remainder after the conversion.
		double lastEngineExtractionKW{0.0};
		double lastEngineRejectionKW{0.0};
		double lastHeatInKW{0.0};       // absorbed + routed + loop + condenser + generator + electrolysis + fuel cell this tick
		double lastRejectedKW{0.0};     // radiator net rejection this tick
		double lastNetKW{0.0};          // heat in minus rejected this tick
		double startingEnergyKJ{0.0};
		double currentEnergyKJ{0.0};
		double absorbedEnergyKJ{0.0};
		double routedEnergyKJ{0.0};
		double loopEnergyKJ{0.0}; // cumulative net loop exchange into the reservoir
		double condenserEnergyKJ{0.0}; // cumulative condenser heat rejected into the reservoir
		double generatorEnergyKJ{0.0}; // cumulative generator waste/startup/decay heat into the reservoir
		double electrolysisEnergyKJ{0.0}; // cumulative electrolysis process heat into the reservoir
		double fuelCellEnergyKJ{0.0}; // cumulative fuel-cell waste heat into the reservoir
		double sabatierEnergyKJ{0.0}; // cumulative Sabatier reaction and process heat into the reservoir (#405)
		double methaneProcessingEnergyKJ{0.0}; // cumulative methane-processing waste heat into the reservoir (#405)
		double bufferChargeEnergyKJ{0.0}; // cumulative heat absorbed out of the reservoir by buffers
		double bufferDischargeEnergyKJ{0.0}; // cumulative heat returned into the reservoir by buffers
		double engineExtractionEnergyKJ{0.0}; // cumulative heat drawn out of the reservoir by heat engines (#804)
		double engineRejectionEnergyKJ{0.0}; // cumulative heat booked into the reservoir by heat engines (#804)
		double atmosphereEnergyKJ{0.0}; // cumulative net compartment exchange into the reservoir
		double rejectedEnergyKJ{0.0};
		double residualKJ{0.0}; // starting + hazard + absorbed + routed + loop + condenser + generator + electrolysis + fuel cell + Sabatier + methane processing + microbial + atmosphere + buffer discharge + engine rejection - buffer charge - engine extraction - rejected - current
		double hazardEnergyKJ{0.0}; // cumulative Trip hazard energy booked in (#841)
		bool operator==(StationThermalReservoirSnapshot const&) const = default;
	};
	// One coolant loop as of the most recent tick (#396): the authored
	// contract beside the runtime's finite coolant inventory, the pump
	// and heater service results, and the loop's own thermal ledger. The
	// loop's energy is tracked against the same 0 degrees C reference as
	// the reservoirs: starting + pump work + heater + exchanged from the
	// reservoir must equal capacity x temperature within the declared
	// tolerance. Pump work and heater heat enter the loop exactly once
	// and reach the reservoir only through the circulation-gated
	// exchanger — never a second booking. belowFreezeSetpoint exposes
	// the freeze condition; it never clamps the integrated state.
	struct StationCoolantLoopSnapshot
	{
		std::vector<StationFaultSnapshot> activeFaults;
		double leakedCoolantKg{0.0}, leakedCoolantEnergyKJ{0.0};
		std::string id;
		std::string reservoirId;
		double coolantCapacityKg{0.0};
		double coolantKg{0.0};
		double fillFraction{0.0};           // coolant / capacity
		double pumpPowerKW{0.0};
		double ratedFlowKgPerS{0.0};
		double pumpDemandKW{0.0};          // inventory-sustainable pump draw
		double pumpSuppliedKW{0.0};
		double pumpUnmetKW{0.0};
		double circulationFraction{0.0};   // fraction of rated flow actually circulating
		double flowKgPerS{0.0};
		double unmetFlowKgPerS{0.0};
		double heaterCapacityKW{0.0};
		double heaterDemandKW{0.0};        // thermostat demand this tick
		double heaterSuppliedKW{0.0};
		double heaterUnmetKW{0.0};
		double loopTemperatureC{0.0};
		double startingTemperatureC{0.0};
		double freezeProtectionSetpointC{0.0};
		double exchangerConductanceKWPerK{0.0};
		// Signed heat this tick: positive is the loop absorbing from the
		// reservoir through the exchanger, negative is the loop dumping
		// into it. The reservoir books the exact negation — one exchange,
		// booked once on each side.
		double exchangeIntoLoopKW{0.0};
		// Chiller extraction this tick (#397): heat the chiller on this
		// cold-side loop actually extracted from the circulating coolant.
		// It enters the loop ledger as a removal exactly once, so the
		// loop's energy closes over it.
		double chillerExtractionKW{0.0};
		bool belowFreezeSetpoint{false};
		bool disabled{false};              // zero coolant: no fluid, no service
		double availabilityFactor{1.0};   // circulation capacity available under outages (#413)
		// The Condition layer (#840): the aggregate intactness of the
		// machine, the stressors actually eroding it this tick, and the
		// funded recovery in flight. Reported for every loop; a loop with
		// no Condition model reports a full-condition, un-stressed,
		// not-restoring machine, which is exactly what it is.
		double condition{1.0};            // [0, 1]; one is fully capable
		double lastConditionDelta{0.0};   // signed change over the last tick
		std::string conditionStressors;   // '+'-joined active stressors, empty when none
		bool restoring{false};            // funded recovery advanced this tick
		double restoreTargetCondition{0.0};// commanded target, 0 when none
		double restoreStockConsumedUnits{0.0}; // cumulative service stock spent
		double conditionCapability{1.0};  // delivered-capability fraction Condition leaves, [0, 1]
		// #843: the permanent wear fade and the accumulators that drive
		// it. The effective rated flow is the authored rating scaled by
		// (1 - wearFraction): the circulation the worn pump can actually
		// deliver now. A loop with no Wear model reports zero wear and an
		// effective rating equal to the authored one.
		double wearFraction{0.0};
		double wearOperatingHours{0.0};
		double wearThroughputKg{0.0};
		double wearCalendarHours{0.0};
		double effectiveRatedFlowKgPerS{0.0};
		double startingEnergyKJ{0.0};
		double currentEnergyKJ{0.0};
		double pumpWorkEnergyKJ{0.0};
		double heaterEnergyKJ{0.0};
		double exchangedEnergyKJ{0.0};
		double extractedEnergyKJ{0.0}; // cumulative chiller extraction out of the loop
		// Condenser-side water recovery heat this tick and cumulatively
		// (#402): the latent heat the condensate gave up on the loop's
		// cold surface. It enters the loop ledger as an inflow exactly once
		// and never appears in the compartment's thermal ledger.
		double collectorHeatKW{0.0};
		double collectorHeatEnergyKJ{0.0};
		double residualKJ{0.0}; // starting + pump + heater + exchanged + collector - extracted - current
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationCoolantLoopSnapshot const&) const = default;
	};
	// One chiller as of the most recent tick (#397): the authored
	// contract beside the cold-side duty actually extracted, the
	// electrical work plus auxiliaries actually supplied, the condenser
	// heat actually rejected, and the unmet remainder. The chiller's own
	// energy ledger closes over extracted + work = condenser within the
	// declared tolerance: condenser heat equals extracted heat plus the
	// work and applicable auxiliaries at one consistent boundary, never
	// a duplicate booking. admitted with a blockReason exposes why a
	// feasible-looking demand could not run (sink over the maximum
	// condensing temperature, no cold-side circulation); the chilled
	// setpoint and sink-over-temperature conditions are reported, never
	// clamped into the integrated state.
	struct StationChillerSnapshot
	{
		std::string id;
		std::string thermalClass;
		std::string coldLoopId;
		std::string condenserReservoirId;
		double ratedCoolingCapacityKW{0.0};
		double cop{1.0};
		double auxiliaryPowerKW{0.0};
		double chilledSetpointC{0.0};
		double maxCondensingTemperatureC{0.0};
		double coolingDemandKW{0.0};      // cold service demanded this tick
		double coolingKW{0.0};           // cold-side duty actually extracted
		double coolingUnmetKW{0.0};
		double deliverableKW{0.0};       // capacity capped by cold-side circulation
		double powerDemandKW{0.0};      // work + auxiliaries demanded
		double powerSuppliedKW{0.0};
		double powerUnmetKW{0.0};
		double deliveredFraction{0.0};  // fraction of the power demand supplied
		double condenserHeatKW{0.0};    // extracted + supplied work and auxiliaries
		double coldLoopTemperatureC{0.0};
		double coldLoopCirculationFraction{0.0};
		double sinkTemperatureC{0.0};
		bool admitted{false};
		std::string blockReason;
		bool atChilledSetpoint{false};
		bool sinkOverMaxCondensing{false};
		bool disabled{false};            // zero rated capacity: no service
		double availabilityFactor{1.0}; // cooling capacity available under outages (#413)
		double extractedEnergyKJ{0.0};  // cumulative cold-side heat extracted
		double workEnergyKJ{0.0};       // cumulative electrical work + auxiliaries
		double condenserEnergyKJ{0.0};  // cumulative condenser heat rejected
		double residualKJ{0.0};         // extracted + work - condenser
		// #844: the chiller's finite refrigerant charge store, the
		// cumulative leak booking, and the active-Fault ledger. A chiller
		// with no refrigerant-leak opt-in reports zero charge fields and
		// no Faults.
		std::vector<StationFaultSnapshot> activeFaults;
		double refrigerantChargeKg{0.0};
		double refrigerantKg{0.0};
		double leakedRefrigerantKg{0.0};
		double leakedRefrigerantEnergyKJ{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationChillerSnapshot const&) const = default;
	};
	// The heat engine's own status vocabulary (#804): Disabled is the
	// authored zero rating, Idle is a live machine with nothing to convert
	// (dead gradient, too-cold source, or gross output that cannot cover
	// its own auxiliaries), Active is converting, and Tripped is the
	// protective refusal while the rejection reservoir sits at or above
	// the maximum sink temperature. The trip clears on its own when the
	// sink falls back below the gate: the second law is a reported
	// status, never a clamp on the integrated state.
	enum class StationHeatEngineStatus { Disabled, Idle, Active, Tripped };
	// One heat engine as of the most recent tick (#804): the authored
	// contract beside the thermal intake actually drawn, the net bus
	// power actually delivered, the rejection heat actually booked, and
	// the closing conversion ledger. The engine's ledger closes over
	// thermal input = net bus electrical + generation cable loss + rejection:
	// conversion funds both delivered work and external transport loss.
	// Actual efficiency is published beside the Carnot
	// efficiency at the same tick-start interface temperatures the
	// conversion used, so the operator can see how much of the ideal
	// the declared carnotFraction is really capturing.
	struct StationHeatEngineSnapshot
	{
		std::string id;
		std::string thermalClass;
		std::string hotReservoirId;
		std::string rejectionReservoirId;
		double ratedThermalInputKW{0.0};
		double commandedInputKW{0.0};      // the intake dispatch in force, [0, rated]
		// The availability the outage and maintenance contracts leave this
		// engine for the current tick (#807): 1.0 in full service, less
		// during a partial outage, 0.0 when an outage took the whole rating
		// or a maintenance job holds the engine isolated. The deliverable
		// intake is capped at ratedThermalInputKW x availabilityFactor.
		double availabilityFactor{1.0};
		double carnotFraction{0.0};
		double auxiliaryPowerKW{0.0};
		double minHotTemperatureC{0.0};
		double maxSinkTemperatureC{0.0};
		double lastThermalInputKW{0.0};      // heat delivered through the intake connection this tick
		double lastElectricalOutputKW{0.0};  // net power delivered to the bus this tick
		double lastRejectionKW{0.0};        // rejection heat produced (before transport loss/retention)
		double lastAuxKW{0.0};             // auxiliaries carried by the conversion this tick
		std::string lastLimiter;            // Disabled/OutOfService/CapacityReduction/NoGradient/TooCold/SinkOver/AuxiliaryPower/HotSideSupply/IntakeConnection/CommandedInput/DemandAcceptance/GenerationConnection
		double efficiencyNow{0.0};        // actual eta used by the last conversion
		double carnotEfficiencyNow{0.0};  // Carnot eta at the same interface temperatures
		double hotTemperatureC{0.0};
		double sinkTemperatureC{0.0};
		bool disabled{false};              // zero rated thermal input: no conversion, ever
		bool tripped{false};               // rejection reservoir at or above the max sink gate
		double thermalInputEnergyKJ{0.0};  // cumulative heat delivered to the machine
		double electricalEnergyKJ{0.0};    // cumulative net power delivered to the bus
		double rejectionEnergyKJ{0.0};     // cumulative rejection produced, including lost/retained heat
		double residualKJ{0.0};            // input - (bus electrical + cable loss + rejection): closes on zero
		// #844: the engine's active-Fault ledger. A seized engine reports
		// its `mechanical-seizure` entry with full availability impact.
		std::vector<StationFaultSnapshot> activeFaults;
		StationHeatEngineStatus status{StationHeatEngineStatus::Disabled};
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationHeatEngineSnapshot const&) const = default;
	};
	// One thermal buffer as of the most recent tick (#398): the authored
	// contract beside the finite stored energy, the derived buffer
	// temperature across the usable band, the access gate state, the
	// charge/discharge actually moved this tick, the exhaustion flags
	// and the closing storage ledger. The buffer's ledger closes over
	// starting + charged - discharged = stored within the declared
	// tolerance: energy enters or leaves the store only through the
	// booked exchange, never a silent reset. The available capacity is
	// the headroom the store can still take; full means the store has
	// no headroom left to absorb (exhausted for ride-through),
	// depleted means it has nothing left to return.
	struct StationThermalBufferSnapshot
	{
		std::string id;
		std::string reservoirId;
		std::string accessMode;
		std::string circulationLoopId;
		double capacityKJ{0.0};
		double storedEnergyKJ{0.0};
		double availableCapacityKJ{0.0};   // capacity - stored
		double fillFraction{0.0};          // stored / capacity
		double minUsableTemperatureC{0.0};
		double maxUsableTemperatureC{0.0};
		double bufferTemperatureC{0.0};    // derived from stored energy across the band
		double maxChargeKW{0.0};
		double maxDischargeKW{0.0};
		double transferConductanceKWPerK{0.0};
		bool accessible{false};            // access gate open this tick
		double lastChargeKW{0.0};          // heat absorbed out of the host this tick
		double lastDischargeKW{0.0};       // heat returned into the host this tick
		bool full{false};                  // no headroom left to absorb
		bool depleted{false};              // nothing left to return
		std::string lastLimiter;           // what capped or blocked the exchange
		double startingEnergyKJ{0.0};
		double chargedEnergyKJ{0.0};
		double dischargedEnergyKJ{0.0};
		double residualKJ{0.0};            // starting + charged - discharged - stored
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationThermalBufferSnapshot const&) const = default;
	};
	// The cold-service rollup for one thermal service class (#397):
	// what the chillers of this class were asked to provide, what they
	// delivered, the work and auxiliaries they drew, the condenser heat
	// they rejected, and the demand that stayed unmet. Classes are
	// segregated: this is the account of one class's own capacity, and
	// another class's spare capacity never appears here.
	struct StationCoolingClassSnapshot
	{
		std::string thermalClass;
		double capacityKW{0.0};        // summed rated capacity of the class's chillers
		double providedKW{0.0};        // cold-side duty delivered this tick
		double unmetKW{0.0};           // demand not delivered this tick
		double workKW{0.0};            // electrical work + auxiliaries supplied
		double condenserKW{0.0};       // condenser heat rejected this tick
		bool operator==(StationCoolingClassSnapshot const&) const = default;
	};
	struct StationHullSnapshot
	{
		double temperatureC{};
		double absorbedKW{}, emittedKW{}; // duration-weighted means over last Station step
		double absorbedEnergyKJ{}, emittedEnergyKJ{};
		double conductedKW{}, conductedKJ{}; // signed gain, separate from external radiation
		double startingEnergyKJ{}, currentEnergyKJ{}, residualKJ{};
		bool belowMin{}, aboveMax{};
		bool operator==(StationHullSnapshot const&) const = default;
	};
	struct StationThermalSnapshot
	{
		std::optional<StationHullSnapshot> hull;
		std::vector<StationThermalReservoirSnapshot> reservoirs;
		std::vector<StationRadiatorSnapshot> radiators;
		double totalAbsorbedKW{0.0};
		double totalPrimaryHeatInputKW{};
		double totalRoutedKW{0.0};
		double totalRejectedKW{0.0};
		// Coolant service totals (#396): actual circulation and actual
		// freeze-protection heating delivered this tick.
		double totalCirculationKgPerS{0.0};
		double totalFreezeHeatingKW{0.0};
		// Cold-service totals by thermal service class (#397): the
		// delivered cold-side duty, electrical work plus auxiliaries,
		// condenser heat and unmet demand rolled up per declared class.
		// Spare capacity in one class is never usable by another, so the
		// rollup is per class, never a single pooled number.
		std::vector<StationCoolingClassSnapshot> coolingByClass;
		double totalCoolingProvidedKW{0.0};
		double totalCoolingUnmetKW{0.0};
		double totalCondenserHeatKW{0.0};
		// Thermal buffer totals (#398): the finite stored energy and
		// available capacity across every buffer, and the charge and
		// discharge actually moved this tick. The per-buffer detail
		// lives in the snapshot's thermalBuffers.
		double totalBufferStoredKJ{0.0};
		double totalBufferAvailableCapacityKJ{0.0};
		double totalBufferChargeKW{0.0};
		double totalBufferDischargeKW{0.0};
		bool operator==(StationThermalSnapshot const&) const = default;
	};

	// One compartment atmosphere as of the most recent tick (#400): the
	// authored contract beside the initial versus current gas inventory,
	// the derived absolute and partial pressures, the humidity state, the
	// last tick's flows through every pathway (occupancy, condensation,
	// leakage, airlock cycling), the exposed conditions and the closing
	// per-component and thermal ledgers. The compartment's gas ledger
	// closes per component: starting + added - consumed/condensed/leaked/
	// cycled must equal the current inventory within the declared
	// tolerance, and the thermal ledger closes over occupant heat, latent
	// heat and the reservoir exchange against capacity x temperature.
	// Every gram that left the compartment is visible as leaked,
	// recovered or lost - containment is proven by the ledger, not by a
	// pressure setpoint. Limits are reported, never clamped.
	enum class StationAtmospherePathwayStatus { Idle, Active };
	struct StationAtmospherePathwaySnapshot
	{
		std::string id;
		std::string aAtmosphereId;
		std::string bAtmosphereId;
		double areaM2{0.0};
		double openFraction{0.0};
		// Positive means A to B. Rates are averages over the last step;
		// totals are signed net mass since configuration, in StationPathwayGases order.
		std::array<double, 4> flowRateKgPerS{};
		std::array<double, 4> netTransferredKg{};
		double dischargeCoefficient{0.7};
		StationAtmosphereFlowRegime flowRegime{StationAtmosphereFlowRegime::Sealed};
		double pressureRatio{1.0}; // instantaneous downstream/upstream absolute pressure
		double bulkFlowRateKgPerS{0.0}; // last-step mean; excludes counterdiffusion
		// Advected enthalpy (#802): the signed net thermal energy this
		// pathway has carried with its moving gas since configuration,
		// positive A to B, and the rate averaged over the last step. It is
		// reported separately from a Thermal link's conducted heat so a
		// hatch's contribution never blurs into a wall's. There is
		// deliberately no requested/actual/limiter shape: nothing asks for
		// the heat to move - it is wholly determined by the temperature
		// difference and the mass flow, same reasoning as #793/#794.
		// For an Exterior pathway this is the energy carried away to
		// vacuum, booked as an explicit loss, never silently destroyed.
		double advectedKJ{0.0};
		double lastAdvectedKW{0.0};
		StationAtmospherePathwayStatus status{StationAtmospherePathwayStatus::Idle};
		bool operator==(StationAtmospherePathwaySnapshot const&) const = default;
	};

	// Reporting vocabulary for a Thermal link is flux, not service
	// (#794): nothing asks for heat to move - the flow is wholly determined
	// by the temperature difference - so there is deliberately no
	// requested / actual / limiter field. Forcing the service shape would
	// make every link report that it received everything it asked for on
	// every tick, which reads as a healthy service and reports nothing.
	enum class StationThermalLinkStatus { Idle, Active };
	struct StationThermalLinkSnapshot
	{
		std::string id;
		std::string aAtmosphereId;
		std::string bAtmosphereId;
		double conductanceKWPerK{0.0};
		// Positive means A to B. The rate is the average over the last
		// step; the total is the signed net energy since configuration.
		double flowRateKW{0.0};
		double netTransferredKJ{0.0};
		StationThermalLinkStatus status{StationThermalLinkStatus::Idle};
		ThermalNodeKind aKind{ThermalNodeKind::Atmosphere};
		ThermalNodeKind bKind{ThermalNodeKind::Atmosphere};
		bool operator==(StationThermalLinkSnapshot const&) const = default;
	};

	// #867 (spec §4.1): one row of the atmosphere habitability default table.
	// `key` is the stable identity suffix, so the pO2-low default for the
	// compartment `room` is authored as the Monitor `room-po2-low`.
	struct StationMonitorTemplate
	{
		std::string_view key;
		std::string_view metric;
		AlertRule rule;
		AlertSeverity severity;
		MonitorProvenance provenance;
	};
	// The twelve §4.1 rows, in table order.
	std::vector<StationMonitorTemplate> const& stationAtmosphereHabitabilityTemplates();
	// The §4.1 table expanded over every atmosphere compartment in
	// `settings`: twelve disabled, provenance-tagged Monitors per compartment,
	// with deterministic `<subjectId>-<key>` ids. The expansion is pure: it
	// reads the configuration and authors settings, and never touches runtime.
	std::vector<StationMonitorSettings> stationAtmosphereDefaultMonitors(
		StationSystemsSettings const& settings);
	// Append the defaults this configuration is missing. Idempotent and
	// non-destructive: a Monitor already present keeps its authored values,
	// so a designer's per-Monitor tuning survives a re-apply.
	void applyStationAtmosphereDefaultMonitors(StationSystemsSettings& settings);
	// #871: shared store/connection catalogue. Reads only published values;
	// absent subjects or unavailable stream quality return no sample.
	struct StationSystemsSnapshot;
	std::optional<double> readStationMonitorMetric(StationSystemsSettings const&, StationSystemsSnapshot const&,
		std::string_view type, std::string_view id, std::string_view metric);
	std::vector<StationMonitorTemplate> const& stationStoreMonitorTemplates();
	std::vector<StationMonitorTemplate> const& stationConnectionMonitorTemplates();
	std::vector<StationMonitorSettings> stationStoreConnectionDefaultMonitors(StationSystemsSettings const&);
	void applyStationStoreConnectionDefaultMonitors(StationSystemsSettings&);
	std::vector<StationMonitorTemplate> const& stationPowerSourceMonitorTemplates(std::string_view subjectType);
	std::vector<StationMonitorSettings> stationPowerSourceDefaultMonitors(StationSystemsSettings const&);
	void applyStationPowerSourceDefaultMonitors(StationSystemsSettings&);
	std::vector<StationMonitorTemplate> const& stationThermalPlantMonitorTemplates(std::string_view subjectType);
	std::vector<StationMonitorSettings> stationThermalPlantDefaultMonitors(StationSystemsSettings const&);
	void applyStationThermalPlantDefaultMonitors(StationSystemsSettings&);
	std::vector<StationMonitorTemplate> const& stationEclssBiologyMonitorTemplates(std::string_view subjectType);
	std::vector<StationMonitorSettings> stationEclssBiologyDefaultMonitors(StationSystemsSettings const&);
	void applyStationEclssBiologyDefaultMonitors(StationSystemsSettings&);

	// Compatibility reporting for the pre-Monitor limits. These immediate rules
	// remain enabled when omitted by old documents; an authored ID overrides them,
	// including explicit disable. No physical control consumes their Alerts.
	std::vector<StationMonitorSettings> stationLegacyReportingMonitors(StationSystemsSettings const&);
	void applyStationLegacyReportingMonitors(StationSystemsSettings&);
	std::vector<StationMonitorSettings> stationEffectiveMonitors(StationSystemsSettings const&);
	// One scheduled airlock cycle as of the most recent tick (#400): the
	// authored schedule beside the runtime's cycle history - what each
	// occurrence removed from its compartment, how much was recovered
	// into the receiver and how much was lost through the outer hatch,
	// and when the next cycle is due. A cycle that found no gas to draw
	// is refused whole and still counts as an occurrence, so a schedule
	// never silently disappears.
	struct StationAirlockCycleSnapshot
	{
		std::vector<StationFaultSnapshot> activeFaults;
		double faultLostKg{0.0}; // subset of lostTotal, never an additional removal
		std::string id;
		std::string atmosphereId;
		double gasMassKg{0.0};
		double firstOccurrenceSeconds{0.0};
		double recurrenceSeconds{0.0}; // 0 = one-off
		double recoveredFraction{0.0};
		int priority{0};
		uint64_t occurrencesCompleted{0};
		double lastOccurrenceSeconds{0.0};
		double lastRemovedKg{0.0};
		double lastRecoveredKg{0.0};
		double lastLostKg{0.0};
		double removedTotal{0.0};
		double recoveredTotal{0.0};
		double lostTotal{0.0};
		bool exhausted{false};
		bool hasNextOccurrence{false};
		double nextOccurrenceSeconds{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationAirlockCycleSnapshot const&) const = default;
	};
	// One crew as of the most recent tick (#401): the authored contract
	// beside the last tick's requested-versus-actual exchange on every
	// metabolic pathway, the uniform metabolic scale the supplies and
	// destination headroom sustained, the limiter that bound it, and the
	// closing cumulative ledger. The crew mass ledger closes over the
	// pathways: consumed (food + water + oxygen) = returned (CO2 +
	// moisture + urine + organic waste) + retained body mass within the
	// declared tolerance - the retained term is explicit, never an
	// unexplained disposal. Store-denominated amounts are in the
	// referenced store's unit; gases, moisture and heat are kg and kW.
	struct StationCrewSnapshot
	{
		std::string greywaterStoreId;
		double greywaterKgPerCrewDay{0.0};
		std::map<std::string, double> greywaterLoadKgPerCrewDay;
		double lastGreywaterKg{0.0}, greywaterKg{0.0};
		std::map<std::string, double> lastGreywaterConstituents, greywaterConstituents;
		StationWaterQualitySnapshot waterQuality;
		std::string id;
		std::string atmosphereId;
		int crewCount{0};
		std::string foodStoreId;
		std::string waterStoreId;
		std::string urineStoreId;
		std::string organicWasteStoreId;
		double activityLevel{0.0};   // evaluated activity profile level, [0, 1]
		double metabolicScale{0.0};  // supply/headroom-sustained fraction, [0, 1]
		std::string limiter;         // what bound the scale this tick (empty: none)
		// Last-tick requested / actual / unmet per pathway.
		double lastFoodRequested{0.0};
		double lastFoodActual{0.0};
		double lastFoodUnmet{0.0};
		double lastWaterRequested{0.0};
		double lastWaterActual{0.0};
		double lastWaterUnmet{0.0};
		double lastOxygenRequestedKg{0.0};
		double lastOxygenActualKg{0.0};
		double lastOxygenUnmetKg{0.0};
		double lastCo2ProducedKg{0.0};
		double lastMoistureProducedKg{0.0};
		double lastUrineRequested{0.0};
		double lastUrineActual{0.0};
		double lastUrineUnmet{0.0};
		double lastOrganicWasteRequested{0.0};
		double lastOrganicWasteActual{0.0};
		double lastOrganicWasteUnmet{0.0};
		double lastHeatKW{0.0};
		// Cumulative pathway ledger.
		double foodConsumed{0.0};
		double waterConsumed{0.0};
		double oxygenConsumedKg{0.0};
		double co2ProducedKg{0.0};
		double moistureProducedKg{0.0};
		double urineProduced{0.0};
		double organicWasteProduced{0.0};
		double heatKJ{0.0};
		double retainedKg{0.0};      // consumed mass the returns did not carry away
		bool disabled{false};        // zero crew count: no demand, no returns
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationCrewSnapshot const&) const = default;
	};
	// One water collector as of the most recent tick (#402): the authored
	// contract beside the condensate actually collected, the cold-surface
	// conditions that gated collection, and the latent heat routed into
	// the coolant loop. The collected mass leaves the compartment's vapour
	// and enters the raw water store once; the latent heat enters the loop
	// ledger once and never the compartment's, so the same condensation is
	// never a heat source twice. A collector that could not collect reports
	// the binding condition as a blockReason rather than silently idling.
	struct StationWaterCollectorSnapshot
	{
		std::string id;
		std::string atmosphereId;
		std::string coolingLoopId;
		std::string thermalClass;
		std::string destStoreId;
		double maxRateKgPerS{0.0};
		double targetRelativeHumidity{0.0};
		double lastRequestedKg{0.0};    // rate x dt demanded this tick
		double lastCondensableKg{0.0};  // vapour above the humidity target
		double lastCollectedKg{0.0};    // condensate actually delivered
		double lastLatentHeatKW{0.0};   // latent heat routed into the loop
		double lastCirculationFraction{0.0};
		bool coldSurfaceAvailable{false};
		std::string blockReason;        // why nothing was collected (empty: none)
		bool disabled{false};           // zero max rate: no collection
		double collectedKg{0.0};        // cumulative condensate delivered
		double latentHeatEnergyKJ{0.0}; // cumulative latent heat routed to the loop
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationWaterCollectorSnapshot const&) const = default;
	};
	// One water treatment as of the most recent tick (#402): the authored
	// contract beside the raw water actually processed, the treated water
	// and retained concentrate produced, the electricity and consumables
	// actually drawn, and the quality outcome. Full rated power qualifies
	// the treated output; curtailed power holds it unqualified. The
	// treatment's own mass ledger closes over processed = treated +
	// concentrate within the declared tolerance, and the concentrate stays
	// accounted as retained material pending a disposal capability.
	struct StationWaterTreatmentSnapshot
	{
		std::map<std::string, double> removalFractions; // authored overrides; omitted dry species = 1
		// kg per constituent (water included); the energy slot is kJ.
		std::map<std::string, double> lastRemovedConstituents, lastPassedConstituents;
		std::map<std::string, double> removedConstituents, passedConstituents;
		std::string id;
		std::string sourceStoreId;
		std::string treatedStoreId;
		std::string concentrateStoreId;
		std::string consumableStoreId;
		double throughputKgPerS{0.0};
		double recoveryRatio{0.0};
		double specificEnergyKWhPerKg{0.0};
		double consumablePerKg{0.0};
		int priority{0};
		double scale{0.0}; // fraction of rated throughput that ran, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastProcessedKg{0.0};
		double lastTreatedKg{0.0};
		double lastConcentrateKg{0.0};
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastConsumableRequested{0.0};
		double lastConsumableUsed{0.0};
		double lastConsumableUnmet{0.0};
		bool qualityAccepted{false};
		std::string holdReason; // why the output is held unqualified (empty: none)
		// The quality gates: filtration ran with the process, and the
		// treatment energy evidence is complete. The output qualifies on
		// the evidence alone; a curtailed dose holds it unqualified.
		bool filtrationCompleted{false};
		bool treatmentEvidenced{false};
		bool disabled{false};   // zero throughput: no processing
		double processedKg{0.0};
		double treatedKg{0.0};
		double acceptedKg{0.0};   // treated water admitted as qualified
		double heldKg{0.0};       // treated water held unqualified
		double concentrateKg{0.0}; // retained concentrate pending disposal
		double energySuppliedKJ{0.0};
		double energyOwedKJ{0.0}; // processed x specific energy at full cost
		double consumablesUsed{0.0};
		double residualKg{0.0}; // processed - treated - concentrate
		StationTransferStatus status{StationTransferStatus::Idle};
		std::vector<StationFaultSnapshot> activeFaults;
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationWaterTreatmentSnapshot const&) const = default;
	};
	// One electrolysis cell as of the most recent tick (#404): the authored
	// contract beside the water actually split, the oxygen and hydrogen
	// actually produced, the electrical dose actually supplied, the heat
	// actually routed to the sink, and the closing mass and energy ledgers.
	// The cell's mass ledger closes over the split: water = oxygen +
	// hydrogen within the declared tolerance, and its energy ledger closes
	// over supplied = chemical + heat: the electrical dose either ended up
	// bound in the products or routed into the sink as process heat -
	// never a third destination and never vanished.
	struct StationElectrolyzerSnapshot
	{
		StationWaterQualitySnapshot waterQuality;
		std::string id;
		std::string waterStoreId;
		std::string oxygenStoreId;
		std::string hydrogenStoreId;
		std::string atmosphereId;
		std::string thermalClass;
		std::string heatSinkId;
		double rateKgPerS{0.0};
		double specificEnergyKWhPerKgWater{0.0};
		double maxSinkTemperatureC{0.0};
		int priority{0};
		double scale{0.0}; // fraction of rated split the feed and headroom sustained, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastRequestedKg{0.0};   // rated split demanded this tick
		double lastSplitKg{0.0};       // water actually split this tick
		double lastOxygenKg{0.0};      // oxygen actually produced this tick
		double lastHydrogenKg{0.0};    // hydrogen actually produced this tick
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastUnmetProductionKg{0.0}; // split the supplied energy could not make
		bool coolingAvailable{false};
		std::string blockReason;       // why nothing ran this tick (empty: none)
		bool disabled{false};          // zero rated split: no production
		double waterSplitKg{0.0};
		double oxygenProducedKg{0.0};
		double hydrogenProducedKg{0.0};
		double energySuppliedKJ{0.0};
		double chemicalEnergyKJ{0.0};  // energy retained in the produced hydrogen
		double heatRoutedKJ{0.0};      // total process heat: to casing when present, otherwise sink
		double residualKg{0.0};        // split - oxygen - hydrogen
		double residualKJ{0.0};        // supplied - chemical - heat
		StationTransferStatus status{StationTransferStatus::Idle};
		std::vector<StationFaultSnapshot> activeFaults;
		double wearFraction{0.0};
		double wearOperatingHours{0.0};
		double effectiveRateKgPerS{0.0};
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool hasThermalBody{false};
		double bodyTemperatureC{0.0};
		double lastBodyChargeKW{0.0};
		double lastSinkLegKW{0.0}; // energy leaving casing, before connection loss
		double bodyChargeKJ{0.0};
		double sinkLegKJ{0.0};
		double bodyConductedKJ{0.0}; // signed gain through Thermal links
		double lastBodyConductedKW{0.0};
		double bodyResidualKJ{0.0}; // charge + conduction - sink outflow - stored energy change
		bool operator==(StationElectrolyzerSnapshot const&) const = default;
	};
	// One CO2 capture machine as of the most recent tick (#404): the
	// authored contract beside the CO2 actually captured, the unmet
	// atmospheric service, the electricity and consumable actually drawn,
	// and the closing capture ledger. Captured mass leaves the compartment
	// through a booked pathway and occupies the capture store: the ledger
	// shows where every kilogram went, so no carbon vanishes.
	struct StationCo2CaptureSnapshot
	{
		std::string id;
		std::string atmosphereId;
		std::string captureStoreId;
		std::string consumableStoreId;
		double maxRateKgPerS{0.0};
		double specificEnergyKWhPerKg{0.0};
		double consumablePerKg{0.0};
		int priority{0};
		double scale{0.0}; // fraction of rated capture the service limits sustained, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastRequestedKg{0.0};   // rated capture demanded this tick
		double lastCapturedKg{0.0};    // CO2 actually captured this tick
		double lastUnmetServiceKg{0.0}; // removal the compartment/store/consumable could not supply
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastConsumableRequested{0.0};
		double lastConsumableUsed{0.0};
		double lastConsumableUnmet{0.0};
		std::string blockReason;       // why nothing was captured this tick (empty: none)
		bool disabled{false};          // zero rated rate: no capture
		double capturedKg{0.0};        // cumulative CO2 captured into inventory
		double energySuppliedKJ{0.0};  // cumulative electricity drawn
		double consumablesUsed{0.0};   // cumulative consumable drawn
		StationTransferStatus status{StationTransferStatus::Idle};
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationCo2CaptureSnapshot const&) const = default;
	};
	// One fuel-cell backup unit as of the most recent tick (#406): the
	// authored contract beside the electrical output actually delivered,
	// the reactants actually consumed, the product water actually
	// delivered, the waste heat actually routed, and the closing mass and
	// energy ledgers. The cell's mass ledger closes over the reaction:
	// hydrogen + oxygen = water within the declared tolerance, and its
	// energy ledger closes over chemical = electrical + waste: the
	// reactants' chemical energy either left as electricity into the
	// shared pool or as waste heat into the sink - never a third
	// destination and never vanished. enduranceSeconds reports how long
	// the usable reactants (drawable above each store's protected
	// reserve, in stoichiometric balance) last at the reference output:
	// the cell's actual output this tick, or its rated output when the
	// cell is not running.
	struct StationFuelCellSnapshot
	{
		std::string id;
		std::string hydrogenStoreId;
		std::string oxygenStoreId;
		std::string waterStoreId;
		std::string thermalClass;
		std::string heatSinkId;
		double ratedOutputKW{0.0};
		double efficiency{0.0};
		double commandedOutputKW{0.0};
		// #415: the command the dispatch controller issued this tick, and
		// whether the running command came from the dispatcher rather than
		// the authored setpoint. In Manual mode the dispatch command is 0
		// and dispatchCommanded is false.
		double dispatchCommandKW{0.0};
		bool dispatchCommanded{false};
		double maxSinkTemperatureC{0.0};
		double lastOutputKW{0.0};        // electrical output delivered to the pool this tick
		double lastHydrogenKg{0.0};     // H2 consumed this tick
		double lastOxygenKg{0.0};       // O2 consumed this tick
		double lastWaterKg{0.0};        // product water delivered this tick
		double lastWasteHeatKW{0.0};    // waste heat routed into the sink this tick
		double lastUnmetOutputKW{0.0};  // commanded output the limits could not sustain
		double sinkTemperatureC{0.0};
		bool coolingAvailable{false};
		std::string limiter; // what bound the output this tick (empty: none)
		std::string blockReason; // why nothing ran this tick (empty: none)
		bool disabled{false};  // zero rated output: no backup service
		double hydrogenConsumedKg{0.0};
		double oxygenConsumedKg{0.0};
		double waterProducedKg{0.0};
		double chemicalEnergyKJ{0.0}; // reactant chemical energy consumed
		double electricalKJ{0.0};     // electrical energy delivered to the pool
		double wasteHeatKJ{0.0};      // waste heat routed into the sink
		double residualKg{0.0};       // hydrogen + oxygen - water
		double residualKJ{0.0};       // chemical - electrical - waste
		double enduranceSeconds{0.0}; // usable reactants at the reference output
		StationTransferStatus status{StationTransferStatus::Idle};
		std::vector<StationFaultSnapshot> activeFaults;
		double wearFraction{0.0};
		double wearOperatingHours{0.0};
		double effectiveRatedOutputKW{0.0};
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		std::string chemistry{"PEM"};
		bool operator==(StationFuelCellSnapshot const&) const = default;
	};
	// One Sabatier reactor as of the most recent tick (#405): the
	// authored contract beside the CO2 and hydrogen actually consumed,
	// the methane and water actually produced, the electrical dose
	// actually supplied, the heat actually routed to the sink, and the
	// closing mass and energy ledgers. The reactor's mass ledger closes
	// over the conversion: CO2 + hydrogen = methane + water within the
	// declared tolerance, and its energy ledger closes over supplied +
	// released = routed: the electrical dose plus the exothermic heat
	// the products do not retain all left through the sink - never a
	// third destination and never vanished.
	struct StationSabatierReactorSnapshot
	{
		std::string id;
		std::string co2StoreId;
		std::string hydrogenStoreId;
		std::string methaneStoreId;
		std::string waterStoreId;
		std::string thermalClass;
		std::string heatSinkId;
		double rateKgPerS{0.0};
		double specificEnergyKWhPerKgCO2{0.0};
		double maxSinkTemperatureC{0.0};
		int priority{0};
		double scale{0.0}; // fraction of rated conversion the feeds and headroom sustained, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastRequestedKg{0.0};   // rated CO2 conversion demanded this tick
		double lastCo2ConsumedKg{0.0};    // CO2 actually consumed this tick
		double lastHydrogenConsumedKg{0.0}; // H2 actually consumed this tick
		double lastMethaneProducedKg{0.0};  // CH4 actually produced this tick
		double lastWaterProducedKg{0.0};    // H2O actually produced this tick
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastUnmetConversionKg{0.0}; // conversion the supplied energy could not pay for
		double lastReactionHeatKW{0.0};    // exothermic reaction heat released this tick
		bool coolingAvailable{false};
		std::string blockReason;       // why nothing ran this tick (empty: none)
		bool disabled{false};          // zero rated conversion: no production
		double co2ConsumedKg{0.0};
		double hydrogenConsumedKg{0.0};
		double methaneProducedKg{0.0};
		double waterProducedKg{0.0};
		double energySuppliedKJ{0.0};
		double reactionHeatKJ{0.0};  // cumulative exothermic heat released by conversion
		double heatRoutedKJ{0.0};    // cumulative dose + released heat routed into the sink
		double residualKg{0.0};      // (co2 + hydrogen) - (methane + water)
		double residualKJ{0.0};      // (supplied + released) - routed
		StationTransferStatus status{StationTransferStatus::Idle};
		bool tripped{false};
		std::vector<StationFaultSnapshot> activeFaults;
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationSabatierReactorSnapshot const&) const = default;
	};
	// One methane processor as of the most recent tick (#405): the
	// authored contract beside the methane actually consumed, the
	// hydrogen actually returned and the solid carbon actually produced,
	// the electrical dose actually supplied, the reaction heat actually
	// absorbed and the waste heat actually routed, and the closing mass
	// and energy ledgers. The processor's mass ledger closes over the
	// split: methane = hydrogen + carbon within the declared tolerance,
	// and its energy ledger closes over supplied = absorbed + routed:
	// the electrical dose either drove the endothermic reaction or
	// left as process heat into the sink - never a third destination
	// and never vanished.
	struct StationMethaneProcessorSnapshot
	{
		std::string id;
		std::string methaneStoreId;
		std::string hydrogenStoreId;
		std::string carbonStoreId;
		std::string thermalClass;
		std::string heatSinkId;
		double rateKgPerS{0.0};
		double specificEnergyKWhPerKgCH4{0.0};
		double maxSinkTemperatureC{0.0};
		int priority{0};
		double scale{0.0}; // fraction of rated processing the feed and headroom sustained, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastRequestedKg{0.0};   // rated CH4 processing demanded this tick
		double lastMethaneConsumedKg{0.0};  // CH4 actually consumed this tick
		double lastHydrogenReturnedKg{0.0}; // H2 actually returned this tick
		double lastCarbonProducedKg{0.0};   // solid C actually produced this tick
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastUnmetProcessingKg{0.0}; // processing the supplied energy could not drive
		double lastReactionHeatKW{0.0};    // endothermic reaction heat absorbed this tick
		bool coolingAvailable{false};
		std::string blockReason;       // why nothing ran this tick (empty: none)
		bool disabled{false};          // zero rated processing: no service
		double methaneConsumedKg{0.0};
		double hydrogenReturnedKg{0.0};
		double carbonProducedKg{0.0};
		double energySuppliedKJ{0.0};
		double reactionHeatKJ{0.0};  // cumulative endothermic heat absorbed by processing
		double heatRoutedKJ{0.0};    // cumulative dose minus absorbed heat routed into the sink
		double residualKg{0.0};      // methane - hydrogen - carbon
		double residualKJ{0.0};      // supplied - absorbed - routed
		StationTransferStatus status{StationTransferStatus::Idle};
		bool tripped{false};
		std::vector<StationFaultSnapshot> activeFaults;
		// #848: the permanent Wear this machine has accumulated.
		StationMachineWearSnapshot wear;
		bool operator==(StationMethaneProcessorSnapshot const&) const = default;
	};
	// One microbial processor as of the most recent tick (#407): the
	// authored contract beside the feed, oxygen, process water and
	// consumable actually consumed, the recovered nutrients, recovered
	// water, CO2 and residue actually produced, the electrical dose
	// actually supplied and the heat actually routed, and the closing
	// mass and energy ledgers. The processor's mass ledger closes over
	// the digestion: feed + oxygen + process water = nutrients +
	// recovered water + CO2 + residue within the declared tolerance -
	// the residue is the balance term, so nothing vanishes and nothing
	// appears - and its energy ledger closes over supplied + released =
	// routed: the electrical dose plus the exothermic reaction heat the
	// products do not retain all left through the sink. The per-
	// constituent recovery totals show the recovered nitrogen, phosphorus,
	// potassium and grouped minerals separately, funded only by the
	// feed's bound nutrient constituents; the feed's atmospheric nitrogen
	// never appears in the recovered totals because no configured process
	// here converts it. The scale reports the fraction of rated feed the
	// feeds, destinations and supplied energy sustained, and the limiter
	// names what bound it.
	struct StationMicrobialProcessorSnapshot
	{
		StationWaterQualitySnapshot waterQuality;
		StationRateBasis rateBasis{StationRateBasis::FixedPerFeedMass};
		double organicOxidationFraction{0.0};
		double o2PerKgOrganicOxidized{0.0};
		double nitrificationFraction{0.0};
		double o2PerKgNitrifiedN{0.0};
		double co2PerKgOrganicOxidized{0.0};
		double processWaterTargetMoisture{0.0};
		double heatReleaseFraction{0.0};
		double consumablePerKgOrganicOxidized{0.0};
		std::map<std::string, double> recoveredWaterCarryoverFractions;
		std::map<std::string, double> lastCarriedConstituents, carriedConstituents;
		double lastNitrifiedNitrogenKg{0.0};
		double nitrifiedNitrogenKg{0.0};
		std::string id;
		std::string feedStoreId;
		std::string oxygenStoreId;
		std::string waterStoreId;
		std::string consumableStoreId;
		std::string nutrientStoreId;
		std::string recoveredWaterStoreId;
		std::string co2StoreId;
		std::string residueStoreId;
		std::string thermalClass;
		std::string heatSinkId;
		double rateKgPerS{0.0};
		double specificEnergyKWhPerKgFeed{0.0};
		double oxygenPerKgFeedKg{0.0};
		double processWaterPerKgFeedKg{0.0};
		double consumablePerKgFeed{0.0};
		double co2PerKgFeedKg{0.0};
		double nitrogenRecovery{0.0};
		double phosphorusRecovery{0.0};
		double potassiumRecovery{0.0};
		double mineralsRecovery{0.0};
		double waterRecovery{0.0};
		double reactionHeatKJPerKgFeed{0.0};
		double minOperatingTemperatureC{0.0};
		double maxSinkTemperatureC{0.0};
		double initialCondition{0.0};
		double starvationDegradePerHour{0.0};
		double consumableExhaustionDegradePerHour{0.0};
		double coldStressDegradePerHour{0.0};
		double heatStressDegradePerHour{0.0};
		double recoveryPerHour{0.0};
		std::string stockStoreId;
		double stockUnitsPerConditionPoint{0.0};
		int priority{0};
		// The condition picture (#408): the aggregate culture condition,
		// the signed change applied this tick, the adverse rules that
		// acted this tick (empty: benign), whether recovery advanced this
		// tick, and the replacement stock actually consumed.
		double condition{0.0};
		double lastConditionDelta{0.0};
		std::string lastStressors; // e.g. "starvation+heat stress" (empty: none acted)
		bool recovering{false};
		double lastStockConsumedUnits{0.0};
		double stockConsumedUnits{0.0};
		double scale{0.0}; // fraction of rated feed the feeds, destinations, condition and energy sustained, [0, 1]
		std::string limiter; // what bound the scale this tick (empty: none)
		double lastRequestedKg{0.0};   // rated feed demanded this tick
		double lastFeedConsumedKg{0.0};
		double lastOxygenConsumedKg{0.0};
		double lastWaterConsumedKg{0.0};
		double lastConsumableRequested{0.0};
		double lastConsumableUsed{0.0};
		double lastConsumableUnmet{0.0};
		double lastNutrientProducedKg{0.0};
		double lastRecoveredWaterKg{0.0};
		double lastCo2ProducedKg{0.0};
		double lastResidueProducedKg{0.0};
		double lastPowerDemandKW{0.0};
		double lastPowerSuppliedKW{0.0};
		double lastPowerUnmetKW{0.0};
		double lastUnmetProcessingKg{0.0}; // feed the supplied energy could not digest
		double lastReactionHeatKW{0.0};    // exothermic digestion heat released this tick
		bool temperatureEligible{false};   // interface inside the declared operating window
		std::string blockReason;           // why nothing ran this tick (empty: none)
		bool disabled{false};              // zero rated feed: no service
		double feedConsumedKg{0.0};
		double oxygenConsumedKg{0.0};
		double waterConsumedKg{0.0};
		double consumablesUsed{0.0};
		double nutrientProducedKg{0.0};
		double recoveredWaterKg{0.0};
		double co2ProducedKg{0.0};
		double residueProducedKg{0.0};
		double nitrogenRecoveredKg{0.0};   // recovered bound N, funded by the feed's nutrient nitrogen only
		double phosphorusRecoveredKg{0.0};
		double potassiumRecoveredKg{0.0};
		double mineralsRecoveredKg{0.0};
		double energySuppliedKJ{0.0};
		double reactionHeatKJ{0.0};  // cumulative exothermic heat released by digestion
		double heatRoutedKJ{0.0};    // cumulative dose + released heat routed into the sink
		double residualKg{0.0};      // (feed + oxygen + water) - (nutrients + recoveredWater + co2 + residue)
		double residualKJ{0.0};      // (supplied + released) - routed
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationMicrobialProcessorSnapshot const&) const = default;
	};
	// One crop population as of the most recent tick (#409): the authored
	// contract beside the dry Living biomass actually held, the declared
	// Growing capacity, the light actually delivered versus requested, the
	// growth and respiration actually achieved, every resource flow that
	// funded or resulted from them, the growth scale and the limiter that
	// bound it, and the closing mass and energy ledgers. The mass ledger
	// closes over the whole population: CO2 drawn + irrigation water drawn
	// + nutrients drawn + respiration oxygen drawn = oxygen released by
	// growth + CO2 returned by respiration + vapour returned (assimilation
	// water, transpiration and respiration water) + the change in dry
	// Living biomass - within the declared tolerance, so growth never
	// creates material out of light and respiration never destroys it.
	// The energy ledger closes over delivered = fixed + respired + heat:
	// the light energy actually supplied either became the declared
	// chemical energy of new biomass, came back out as respiration heat,
	// or left as heat into the compartment - nothing is free and nothing
	// closes over fixed + respired = delivered. Harvest (#410) removes
	// actual living material at the declared trigger and preserves its
	// actual constituents and remaining energy into food and waste stores; full
	// destinations constrain the harvest or route the edible surplus
	// through the waste path, and starvation death routes the dead
	// residue to waste or books it as explicit uncontained loss.
	struct StationCropPopulationSnapshot
	{
		// N/P/K supplied by selective irrigation (kg), not fertilizer ports.
		std::map<std::string, double> irrigationUptakeFractions;
		std::map<std::string, double> lastIrrigationUptake, irrigationUptake;
		StationWaterQualitySnapshot waterQuality;
		std::string id;
		std::string atmosphereId;
		std::string waterStoreId;
		std::string nitrogenStoreId;
		std::string phosphorusStoreId;
		std::string potassiumStoreId;
		std::string mineralsStoreId;
		ResourceCompositionSettings composition;
		double growingAreaM2{0.0};
		double maxBiomassDensityKgPerM2{0.0};
		double capacityKg{0.0};         // growingAreaM2 x maxBiomassDensityKgPerM2
		double initialBiomassKg{0.0};
		double circulationPowerKW{0.0};
		double growthYieldKgPerKWh{0.0};
		double respirationPerHour{0.0};
		double transpirationKgPerKgGrowth{0.0};
		double minGrowingTemperatureC{0.0};
		double maxGrowingTemperatureC{0.0};
		std::string foodStoreId;
		std::string wasteStoreId;
		double edibleFraction{0.0};
		double harvestTriggerKg{0.0};
		double harvestFraction{0.0};
		int priority{0};
		// The living-biomass picture: current dry Living biomass beside the
		// declared capacity and the fraction of it filled.
		double livingBiomassKg{0.0};
		double capacityFraction{0.0}; // livingBiomassKg / capacityKg, [0, 1]
		// The living constituent inventory (#571): the actual dry-mass
		// organic matter and bound nutrients the living stand carries,
		// summing to livingBiomassKg. Respiration burns only the organic
		// share, so the nutrients stay put until harvest or death routes
		// them out instead of being reclassified as fuel each tick.
		StationBiologicalConstituents livingConstituents;
		// The chemical-energy basis for the remaining organic material
		// (#571): declared dry-biomass energy over the declared organic
		// fraction, since bound nutrients carry no fuel. Respiration heat
		// and carried living energy both use it, so chemical energy fixed
		// equals energy respired plus energy still carried.
		double energyKJPerKgOrganic{0.0};
		double livingChemicalEnergyKJ{0.0};
		// Cumulative constituent routing (#571): where this population's
		// actual constituents went. Harvest splits by destination; death
		// residue splits between the waste path and uncontained loss.
		StationBiologicalConstituents harvestedToFoodConstituents;
		StationBiologicalConstituents harvestedToWasteConstituents;
		StationBiologicalConstituents deadResidueToWasteConstituents;
		StationBiologicalConstituents deadResidueLostConstituents;
		// The light and circulation actually delivered this tick.
		double lastIlluminationLevel{0.0};   // profile level [0, 1]
		double lastLightRequestedKW{0.0};
		double lastLightSuppliedKW{0.0};
		double lastCirculationRequestedKW{0.0};
		double lastCirculationSuppliedKW{0.0};
		// The growth the delivered resources funded this tick, its uniform
		// scale and the limiter that bound it, plus every input the growth
		// actually consumed and everything it returned.
		double lastGrowthPotentialKg{0.0}; // light-driven potential before limits
		double lastGrowthScale{0.0};       // uniform growth scale after limits, [0, 1]
		double lastResourceUnmetFraction{0.0}; // growth-resource shortage, excluding temperature/capacity
		std::string lastGrowthLimiter;     // what bound the growth scale (empty: none)
		double lastGrowthKg{0.0};
		double lastCo2DrawnKg{0.0};
		double lastAssimilationWaterKg{0.0};
		double lastNitrogenDrawnKg{0.0};
		double lastPhosphorusDrawnKg{0.0};
		double lastPotassiumDrawnKg{0.0};
		double lastMineralsDrawnKg{0.0};
		double lastOxygenReleasedKg{0.0};
		double lastTranspiredKg{0.0};
		// The respiration that ran this tick: the organic actually burned,
		// the oxygen actually drawn and the CO2, vapour and heat returned.
		double lastRespirationRequestedKg{0.0};
		double lastRespirationKg{0.0};
		double lastRespirationO2DrawnKg{0.0};
		std::string lastRespirationLimiter;
		double lastRespirationCo2ReturnedKg{0.0};
		double lastRespirationWaterKg{0.0};
		double lastRespirationHeatKW{0.0};
		bool lastOxygenLimited{false}; // respiration bound by compartment oxygen
		// The harvest that ran this tick (#410): the requested removal, the
		// living material actually taken, what landed in the food and waste
		// stores, the edible surplus that had to ride the waste path, what
		// the full destinations refused, and the limiter that bound it.
		double lastHarvestRequestedKg{0.0};
		double lastHarvestedKg{0.0};
		double lastFoodAddedKg{0.0};
		double lastWasteAddedKg{0.0};
		double lastSurplusEdibleToWasteKg{0.0};
		double lastHarvestConstrainedKg{0.0};
		std::string lastHarvestLimiter;
		// The adverse-condition loss (#410): once the living organic content
		// is respired away the population is dead, its remaining residue
		// routed to the waste store or booked as uncontained loss, and it
		// never grows, respires or harvests again.
		bool dead{false};
		double lastDeadResidueToWasteKg{0.0};
		double lastDeadResidueLostKg{0.0};
		// Heat into the compartment this tick: the light energy not fixed as
		// biomass chemical energy, plus the respiration heat.
		double lastHeatIntoCompartmentKW{0.0};
		bool temperatureEligible{false}; // compartment inside the growing window
		std::string blockReason;         // why nothing grew this tick (empty: none)
		bool disabled{false};            // zero growing area: no service
		// Cumulative pathway ledger.
		double growthKg{0.0};
		double co2DrawnKg{0.0};
		double waterDrawnKg{0.0};
		double nitrogenDrawnKg{0.0};
		double phosphorusDrawnKg{0.0};
		double potassiumDrawnKg{0.0};
		double mineralsDrawnKg{0.0};
		double oxygenReleasedKg{0.0};
		double transpiredKg{0.0};
		double respirationKg{0.0};
		double respirationO2DrawnKg{0.0};
		double respirationCo2ReturnedKg{0.0};
		double respirationWaterKg{0.0};
		double lightEnergyKJ{0.0};
		double circulationEnergyKJ{0.0};
		double chemicalEnergyFixedKJ{0.0};   // declared chemical energy of new growth
		double respirationHeatKJ{0.0};
		double heatIntoCompartmentKJ{0.0};
		// Cumulative harvest and loss (#410): everything removed from the
		// living pool, split by destination, with the dead residue that
		// could not be contained and is gone for good.
		double harvestedKg{0.0};
		double foodProducedKg{0.0};
		double wasteProducedKg{0.0};
		double surplusEdibleToWasteKg{0.0};
		double deadResidueToWasteKg{0.0};
		double deadResidueLostKg{0.0};
		// Closing residuals: the mass ledger residual over
		// (CO2 + water + nutrients + respiration O2) - (O2 released + CO2
		// returned + vapour returned) - delta biomass, and the energy ledger
		// residual over delivered light + circulation work + respired heat
		// - fixed chemical - heat into the compartment (respiration heat is
		// paid from the burned biomass, not the delivered light, so it
		// closes on the plus side beside the delivered energy).
		double residualKg{0.0};
		double residualKJ{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationCropPopulationSnapshot const&) const = default;
	};
	// The algae culture snapshot (#411): the authored contract beside the
	// living-biomass picture, the delivered light and circulation, the
	// resource-limited growth and day-and-night respiration with every
	// gas, water and nutrient flow, the three-way harvest partition with
	// its cascade surpluses, the death and residue accounting, and the
	// closing mass and energy residuals. The algae population is exposed
	// distinctly from the crop populations: its own capacity basis
	// (culture volume x density per litre), its own coefficients and its
	// own ledgers, so neither population's state can be read as the
	// other's.
	struct StationAlgaeCultureSnapshot
	{
		StationWaterQualitySnapshot waterQuality;
		std::string id;
		std::string atmosphereId;
		std::string waterStoreId;
		std::string nitrogenStoreId;
		std::string phosphorusStoreId;
		std::string potassiumStoreId;
		std::string mineralsStoreId;
		ResourceCompositionSettings composition;
		double cultureVolumeL{0.0};
		double maxBiomassDensityKgPerL{0.0};
		double capacityKg{0.0};        // cultureVolumeL x maxBiomassDensityKgPerL
		double initialBiomassKg{0.0};
		double circulationPowerKW{0.0};
		double growthYieldKgPerKWh{0.0};
		double respirationPerHour{0.0};
		double evaporationKgPerKgGrowth{0.0};
		double minGrowingTemperatureC{0.0};
		double maxGrowingTemperatureC{0.0};
		std::string foodStoreId;
		std::string recoveryStoreId;
		std::string wasteStoreId;
		double foodFraction{0.0};
		double recoveryFraction{0.0};
		double harvestTriggerKg{0.0};
		double harvestFraction{0.0};
		int priority{0};
		// The living-biomass picture: current dry Living biomass beside
		// the declared culture capacity and the fraction of it filled.
		double livingBiomassKg{0.0};
		double capacityFraction{0.0}; // livingBiomassKg / capacityKg, [0, 1]
		// The living constituent inventory (#571): the actual dry-mass
		// organic matter and bound nutrients the culture carries, summing
		// to livingBiomassKg. Respiration burns only the organic share,
		// so the nutrients stay put until harvest or death routes them out
		// instead of being reclassified as fuel each tick.
		StationBiologicalConstituents livingConstituents;
		// The chemical-energy basis for the remaining organic material
		// (#571): declared dry-biomass energy over the declared organic
		// fraction, since bound nutrients carry no fuel. Respiration heat
		// and carried living energy both use it, so chemical energy fixed
		// equals energy respired plus energy still carried.
		double energyKJPerKgOrganic{0.0};
		double livingChemicalEnergyKJ{0.0};
		// Cumulative constituent routing (#571): where this culture's
		// actual constituents went. Harvest splits across the three
		// destinations; death residue splits between waste and loss.
		StationBiologicalConstituents harvestedToFoodConstituents;
		StationBiologicalConstituents harvestedToRecoveryConstituents;
		StationBiologicalConstituents harvestedToWasteConstituents;
		StationBiologicalConstituents deadResidueToWasteConstituents;
		StationBiologicalConstituents deadResidueLostConstituents;
		// The light and circulation actually delivered this tick.
		double lastIlluminationLevel{0.0};
		double lastLightRequestedKW{0.0};
		double lastLightSuppliedKW{0.0};
		double lastCirculationRequestedKW{0.0};
		double lastCirculationSuppliedKW{0.0};
		// The growth the delivered resources funded this tick.
		double lastGrowthPotentialKg{0.0};
		double lastGrowthScale{0.0};
		double lastResourceUnmetFraction{0.0}; // growth-resource shortage, excluding temperature/capacity
		std::string lastGrowthLimiter;
		double lastGrowthKg{0.0};
		double lastCo2DrawnKg{0.0};
		double lastAssimilationWaterKg{0.0};
		double lastNitrogenDrawnKg{0.0};
		double lastPhosphorusDrawnKg{0.0};
		double lastPotassiumDrawnKg{0.0};
		double lastMineralsDrawnKg{0.0};
		double lastOxygenReleasedKg{0.0};
		double lastEvaporatedKg{0.0};
		// The respiration that ran this tick.
		double lastRespirationRequestedKg{0.0};
		double lastRespirationKg{0.0};
		double lastRespirationO2DrawnKg{0.0};
		std::string lastRespirationLimiter;
		double lastRespirationCo2ReturnedKg{0.0};
		double lastRespirationWaterKg{0.0};
		double lastRespirationHeatKW{0.0};
		bool lastOxygenLimited{false};
		// The three-way harvest partition that ran this tick: the
		// requested removal, what each destination accepted, the food
		// surplus that rode the recovery path, the recovery surplus that
		// rode the waste path, what the full destinations refused, and
		// the limiter that bound it.
		double lastHarvestRequestedKg{0.0};
		double lastHarvestedKg{0.0};
		double lastFoodAddedKg{0.0};
		double lastRecoveryAddedKg{0.0};
		double lastWasteAddedKg{0.0};
		double lastFoodSurplusToRecoveryKg{0.0};
		double lastRecoverySurplusToWasteKg{0.0};
		double lastHarvestConstrainedKg{0.0};
		std::string lastHarvestLimiter;
		// The adverse-condition loss: once the living organic content is
		// respired away the culture is dead, its remaining residue routed
		// to the waste store or booked as uncontained loss, and it never
		// grows, respires or harvests again.
		bool dead{false};
		double lastDeadResidueToWasteKg{0.0};
		double lastDeadResidueLostKg{0.0};
		// Heat into the compartment this tick.
		double lastHeatIntoCompartmentKW{0.0};
		bool temperatureEligible{false};
		std::string blockReason;
		bool disabled{false}; // zero culture volume: no service
		// Cumulative pathway ledger.
		double growthKg{0.0};
		double co2DrawnKg{0.0};
		double waterDrawnKg{0.0};
		double nitrogenDrawnKg{0.0};
		double phosphorusDrawnKg{0.0};
		double potassiumDrawnKg{0.0};
		double mineralsDrawnKg{0.0};
		double oxygenReleasedKg{0.0};
		double evaporatedKg{0.0};
		double respirationKg{0.0};
		double respirationO2DrawnKg{0.0};
		double respirationCo2ReturnedKg{0.0};
		double respirationWaterKg{0.0};
		double lightEnergyKJ{0.0};
		double circulationEnergyKJ{0.0};
		double chemicalEnergyFixedKJ{0.0};
		double respirationHeatKJ{0.0};
		double heatIntoCompartmentKJ{0.0};
		// Cumulative harvest and loss: everything removed from the living
		// pool, split by destination and cascade route, with the dead
		// residue that could not be contained and is gone for good.
		double harvestedKg{0.0};
		double foodProducedKg{0.0};
		double recoveryProducedKg{0.0};
		double wasteProducedKg{0.0};
		double foodSurplusToRecoveryKg{0.0};
		double recoverySurplusToWasteKg{0.0};
		double deadResidueToWasteKg{0.0};
		double deadResidueLostKg{0.0};
		// Closing residuals: the mass ledger residual over
		// (CO2 + water + nutrients + respiration O2) - (O2 released +
		// CO2 returned + vapour returned) - delta biomass - harvest and
		// residue outflow, and the energy ledger residual over delivered
		// light + circulation work + respired heat - fixed chemical -
		// heat into the compartment.
		double residualKg{0.0};
		double residualKJ{0.0};
		StationTransferStatus status{StationTransferStatus::Idle};
		bool operator==(StationAlgaeCultureSnapshot const&) const = default;
	};
	// Authoritative transport account. Rates use rateUnit; totals use the
	// corresponding quantity unit (kJ for kW, native Store units otherwise).
	struct StationConnectionSnapshot
	{
		StationConnectionKey key;
		std::string rateUnit;
		double lastInputRate{}, lastDeliveredRate{}, lastLossRate{};
		double inputTotal{}, deliveredTotal{}, lostTotal{};
		// Produced heat that could not enter the connection is retained at
		// its source, never erased or confused with transmission loss.
		double retainedHeatKJ{}, producedHeatKJ{};
		bool heatProducer{false};
		bool reversed{false};
		bool operator==(StationConnectionSnapshot const&) const = default;
	};
	struct StationSystemsSnapshot
	{
		// Run-scoped observation results, never authored or serialized.
		std::vector<StationAlertSnapshot> activeAlerts, closedAlerts;
		// Evaluated at elapsedSeconds (right-continuous), not a step average.
		// No authored cycle means no station-wide sun, not darkness.
		std::optional<double> solarCycle;
		bool enabled{false};
		bool healthy{true};
		// Effective runtime clock, including an explicit test-runner override.
		// Variable mode has no World ratio; duration is the last committed
		// update delta (zero before the first update).
		bool variableTimestep{false};
		uint32_t systemsStepWorldTicks{1};
		double systemsTimestepSeconds{0};
		// Legacy tick-named fields count Station steps, not World ticks.
		uint64_t worldTick{0};
		double elapsedSeconds{0};
		std::array<uint64_t, 4> completedTicks{};
		std::vector<StationSystemsStoreSnapshot> stores;
		std::vector<StationTransferSnapshot> transfers;
		std::vector<StationConnectionSnapshot> connections;
		std::vector<StationConversionSnapshot> conversions;
		std::vector<StationBalanceSnapshot> balances;
		std::vector<StationConstituentBalanceSnapshot> constituentBalances;
		std::vector<StationDeliverySnapshot> deliveries;
		// The terminal-waste ejection picture (#403): per-ejection
		// schedule, last-occurrence ejected/unmet split and cumulative
		// totals. Ejected material appears in the balance ledgers as
		// external loss.
		std::vector<StationWasteEjectionSnapshot> wasteEjections;
		// The installation outage picture (#413): per-outage schedule,
		// whether its window is open, the started/completed occurrence
		// counts and the target installation's effective availability.
		// Availability is a pure function of World time, so the same tick
		// sequence reproduces the same outage state on reset and replay.
		std::vector<StationOutageSnapshot> outages;
		// The maintenance picture (#414): per-job schedule, phase,
		// progress and blocking reason, the reservation and actual spend
		// of every requirement, the target installation's isolation
		// state and the occurrence history. Consumed material appears
		// on the waste store; reusable tools never leave the inventory
		// ledger.
		std::vector<StationMaintenanceSnapshot> maintenanceJobs;
		StationPowerSnapshot power;
		// The thermal picture (#395): reservoir temperatures, heat flows
		// and limits, radiator rejection and the per-reservoir energy
		// ledger. The ledger closes over absorbed, routed and rejected
		// energy against capacity x temperature within the declared
		// tolerance; a residual beyond it is a conservation violation.
		StationThermalSnapshot thermal;
		// The coolant picture (#396): per-loop finite inventories,
		// circulation and freeze-protection service, and each loop's
		// closing thermal energy ledger.
		std::vector<StationCoolantLoopSnapshot> coolantLoops;
		// The chilled service picture (#397): per-chiller cold-side duty,
		// work, condenser heat, unmet demand and the closing chiller
		// energy ledger, with the per-class rollup in the thermal
		// snapshot's coolingByClass.
		std::vector<StationChillerSnapshot> chillers;
		// The heat engine picture (#804): per-engine intake, net bus output,
		// rejection heat, status with its named limiter, and actual
		// efficiency beside Carnot efficiency at the interface temperatures.
		std::vector<StationHeatEngineSnapshot> heatEngines;
		std::vector<StationLightingFixtureSnapshot> lightingFixtures;
		std::vector<StationStandbyPairingSnapshot> standbyPairings;
		std::vector<StationHeatSourceSnapshot> heatSources;
		// The thermal buffer picture (#398): per-buffer finite stored
		// energy distinct from temperature, configured access gating,
		// deficit-driven charging and surplus-driven discharge, with
		// the closing storage ledger and the totals rolled up in the
		// thermal snapshot.
		std::vector<StationThermalBufferSnapshot> thermalBuffers;
		// The atmosphere picture (#400): per-compartment initial versus
		// current gas inventory, absolute and partial pressures, humidity,
		// the last tick's occupancy/leak/airlock flows and the closing
		// per-component and thermal ledgers, plus the scheduled airlock
		// cycle history with the recovered-versus-lost split.
		std::vector<StationAtmosphereSnapshot> atmospheres;
		std::vector<StationAtmospherePathwaySnapshot> atmospherePathways;
		std::vector<StationThermalLinkSnapshot> thermalLinks;
		std::vector<StationAirlockCycleSnapshot> airlockCycles;
		// The crew picture (#401): per-crew requested-versus-actual
		// exchange on every metabolic pathway, the uniform metabolic
		// scale the supplies and destination limits sustained, the
		// limiter that bound it, and the closing consumption/return/
		// retained ledger.
		std::vector<StationCrewSnapshot> crews;
		// The water recovery picture (#402): per-collector condensate,
		// cold-surface gating and latent heat routed to the coolant loop,
		// and per-treatment processed/treated/concentrate split with the
		// electricity and consumables drawn and the quality hold state.
		std::vector<StationWaterCollectorSnapshot> waterCollectors;
		std::vector<StationWaterTreatmentSnapshot> waterTreatments;
		// The oxygen production and CO2 capture picture (#404): per-cell
		// water split, products, energy and heat ledgers, and per-capture
		// captured mass, unmet atmospheric service, electricity and
		// consumable draw.
		std::vector<StationElectrolyzerSnapshot> electrolyzers;
		std::vector<StationCo2CaptureSnapshot> co2Captures;
		// The carbon recovery picture (#405): per-reactor CO2 and
		// hydrogen consumption, methane and water production, energy and
		// heat ledgers, and per-processor methane split into returned
		// hydrogen and solid carbon with the closing ledgers.
		std::vector<StationSabatierReactorSnapshot> sabatierReactors;
		std::vector<StationMethaneProcessorSnapshot> methaneProcessors;
		// The nutrient recovery picture (#407): per-processor feed,
		// oxygen, water and consumable consumption, recovered nutrient /
		// water / CO2 / residue production with the per-constituent
		// recovery split, and the closing mass and energy ledgers.
		std::vector<StationMicrobialProcessorSnapshot> microbialProcessors;
		// The crop production picture (#409): per-population dry Living
		// biomass against the declared Growing capacity, the light and
		// circulation actually delivered, the resource-limited growth and
		// day-and-night respiration with every gas, water and nutrient flow,
		// the limiting resource, and the closing mass and energy ledgers.
		std::vector<StationCropPopulationSnapshot> cropPopulations;
		// The algae picture (#411): separate cultures with their own
		// living biomass, culture-volume capacity, coefficients, harvest
		// partition and ledgers, exposed distinctly from the crops.
		std::vector<StationAlgaeCultureSnapshot> algaeCultures;
		// The fuel-cell backup picture (#406): per-cell output, reactant
	// consumption, product water, waste heat, closing ledgers and the
		// endurance the usable reactants still buy.
		std::vector<StationFuelCellSnapshot> fuelCells;
		// Soonest authored arrival still pending across every delivery, for
		// the overview's "next delivery" readout (#392).
		bool hasNextDelivery{false};
		double nextDeliverySeconds{0.0};
		// Soonest authored ejection still pending across every waste
		// ejection schedule, for the overview's "next ejection" readout
		// (#403).
		bool hasNextWasteEjection{false};
		double nextWasteEjectionSeconds{0.0};
		// The per-resource endurance explanation (#419): for every Resource
		// store and every battery, the inventory split, the net rate observed
		// over the declared observation interval, the basis and stability of
		// the estimate, and the soonest next-delivery context. Derived
		// runtime observability: never persisted, cleared by Reset/load like
		// every other runtime state.
			std::vector<StationEnduranceSnapshot> endurance;
		// The scenario forecast (#420): the bounded forward projection of
		// every Resource store under the declared-profile scenario -
		// authored demand/production profiles and scheduled deliveries -
		// with projected inventory at resupply, shortfall onset and
		// limiting service, and scenario/uncertainty labels. Derived
		// runtime observability computed from the committed state: it never
		// mutates live inventories, advances the World or consumes a
		// deterministic stream, and it is never persisted.
		StationForecastSnapshot forecast;
		double balanceTolerance{StationBalanceTolerance};
		bool operator==(StationSystemsSnapshot const&) const = default;
	};
	struct StationSystemsEvent
	{
		uint64_t worldTick{0};
		std::string message;
		// Stable presentation category (#418): the subsystem family that
		// announced the event, drawn from a fixed vocabulary ("station",
		// "observation", "transfer", "conversion", "load", "solar",
		// "battery", "dispatch", "thermal", "coolant", "chiller",
		// "generator", "atmosphere", "airlock", "crew", "electrolyzer",
		// "co2-capture", "fuel-cell", "sabatier", "methane",
		// "microbial", "crop", "algae", "delivery", "waste-ejection",
		// "outage", "maintenance"). Later features add
		// event types through this same contract so the presentation
		// never parses the message to decide how to show an incident.
		std::string category;
		bool operator==(StationSystemsEvent const&) const = default;
	};
	// #868 (ADR-0031): the only write surface a bound Alert handler gets. The
	// run-scoped Alert ledger and the bounded event stream are reachable here
	// and nowhere else, so the callback path is the ledger's only writer: the
	// Monitor evaluation loop holds no ledger handle of its own.
	struct AlertSink
	{
		std::vector<StationAlertSnapshot>& active;
		std::vector<StationAlertSnapshot>& closed;
		std::vector<StationSystemsEvent>& events;
		// The Station step the transition landed on, stamped on the event.
		uint64_t step{0};

		// Record the Alert as active.
		void recordFired(StationAlertSnapshot const& alert) const;
		// Record the closed-out Alert and retire its active entry.
		void recordCleared(StationAlertSnapshot const& alert) const;
		// Announce one transition on the event stream in the shape every
		// other subsystem event uses: the stable "alert" category, and a
		// message carrying the whole payload (monitor id, subject, metric,
		// value, threshold, severity, sim time) so a scenario author reads
		// the Alert off the stream without querying the ledger.
		void announce(std::string_view transition, StationAlertSnapshot const& alert) const;
	};
	// One bounded trend sample (#418): the requested and actual value of
	// one subsystem metric at one simulation instant. The unit comes from
	// the owning series; the timestamp is World simulation time, never
	// wall-clock, so accelerated runs stay on the same axis.
	struct StationTrendSample
	{
		uint64_t worldTick{0};
		double elapsedSeconds{0.0};
		double requested{0.0};
		double actual{0.0};
		bool operator==(StationTrendSample const&) const = default;
	};
	// One bounded trend series (#418): the stable presentation contract
	// for station resource/service trends. Subsystem identity, unit and
	// the sampling window are explicit so the viewer never implies a
	// complete history. In Fixed mode #603 made the stride a hard invariant:
	// consecutive samples always stand exactly sampleIntervalSeconds of
	// simulation time apart - the series is uniformly spaced at its
	// current cadence, never a mix of a merged coarse tail and raw
	// fine-grained appends. downsampleLevel counts the coarsenings
	// (stride = base observation interval x 2^level, capped at
	// StationTrendDownsampleLevelCap), and truncated reports that
	// older history was merged or dropped beyond what is retained
	// here. Later features add series through this same shape; the
	// presentation never depends on their internals. Variable mode instead
	// reports sampleIntervalSeconds == 0: use the actual timestamps and
	// step ids; coarsened values are weighted by observed interval duration.
	struct StationTrendSeries
	{
		std::string subsystem; // metric family: "power", "equipment-load", "transfer"
		std::string id;        // subsystem instance identity (empty: station-wide)
		std::string unit;      // display unit of the requested/actual values
		double sampleIntervalSeconds{0.0};
		uint32_t downsampleLevel{0};
		bool truncated{false};
		std::vector<StationTrendSample> samples;
		bool operator==(StationTrendSeries const&) const = default;
	};
	// #418/#603: the bounded trend-history primitive. Records one
	// observation into the series with cadence-aware decimation: a
	// series at downsampleLevel L accepts one sample every 2^L base
	// observations, so the uniform-spacing invariant holds and every
	// sample always stands for the same amount of simulation time -
	// the pairwise merge average stays time-weighted-correct because
	// merged and newly recorded samples carry equal spans.
	// Observations inside the current stride are not recorded (the
	// live value stays visible in the snapshot). On overflow the count
	// halves and the stride doubles until the level cap, past which
	// the window slides oldest-first so storage and every published
	// number stay finite. Samples older than the authored retention
	// are dropped outright. Deterministic and read-only with respect
	// to the simulated state: trends never feed back into the ledger.
	void appendBoundedTrendSample(std::vector<StationTrendSeries>& series,
		std::string const& subsystem, std::string const& id,
		std::string const& unit, double baseIntervalSeconds,
		double retentionSeconds,
		uint64_t tick, double elapsedSeconds, double requested, double actual);
	struct StationSystemsDiagnostic
	{
		uint64_t worldTick{0};
		std::string system;
		ScriptExecutionFailure failure{ScriptExecutionFailure::None};
		std::string message;
	};

	// #424: the long-duration stability audit. A pure, read-only pass over
	// the public snapshot, the bounded event history and the reported
	// diagnostics: it checks that every published state-carrying value is
	// finite, that finite capacities are never exceeded (stores, batteries,
	// coolant, buffers), that every closing conservation ledger stays
	// inside the published tolerance for its own scale, and that the
	// retained runtime history stays inside its published bounds. It is
	// observability only: it never clamps, never mutates and never relaxes
	// a tolerance - a violation is reported with its unit and magnitude,
	// never silently corrected. A physical shortage (a store running low)
	// is not a stability violation; a non-finite value, a capacity
	// breach, an unclosed ledger or an over-bound history is.
	struct StationStabilityAudit
	{
		bool enabled{false};
		uint64_t worldTick{0};
		double elapsedSeconds{0.0};
		// Finite-value tracking over every published state-carrying field.
		size_t finiteChecks{0};
		std::vector<std::string> nonFiniteFindings;
		// Finite-capacity bounds: contents never exceed capacity and never
		// go below zero or above their declared reserve relationships.
		size_t boundChecks{0};
		std::vector<std::string> boundViolations;
		// Closing conservation ledgers: |residual| against
		// balanceTolerance x max(1, |ledger scale|).
		size_t ledgerChecks{0};
		double maxResidualRatio{0.0};
		std::vector<std::string> conservationViolations;
		// Bounded runtime history: the newest-N event window and the
		// per-series trend sample bound.
		size_t eventsRetained{0};
		size_t eventHistoryLimit{StationEventHistoryLimit};
		size_t trendSeries{0};
		size_t trendSamplesRetained{0};
		size_t trendSampleLimit{StationTrendSampleLimit};
		bool trendsTruncated{false};
		// Software failures the runtime reported (Lua errors, budget
		// exhaustion, invalid accounting): every one is a stability defect.
		size_t softwareFailures{0};
		// True when nothing above reported a violation. A disabled Station
		// audits clean: there is nothing running to violate anything.
		bool clean{true};
	};

	// Run the stability audit over one public snapshot plus the bounded
	// event history, the bounded trend series and the reported diagnostics.
	// Deterministic and side-effect free: the same inputs always produce
	// the same audit.
	StationStabilityAudit auditStationStability(StationSystemsSnapshot const& snapshot,
		std::vector<StationSystemsEvent> const& events,
		std::vector<StationTrendSeries> const& trends,
		std::vector<StationSystemsDiagnostic> const& diagnostics);

	// Per-runtime source selection; executed Lua objects are never shared.
	// nullopt selects the embedded implementation. A supplied string is explicit
	// source (including empty text), copied at construction, never a fallback.
	using StationSystemsSource = std::optional<std::string>;
	std::string bundledStationSystemsSource();

	class StationSystemsRuntime;
}
