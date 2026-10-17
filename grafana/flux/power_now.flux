// Current power (W): the mean power of the last minutes. Measured (actW) where the meter sends it, else calculated
// from the energy counter (first and last reading of the window). Variables: bucket, eMeter, nowWindow (e.g. 10m)
//@include _source
from_ = date.sub(d: ${nowWindow}, from: v.timeRangeStop)
measured =
    src(start: from_, field: "actW", agg: (tables=<-) => tables |> mean())
        |> map(fn: (r) => ({r with _time: v.timeRangeStop}))
first_ = src(start: from_, field: "sumWh", agg: (tables=<-) => tables |> first())
last_ = src(start: from_, field: "sumWh", agg: (tables=<-) => tables |> last())
calc =
    join(tables: {a: first_, b: last_}, on: ["eMeter"])
        |> map(fn: (r) => ({eMeter: r.eMeter, _field: "actW_calc", _time: v.timeRangeStop,
            _value: if int(v: r._time_b) > int(v: r._time_a) and r._value_b >= r._value_a
                then (r._value_b - r._value_a) * 3600.0 / (float(v: int(v: r._time_b) - int(v: r._time_a)) / 1000000000.0)
                else 0.0}))
        |> group(columns: ["eMeter", "_field"])
union(tables: [measured, calc])
