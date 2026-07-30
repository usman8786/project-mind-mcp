#ifndef PMM_LZ4_STORE_H
#define PMM_LZ4_STORE_H

// LZ4 HC compression (level 9).
int pmm_lz4_compress_hc(const char *src, int srcLen, char *dst, int dstCap);

// LZ4 decompression.
int pmm_lz4_decompress(const char *src, int srcLen, char *dst, int originalLen);

// Maximum compressed size bound.
int pmm_lz4_bound(int inputSize);

#endif // PMM_LZ4_STORE_H
