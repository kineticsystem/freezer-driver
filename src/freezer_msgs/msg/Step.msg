# One step of a shot: output a pattern on the 16 optocouplers of the Freezer
# board, then hold it.

uint8 SET_AND_HOLD = 0  # The other values are reserved for repeat and wait steps.
uint8 type

# The 16 outputs, one bit per optocoupler. For jack k, bit 2(k-1) is the
# shutter, the tip of the plug, and bit 2(k-1)+1 is the focus, its ring.
uint16 OUT1_SHUTTER = 1
uint16 OUT1_FOCUS = 2
uint16 OUT2_SHUTTER = 4
uint16 OUT2_FOCUS = 8
uint16 OUT3_SHUTTER = 16
uint16 OUT3_FOCUS = 32
uint16 OUT4_SHUTTER = 64
uint16 OUT4_FOCUS = 128
uint16 OUT5_SHUTTER = 256
uint16 OUT5_FOCUS = 512
uint16 OUT6_SHUTTER = 1024
uint16 OUT6_FOCUS = 2048
uint16 OUT7_SHUTTER = 4096
uint16 OUT7_FOCUS = 8192
uint16 OUT8_SHUTTER = 16384
uint16 OUT8_FOCUS = 32768
uint16 outputs

# How long to hold the pattern, in microseconds. It is what the controller
# counts, so a client sees exactly what will run, with no rounding.
uint32 hold_us
