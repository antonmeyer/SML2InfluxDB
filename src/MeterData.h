#ifndef METERDATA_H
#define METERDATA_H

// SML frame decoding, deliberately free of Arduino/ESP32 dependencies,
// so that it can be built and tested on the host (see test/test_decoder).

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sml/sml_crc16.h>
#include <sml/sml_file.h>
#include <sml/sml_value.h>

#define TBLENTRIES 8

// result of smlDecodeFrame()
enum SmlFrameStatus
{
    SML_FRAME_OK = 0,
    SML_FRAME_TOO_SHORT, // shorter than start + end sequence
    SML_FRAME_NO_START,  // no 1b1b1b1b 01010101 at the beginning
    SML_FRAME_NO_END,    // no 1b1b1b1b 1a at the end
    SML_FRAME_BAD_CRC,   // CRC16 mismatch
    SML_FRAME_NO_DATA    // valid transport frame, but no GetListResponse with values
};

class MeterDataSet
{
public:
    const char *alias = nullptr; // name of the connection (like "VHWP"), used if the meter is not in the lookup table
    char meterid[32] = "";       // vendor (3 chars) + serial as hex without blanks, empty if unknown

    // The values are only valid if the corresponding has... flag is set.
    // Not every meter delivers everything: e.g. the Landis+Gyr LGZ meters do not send the actual power
    // (1-0:16.7.0) unless the PIN was entered on the meter, and no export energy on a pure consumer.
    double actW = 0;  // actual power in W,          OBIS 1-0:16.7.0
    double sumWh = 0; // consumed energy in Wh,     OBIS 1-0:1.8.0
    double expWh = 0; // delivered energy in Wh,    OBIS 1-0:2.8.0
    bool hasActW = false;
    bool hasSumWh = false;
    bool hasExpWh = false;

    // Time of the meter (actSensorTime of the GetListResponse). Depending on the meter this is a counter of seconds
    // since power-up (secIndex) or an epoch timestamp. The frame itself has no wall clock time, but two frames of
    // the same meter can be put in relation with it (e.g. to calculate the power from the energy delta).
    uint32_t sensorTime = 0;
    bool hasSensorTime = false;

    // vendor + serial -> alias, GH = GartenHaus, VH = VorderHaus
    // WP = Waermepumpe, HS = Haushalt
    const char *lookup(const char *id) const
    {
        static const char *const tbl[TBLENTRIES][2] = {
            {"GHWP", "EMH0901454d4800006bd439"}, // old meter, replaced in 2026
            {"GHWP", "LGZ0a014c475a0004370688"}, // Landis+Gyr, since 2026, label "1 LGZ00 7071 5016"
            {"GHHS", "ISK090149534b000421d3d9"},
            {"VHWP", "EMH0901454d4800006bd340"}, // old meter, replaced in 2026
            {"VHWP", "LGZ0a014c475a0001260d00"}, // Landis+Gyr, since 2026, label "1 LGZ00 1927 0912" (last 4 bytes decimal)
            {"VHHS", "ISK090149534b000421d3da"},
            {"TG", "ISK090149534b000421d3dc"},
            {"Loewe", "EMH01a815133577030102"}};
        for (int i = 0; i < TBLENTRIES; i++)
            if (strcmp(id, tbl[i][1]) == 0)
                return tbl[i][0];
        return nullptr;
    }

    // name for the influx tag: lookup table, else configured alias, else the meterid itself
    const char *getalias() const
    {
        const char *a = lookup(meterid);
        if (a)
            return a;
        if (alias && alias[0])
            return alias;
        return meterid;
    }

    // forget the values of the last frame, keeps alias
    void clearValues()
    {
        meterid[0] = 0;
        actW = sumWh = expWh = 0;
        hasActW = hasSumWh = hasExpWh = hasSensorTime = false;
        sensorTime = 0;
    }

    bool hasData() const { return hasActW || hasSumWh || hasExpWh; }

    void print() const
    {
        printf("meterid: %s (%s)", meterid, getalias());
        if (hasSensorTime)
            printf(", t: %u", (unsigned)sensorTime);
        if (hasActW)
            printf(", actW: %.1f", actW);
        if (hasSumWh)
            printf(", sumWh: %.1f", sumWh);
        if (hasExpWh)
            printf(", expWh: %.1f", expWh);
        printf("\n");
    }

    // build meterid = vendor(3 chars) + hex(serial); vendor might be shorter, serial is cut if too long
    void setMeterId(const unsigned char *vendor, int vendor_len, const unsigned char *serial, int serial_len)
    {
        static const char hex_str[] = "0123456789abcdef";
        int j = 0;
        for (int i = 0; i < 3 && i < vendor_len; i++)
            meterid[j++] = (char)vendor[i];
        for (int i = 0; i < serial_len && j + 2 < (int)sizeof(meterid); i++)
        {
            meterid[j++] = hex_str[(serial[i] >> 4) & 0x0F];
            meterid[j++] = hex_str[serial[i] & 0x0F];
        }
        meterid[j] = 0;
    }
};

// 6 byte OBIS code
static inline bool obisIs(const octet_string *name, const unsigned char (&obis)[6])
{
    return name && name->len == 6 && memcmp(name->str, obis, 6) == 0;
}

static inline bool smlIsOctets(const sml_value *v)
{
    return v && v->type == SML_TYPE_OCTET_STRING && v->data.bytes && v->data.bytes->str;
}

// numeric value with scaler applied, false if the entry is not a number
static inline bool smlNumber(const sml_list *e, double &out)
{
    if (!e->value)
        return false;
    uint8_t t = e->value->type & SML_TYPE_FIELD;
    if (t != SML_TYPE_INTEGER && t != SML_TYPE_UNSIGNED)
        return false;
    int scaler = e->scaler ? *e->scaler : 0;
    out = sml_value_to_double(e->value) * pow(10, scaler);
    return true;
}

// Which value is meant by an OBIS code. The same value is sent under different codes by different meters
// (Iskra/EMH: 129-129:199.130.3 and 1-0:0.0.9, Landis+Gyr: 1-0:96.50.1 and 1-0:96.1.0).
// A new meter type is usually just another line here. The whole code is compared, as F != 255 would be a
// historical value.
enum SmlField
{
    F_VENDOR, // octet string, 3 chars
    F_SERIAL, // octet string
    F_ACTW,
    F_SUMWH,
    F_EXPWH
};

struct ObisMap
{
    unsigned char obis[6];
    SmlField field;
};

static const ObisMap obisMap[] = {
    {{129, 129, 199, 130, 3, 255}, F_VENDOR}, // Iskra, EMH
    {{1, 0, 96, 50, 1, 1}, F_VENDOR},         // Landis+Gyr
    {{1, 0, 0, 0, 9, 255}, F_SERIAL},         // Iskra, EMH
    {{1, 0, 96, 1, 0, 255}, F_SERIAL},        // Landis+Gyr
    {{1, 0, 16, 7, 0, 255}, F_ACTW},          // actual power, Landis+Gyr only with PIN
    {{1, 0, 1, 8, 0, 255}, F_SUMWH},          // total consumed energy
    {{1, 0, 2, 8, 0, 255}, F_EXPWH},          // total delivered energy
};

// Extract the values we are interested in from the parsed SML file. Returns true if there was a GetListResponse.
static inline bool smlFilterValues(sml_file *file, MeterDataSet &ds)
{
    bool found = false;
    const unsigned char *vendor = nullptr, *serial = nullptr;
    int vendor_len = 0, serial_len = 0;

    for (int i = 0; i < file->messages_len; i++)
    {
        sml_message *message = file->messages[i];
        if (!message || !message->message_body || !message->message_body->tag ||
            *message->message_body->tag != SML_MESSAGE_GET_LIST_RESPONSE)
            continue;

        sml_get_list_response *body = (sml_get_list_response *)message->message_body->data;
        if (!body)
            continue;
        found = true;

        if (body->act_sensor_time && body->act_sensor_time->data.timestamp)
        {
            ds.sensorTime = *body->act_sensor_time->data.timestamp;
            ds.hasSensorTime = true;
        }

        // the server id is the fallback if the meter does not send a serial as value
        if (body->server_id && body->server_id->str)
        {
            serial = body->server_id->str;
            serial_len = body->server_id->len;
        }

        for (sml_list *entry = body->val_list; entry != NULL; entry = entry->next)
        {
            if (!entry->value)
                continue; // do not crash on null value

            for (const ObisMap &m : obisMap)
            {
                if (!obisIs(entry->obj_name, m.obis))
                    continue;

                double value;
                switch (m.field)
                {
                case F_VENDOR:
                    if (smlIsOctets(entry->value))
                    {
                        vendor = entry->value->data.bytes->str;
                        vendor_len = entry->value->data.bytes->len;
                    }
                    break;
                case F_SERIAL:
                    if (smlIsOctets(entry->value))
                    {
                        serial = entry->value->data.bytes->str;
                        serial_len = entry->value->data.bytes->len;
                    }
                    break;
                case F_ACTW:
                    if (smlNumber(entry, value))
                    {
                        ds.actW = value;
                        ds.hasActW = true;
                    }
                    break;
                case F_SUMWH:
                    if (smlNumber(entry, value))
                    {
                        ds.sumWh = value;
                        ds.hasSumWh = true;
                    }
                    break;
                case F_EXPWH:
                    if (smlNumber(entry, value))
                    {
                        ds.expWh = value;
                        ds.hasExpWh = true;
                    }
                    break;
                }
                break; // an entry has only one meaning
            }
        }
    }

    // the pointers are still valid, the file is freed by the caller
    if (serial)
        ds.setMeterId(vendor, vendor_len, serial, serial_len);

    return found;
}

// Decode a complete SML transport frame:
//   1b1b1b1b 01010101 | SML messages (+ padding) | 1b1b1b1b 1a <padding count> <crc16 hi> <crc16 lo>
// The CRC is checked, the values are written to ds (cleared before).
static inline SmlFrameStatus smlDecodeFrame(const uint8_t *frame, size_t len, MeterDataSet &ds)
{
    static const uint8_t startSeq[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};
    static const uint8_t endSeq[5] = {0x1b, 0x1b, 0x1b, 0x1b, 0x1a};

    if (len < 8 + 8)
        return SML_FRAME_TOO_SHORT;
    if (memcmp(frame, startSeq, sizeof(startSeq)) != 0)
        return SML_FRAME_NO_START;
    if (memcmp(frame + len - 8, endSeq, sizeof(endSeq)) != 0)
        return SML_FRAME_NO_END;

    uint16_t crc_frame = (uint16_t)((frame[len - 2] << 8) | frame[len - 1]);
    if (sml_crc16_calculate((unsigned char *)frame, (int)len - 2) != crc_frame)
        return SML_FRAME_BAD_CRC;

    ds.clearValues();

    // skip the start flag, the end flag with padding count and CRC
    sml_file *file = sml_file_parse((unsigned char *)frame + 8, len - 16);
    if (!file)
        return SML_FRAME_NO_DATA;
    bool found = smlFilterValues(file, ds);
    sml_file_free(file);

    return found ? SML_FRAME_OK : SML_FRAME_NO_DATA;
}

#endif
