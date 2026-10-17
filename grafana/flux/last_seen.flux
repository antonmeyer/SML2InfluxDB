// Last counter reading and its time per meter, to see which meter has not sent anything. Variables: bucket, eMeter
//@include _source
src(start: date.sub(d: 30d, from: v.timeRangeStop), field: "sumWh", agg: (tables=<-) => tables |> last())
    |> last()
    |> map(fn: (r) => ({r with age_min: float(v: int(v: now()) - int(v: r._time)) / 60000000000.0}))
    |> group()
    |> keep(columns: ["eMeter", "_time", "_value", "age_min"])
