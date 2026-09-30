// Ersatz für MAMEs endianness.h — nur was z8000cpu.h braucht (Wirt: little-endian).
#pragma once
#define BYTE_XOR_BE(a)  ((a) ^ 1)
#define BYTE4_XOR_BE(a) ((a) ^ 3)
#define BYTE8_XOR_BE(a) ((a) ^ 7)
