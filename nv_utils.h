#pragma once

/*
Routines for writing / reading blocks of data to / from EEPROM
with checksum protection. The checksum is written
twice - before and after the block of data. The latter
is written with all bits inverted.
*/

#include <EEPROM.h>

static inline uint8_t crc8_up(uint8_t crc, uint8_t up)
{
	for (uint8_t i = 8; i; --i) {
		uint8_t mix = (crc ^ up) & 1;
		crc >>= 1;
		if (mix) crc ^= 0x8C;
		up >>= 1;
	}
	return crc;
}

static inline bool nv_get(void* val, unsigned size, unsigned addr)
{
	uint8_t crc = 0;
	uint8_t *ptr = val;
	unsigned sz, a;
	for (sz = size, a = addr + 1; sz; --sz, ++a) {
		uint8_t byte = EEPROM.read(a);
		crc = crc8_up(crc, byte);
	}
	if (EEPROM.read(addr) != crc)
		return false;
	if (EEPROM.read(a) != (uint8_t)~crc)
		return false;
	for (sz = size, a = addr + 1; sz; --sz, ++a, ++ptr) {
		uint8_t byte = EEPROM.read(a);
		*ptr = byte;
	}
	return true;
}

static inline void nv_put(void* val, unsigned size, unsigned addr)
{
	uint8_t crc = 0;
	uint8_t *ptr = val;
	unsigned sz, a;
	EEPROM.write(addr + size + 1, EEPROM.read(addr));
	for (sz = size, a = addr + 1; sz; --sz, ++a, ++ptr) {
		uint8_t byte = *ptr;
		EEPROM.write(a, byte);
		crc = crc8_up(crc, byte);
	}
	EEPROM.write(addr, crc);
	EEPROM.write(a,   ~crc);
}
