// Hardware self test of the UART receive path (PowerMeter.h) on one ESP32, without any external hardware.
//
// A task sends captured SML frames on UART2 TX at 9600 baud. The UART is switched to internal loopback,
// so the bytes arrive on the RX side of the same UART where PowerMeter (exactly the class of the real
// firmware) receives them with pattern detection, CRC check and decoder.
// The result of every frame is compared with the expectation from the host decoder (smlDecodeFrame).
//
// Run:     pio run -e selftest -t upload && pio device monitor -e selftest
// Options: -DSELFTEST_REALTIME=1  pause between frames like the sensor time of the meter (default: fast)
//
// Not tested here: the electrical side (optical head, levels) - the frames are generated on chip.
#include <Arduino.h>
#include "PowerMeter.h"
#include "replay_frames.h"

#ifndef SELFTEST_REALTIME
#define SELFTEST_REALTIME 0
#endif
#ifndef SELFTEST_BLOCK_MS
#define SELFTEST_BLOCK_MS 2500
#endif
#define FAULT_REPEATS 5     // per fault type
#define FAST_GAP_MS 150     // pause between items, the real meters have ~0.7 s
#define UART_TEST UART_NUM_2

enum Fault : uint8_t
{
    F_NONE,
    F_BADCRC, // one bit flipped in the payload
    F_TRUNC,  // the last 20 bytes (end flag, padding, CRC) are missing
    F_NOISE,  // 17 bytes of junk, no frame
    F_HUGE,   // start flag, 1200 bytes of data, end flag: bigger than the receive buffer
    F_COUNT
};
static const char *faultName[F_COUNT] = {"none", "bad CRC", "truncated", "noise", "oversize"};

struct Item
{
    int16_t frame;
    Fault fault;
};

static const int NITEMS = REPLAY_COUNT + (F_COUNT - 1) * FAULT_REPEATS * 2;
static Item items[NITEMS];
static uint8_t result[NITEMS]; // 0 nothing received, 1 received and identical, 2 received but values differ
static MeterDataSet expected[REPLAY_COUNT];
static volatile int curItem = -1;
static volatile bool senderDone = false;
static bool reported = false;
static uint32_t falseAccept = 0; // something decoded while a damaged frame or junk was on the line

PowerMeter pm(UART_TEST, GPIO_NUM_16);

static bool same(const MeterDataSet &a, const MeterDataSet &b)
{
    return strcmp(a.meterid, b.meterid) == 0 && a.hasSensorTime == b.hasSensorTime && a.sensorTime == b.sensorTime &&
           a.hasActW == b.hasActW && a.actW == b.actW && a.hasSumWh == b.hasSumWh && a.sumWh == b.sumWh &&
           a.hasExpWh == b.hasExpWh && a.expWh == b.expWh;
}

static void buildItems()
{
    int n = 0;
    for (int i = 0; i < REPLAY_COUNT; i++)
        items[n++] = {(int16_t)i, F_NONE};
    // damaged frame / junk, followed by a good one (alternating meters)
    for (int r = 0; r < FAULT_REPEATS; r++)
        for (int f = F_BADCRC; f < F_COUNT; f++)
        {
            int a = (r * 37 + f * 11) % REPLAY_COUNT;
            int b = (a * 7 + 3 + r) % REPLAY_COUNT;
            items[n++] = {(int16_t)a, (Fault)f};
            items[n++] = {(int16_t)b, F_NONE};
        }
}

static int gapAfter(int i)
{
#if SELFTEST_REALTIME
    // two consecutive frames of the same meter: wait like the sensor time says
    if (i + 1 < NITEMS && items[i].fault == F_NONE && items[i + 1].fault == F_NONE)
    {
        const MeterDataSet &a = expected[items[i].frame], &b = expected[items[i + 1].frame];
        if (strcmp(a.meterid, b.meterid) == 0 && b.sensorTime > a.sensorTime && b.sensorTime - a.sensorTime <= 10)
        {
            int ms = (int)(b.sensorTime - a.sensorTime) * 1000 - (int)(replayFrames[items[i].frame].len * 10000 / 9600);
            return ms > FAST_GAP_MS ? ms : FAST_GAP_MS;
        }
    }
#endif
    return FAST_GAP_MS;
}

static void senderTask(void *)
{
    delay(3000); // let the receiver settle
    static uint8_t buf[1300];
    for (int i = 0; i < NITEMS; i++)
    {
        const ReplayFrame &f = replayFrames[items[i].frame];
        size_t n = f.len;
        memcpy(buf, f.data, n);
        switch (items[i].fault)
        {
        case F_BADCRC:
            buf[30] ^= 0x01;
            break;
        case F_TRUNC:
            n -= 20;
            break;
        case F_NOISE:
            for (int k = 0; k < 17; k++)
                buf[k] = (uint8_t)(0x20 + k * 5); // never 0x1b
            n = 17;
            break;
        case F_HUGE:
            memset(buf, 0x00, sizeof(buf));
            memcpy(buf, f.data, 8); // start flag
            memcpy(buf + 1200 - 8, f.data + f.len - 8, 8);
            n = 1200;
            break;
        default:
            break;
        }
        curItem = i;
        uart_write_bytes(UART_TEST, (const char *)buf, n);
        uart_wait_tx_done(UART_TEST, 2000 / portTICK_PERIOD_MS);
        delay(gapAfter(i));
    }
    delay(2000);
    senderDone = true;
    vTaskDelete(NULL);
}

static void report()
{
    int aOk = 0, aWrong = 0, aLost = 0, aN = 0;
    int recovered[F_COUNT] = {0}, total[F_COUNT] = {0}, wrong = 0;
    for (int i = 0; i < NITEMS; i++)
    {
        if (items[i].fault != F_NONE)
            continue;
        bool afterFault = i > 0 && items[i - 1].fault != F_NONE;
        if (afterFault)
        {
            Fault f = items[i - 1].fault;
            total[f]++;
            if (result[i] == 1)
                recovered[f]++;
        }
        else
        {
            aN++;
            aOk += result[i] == 1;
            aLost += result[i] == 0;
            if (result[i] == 0)
                Serial.printf("lost: item %d (%s)\n", i, replayFrames[items[i].frame].name);
        }
        if (result[i] == 2)
        {
            wrong++;
            aWrong += !afterFault;
        }
    }
    Serial.println();
    Serial.println("==== SELFTEST RESULT ====");
    Serial.printf("good frames in a row:   %d sent, %d ok, %d lost, %d wrong values\n", aN, aOk, aLost, aWrong);
    for (int f = F_BADCRC; f < F_COUNT; f++)
        Serial.printf("frame after %-9s:   %d sent, %d received\n", faultName[f], total[f], recovered[f]);
    Serial.printf("data that matches no sent frame: %u\n", (unsigned)falseAccept);
    Serial.printf("frames with wrong data: %d\n", wrong);
    bool pass = aOk == aN && wrong == 0 && falseAccept == 0;
    Serial.println(pass ? "PASS: no loss, no wrong data (loss after a fault is listed above)" : "FAIL");
    Serial.println("==== END ====");
}

void setup()
{
    Serial.begin(115200);
    Serial.println("\nSML UART selftest");

    buildItems();
    for (int i = 0; i < REPLAY_COUNT; i++)
    {
        expected[i].clearValues();
        if (smlDecodeFrame(replayFrames[i].data, replayFrames[i].len, expected[i]) != SML_FRAME_OK)
            Serial.printf("host decoder fails for %s\n", replayFrames[i].name);
    }
    uart_set_loop_back(UART_TEST, true); // TX goes straight to RX inside the chip
    Serial.printf("%d frames, %d items, %s\n", REPLAY_COUNT, NITEMS, SELFTEST_REALTIME ? "real time" : "fast");

    xTaskCreatePinnedToCore(senderTask, "sender", 4096, NULL, 1, NULL, 0);
}

void loop()
{
    // Once: do nothing for 2.5 s, like loop() does while a slow HTTPS write or an OTA check blocks. The sender goes
    // on, the frames must wait in the UART buffers (SELFTEST_BLOCK_MS=0 switches it off).
    static bool blocked = false;
    if (!blocked && curItem >= 60 && SELFTEST_BLOCK_MS > 0)
    {
        blocked = true;
        Serial.printf("blocking loop() for %d ms\n", SELFTEST_BLOCK_MS);
        delay(SELFTEST_BLOCK_MS);
    }
    if (pm.handle_event())
    {
        // Frames can arrive late (loop() was blocked), so do not assume it is the one sent just now: find the
        // good frame that has exactly these values. Nothing matches = the receiver invented data.
        int match = -1;
        for (int j = 0; j < NITEMS && match < 0; j++)
            if (items[j].fault == F_NONE && result[j] == 0 && same(pm.dataset, expected[items[j].frame]))
                match = j;
        if (match >= 0)
            result[match] = 1;
        else
        {
            falseAccept++;
            Serial.print("WRONG DATA: ");
            pm.dataset.print();
        }
    }
    if (senderDone && !reported)
    {
        reported = true;
        report();
    }
    delay(1);
}
