// Power (W) calculated from the energy counter, for meters that do not send the power (LGZ without PIN).
// Variables: bucket, eMeter, maxPowerW (e.g. 25000). The window is the one Grafana chooses for the panel
// (v.windowPeriod); set a minimum in the panel (Query options -> Min interval), e.g. 5m.
//
// The counter has a resolution of 0.1 Wh, so a window of 5 min is good for about +-1.2 W, 1 min for about +-6 W.
// Negative steps and steps bigger than maxPowerW are meter changes or resets and left out.
//@include _source
src(start: date.sub(d: v.windowPeriod, from: v.timeRangeStart), field: "sumWh", agg: (tables=<-) => tables |> aggregateWindow(every: v.windowPeriod, fn: max, createEmpty: false))
    |> derivative(unit: 1h, nonNegative: false)
    |> filter(fn: (r) => r._value >= 0.0 and r._value <= float(v: ${maxPowerW}))
    |> filter(fn: (r) => r._time >= v.timeRangeStart)
    |> map(fn: (r) => ({r with _field: "actW_calc"}))
