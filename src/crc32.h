#ifndef YUKINO_CRC32_H_
#define YUKINO_CRC32_H_

#include <stddef.h>
#include <stdint.h>

// from Apple Inc.
extern "C" uint32_t crc32(uint32_t crc, const void *buf, size_t size);

#endif // YUKINO_CRC32_H_