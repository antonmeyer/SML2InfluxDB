#ifndef POWERMETER
#define POWERMETER

#include "driver/uart.h"
#include "endian.h"
#include <sml/sml_file.h>
#include <sml/sml_value.h>
#include "unit.h"
#include <string.h>

#define BUF_SIZE 1024

#define SMLFLAG 0x1b1b1b1b
#define SMLSTART 0x01010101

class MeterDataSet
{

public:
    String alias;     //like WPGH or HSVH - to save data and have meaningful identifiyer
    char meterid[30]; //vendor ID and 9 or 10 bytes HEX without space/blanks
    double actW;      //actual Power in Watt
    double sumWh;     //total Energy in Wh

    //this table is used to MAP vendor and serial number to an alias GH = GarenHaus, VH = VorderHaus
#define TBLENTRIES 6
    const char *alias_lkup_tbl[TBLENTRIES][2] = {{"GHWP", "EMH0901454d4800006bd439"},
                                                 {"GHHS", "ISK090149534b000421d3d9"},
                                                 {"VHWP", "EMH0901454d4800006bd340"},
                                                 {"VHHS", "ISK090149534b000421d3da"},
                                                 {"TG", "ISK090149534b000421d3dc"},
                                                 {"Loewe", "EMH01a815133577030102"}};

    void setMeterIDserial(unsigned char *ba, int len)
    {
        const char hex_str[] = "0123456789abcdef";
        //ToDo check the len here, limit to meterid array
        int j = 3; //first 3 chars are the vendor
        for (int i = 0; i < len; i++)
        {
            meterid[j++] = hex_str[(ba[i] >> 4) & 0x0F];
            meterid[j++] = hex_str[ba[i] & 0x0F];
        }
        meterid[j] = 0; //zero terminated string
    };

    const char *getalias()
    {
        //returns pointer to string alias, if no alias in lookup table found returns meterid
        int i = 0;
        while (!(strcmp(meterid, alias_lkup_tbl[i][1]) == 0))
        {
            if (++i == TBLENTRIES)
                break;
        }

        if (i < TBLENTRIES)
        {
            return alias_lkup_tbl[i][0];
        }
        else
            return meterid; //we did not found a alias
    };
};

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

    uint8_t uartbuf[BUF_SIZE];

    uart_port_t uart_num;
    QueueHandle_t queue_uart;

    int length = 0;
    int pos = 0;
    int sml_start_flag = 0;
    int len = 0;

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
        uart_driver_install(uart_num, BUF_SIZE, 0, 20, &queue_uart, 0);
        uart_enable_pattern_det_intr(uart_num, 0x1B, 4, 10000, 10, 10); // detect 4x 0x1B
        uart_pattern_queue_reset(uart_num, 5);                          // keep 5 pattern positions in queue
    };

    char handle_event()
    {
        char result = 0;
        uart_event_t event;

        if (xQueueReceive(queue_uart, (void *)&event, (TickType_t)0))
        {
            printf("UART: %d\n", uart_num);
            Serial.print("we got an event: ");

            switch (event.type)
            {

            case UART_PATTERN_DET:
                Serial.println(event.type, HEX);
                Serial.print("pattern found, pos: ");

                pos = uart_pattern_pop_pos(uart_num);
                Serial.print(pos);
                uart_get_buffered_data_len(uart_num, (size_t *)&length);
                Serial.print(" / ");
                Serial.println(length);
                if (pos > 0)
                { //the assumption: first pattern is at position 0x0;
                    // so if we are here, we should have the 2nd at the end of the message
                    // if it is a short fragment we just read it, check later will fail and we start over
                    // as there is always a pause between messages we should hit the next one at pos 0x0 in the FIFO

                    // so let´s read the complete message - we had allocated enough BUFF_SIZE - at least we hope so
                    len = uart_read_bytes(uart_num, uartbuf, pos, 20 / portTICK_RATE_MS);

                    //we  check the start of SML frame
                    //otherwise, just skipp,
                    uint32_t *smlarray = (uint32_t *)uartbuf; //we assume that the sml are 32 bit alligned, force casting

                    if ((smlarray[0] == SMLFLAG) && (smlarray[1] == SMLSTART))
                    {
                        Serial.println("StartFlag found");
                        //read the endflag and crc- it should be here, as we got the event and the pos

                        uart_read_bytes(uart_num, &uartbuf[len], 8, 20 / portTICK_RATE_MS);

                        //we could do some checks here, but is boring ..

                        //and now we can search for the values within uartbuf

                        uint32_t startCounter = ESP.getCycleCount();
                        //tricky skipp the start flag and the end flag and CRC
                        sml_file *file = sml_file_parse(uartbuf + 8, len - 8);
                        printf("sml_parse cpu-cycles: %d\n", (ESP.getCycleCount() - startCounter));

                        // DEBUG_SML_FILE(file);
                        filterValues(file);

                        // free the malloc'd memory
                        sml_file_free(file);
                        result = 1;
                    }
                    //else we just wait for the next pattern event
                    //could be better to flush here to get back in sync?
                    //we might loss the first messages, but it should get in sync as between the messages are several seconds pause
                    //else uart_flush(uart_num); //does not work, makes thinks worse
                }

                break;
            default:
                Serial.println(event.type, HEX);
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

    void filterValues(sml_file *file)
    {
        const unsigned char objID_vendor[6] = {129, 129, 199, 130, 3, 255}; //129-129:199.130.3*255
        const unsigned char objID_serial[6] = {1, 0, 0, 0, 9, 255};         //1-0:0.0.9*255
        const unsigned char objID_actW[6] = {1, 0, 16, 7, 0, 255};          //1-0:16.7.0*255 actual ePower
        const unsigned char objID_sumWh[6] = {1, 0, 1, 8, 0, 255};          // 1-0:1.8.0*255 total consumed e Energy

        // have a list of object identifiers to filter
        // gleich ins influxdb line protocol umwandeln?
        // nur senden wenn sich ein Wert verändert hat

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

                    if (memcmp(entry->obj_name->str, objID_vendor, 6) == 0)
                    {
                        //we have the vendor, should be an octet String of lenght 3, -> 3 first char in meterid
                        strncpy(dataset.meterid, (const char *)(entry->value->data.bytes->str), 3);
                        printf("vendor: %.3s\n", dataset.meterid);
                    }
                    else if (memcmp(entry->obj_name->str, objID_serial, 6) == 0)
                    {
                        //we have the serial, should be an octed string of 10 , might vary
                        //strncpy(dataset.serial, (const char *)(entry->value->data.bytes->str), 10);
                        dataset.setMeterIDserial(entry->value->data.bytes->str, entry->value->data.bytes->len);
                        printf("serial: %s\n", dataset.meterid + 3); //first 3 char are verndor ID
                    }
                    else if (memcmp(entry->obj_name->str, objID_actW, 6) == 0)
                    {
                        //we have the actual power in W
                        // should be of type integer

                        double value = sml_value_to_double(entry->value);
                        int scaler = (entry->scaler) ? *entry->scaler : 0;
                        int prec = -scaler;
                        if (prec < 0)
                            prec = 0;
                        dataset.actW = value * pow(10, scaler);
                        printf("actW = %.*f\n", prec, dataset.actW);
                        //ToDo do we want to deal with prec?
                    }
                    else if (memcmp(entry->obj_name->str, objID_sumWh, 6) == 0)
                    {
                        //we have the total e Energy of the meter
                        //should be integer

                        double value = sml_value_to_double(entry->value);
                        int scaler = (entry->scaler) ? *entry->scaler : 0;
                        int prec = -scaler;
                        if (prec < 0)
                            prec = 0;
                        dataset.sumWh = value * pow(10, scaler);
                        printf("actW = %.*f\n", prec, dataset.sumWh);
                        //ToDo do we want to deal with prec?
                    }
                }
            }
        }
        printf("minHeap: %d, maxSeg: %d\n", ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
        printf("meterid: %s\n", dataset.meterid);

        printf("alias: %s\n", dataset.getalias());

    }; // filter Value
};     // end of class PowerMeter

#endif