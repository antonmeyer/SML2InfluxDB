// Energy (Wh) in the selected time range, per meter (jumps of meter changes are not counted).
// Variables: bucket, eMeter, maxDayWh (see energy_per_period.flux)
//@include _source
src(start: date.sub(d: 2d, from: v.timeRangeStart), field: "sumWh", agg: (tables=<-) => tables |> aggregateWindow(every: 1h, fn: max, createEmpty: false))
    |> aggregateWindow(every: 1d, fn: last, createEmpty: false, location: tz, timeSrc: "_start")
    |> difference()
    |> filter(fn: (r) => r._value >= 0.0 and r._value <= float(v: ${maxDayWh}))
    |> filter(fn: (r) => r._time >= v.timeRangeStart)
    |> group(columns: ["eMeter"])
    |> sum()
