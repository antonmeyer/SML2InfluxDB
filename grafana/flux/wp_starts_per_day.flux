// Number of starts per day of the heat pump (change from off to on). Variables: as wp_runtime_per_day
//@include _source
//@include _wp_state
state
    |> difference()
    |> filter(fn: (r) => r._value == 1.0)
    |> aggregateWindow(every: 1d, fn: count, createEmpty: true, location: tz, timeSrc: "_start")
    |> map(fn: (r) => ({r with _value: float(v: r._value), _field: "starts"}))
