// Counter reading at the end of each day, one column per meter (table). Variables: bucket, eMeter
//@include _source
src(start: v.timeRangeStart, field: "sumWh", agg: (tables=<-) => tables |> aggregateWindow(every: 1h, fn: max, createEmpty: false))
    |> aggregateWindow(every: 1d, fn: last, createEmpty: false, location: tz, timeSrc: "_start")
    |> pivot(rowKey: ["_time"], columnKey: ["eMeter"], valueColumn: "_value")
    |> group()
    |> sort(columns: ["_time"], desc: true)
