// Counter readings (Wh): last value per Grafana window. Variables: bucket, eMeter
//@include _source
src(start: v.timeRangeStart, field: "sumWh", agg: (tables=<-) => tables |> aggregateWindow(every: v.windowPeriod, fn: max, createEmpty: false))
