/*
 * A-Law and U-Law companding functions
 * 
 * This source code is based on:
 * http://dystopiancode.blogspot.com/2012/02/pcm-law-and-u-law-companding-algorithms.html
 * http://web.mit.edu/audio/src/build/i386_linux2/sox-11gamma-cb/g711.c
 *
 * sikor
 */

#define	SIGN_BIT	(0x80)		/* Sign bit for a A-law byte. */
#define	QUANT_MASK	(0xf)		/* Quantization field mask. */
#define	NSEGS		(8)		/* Number of A-law segments. */
#define	SEG_SHIFT	(4)		/* Left shift for segment number. */
#define	SEG_MASK	(0x70)		/* Segment field mask. */
#define	BIAS		(0x84)		/* Bias for linear code. */

/*
 * alaw2linear() - Convert an A-law value to 16-bit linear PCM
 *
 * Parameters:
 *  number - the number who will be decoded
 * Returns:
 *  The decoded number
 */
int16_t alaw2linear(uint8_t a_val)
{
	int16_t		t;
	int16_t		seg;

	a_val ^= 0x55;

	t = (a_val & QUANT_MASK) << 4;
	seg = ((unsigned)a_val & SEG_MASK) >> SEG_SHIFT;
	switch (seg) {
	case 0:
		t += 8;
		break;
	case 1:
		t += 0x108;
		break;
	default:
		t += 0x108;
		t <<= seg - 1;
	}
	return ((a_val & SIGN_BIT) ? t : -t);
}



/*
 * ulaw2linear() - Convert a u-law value to 16-bit linear PCM
 *
 * First, a biased linear code is derived from the code word. An unbiased
 * output can then be obtained by subtracting 33 from the biased code.
 *
 * Note that this function expects to be passed the complement of the
 * original code word. This is in keeping with ISDN conventions.
 */
int16_t ulaw2linear(uint8_t	u_val)
{
	int16_t		t;

	/* Complement to obtain normal u-law value. */
	u_val = ~u_val;

	/*
	 * Extract and bias the quantization bits. Then
	 * shift up by the segment number and subtract out the bias.
	 */
	t = ((u_val & QUANT_MASK) << 3) + BIAS;
	t <<= ((unsigned)u_val & SEG_MASK) >> SEG_SHIFT;

	return ((u_val & SIGN_BIT) ? (BIAS - t) : (t - BIAS));
}

/*
 * linear2alaw() - Convert a 16-bit linear PCM value to 8-bit A-law
 *
 * linear2alaw() accepts an 16-bit integer and encodes it as A-law data.
 *
 *		Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	0000000wxyza			000wxyz
 *	0000001wxyza			001wxyz
 *	000001wxyzab			010wxyz
 *	00001wxyzabc			011wxyz
 *	0001wxyzabcd			100wxyz
 *	001wxyzabcde			101wxyz
 *	01wxyzabcdef			110wxyz
 *	1wxyzabcdefg			111wxyz
 */
int8_t linear2alaw(int16_t number)
{
   const uint16_t ALAW_MAX = 0xFFF;
   uint16_t mask = 0x800;
   uint8_t sign = 0;
   uint8_t position = 11;
   uint8_t lsb = 0;
   if (number < 0)
   {
      number = -number;
      sign = 0x80;
   }
   if (number > ALAW_MAX)
   {
      number = ALAW_MAX;
   }
   for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--);
   lsb = (number >> ((position == 4) ? (1) : (position - 4))) & 0x0f;
   return (sign | ((position - 4) << 4) | lsb) ^ 0x55;
}

/*
 * linear2ulaw() - Convert a linear PCM value to u-law
 *
 * linear2ulaw() accepts an 16-bit integer and encodes it as u-law data.
 *
 *	Biased Linear Input Code	Compressed Code
 *	------------------------	---------------
 *	00000001wxyza			000wxyz
 *	0000001wxyzab			001wxyz
 *	000001wxyzabc			010wxyz
 *	00001wxyzabcd			011wxyz
 *	0001wxyzabcde			100wxyz
 *	001wxyzabcdef			101wxyz
 *	01wxyzabcdefg			110wxyz
 *	1wxyzabcdefgh			111wxyz
 */
int8_t linear2ulaw(int16_t number)
{
   const uint16_t MULAW_MAX = 0x1FFF;
   const uint16_t MULAW_BIAS = 33;
   uint16_t mask = 0x1000;
   uint8_t sign = 0;
   uint8_t position = 12;
   uint8_t lsb = 0;
   if (number < 0)
   {
      number = -number;
      sign = 0x80;
   }
   number += MULAW_BIAS;
   if (number > MULAW_MAX)
   {
      number = MULAW_MAX;
   }
   for (; ((number & mask) != mask && position >= 5); mask >>= 1, position--)
        ;
   lsb = (number >> (position - 4)) & 0x0f;
   return (~(sign | ((position - 5) << 4) | lsb));
}