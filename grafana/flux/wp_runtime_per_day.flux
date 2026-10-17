// Hours per day the heat pump runs (mean power of a window above onW). Variables: bucket, eMeter, runWindow, runMinutes
// (runWindow in minutes as a number), onW, maxPowerW
//@include _source
//@include _wp_state
state
    |> aggregateWindow(every: 1d, fn: sum, createEmpty: false, location: tz, timeSrc: "_start")
    |> map(fn: (r) => ({r with _value: r._value * float(v: ${runMinutes}) / 60.0, _field: "runtime_h"}))
