// Status of the ESP32 (eMonVH): heap, WLAN strength, reset reason, restarts, write errors. Fixed bucket cloudbktcp.
from(bucket: "cloudbktcp")
    |> range(start: v.timeRangeStart, stop: v.timeRangeStop)
    |> filter(fn: (r) => r._measurement == "ESP32" and r.device == "eMonVH")
    |> aggregateWindow(every: v.windowPeriod, fn: last, createEmpty: false)
