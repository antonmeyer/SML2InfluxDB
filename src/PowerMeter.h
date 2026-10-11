#ifndef POWERMETER
#define POWERMETER

#include "driver/uart.h"
#include "endian.h"
#include <sml/sml_file.h>
#include <sml/sml_value.h>
#include "MeterData.h" // MeterDataSet and the SML decoder
#include "unit.h"
#include <string.h>

#define BUF_SIZE 1024 // longest frame we accept (without start/end flag)

// loop() is blocked now and then for seconds (HTTPS write, OTA check). So long the UART must keep the data:
// a 9600 baud stream is ~1 KB/s, the frames of the meters are 300..1000 bytes.
#define UART_RX_RING_SIZE 4096 // driver ring buffer, ~4 s
#define UART_EVENT_QUEUE_LEN 256 // with the 8 byte FIFO threshold every frame makes ~40 data events
#define UART_PATTERN_QUEUE_LEN 20 // positions of start/end flags, 2 per frame

// -DSML_TRACE=1 prints every pattern event, for debugging the receive path (see tools/selftest)
#if SML_TRACE
#define TRACE(...) printf(__VA_ARGS__)
#else
#define TRACE(...)
#endif

class PowerMeter
{

public:
    uart_config_t conf_uart = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    MeterDataSet dataset;

    uint8_t uartbuf[BUF_SIZE + 8]; // frame up to the end pattern + end flag, padding count and CRC

    uart_port_t uart_num;
    QueueHandle_t queue_uart;

    int length = 0;
    int pos = 0;
    int len = 0;
    bool startTaken = false; // a start flag was read from the line already and is in uartbuf[0..7]
    SmlFrameStatus lastStatus = SML_FRAME_OK; // result of the last decoded frame, for diagnostics

    PowerMeter(uart_port_t uartnr, int rx_pin)
    {
        this->uart_num = uartnr;
        uart_param_config(uart_num, &conf_uart);

        /*     if (uart_num == UART_NUM_0) {
            //we hope we can use UART0 for Rx but keep Tx für debug output, or would that collide anyhow?
            uart_set_pin(UART_NUM_0, GPIO_NUM_1, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        }
        else {
        uart_set_pin(uart_num, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);};
    */
        uart_set_pin(uart_num, UART_PIN_NO_CHANGE, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        uart_driver_install(uart_num, UART_RX_RING_SIZE, 0, UART_EVENT_QUEUE_LEN, &queue_uart, 0);
        // An SML frame is a continuous stream without pauses. With the default threshold (120 bytes) the UART hands
        // the data over to the ring buffer in big chunks, so the 4 bytes after a pattern would not be there when
        // handle_event() reads them. Hand them over every 8 bytes instead.
        uart_set_rx_full_threshold(uart_num, 8);
        uart_enable_pattern_det_intr(uart_num, 0x1B, 4, 10000, 10, 10); // detect 4x 0x1B
        uart_pattern_queue_reset(uart_num, UART_PATTERN_QUEUE_LEN);
    };

    char handle_event()
    {
        char result = 0;
        uart_event_t event;

        // work off the queue, but stop after a decoded frame so the caller can use the dataset
        // (after a blocked loop() there might be a lot of events waiting)
        while (!result && xQueueReceive(queue_uart, (void *)&event, (TickType_t)0))
        {
           // printf("UART: %d\n", uart_num);
           // Serial.print("we got an event: ");

            switch (event.type)
            {

            case UART_PATTERN_DET:
            //    Serial.println(event.type, HEX);
            //    Serial.print("pattern found, pos: ");

                pos = uart_pattern_pop_pos(uart_num);
                TRACE("[ev pos=%d startTaken=%d]", pos, startTaken);
             //   Serial.print(pos);
                uart_get_buffered_data_len(uart_num, (size_t *)&length);
            //    Serial.print(" / ");
            //    Serial.println(length);
                if (pos == 0)
                {
                    // start flag at the head of the buffer, the frame is coming. A start flag we took from
                    // the line earlier (startTaken) is outdated now, this one wins.
                    startTaken = false;
                }
                else if (pos > 0)
                {
                    // The pattern is at pos > 0: normally the end flag of a frame whose start flag is still in
                    // the buffer. But it can also be the start flag of the next frame, if there was junk or
                    // a fragment in front of it (noise, a frame cut off, power up in the middle of a frame).
                    // That is detected below and the start flag is kept for the next event.
                    size_t off = startTaken ? 8 : 0; // start flag already in uartbuf[0..7]
                    startTaken = false;
                    if (pos > BUF_SIZE - (int)off)
                    {
                        // can not be a valid frame, drop everything and wait for the next start flag
                        uart_flush_input(uart_num);
                        break;
                    }
                    len = uart_read_bytes(uart_num, uartbuf + off, pos, 50 / portTICK_RATE_MS);
                    if (len < 0)
                        len = 0;
                    len += off;
                    TRACE("[read len=%d]", len);

                    // 8 bytes more: end flag, padding count and CRC - or the start flag, see above
                    int tail = uart_read_bytes(uart_num, &uartbuf[len], 8, 50 / portTICK_RATE_MS);

                    TRACE("[tail=%d first=%02x%02x]", tail, uartbuf[len], uartbuf[len + 1]);
                    static const uint8_t startFlag[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};
                    if (tail == 8 && memcmp(&uartbuf[len], startFlag, 8) == 0)
                    {
                        // what we read before was junk: forget it, the frame body follows
                        memcpy(uartbuf, startFlag, 8);
                        startTaken = true;
                    }
                    else if (tail == 8)
                    {
                        // checks start flag, end flag and CRC, then extracts the values
                        lastStatus = smlDecodeFrame(uartbuf, len + tail, dataset);
                        TRACE("[decode=%d]\n", (int)lastStatus);
                        result = (lastStatus == SML_FRAME_OK && dataset.hasData());
                    }
                }

                break;
                
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(uart_num);
                xQueueReset(queue_uart);
                break;

            default: ;
                //Serial.println(event.type, HEX);
            }
        }
        return result;
    };

    void DEBUG_SML_FILE(sml_file *file)
    {
        sml_file_print(file);

        // read here some values ...
        printf("OBIS data\n");
        for (int i = 0; i < file->messages_len; i++)
        {
            sml_message *message = file->messages[i];
            if (*message->message_body->tag == SML_MESSAGE_GET_LIST_RESPONSE)
            {
                sml_list *entry;
                sml_get_list_response *body;
                body = (sml_get_list_response *)message->message_body->data;
                for (entry = body->val_list; entry != NULL; entry = entry->next)
                {
                    if (!entry->value)
                    { // do not crash on null value
                        fprintf(stderr, "Error in data stream. entry->value should not be NULL. Skipping this.\n");
                        continue;
                    }
                    if (entry->value->type == SML_TYPE_OCTET_STRING)
                    {
                        char *str;
                        printf("%d-%d:%d.%d.%d*%d#%s#\n",
                               entry->obj_name->str[0], entry->obj_name->str[1],
                               entry->obj_name->str[2], entry->obj_name->str[3],
                               entry->obj_name->str[4], entry->obj_name->str[5],
                               sml_value_to_strhex(entry->value, &str, true));
                        free(str);
                    }
                    else if (entry->value->type == SML_TYPE_BOOLEAN)
                    {
                        printf("%d-%d:%d.%d.%d*%d#%s#\n",
                               entry->obj_name->str[0], entry->obj_name->str[1],
                               entry->obj_name->str[2], entry->obj_name->str[3],
                               entry->obj_name->str[4], entry->obj_name->str[5],
                               entry->value->data.boolean ? "true" : "false");
                    }
                    else if (((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_INTEGER) ||
                             ((entry->value->type & SML_TYPE_FIELD) == SML_TYPE_UNSIGNED))
                    {
                        double value = sml_value_to_double(entry->value);
                        int scaler = (entry->scaler) ? *entry->scaler : 0;
                        int prec = -scaler;
                        if (prec < 0)
                            prec = 0;
                        value = value * pow(10, scaler);
                        printf("%d-%d:%d.%d.%d*%d#%.*f#",
                               entry->obj_name->str[0], entry->obj_name->str[1],
                               entry->obj_name->str[2], entry->obj_name->str[3],
                               entry->obj_name->str[4], entry->obj_name->str[5], prec, value);
                        const char *unit = NULL;
                        if (entry->unit && // do not crash on null (unit is optional)
                            (unit = dlms_get_unit((unsigned char)*entry->unit)) != NULL)
                            printf("%s", unit);
                        printf("\n");
                        // flush the stdout puffer, that pipes work without waiting
                        fflush(stdout);
                    }
                }
            }
        }
    };

};     // end of class PowerMeter

#endif