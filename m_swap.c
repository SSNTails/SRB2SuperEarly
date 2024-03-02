// m_swap.c :  endianese handling, swapping 16bit and 32bit.


// Not needed with big endian.
#ifndef __BIG_ENDIAN__

// Swap 16bit, that is, MSB and LSB byte.
unsigned short SwapSHORT(unsigned short x)
{
    // No masking with 0xFF should be necessary.
    return (x>>8) | (x<<8);
}

// Swapping 32bit.
unsigned long SwapLONG( unsigned long x)
{
    return
        (x>>24)
        | ((x>>8) & 0xff00)
        | ((x<<8) & 0xff0000)
        | (x<<24);
}


#endif
