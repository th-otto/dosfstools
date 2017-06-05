static inline __uint16_t
bswap16 (register __uint16_t x)
{
	register __uint16_t r;

	r  = (x << 8) & 0xff00;
	r |= (x >> 8) & 0x00ff;

	return r;
}

static inline __uint32_t
bswap32 (register __uint32_t x)
{
	register __uint32_t r;

	r  = (x << 24) & 0xff000000;
	r |= (x <<  8) & 0x00ff0000;
	r |= (x >>  8) & 0x0000ff00;
	r |= (x >> 24) & 0x000000ff;

	return r;
}

# if BYTE_ORDER == BIG_ENDIAN
#define htole16(x)	(bswap16 (x))
#define htole32(x)	(bswap32 (x))
#define le16toh(x)	(bswap16 (x))
#define le32toh(x)	(bswap32 (x))
#define htobe16(x)	(x)
#define htobe32(x)	(x)
#define be16toh(x)	(x)
#define be32toh(x)	(x)
# elif BYTE_ORDER == LITTLE_ENDIAN
#define htole16(x)	(x)
#define htole32(x)	(x)
#define le16toh(x)	(x)
#define le32toh(x)	(x)
#define htobe16(x)	(bswap16 (x))
#define htobe32(x)	(bswap32 (x))
#define be16toh(x)	(bswap16 (x))
#define be32toh(x)	(bswap32 (x))
# else
# error unknown BYTE_ORDER
# endif

