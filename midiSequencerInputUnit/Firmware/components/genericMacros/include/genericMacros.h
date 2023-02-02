

#define CLEAR_UPPER_NIBBLE(x)   (x & 0x0F)
#define CLEAR_LOWER_NIBBLE(x)   (x & 0xF0)
#define CLEAR_MSBIT_IN_BYTE(x)  (x & 0x7F)
#define GET_MSBIT_IN_BYTE(x)    (x & 0x80)
