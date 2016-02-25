/****************************************************************
 *
 * The author of this software is David M. Gay.
 *
 * Copyright (c) 1991, 1996 by Lucent Technologies.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose without fee is hereby granted, provided that this entire notice
 * is included in all copies of any software which is or includes a copy
 * or modification of this software and in all copies of the supporting
 * documentation for such software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR IMPLIED
 * WARRANTY.  IN PARTICULAR, NEITHER THE AUTHOR NOR LUCENT MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE MERCHANTABILITY
 * OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR PURPOSE.
 *
 ***************************************************************/

/* g_fmt(buf,x) stores the closest decimal approximation to x in buf;
 * it suffices to declare buf
 *	char buf[32];
 */

#ifdef __cplusplus
extern "C" {
#endif
 extern char *dtoa(double, int, int, int *, int *, char **);
 extern int g_fmt(char *, double, int);
 extern void freedtoa(char*);
#ifdef __cplusplus
	}
#endif

int
fpconv_g_fmt(char *b, double x, int precision)
{
	register int i, k;
	register char *s;
	int decpt, j, sign;
	char *b0, *s0, *se;

	b0 = b;
#ifdef IGNORE_ZERO_SIGN
	if (!x) {
		*b++ = '0';
		*b = 0;
		goto done;
		}
#endif
	s = s0 = dtoa(x, 2, precision, &decpt, &sign, &se);
	if (sign)
		*b++ = '-';
	if (decpt == 9999) /* Infinity or Nan */ {
		while((*b++ = *s++));
		/* "b" is used to calculate the return length. Decrement to exclude the
		 * Null terminator from the length */
		b--;
		goto done0;
		}
	if (decpt <= -4 || decpt > precision) {
		*b++ = *s++;
		if (*s) {
			*b++ = '.';
			while((*b = *s++))
				b++;
			}
		*b++ = 'e';
		/* sprintf(b, "%+.2d", decpt - 1); */
		if (--decpt < 0) {
			*b++ = '-';
			decpt = -decpt;
			}
		else
			*b++ = '+';
		for(j = 2, k = 10; 10*k <= decpt; j++, k *= 10);
		for(;;) {
			i = decpt / k;
			*b++ = i + '0';
			if (--j <= 0)
				break;
			decpt -= i*k;
			decpt *= 10;
			}
		*b = 0;
		}
	else if (decpt <= 0) {
		*b++ = '0';
		*b++ = '.';
		for(; decpt < 0; decpt++)
			*b++ = '0';
		while((*b++ = *s++));
		b--;
		}
	else {
		while((*b = *s++)) {
			b++;
			if (--decpt == 0 && *s)
				*b++ = '.';
			}
		for(; decpt > 0; decpt--)
			*b++ = '0';
		*b = 0;
		}
 done0:
	freedtoa(s0);
#ifdef IGNORE_ZERO_SIGN
 done:
#endif
	return b - b0;
	}
