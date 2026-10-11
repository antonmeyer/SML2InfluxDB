#ifndef OTAVERSION_H
#define OTAVERSION_H

#include <stdio.h>

// Compares two version strings "a.b.c.d" (1 to 4 numbers, missing ones are 0). True if candidate is newer.
// Text that is not a version counts as 0.0.0.0, so it is never newer than anything.
static inline bool versionNewer(const char *candidate, const char *current)
{
    unsigned c[4] = {0, 0, 0, 0}, r[4] = {0, 0, 0, 0};
    if (candidate)
        sscanf(candidate, "%u.%u.%u.%u", &c[0], &c[1], &c[2], &c[3]);
    if (current)
        sscanf(current, "%u.%u.%u.%u", &r[0], &r[1], &r[2], &r[3]);
    for (int i = 0; i < 4; i++)
    {
        if (c[i] != r[i])
            return c[i] > r[i];
    }
    return false;
}

#endif
