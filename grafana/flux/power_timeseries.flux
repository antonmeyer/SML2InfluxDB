// Power (W): mean, min and max per Grafana window. min/max only exist for data of the new schema.
// Variables: bucket, eMeter
//@include _source
mean = src(start: v.timeRangeStart, field: "actW", agg: (tables=<-) => tables |> aggregateWindow(every: v.windowPeriod, fn: mean, createEmpty: false))
low = src(start: v.timeRangeStart, field: "actWmin", agg: (tables=<-) => tables |> aggregateWindow(every: v.windowPeriod, fn: min, createEmpty: false))
high = src(start: v.timeRangeStart, field: "actWmax", agg: (tables=<-) => tables |> aggregateWindow(every: v.windowPeriod, fn: max, createEmpty: false))

union(tables: [mean, low, high])
