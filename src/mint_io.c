/*
 * mint_io.c
 *
 * Copyright (C) 2000 Frank Naumann <fnaumann@freemint.de>.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

# ifdef __MINT__

# include <assert.h>
# include <ctype.h>
# include <errno.h>
# include <fcntl.h>
# include <sys/ioctl.h>
# include <limits.h>
# include <stdarg.h>
# include <stdlib.h>
# include <stdio.h>
# include <string.h>
# include <time.h>
# include <unistd.h>

# include <sys/stat.h>

# include <mintbind.h>
# include "mint_io.h"


# if 0
# define DEBUG(x)	printf x
# else
# define DEBUG(x)
# endif


int __open_v(const char *_filename, int iomode, va_list argp);

int __close(int fd);

int __read(int fd, void *buf, size_t size);

int __write(int fd, const void *buf, size_t size);

int __ioctl(int fd, int cmd, void *arg);

int __fstat(int, struct stat *);

int __fsync(int fd);

off_t __lseek(int fd, off_t offset, int whence);


/* XHDI prototypes */

typedef struct __bpb __BPB;

static long init_XHDI(void);

static long XHGetVersion(void);

static long XHInqTarget(ushort major, ushort minor, ulong * block_size, ulong * device_flags, char *product_name);

static long XHReadWrite(ushort major, ushort minor, ushort rwflag, ulong recno, ushort count, void *buf);

static long XHInqDev2(ushort bios, ushort * major, ushort * minor, ulong * start, __BPB * bpb, ulong * blocks, char *partid);


struct device
{
	int used;

	int drv;
	int open_flags;

	ushort xhdi_maj;
	ushort xhdi_min;
	ulong xhdi_start;
	ulong xhdi_blocks;
	ulong xhdi_blocksize;
	char xhdi_id[4];

	loff_t pos;
};

# define DEVS 16
static struct device devs[DEVS];

static void init_device(struct device *dev)
{
	dev->used = 0;

	dev->drv = -1;
	dev->open_flags = 0;
	dev->xhdi_maj = 0;
	dev->xhdi_min = 0;
	dev->xhdi_start = 0;
	dev->xhdi_blocks = 0;
	dev->xhdi_blocksize = 0;

	dev->pos = 0;
}

static inline void init(void)
{
	static int done = 0;
	int i;

	if (done)
		return;

	for (i = 0; i < DEVS; i++)
		init_device(&devs[i]);

	init_XHDI();

	/* we are now initialized */
	done = 1;
}

static struct device *get_device(int fd)
{
	struct device *dev;

	if ((fd < 1024) || (fd >= (1024 + DEVS)))
		return NULL;

	fd -= 1024;
	dev = &devs[fd];

	assert(dev->used);

	return dev;
}

static int alloc_device(void)
{
	int i;

	for (i = 0; i < DEVS; i++)
	{
		struct device *dev = &devs[i];

		if (!dev->used)
		{
			dev->used = 1;
			return (i + 1024);
		}
	}

	__set_errno(ENOMEM);
	return -1;
}

static void free_device(struct device *dev)
{
	assert(dev->used);

	init_device(dev);
}

int open(const char *filename, int iomode, ...)
{
	const char *f = filename;
	struct device *mydev = NULL;
	int dev = -1;
	long ret;

	init();

	if (!filename)
	{
		__set_errno(EINVAL);
		return -1;
	}

	if ((f[1] == ':') && (f[2] == '\0'))
	{
		int c = tolower(f[0]);

		if (c >= 'a' && c <= 'z')
			c = c - 'a';
		else if (c >= '1' && c <= '6')
			c = 26 + (c - '1');

		if ((c >= 0) && (c < 32))
		{
			dev = alloc_device();
			if (dev != -1)
			{
				mydev = get_device(dev);
				assert(mydev);

				mydev->drv = c;
				mydev->open_flags = iomode;
			}
		}
	}

	if (dev == -1)
	{
		/* fall through */
		va_list args;
		int retval;

		va_start(args, iomode);
		retval = __open_v(filename, iomode, args);
		va_end(args);

		return retval;
	}

	if (mydev->open_flags == O_RDONLY)
	{
		DEBUG(("readonly mode!\n"));
		sync();
	} else if (Dlock(1, mydev->drv))
	{
		printf("Can't lock partition %c:!\n", mydev->drv + 'A');

		if (mydev)
			free_device(mydev);

		__set_errno(EACCES);
		return -1;
	}

	__set_errno(EERROR);

	ret = XHGetVersion();
	DEBUG(("XHDI version: %lx\n", ret));

	ret =
		XHInqDev2(mydev->drv, &mydev->xhdi_maj, &mydev->xhdi_min, &mydev->xhdi_start, NULL, &mydev->xhdi_blocks,
				  mydev->xhdi_id);
	if (ret)
	{
		printf("XHInqDev2 [%c] fail (ret = %li, errno = %i)\n", mydev->drv + 'A', ret, errno);
		ret = -1;
	} else
	{
		ret = XHInqTarget(mydev->xhdi_maj, mydev->xhdi_min, &mydev->xhdi_blocksize, NULL, NULL);
		if (ret)
		{
			printf("XHInqTarget [%i:%i] fail (ret = %li, errno = %i)\n", mydev->xhdi_maj, mydev->xhdi_min, ret, errno);
			ret = -1;
		} else
		{
			char *xhdi_id = mydev->xhdi_id;

			if (((xhdi_id[0] == 'G') && (xhdi_id[1] == 'E') && (xhdi_id[2] == 'M'))	|| /* GEM */
				((xhdi_id[0] == 'B') && (xhdi_id[1] == 'G') && (xhdi_id[2] == 'M'))	|| /* BGM */
				((xhdi_id[0] == 'F') && (xhdi_id[1] == '3') && (xhdi_id[2] == '2'))	|| /* F32 */
				((xhdi_id[0] == 'R') && (xhdi_id[1] == 'A') && (xhdi_id[2] == 'W'))	|| /* RAW */
				((xhdi_id[0] == 'L') && (xhdi_id[1] == 'N') && (xhdi_id[2] == 'X'))	|| /* LNX */
				((xhdi_id[0] == 'M') && (xhdi_id[1] == 'I') && (xhdi_id[2] == 'X'))	|| /* MIX */
				((xhdi_id[0] == '\0') && (xhdi_id[1] == 'D')))	/* any DOS */
			{
				DEBUG(("Partition ok and accepted!\n"));
				DEBUG(("start = %lu, blocks = %lu, blocksize = %lu\n", mydev->xhdi_start, mydev->xhdi_blocks,
					   mydev->xhdi_blocksize));
			} else
			{
				xhdi_id[3] = '\0';
				printf("Wrong partition ID [%s]!\n", xhdi_id);
				printf("Only 'RAW', 'LNX' and DOS partitions are supported.\n");

				__set_errno(EPERM);
				ret = -1;
			}
		}
	}

	if (ret)
	{
		if (mydev)
			free_device(mydev);

		dev = -1;
	}

	return dev;
}


int close(int fd)
{
	struct device *mydev = get_device(fd);
	int ret = 0;

	if (!mydev)
		/* fall through */
		return __close(fd);

	if (mydev->open_flags == O_RDONLY)
	{
		;
	} else if (Dlock(0, mydev->drv))
	{
		printf("Can't unlock partition %c:!\n", 'A' + mydev->drv);

		__set_errno(EACCES);
		ret = -1;
	}

	free_device(mydev);
	return ret;
}

static long rwabs_xhdi(struct device *mydev, ushort rw, void *buf, ulong size, ulong recno)
{
	/* simple buffer */
	static char buffer[1024L * 128];
	static ulong buf_recno = 0;
	static long buf_n = 0;
	ulong n = size / mydev->xhdi_blocksize;
	long r;

	assert((size % mydev->xhdi_blocksize) == 0);

	if (!n || (recno + n) > mydev->xhdi_blocks)
	{
		printf("rwabs_xhdi: access outside partition (drv = %c:)\n", 'A' + mydev->drv);
		exit(2);
	}

	if (n > 65535UL)
	{
		printf("rwabs_xhdi: n to large (drv = %c)\n", 'A' + mydev->drv);
		exit(2);
	}

	if (!rw && (buf_recno == recno) && (buf_n == n))
	{
		bcopy(buffer, buf, buf_n * mydev->xhdi_blocksize);
		return 0;
	}

	r = XHReadWrite(mydev->xhdi_maj, mydev->xhdi_min, rw, mydev->xhdi_start + recno, n, buf);

	if (!r && (n * mydev->xhdi_blocksize) <= sizeof(buffer))
	{
		bcopy(buf, buffer, n * mydev->xhdi_blocksize);

		buf_recno = recno;
		buf_n = n;
	} else
		buf_n = 0;

	return r;
}

# define max(a,b)	((a) > (b) ? (a) : (b))
# define min(a,b)	((a) > (b) ? (b) : (a))


int read(int fd, void *buf, size_t size)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __read(fd, buf, size);

	{
		long todo;						/* characters remaining */
		long done;						/* characters processed */
		todo = size;
		done = 0;

		if (todo == 0)
			return 0;

		/* partial block copy
		 */
		if (mydev->pos % mydev->xhdi_blocksize)
		{
			char tmp[mydev->xhdi_blocksize];
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			ulong offset = mydev->pos % mydev->xhdi_blocksize;
			ulong data;
			long ret;

			ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("read: partial part: read failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			data = mydev->xhdi_blocksize - offset;
			data = min(todo, data);

			memcpy(buf, tmp + offset, data);

			buf += data;
			todo -= data;
			done += data;
			mydev->pos += data;
		}

		if (todo)
		{
			assert((todo > 0));
			assert((mydev->pos % mydev->xhdi_blocksize) == 0);
		}


		/* full blocks
		 */
		if (todo / mydev->xhdi_blocksize)
		{
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			ulong data = (todo / mydev->xhdi_blocksize) * mydev->xhdi_blocksize;
			long ret;

			ret = rwabs_xhdi(mydev, 0, buf, data, recno);
			if (ret)
			{
				DEBUG(("read: full blocks: read failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			buf += data;
			todo -= data;
			done += data;
			mydev->pos += data;
		}

		if (todo)
		{
			assert((todo > 0) && (todo < mydev->xhdi_blocksize));
			assert((mydev->pos % mydev->xhdi_blocksize) == 0);
		}

		/* anything left?
		 */
		if (todo)
		{
			char tmp[mydev->xhdi_blocksize];
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			long ret;

			ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("read: left part: read failure (r = %li, errno = %i)]\n", ret, errno));
				goto out;
			}

			memcpy(buf, tmp, todo);

			done += todo;
			mydev->pos += todo;
		}

		assert(done == size);

	  out:
		return done;
	}
}

int write(int fd, const void *buf, size_t size)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __write(fd, buf, size);

	if (mydev->open_flags == O_RDONLY)
	{
		__set_errno(EPERM);
		return -1;
	}
	{
		long todo;						/* characters remaining */
		long done;						/* characters processed */

		todo = size;
		done = 0;

		if (todo == 0)
			return 0;

		/* partial block copy
		 */
		if (mydev->pos % mydev->xhdi_blocksize)
		{
			char tmp[mydev->xhdi_blocksize];
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			ulong offset = mydev->pos % mydev->xhdi_blocksize;
			ulong data;
			long ret;

			ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("write: partial part: read failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			data = mydev->xhdi_blocksize - offset;
			data = min(todo, data);

			memcpy(tmp + offset, buf, data);

			ret = rwabs_xhdi(mydev, 1, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("write: partial part: write failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			buf += data;
			todo -= data;
			done += data;
			mydev->pos += data;
		}

		if (todo)
		{
			assert((todo > 0));
			assert((mydev->pos % mydev->xhdi_blocksize) == 0);
		}

		/* full blocks
		 */
		if (todo / mydev->xhdi_blocksize)
		{
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			ulong data = (todo / mydev->xhdi_blocksize) * mydev->xhdi_blocksize;
			long ret;

			ret = rwabs_xhdi(mydev, 1, (void *)buf, data, recno);
			if (ret)
			{
				DEBUG(("write: full blocks: write failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			buf += data;
			todo -= data;
			done += data;
			mydev->pos += data;
		}

		if (todo)
		{
			assert((todo > 0) && (todo < mydev->xhdi_blocksize));
			assert((mydev->pos % mydev->xhdi_blocksize) == 0);
		}

		/* anything left?
		 */
		if (todo)
		{
			char tmp[mydev->xhdi_blocksize];
			ulong recno = mydev->pos / mydev->xhdi_blocksize;
			long ret;

			ret = rwabs_xhdi(mydev, 0, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("write: left part: read failure (r = %li, errno = %i)]\n", ret, errno));
				goto out;
			}

			memcpy(tmp, buf, todo);

			ret = rwabs_xhdi(mydev, 1, tmp, mydev->xhdi_blocksize, recno);
			if (ret)
			{
				DEBUG(("write: partial part: write failure (r = %li, errno = %i)\n", ret, errno));
				goto out;
			}

			done += todo;
			mydev->pos += todo;
		}

		assert(done == size);

	  out:
		return done;
	}
}


int ioctl(int fd, int cmd, void *arg)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __ioctl(fd, cmd, arg);

	DEBUG(("ioctl: cmd = %i\n", cmd));

	switch (cmd)
	{
	case BLKGETSIZE:
		{
			ulong *size = arg;

			*size = mydev->xhdi_blocks * (mydev->xhdi_blocksize / 512);
			break;
		}
	case BLOCKSIZE:
		{
			ulong *block_size = arg;

			*block_size = mydev->xhdi_blocksize;
			break;
		}
	default:
		__set_errno(EINVAL);
		return -1;
	}

	return 0;
}

int fstat(int fd, struct stat *st)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __fstat(fd, st);

	bzero(st, sizeof(*st));

	st->st_dev = mydev->xhdi_maj;
	st->st_ino = mydev->drv;
	st->st_mode = S_IFBLK | S_IRUSR | S_IWUSR;
	st->st_nlink = 1;
	st->st_uid = 0;
	st->st_gid = 0;
	st->st_rdev = mydev->xhdi_min;
	st->st_atime = time(NULL);
	st->st_mtime = time(NULL);
	st->st_ctime = time(NULL);
	st->st_size = (int64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
	st->st_blocks = (int64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize / 512;
	st->st_blksize = mydev->xhdi_blocksize;
	st->st_flags = 0;
	st->st_gen = 0;

	return 0;
}

int fsync(int fd)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __fsync(fd);

	/* nothing todo */
	return 0;
}

__off64_t lseek64(int fd, __off64_t offset, int origin)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __lseek(fd, (off_t) offset, origin);

	switch (origin)
	{
	case SEEK_SET:
		break;
	case SEEK_CUR:
		offset += mydev->pos;
		break;
	case SEEK_END:
		offset += (int64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
		break;
	default:
		return -1;
	}

	if (offset > (__off64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
	{
		__set_errno(EINVAL);
		return -1;
	}

	mydev->pos = offset;
	return mydev->pos;
}

loff_t llseek(int fd, loff_t offset, int origin);
loff_t llseek(int fd, loff_t offset, int origin)
{
	return lseek64(fd, offset, origin);
}

#undef lseek
off_t lseek(int fd, off_t offset, int mode)
{
	struct device *mydev = get_device(fd);

	if (!mydev)
		/* fall through */
		return __lseek(fd, offset, mode);

	{
		__off64_t _offset = offset;

		switch (mode)
		{
		case SEEK_SET:
			break;
		case SEEK_CUR:
			_offset += mydev->pos;
			break;
		case SEEK_END:
			_offset += (__off64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize;
			break;
		default:
			return -1;
		}

		if (_offset > LONG_MAX)
		{
			__set_errno(EINVAL);
			return -1;
		}

		if (_offset > (__off64_t) mydev->xhdi_blocks * mydev->xhdi_blocksize)
		{
			__set_errno(EINVAL);
			return -1;
		}

		mydev->pos = _offset;
		return (off_t) mydev->pos;
	}
}


int gettype(int fd)
{
	struct device *mydev = get_device(fd);
	char *xhdi_id;

	if (!mydev)
		return -1;

	/* Get filesystem type by XHDI ID */
	xhdi_id = mydev->xhdi_id;
	if ((xhdi_id[0] == '\0') && (xhdi_id[1] == 'D'))
		return 0;						/* DOS (\0D*) */
	else
		return 1;						/* Atari (GEM/GBM) */
}


/* XHDI bindings */


# include <stdio.h>
# include <stdlib.h>
# include <errno.h>
# include <mintbind.h>
# include <mint/cookie.h>


/*
 * internal usage
 */

/* dummy routine */
static long XHDIfail(void)
{
	return -ENOSYS;
}

/* XHDI handler function */
static long (*XHDI) () = XHDIfail;

static ushort XHDI_installed = 0;


# define C_XHDI		0x58484449L
# define XHDIMAGIC	0x27011992L

/* initalize flag */
static ushort XHDI_init = 1;

static long init_XHDI(void)
{
	long *val;
	long r;
	XHDI_init = 0;

	r = Getcookie(C_XHDI, (long *) &val);
	if (r == C_FOUND)
	{
		long *magic_test = val;

		/* check magic */
		if (magic_test)
		{
			magic_test--;
			if (*magic_test == XHDIMAGIC)
			{
				XHDI = (long (*)()) val;
			}
		}
	}

	r = XHGetVersion();
	if (r < 0)
	{
		perror("XHGetVersion");

		XHDI = XHDIfail;
		return r;
	}

	/* we need at least XHDI 1.10 */
	if (r >= 0x110)
	{
		XHDI_installed = r;
		return 0;
	}

	XHDI = XHDIfail;
	return -1;
}


/*
 * 32bit clean XHDI wrapper routines
 */

# define CALL \
	long oldstack = 0;		\
	long r;				\
					\
	if (XHDI_init) init_XHDI();	\
					\
	if (!Super(1L))			\
		oldstack = Super(0L);	\
					\
	r = XHDI(args);			\
	if (r < 0)			\
	{				\
		__set_errno(-r);	\
		r = -1;			\
	}				\
					\
	if (oldstack)			\
		Super(oldstack);	\
					\
	return r

static long XHGetVersion(void)
{
	struct args_XHGetVersion
	{
		ushort opcode;
	} args = { 0 };

	CALL;
}

static long XHInqTarget(ushort major, ushort minor, ulong * block_size, ulong * device_flags, char *product_name)
{
	struct args_XHInqTarget
	{
		ushort opcode;
		ushort major;
		ushort minor;
		ulong *block_size;
		ulong *device_flags;
		char *product_name;
	} args = { 1, major, minor, block_size, device_flags, product_name };

	CALL;
}

#if 0 /* unused */
long XHInqDriver(ushort bios, char *name, char *version, char *company, ushort * ahdi_version, ushort * maxIPL)
{
	struct args_XHInqDriver
	{
		ushort opcode;
		ushort bios;
		char *name;
		char *version;
		char *company;
		ushort *ahdi_version;
		ushort *maxIPL;
	} args = { 8, bios, name, version, company, ahdi_version, maxIPL };

	CALL;
}
#endif

static long XHReadWrite(ushort major, ushort minor, ushort rwflag, ulong recno, ushort count, void *buf)
{
	struct args_XHReadWrite
	{
		ushort opcode;
		ushort major;
		ushort minor;
		ushort rwflag;
		ulong recno;
		ushort count;
		void *buf;
	} args = { 10, major, minor, rwflag, recno, count, buf };

	CALL;
}


#if 0 /* unused */
long
XHInqTarget2(ushort major, ushort minor, ulong * block_size, ulong * device_flags, char *product_name, ushort stringlen)
{
	struct args_XHInqTarget2
	{
		ushort opcode;
		ushort major;
		ushort minor;
		ulong *block_size;
		ulong *device_flags;
		char *product_name;
		ushort stringlen;
	} args = { 11, major, minor, block_size, device_flags, product_name, stringlen };

	CALL;
}
#endif

static long
XHInqDev2(ushort bios, ushort * major, ushort * minor, ulong * start, __BPB * bpb, ulong * blocks, char *partid)
{
	struct args_XHInqDev2
	{
		ushort opcode;
		ushort bios;
		ushort *major;
		ushort *minor;
		ulong *start;
		__BPB *bpb;
		ulong *blocks;
		char *partid;
	} args = { 12, bios, major, minor, start, bpb, blocks, partid };

	CALL;
}


# endif	/* __MINT__ */
