#ifndef FRAMEASSEMBLER_H
#define FRAMEASSEMBLER_H

// Cuts SML transport frames out of a byte stream (serial port), for the Raspberry Pi service. No dependencies.
//
//   frame:  1b1b1b1b 01010101 | messages + padding | 1b1b1b1b 1a <padding count> <crc16 hi> <crc16 lo>
//
// Bytes can arrive in any chunks. Junk before a start flag and a cut off frame (the next start flag shows up
// before the end flag) are dropped, so the next frame is found again. 1b1b1b1b 1b1b1b1b inside a frame is the
// escaped form of the flag bytes and does not end the frame. The CRC is not checked here (smlDecodeFrame does).

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <string>

class FrameAssembler
{
public:
    // Adds bytes; calls onFrame(const uint8_t *frame, size_t len) for every complete frame.
    template <class F>
    void feed(const uint8_t *data, size_t n, F onFrame)
    {
        buf_.append((const char *)data, n);
        for (;;)
        {
            size_t s = findStart();
            if (s == std::string::npos)
            {
                // keep a possible beginning of a start flag at the end
                if (buf_.size() > 7)
                {
                    junk_ += buf_.size() - 7;
                    buf_.erase(0, buf_.size() - 7);
                }
                return;
            }
            if (s > 0)
            {
                junk_ += s;
                buf_.erase(0, s);
            }
            size_t end = 0; // index after the last byte of the frame
            int r = scan(end);
            if (r == WAIT)
            {
                if (buf_.size() > MAX_FRAME) // never ends: drop the start flag, look for the next one
                {
                    junk_ += 8;
                    buf_.erase(0, 8);
                    continue;
                }
                return;
            }
            if (r == RESTART) // a new start flag came before the end: the frame before was cut off
            {
                cut_++;
                buf_.erase(0, end);
                continue;
            }
            onFrame((const uint8_t *)buf_.data(), end);
            buf_.erase(0, end);
        }
    }

    size_t junkBytes() const { return junk_; } // bytes skipped in front of frames
    size_t cutFrames() const { return cut_; }  // frames that were interrupted by a new start flag

private:
    static const size_t MAX_FRAME = 8192;
    enum { WAIT, RESTART, DONE };
    std::string buf_;
    size_t junk_ = 0, cut_ = 0;

    static bool isEsc(const std::string &b, size_t i) { return memcmp(b.data() + i, "\x1b\x1b\x1b\x1b", 4) == 0; }

    size_t findStart() const
    {
        static const char start[8] = {0x1b, 0x1b, 0x1b, 0x1b, 0x01, 0x01, 0x01, 0x01};
        return buf_.find(std::string(start, 8));
    }

    // buf_ starts with a start flag. DONE: end = frame length. RESTART: end = index of a new start flag.
    int scan(size_t &end) const
    {
        size_t i = 8;
        while (i + 4 <= buf_.size())
        {
            if (!isEsc(buf_, i))
            {
                i++;
                continue;
            }
            if (i + 8 > buf_.size())
                return WAIT; // the byte after the flag is not here yet
            if (isEsc(buf_, i + 4))
            {
                i += 8; // escaped flag bytes in the data
                continue;
            }
            if ((uint8_t)buf_[i + 4] == 0x1a)
            {
                end = i + 8; // 1b1b1b1b 1a <padding> <crc> <crc>
                return buf_.size() >= end ? DONE : WAIT;
            }
            if ((uint8_t)buf_[i + 4] == 0x01 && i + 8 <= buf_.size() && memcmp(buf_.data() + i + 4, "\x01\x01\x01\x01", 4) == 0)
            {
                end = i; // new start flag
                return RESTART;
            }
            i++; // something else after 1b1b1b1b: not a flag
        }
        return WAIT;
    }
};

#endif
