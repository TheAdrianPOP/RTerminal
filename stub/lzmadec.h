#ifndef LZMADEC_H
#define LZMADEC_H

#include <stdint.h>
#include <stddef.h>

#define RC_READ_BYTE \
	if (src == src_end) goto err; \
	a = *src++;

#define RC_NORMALIZE \
	if (!(range >> 24)) { \
		range <<= 8; RC_READ_BYTE \
		code = (code << 8) | a; \
	}

#define RC_BIT(p) \
	RC_NORMALIZE \
	t0 = *p; t1 = (range >> 11) * t0; \
	bit0 = code < t1; \
	if (bit0) { \
		range = t1; \
		*p = (uint16_t)(t0 + ((2048 - t0) >> 5)); \
	} else { \
		range -= t1; \
		code -= t1; \
		*p = (uint16_t)(t0 - (t0 >> 5)); \
	}

#define BIT_TREE \
	value = 1; do { \
		p1 = p3 + value; RC_BIT(p1) \
		value <<= 1; if (!bit0) value++; \
	} while (value < n); \
	value -= n;

/* ── BCJ (x86 Branch/Call/Jump) In-Place Inverse Filter ────────────────────── */
#define Test86MSByte(b) ((b) == 0 || (b) == 0xFF)

static inline void x86_decode(uint8_t *buffer, size_t size) {
	static const uint8_t kMaskToAllowedStatus[8] = {1, 1, 1, 0, 1, 0, 0, 0};
	static const uint8_t kMaskToBitNumber[8]     = {0, 1, 2, 2, 3, 3, 3, 3};
	size_t bufferPos = 0, prevPosT;
	uint32_t prevMask = 0;
	if (size < 5) return;
	prevPosT = (size_t)0 - 1;

	for (;;) {
		uint8_t *p = buffer + bufferPos;
		uint8_t *limit = buffer + size - 4;
		for (; p < limit; p++) {
			if ((*p & 0xFE) == 0xE8) break;
		}
		bufferPos = (size_t)(p - buffer);
		if (p >= limit) break;
		prevPosT = bufferPos - prevPosT;
		if (prevPosT > 3) {
			prevMask = 0;
		} else {
			prevMask = (prevMask << ((int)prevPosT - 1)) & 0x7;
			if (prevMask != 0) {
				uint8_t b = p[4 - kMaskToBitNumber[prevMask]];
				if (!kMaskToAllowedStatus[prevMask] || Test86MSByte(b)) {
					prevPosT = bufferPos;
					prevMask = ((prevMask << 1) & 0x7) | 1;
					bufferPos++;
					continue;
				}
			}
		}
		prevPosT = bufferPos;
		if (Test86MSByte(p[4])) {
			uint32_t src = ((uint32_t)p[4] << 24) | ((uint32_t)p[3] << 16) |
			               ((uint32_t)p[2] << 8)  | (uint32_t)p[1];
			uint32_t dest;
			for (;;) {
				uint8_t b;
				int index;
				dest = src - ((uint32_t)bufferPos + 5);
				if (prevMask == 0) break;
				index = kMaskToBitNumber[prevMask] * 8;
				b = (uint8_t)(dest >> (24 - index));
				if (!Test86MSByte(b)) break;
				src = dest ^ ((1U << (32 - index)) - 1);
			}
			p[4] = (uint8_t)(~(((dest >> 24) & 1) - 1));
			p[3] = (uint8_t)(dest >> 16);
			p[2] = (uint8_t)(dest >> 8);
			p[1] = (uint8_t)dest;
			bufferPos += 5;
		} else {
			prevMask = ((prevMask << 1) & 0x7) | 1;
			bufferPos++;
		}
	}
}

/* ── LZMA1 Decoder ────────────────────────────────────────────────────────── */
static inline uint32_t decode_lzma(const uint8_t *src, size_t *src_size,
                                   uint8_t *dst, size_t *dst_size,
                                   uint16_t *probs) {
	const uint8_t *src_end = src + *src_size;
	uint8_t *dst_start = dst, *dst_end = dst + *dst_size;
	uint32_t dict_size = 0, num_probs; uint64_t max_size = 0;

	uint16_t *p1, *p3;
	unsigned rep0 = 1, rep1 = 1, rep2 = 1, rep3 = 1;
	uint32_t lc, lp, pb, range = ~0U, code = 0, t0, t1;
	uint32_t value = 0, state = 0, bit0, len, a, i, n, k;

	RC_READ_BYTE
	pb = a / 9; lc = a % 9; lp = pb % 5; pb /= 5;
	if (pb > 4) goto err;
	for (i = 0; i < 32; i += 8) { RC_READ_BYTE dict_size |= (uint32_t)a << i; }
	for (i = 0; i < 64; i += 8) { RC_READ_BYTE max_size |= (uint64_t)a << i; }
	RC_READ_BYTE if (a) goto err;
	for (i = 0; i < 4; i++) { RC_READ_BYTE code = (code << 8) | a; }

	if (dict_size < 0x1000) dict_size = 0x1000;
	num_probs = 1846 + (768 << (lc + lp));

	for (i = 0; i < num_probs; i++) probs[i] = 1024;

	while (max_size != (uint64_t)(dst - dst_start)) {
		len = 0;
		k = (dst - dst_start) & ((1 << pb) - 1);
		p3 = probs + state * 16 + k; RC_BIT(p3)
		if (bit0) {
			uint32_t offset;
			k = (dst - dst_start) & ((1 << lp) - 1);
			k = (k << lc) | (value >> (8 - lc));
			p3 = probs + 1846 + 768 * k;
			offset = i = 0;
			if (state >= 7)
				offset = 0x100, i = *(dst - rep0);
			len = value = 1;
			do {
				i <<= 1;
				p1 = p3 + offset + (i & offset) + value; RC_BIT(p1)
				value <<= 1;
				if (bit0) offset &= ~i; else value++, offset &= i; 
			} while (value < 256);
			state -= state < 4 ? state : state > 9 ? 6 : 3;
			value &= 255;
			goto copy;

		} else {
			p1 = probs + 192 + state;
			state = state < 7 ? 0 : 3;
			RC_BIT(p1)
			if (bit0) {
				rep3 = rep2; rep2 = rep1; rep1 = rep0;
				p1 = probs + 818;
			} else {
				p1 += 12; RC_BIT(p1)
				if (bit0) {
					p3 += 240; RC_BIT(p3)
					if (bit0) state |= 9, len = 1;
				} else {
					p1 += 12; RC_BIT(p1)
					if (bit0) n = rep1;
					else {
						p1 += 12; RC_BIT(p1)
						if (bit0) n = rep2; else n = rep3, rep3 = rep2;
						rep2 = rep1;
					}
					rep1 = rep0; rep0 = n;
				}
				if (!len) p1 = probs + 1332, state |= 8;
				else state |= 9;
			}

			if (!len) {
				len = 2; n = 8;
				p3 = p1 + k * 8 + 2; RC_BIT(p1)
				if (!bit0) {
					p1++; len = 10;
					p3 += 128; RC_BIT(p1)
					if (!bit0) n = 256, p3 = p1 + n + 1, len += 8;
				}
				BIT_TREE
				len += value;
				if (state < 4) {
					state += 7; n = 64;
					p3 = probs + 304 + (len < 6 ? len : 5) * n;
					BIT_TREE
					rep0 = value;
					if (rep0 > 3) {
						n = (value >> 1) - 1; i = 1 << n;
						rep0 = (2 | (value & 1)) << n;
						if (n < 6) p3 = probs + 687 + rep0 - value;
						else {
							do {
								RC_NORMALIZE
								i >>= 1; range >>= 1;
								if (code >= range)
									code -= range, rep0 += i;
							} while (i != 16);
							p3 = probs + 802;
						}
						n = value = 1;
						do {
							p1 = p3 + value;
							value <<= 1;
							RC_BIT(p1) if (!bit0) value++, rep0 |= n;
							n <<= 1;
						} while (value < i);
					}
					rep0++;
				}
			}
			if (!rep0) break;
			if (rep0 > dict_size) goto err;
			if (rep0 > (size_t)(dst - dst_start)) goto err;
			if (max_size - (dst - dst_start) < len) goto err;
			do {
				value = *(dst - rep0);
copy:			if (dst == dst_end) goto err;
				*dst++ = value;
			} while (--len);
		}
	}

	*src_size = src_end - src;
	*dst_size = dst - dst_start;
	return 0;

err:
	*dst_size = dst - dst_start;
	return 1;
}

#endif