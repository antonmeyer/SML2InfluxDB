#ifndef POWERMETER
#define POWERMETER

// Receives SML frames from a power meter on one UART of the ESP32.
//
// The UART hardware searches the stream for the 4 x 0x1B of the SML start and end flag and tells us the position.
// handle_event() then reads the frame from the ring buffer, MeterData.h checks it (flags, CRC) and decodes it.
// So the main loop only has to call handle_event() now and then, nothing is polled byte by byte.
//
// Frame:  1b1b1b1b 01010101 | SML messages + padding | 1b1b1b1b 1a <padding count> <crc16>
// Tested on the host (test/test_decoder) and with a loopback test on the ESP32 (tools/selftest).

#include "driver/uart.h"
#include <string.h>
#include "MeterData.h" // MeterDataSet and the SML decoder

// Longest frame we accept, without the 8 byte end part. The frames of the meters have 300..1000 bytes.
#define BUF_SIZE 1024

// loop() is blocked now and then for seconds (HTTPS write, OTA check). So long the UART must keep the data:
// a 9600 baud stream is ~1 KB/s.
#define UART_RX_RING_SIZE 4096    // driver ring buffer, ~4 s
#define UART_EVENT_QUEUE_LEN 256  // with the 8 byte FIFO threshold every frame makes ~40 data events
#define UART_PATTERN_QUEUE_LEN 20 // positions of start/end flags, 2 per frame

// An SML frame is a continuous stream without pauses. With the default threshold (120 bytes) the UART hands the
// data over to the ring buffer in big chunks, so the 4 bytes after a pattern would not be there yet when
// handle_event() reads them. Hand them over every 8 bytes instead.
#define UART_RX_FIFO_THRESHOLD 8

// Pattern detection: 4 x 0x1B. The values are the ones of the deprecated API uart_enable_pattern_det_intr(), which
// works with the meters since 2021. The unit of the three numbers (APB cycles or baud clocks) was not verified, so
// they were left as they are.
#define PATTERN_CHAR 0x1B
#define PATTERN_CHAR_NUM 4
#define PATTERN_CHR_TIMEOUT 10000
#define PATTERN_POST_IDLE 10
#define PATTERN_PRE_IDLE 10

// Wait for the rest of a frame; it follows without a pause, the UART hands the bytes over every 8 bytes.
#define READ_TIMEOUT_MS 50

// -DSML_TRACE=1 prints every pattern event, for debugging the receive path (see tools/selftest)
#if SML_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

class PowerMeter
{
public:
    // result of the last frame, valid after handle_event() returned 1
    MeterDataSet dataset;
    SmlFrameStatus lastStatus = SML_FRAME_OK; // for diagnostics, also of rejected frames

    // uartnr: UART of the ESP32, rx_pin: GPIO of the optical reader. Tx is not used. 9600 8N1 as all SML meters.
    PowerMeter(uart_port_t uartnr, int rx_pin) : uart_num(uartnr)
    {
        uart_config_t conf_uart = {
            .baud_rate = 9600,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
        uart_param_config(uart_num, &conf_uart);
        uart_set_pin(uart_num, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_driver_install(uart_num, UART_RX_RING_SIZE, 0, UART_EVENT_QUEUE_LEN, &queue_uart, 0);
        uart_set_rx_full_threshold(uart_num, UART_RX_FIFO_THRESHOLD);
        uart_enable_pattern_det_intr(uart_num, PATTERN_CHAR, PATTERN_CHAR_NUM, PATTERN_CHR_TIMEOUT, PATTERN_POST_IDLE,
                                     PATTERN_PRE_IDLE);
        uart_pattern_queue_reset(uart_num, UART_PATTERN_QUEUE_LEN);
    }

    // Works off the UART events. Returns 1 if a valid frame with values was decoded, they are in dataset. Call it
    // as often as possible, a blocked loop() is tolerated for a few seconds (the data waits in the ring buffer).
    char handle_event()
    {
        char result = 0;
        uart_event_t event;

        // work off the queue, but stop after a decoded frame so the caller can use the dataset
        // (after a blocked loop() there might be a lot of events waiting)
        while (!result && xQueueReceive(queue_uart, (void *)&event, (TickType_t)0))
        {
            switch (event.type)
            {
            case UART_PATTERN_DET:
                result = handlePattern();
                break;

            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                // data was lost, we are out of sync: forget everything and wait for the next start flag
                uart_flush_input(uart_num);
                xQueueReset(queue_uart);
                startTaken = false;
                break;

            default:; // data events, nothing to do
            }
        }
        return result;
    }

private:
    uart_port_t uart_num;
    QueueHandle_t queue_uart;
    uint8_t uartbuf[BUF_SIZE + 8]; // frame up to the end pattern + end flag, padding count and CRC
    bool startTaken = false;       // a start flag was read from the line already and is in uartbuf[0..7]

    // One pattern (4 x 0x1B) was found. Returns 1 if this completed a valid frame.
    char handlePattern()
    {
        int pos = uart_pattern_pop_pos(uart_num); // bytes in front of the pattern, -1 if none
        TRACE("[ev pos=%d startTaken=%d]", pos, startTaken);

        if (pos == 0)
        {
            // Start flag at the head of the buffer, the frame is coming. A start flag we took from the line
            // earlier (startTaken) is outdated now, this one wins.
            startTaken = false;
            return 0;
        }
        if (pos < 0)
            return 0;

        // The pattern is at pos > 0: normally the end flag of a frame whose start flag is still in the buffer.
        // But it can also be the start flag of the next frame, if there was junk or a fragment in front of it
        // (noise, a frame cut off, power up in the middle of a frame). That is detected below and the start flag
        // is kept for the next event.
        size_t off = startTaken ? 8 : 0; // start flag already in uartbuf[0..7]
        startTaken = false;
        if (pos > BUF_SIZE - (int)off)
        {
            // can not be a valid frame, drop everything and wait for the next start flag
            uart_flush_input(uart_num);
            return 0;
        }

        int n = uart_read_bytes(uart_num, uartbuf + off, pos, READ_TIMEOUT_MS / portTICK_RATE_MS);
        size_t len = off + (n > 0 ? n : 0);
        TRACE("[read len=%d]", (int)len);

        // 8 bytes more: end flag, padding count and CRC - or the start flag, see above
        int tail = uart_read_bytes(uart_num, &uartbuf[len], 8, READ_TIMEOUT_MS / portTICK_RATE_MS);
        TRACE("[tail=%d first=%02x%02x]", tail, uartbuf[len], uartbuf[len + 1]);
        if (tail != 8)
            return 0; // incomplete, wait for the next frame

        static const uint8_t startFlag[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};
        if (memcmp(&uartbuf[len], startFlag, 8) == 0)
        {
            // what we read before was junk: forget it, the frame body follows
            memcpy(uartbuf, startFlag, 8);
            startTaken = true;
            return 0;
        }

        // checks start flag, end flag and CRC, then extracts the values
        lastStatus = smlDecodeFrame(uartbuf, len + tail, dataset);
        TRACE("[decode=%d]\n", (int)lastStatus);
        return lastStatus == SML_FRAME_OK && dataset.hasData();
    }
};

#endif
