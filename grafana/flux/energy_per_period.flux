// Energy (Wh) per day or per month from the counter. Variables: bucket, eMeter, period (1d or 1mo), maxDayWh
//
// Per day: last counter value of the day minus the one of the day before. A step that is negative or bigger than a
// meter can deliver in a day (maxDayWh, e.g. 480000 = 20 kW for 24 h) is a meter change or a counter reset and not
// counted. The month is the sum of the days. Time stamp = start of the day / month (local time).
//@include _source
src(start: date.sub(d: 2d, from: v.timeRangeStart), field: "sumWh", agg: (tables=<-) => tables |> aggregateWindow(every: 1h, fn: max, createEmpty: false))
    |> aggregateWindow(every: 1d, fn: last, createEmpty: false, location: tz, timeSrc: "_start")
    |> difference()
    |> filter(fn: (r) => r._value >= 0.0 and r._value <= float(v: ${maxDayWh}))
    |> filter(fn: (r) => r._time >= v.timeRangeStart)
    |> aggregateWindow(every: ${period}, fn: sum, createEmpty: false, location: tz, timeSrc: "_start")
