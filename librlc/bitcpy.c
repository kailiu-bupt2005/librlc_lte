/* bitcpy.c 
 * Thanks to linux kernel amifb.c
 */

#define BITS_PER_LONG 32

typedef unsigned int u32;

#if BITS_PER_LONG == 32
#define BYTES_PER_LONG  4
#define SHIFT_PER_LONG  5
#elif BITS_PER_LONG == 64
#define BYTES_PER_LONG  8
#define SHIFT_PER_LONG  6
#else
#define Please update me
#endif


/*
 *  Compose two values, using a bitmask as decision value
 *  This is equivalent to (a & mask) | (b & ~mask)
 */
static inline unsigned long comp(unsigned long a, unsigned long b,
                                 unsigned long mask)
{
        return ((a ^ b) & mask) ^ b;
}


static inline unsigned long xor(unsigned long a, unsigned long b,
                                unsigned long mask)
{
        return (a & mask) ^ b;
}

void bitcpy(unsigned long *dst, int dst_idx, const unsigned long *src, int src_idx, u32 n)
{
        unsigned long first, last;
        int shift = dst_idx-src_idx, left, right;
        unsigned long d0, d1;
        int m;

        if (!n)
                return;

        shift = dst_idx-src_idx;
        first = ~0UL >> dst_idx;
        last = ~(~0UL >> ((dst_idx+n) % BITS_PER_LONG));

        if (!shift) {
                // Same alignment for source and dest

                if (dst_idx+n <= BITS_PER_LONG) {
                        // Single word
                        if (last)
                                first &= last;
                        *dst = comp(*src, *dst, first);
                } else {
                        // Multiple destination words
                        // Leading bits
                        if (first) {
                                *dst = comp(*src, *dst, first);
                                dst++;
                                src++;
                                n -= BITS_PER_LONG-dst_idx;
                        }

                        // Main chunk
                        n /= BITS_PER_LONG;
                        while (n >= 8) {
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                *dst++ = *src++;
                                n -= 8;
                        }
                        while (n--)
                                *dst++ = *src++;

                        // Trailing bits
                        if (last)
                                *dst = comp(*src, *dst, last);
                }
        } else {
                // Different alignment for source and dest

                right = shift & (BITS_PER_LONG-1);
                left = -shift & (BITS_PER_LONG-1);

                if (dst_idx+n <= BITS_PER_LONG) {
                        // Single destination word
                        if (last)
                                first &= last;
                        if (shift > 0) {
                                // Single source word
                                *dst = comp(*src >> right, *dst, first);
                        } else if (src_idx+n <= BITS_PER_LONG) {
                                // Single source word
                                *dst = comp(*src << left, *dst, first);
                        } else {
                                // 2 source words
                                d0 = *src++;
                                d1 = *src;
                                *dst = comp(d0 << left | d1 >> right, *dst,
                                            first);
                        }
                } else {
                        // Multiple destination words
                        d0 = *src++;
                        // Leading bits
                        if (shift > 0) {
                                // Single source word
                                *dst = comp(d0 >> right, *dst, first);
                                dst++;
                                n -= BITS_PER_LONG-dst_idx;
                        } else {
                                // 2 source words
                                d1 = *src++;
                                *dst = comp(d0 << left | d1 >> right, *dst,
                                            first);
                                d0 = d1;
                                dst++;
                                n -= BITS_PER_LONG-dst_idx;
                        }

                        // Main chunk
                        m = n % BITS_PER_LONG;
                        n /= BITS_PER_LONG;
                        while (n >= 4) {
                                d1 = *src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = *src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = *src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = *src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                n -= 4;
                        }
                        while (n--) {
                                d1 = *src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                        }

                        // Trailing bits
                        if (last) {
                                if (m <= right) {
                                        // Single source word
                                        *dst = comp(d0 << left, *dst, last);
                                } else {
                                        // 2 source words
                                        d1 = *src;
                                        *dst = comp(d0 << left | d1 >> right,
                                                    *dst, last);
                                }
                        }
                }
        }
}


/*					
 * 					 Unaligned reverse bit copy using 32-bit or 64-bit memory accesses
 */					

void bitcpy_rev(unsigned long *dst, int dst_idx,
                       const unsigned long *src, int src_idx, u32 n)
{
        unsigned long first, last;
        int shift = dst_idx-src_idx, left, right;
        unsigned long d0, d1;
        int m;

        if (!n)
                return;

        dst += (n-1)/BITS_PER_LONG;
        src += (n-1)/BITS_PER_LONG;
        if ((n-1) % BITS_PER_LONG) {
                dst_idx += (n-1) % BITS_PER_LONG;
                dst += dst_idx >> SHIFT_PER_LONG;
                dst_idx &= BITS_PER_LONG-1;
                src_idx += (n-1) % BITS_PER_LONG;
                src += src_idx >> SHIFT_PER_LONG;
                src_idx &= BITS_PER_LONG-1;
        }

        shift = dst_idx-src_idx;
        first = ~0UL << (BITS_PER_LONG-1-dst_idx);
        last = ~(~0UL << (BITS_PER_LONG-1-((dst_idx-n) % BITS_PER_LONG)));

        if (!shift) {
                // Same alignment for source and dest

                if ((unsigned long)dst_idx+1 >= n) {
                        // Single word
                        if (last)
                                first &= last;
                        *dst = comp(*src, *dst, first);
                } else {
                        // Multiple destination words
                        // Leading bits
                        if (first) {
                                *dst = comp(*src, *dst, first);
                                dst--;
                                src--;
                                n -= dst_idx+1;
                        }

                        // Main chunk
                        n /= BITS_PER_LONG;
                        while (n >= 8) {
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                *dst-- = *src--;
                                n -= 8;
                        }
                        while (n--)
                                *dst-- = *src--;

                        // Trailing bits
                        if (last)
                                *dst = comp(*src, *dst, last);
                }
        } else {
                // Different alignment for source and dest

                right = shift & (BITS_PER_LONG-1);
                left = -shift & (BITS_PER_LONG-1);

                if ((unsigned long)dst_idx+1 >= n) {
                        // Single destination word
                        if (last)
                                first &= last;
                        if (shift < 0) {
                                // Single source word
                                *dst = comp(*src << left, *dst, first);
                        } else if (1+(unsigned long)src_idx >= n) {
                                // Single source word
                                *dst = comp(*src >> right, *dst, first);
                        } else {
                                // 2 source words
                                d0 = *src--;
                                d1 = *src;
                                *dst = comp(d0 >> right | d1 << left, *dst,
                                            first);
                        }
                } else {
                        // Multiple destination words
                        d0 = *src--;
                        // Leading bits
                        if (shift < 0) {
                                // Single source word
                                *dst = comp(d0 << left, *dst, first);
                                dst--;
                                n -= dst_idx+1;
                        } else {
                                // 2 source words
                                d1 = *src--;
                                *dst = comp(d0 >> right | d1 << left, *dst,
                                            first);
                                d0 = d1;
                                dst--;
                                n -= dst_idx+1;
                        }

                        // Main chunk
                        m = n % BITS_PER_LONG;
                        n /= BITS_PER_LONG;
                        while (n >= 4) {
                                d1 = *src--;
                                *dst-- = d0 >> right | d1 << left;
                                d0 = d1;
                                d1 = *src--;
                                *dst-- = d0 >> right | d1 << left;
                                d0 = d1;
                                d1 = *src--;
                                *dst-- = d0 >> right | d1 << left;
                                d0 = d1;
                                d1 = *src--;
                                *dst-- = d0 >> right | d1 << left;
                                d0 = d1;
                                n -= 4;
                        }
                        while (n--) {
                                d1 = *src--;
                                *dst-- = d0 >> right | d1 << left;
                                d0 = d1;
                        }

                        // Trailing bits
                        if (last) {
                                if (m <= left) {
                                        // Single source word
                                        *dst = comp(d0 >> right, *dst, last);
                                } else {
                                        // 2 source words
                                        d1 = *src;
                                        *dst = comp(d0 >> right | d1 << left,
                                                    *dst, last);
                                }
                        }
                }
        }
}


/*
 *  Unaligned forward inverting bit copy using 32-bit or 64-bit memory
 *  accesses
 */

void bitcpy_not(unsigned long *dst, int dst_idx,
                       const unsigned long *src, int src_idx, u32 n)
{
        unsigned long first, last;
        int shift = dst_idx-src_idx, left, right;
        unsigned long d0, d1;
        int m;

        if (!n)
                return;

        shift = dst_idx-src_idx;
        first = ~0UL >> dst_idx;
        last = ~(~0UL >> ((dst_idx+n) % BITS_PER_LONG));

        if (!shift) {
                // Same alignment for source and dest

                if (dst_idx+n <= BITS_PER_LONG) {
                        // Single word
                        if (last)
                                first &= last;
                        *dst = comp(~*src, *dst, first);
                } else {
                        // Multiple destination words
                        // Leading bits
                        if (first) {
                                *dst = comp(~*src, *dst, first);
                                dst++;
                                src++;
                                n -= BITS_PER_LONG-dst_idx;
                        }

                        // Main chunk
                        n /= BITS_PER_LONG;
                        while (n >= 8) {
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                *dst++ = ~*src++;
                                n -= 8;
                        }
                        while (n--)
                                *dst++ = ~*src++;

                        // Trailing bits
                        if (last)
                                *dst = comp(~*src, *dst, last);
                }
        } else {
                // Different alignment for source and dest

                right = shift & (BITS_PER_LONG-1);
                left = -shift & (BITS_PER_LONG-1);

                if (dst_idx+n <= BITS_PER_LONG) {
                        // Single destination word
                        if (last)
                                first &= last;
                        if (shift > 0) {
                                // Single source word
                                *dst = comp(~*src >> right, *dst, first);
                        } else if (src_idx+n <= BITS_PER_LONG) {
                                // Single source word
                                *dst = comp(~*src << left, *dst, first);
                        } else {
                                // 2 source words
                                d0 = ~*src++;
                                d1 = ~*src;
                                *dst = comp(d0 << left | d1 >> right, *dst,
                                            first);
                        }
                } else {
                        // Multiple destination words
                        d0 = ~*src++;
                        // Leading bits
                        if (shift > 0) {
                                // Single source word
                                *dst = comp(d0 >> right, *dst, first);
                                dst++;
                                n -= BITS_PER_LONG-dst_idx;
                        } else {
                                // 2 source words
                                d1 = ~*src++;
                                *dst = comp(d0 << left | d1 >> right, *dst,
                                            first);
                                d0 = d1;
                                dst++;
                                n -= BITS_PER_LONG-dst_idx;
                        }

                        // Main chunk
                        m = n % BITS_PER_LONG;
                        n /= BITS_PER_LONG;
                        while (n >= 4) {
                                d1 = ~*src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = ~*src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = ~*src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                d1 = ~*src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                                n -= 4;
                        }
                        while (n--) {
                                d1 = ~*src++;
                                *dst++ = d0 << left | d1 >> right;
                                d0 = d1;
                        }

                        // Trailing bits
                        if (last) {
                                if (m <= right) {
                                        // Single source word
                                        *dst = comp(d0 << left, *dst, last);
                                } else {
                                        // 2 source words
                                        d1 = ~*src;
                                        *dst = comp(d0 << left | d1 >> right,
                                                    *dst, last);
                                }
                        }
                }
        }
}
