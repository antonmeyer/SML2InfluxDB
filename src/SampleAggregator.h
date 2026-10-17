#ifndef SAMPLEAGGREGATOR_H
#define SAMPLEAGGREGATOR_H

// Reduces the frames of one meter (1 per second) to the points we write to the database. No Arduino
// dependencies, tested on the host (test/test_decoder).
//
// Power (W, if the meter sends it): per window of intervalS seconds mean, min and max of the frames.
// Energy (Wh):  the last value; sent when it changed (at most once per window) and at the latest after heartbeatS
//               seconds even if it did not. Power can be calculated from it later in the database, as the
//               accuracy is limited by the resolution of the counter and by the time stamps, not by smoothing.
// Time stamp:   from the clock of the meter (sensorTime) plus a constant offset to the clock of the ESP32. The
//               distances between the points are then exact, also if loop() was blocked and several frames arrive
//               at once. The offset is the smallest "ESP time - meter time" seen (frames can only be late) and
//               follows slow drift once a minute.
//
// One window is closed by the first frame of the next window. A frame closes at most one window, so add()
// produces at most one point.

#include <stdint.h>
#include <string.h>
#include "MeterData.h"

struct SamplePoint
{
    bool hasW = false; // the meter sends the power: mean, min, max over the window
    double wMean = 0, wMin = 0, wMax = 0;
    bool hasSumWh = false; // Wh only if changed (or heartbeat)
    double sumWh = 0;
    bool hasExpWh = false;
    double expWh = 0;
    uint32_t epoch = 0; // time stamp of the point, seconds since 1970
};

class SampleAggregator
{
public:
    // intervalS 0: no reduction, every frame is a point (old behavior)
    SampleAggregator(uint32_t intervalS = 5, uint32_t heartbeatS = 30) : intervalS_(intervalS), heartbeatS_(heartbeatS) {}

    // Feed one decoded frame. nowEpoch: wall clock (time(nullptr)) when it was handled. Returns true if a point is
    // ready in out.
    bool add(const MeterDataSet &ds, uint32_t nowEpoch, SamplePoint &out)
    {
        if (!ds.hasData())
            return false;

        // meter time; without it the wall clock is used and the offset stays 0
        uint32_t t = ds.hasSensorTime ? ds.sensorTime : nowEpoch;

        // another meter, or the meter time jumped (restart, clock set): start from scratch
        if (haveWindow_ && (strcmp(id_, ds.meterid) != 0 || t < lastT_ || t - lastT_ > MAX_GAP_S))
            reset();
        if (!haveWindow_)
        {
            strncpy(id_, ds.meterid, sizeof(id_) - 1);
            id_[sizeof(id_) - 1] = 0;
        }

        updateOffset(ds.hasSensorTime ? (int64_t)nowEpoch - (int64_t)t : 0, t);

        if (intervalS_ == 0) // pass through
        {
            out = SamplePoint();
            if (ds.hasActW)
                setW(out, ds.actW, ds.actW, ds.actW);
            if (ds.hasSumWh)
                setSum(out, ds.sumWh);
            if (ds.hasExpWh)
                setExp(out, ds.expWh);
            out.epoch = stamp(t);
            return true;
        }

        uint32_t bucket = t / intervalS_;
        bool closed = false;
        if (haveWindow_ && bucket != bucket_)
            closed = closeWindow(out);

        if (!haveWindow_ || closed || bucket != bucket_)
            openWindow(bucket);

        // take the frame into the (new) window
        if (ds.hasActW)
        {
            if (n_ == 0 || ds.actW < wMin_)
                wMin_ = ds.actW;
            if (n_ == 0 || ds.actW > wMax_)
                wMax_ = ds.actW;
            wSum_ += ds.actW;
            n_++;
        }
        hasSum_ = ds.hasSumWh;
        sum_ = ds.sumWh;
        hasExp_ = ds.hasExpWh;
        exp_ = ds.expWh;
        lastT_ = t;
        haveWindow_ = true;
        return closed;
    }

    uint32_t offset() const { return offset_; } // for tests and diagnosis

private:
    static const uint32_t MAX_GAP_S = 600;      // longer gap in the meter time: restart
    static const uint32_t OFFSET_BLOCK_S = 60;  // the offset is refreshed from the smallest value of this long
    static const int64_t OFFSET_MIN_STEP = 2;   // smaller changes are rounding of the 1 s ESP clock, ignore

    uint32_t intervalS_, heartbeatS_;
    char id_[32] = "";
    bool haveWindow_ = false;
    uint32_t bucket_ = 0, lastT_ = 0;
    // window
    uint32_t n_ = 0;
    double wSum_ = 0, wMin_ = 0, wMax_ = 0;
    bool hasSum_ = false, hasExp_ = false;
    double sum_ = 0, exp_ = 0;
    // what was sent last
    bool sentSumValid_ = false, sentExpValid_ = false;
    double sentSum_ = 0, sentExp_ = 0;
    bool sentAny_ = false;
    uint32_t sentWhT_ = 0; // meter time of the last point that had Wh
    // clock offset
    bool anchored_ = false;
    int64_t offset_ = 0;
    int64_t blockMin_ = 0;
    uint32_t blockStart_ = 0;

    void reset()
    {
        haveWindow_ = false;
        n_ = 0;
        sentSumValid_ = sentExpValid_ = sentAny_ = false;
        anchored_ = false;
        id_[0] = 0;
    }

    void openWindow(uint32_t bucket)
    {
        bucket_ = bucket;
        n_ = 0;
        wSum_ = wMin_ = wMax_ = 0;
        hasSum_ = hasExp_ = false;
    }

    void updateOffset(int64_t candidate, uint32_t t)
    {
        if (!anchored_)
        {
            anchored_ = true;
            offset_ = blockMin_ = candidate;
            blockStart_ = t;
            return;
        }
        if (candidate < offset_) // the frame was less late than all before: the offset was too big
            offset_ = candidate;
        if (candidate < blockMin_)
            blockMin_ = candidate;
        if (t - blockStart_ >= OFFSET_BLOCK_S) // follow slow drift, ignore changes by rounding
        {
            int64_t d = blockMin_ - offset_;
            if (d >= OFFSET_MIN_STEP || d <= -OFFSET_MIN_STEP)
                offset_ = blockMin_;
            blockMin_ = candidate;
            blockStart_ = t;
        }
    }

    uint32_t stamp(uint32_t t) const { return (uint32_t)((int64_t)t + offset_); }

    static void setW(SamplePoint &p, double mean, double mn, double mx)
    {
        p.hasW = true;
        p.wMean = mean;
        p.wMin = mn;
        p.wMax = mx;
    }
    static void setSum(SamplePoint &p, double v)
    {
        p.hasSumWh = true;
        p.sumWh = v;
    }
    static void setExp(SamplePoint &p, double v)
    {
        p.hasExpWh = true;
        p.expWh = v;
    }

    // The window is complete: decide what to send. Returns true if there is a point.
    bool closeWindow(SamplePoint &out)
    {
        bool changed = (hasSum_ && (!sentSumValid_ || sum_ != sentSum_)) || (hasExp_ && (!sentExpValid_ || exp_ != sentExp_));
        bool heartbeat = !sentAny_ || lastT_ - sentWhT_ >= heartbeatS_;
        bool hasW = n_ > 0;
        if (!hasW && !changed && !heartbeat)
            return false; // nothing new, stay quiet

        out = SamplePoint();
        if (hasW)
            setW(out, wSum_ / n_, wMin_, wMax_);
        if (changed || heartbeat)
        {
            if (hasSum_)
            {
                setSum(out, sum_);
                sentSum_ = sum_;
                sentSumValid_ = true;
            }
            if (hasExp_)
            {
                setExp(out, exp_);
                sentExp_ = exp_;
                sentExpValid_ = true;
            }
            sentWhT_ = lastT_;
        }
        sentAny_ = true;
        out.epoch = stamp(lastT_);
        return out.hasW || out.hasSumWh || out.hasExpWh;
    }
};

#endif
