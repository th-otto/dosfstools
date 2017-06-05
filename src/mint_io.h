/*
 * mint_io.h
 *
 * Copyright (C) 2000 Frank Naumann <fnaumann@freemint.de>.
 *
 * %Begin-Header%
 * This file may be redistributed under the terms of the GNU Public
 * License.
 * %End-Header%
 */

# ifdef __MINT__

# ifndef _mint_io_h
# define _mint_io_h

# define BLKGETSIZE		(('b'<< 8) | 1)
# define BLOCKSIZE		(('b'<< 8) | 2)

int gettype(int fd);

# endif /* _mint_io_h */

# endif /* __MINT__ */
