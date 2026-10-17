// Outdoor temperature (degrees C), daily mean of the DWD station. Fixed bucket dwdbkt.
import "timezone"

from(bucket: "dwdbkt")
    |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
    |> filter(fn: (r) => r._measurement == "weather" and r.dataset == "temperature_air" and r._field == "temperature_air_mean_2m")
    |> aggregateWindow(every: 1d, fn: mean, createEmpty: false, location: timezone.location(name: "Europe/Berlin"), timeSrc: "_start")
    // the raw values are Kelvin in older data and degrees C in newer data
    |> map(fn: (r) => ({r with _value: if r._value > 150.0 then r._value - 273.15 else r._value, _field: "Aussentemperatur"}))
