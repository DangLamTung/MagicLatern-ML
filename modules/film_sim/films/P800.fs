# Kodak Portra 800: pastel, warm, more contrast and grain than 400.
#
# Portra 800 keeps the pastel character but the speed costs grain and buys
# contrast: the shoulder ends at 236, the mid point rides two levels higher
# than portra.fs, and GRAIN 26 at size 34 is roughly double.  The halation is
# stronger too -- a fast emulsion blooms more around a specular highlight, and
# that bloom is a visible part of what 800 looks like.
#
VERSION   3
NAME      PORTRA 800
DESC      PASTEL, WARM, MORE CONTRAST AND GRAIN THAN 400
WB        AS_SHOT
CURVE     0,22 64,80 128,138 192,196 255,236
CURVE_R   0,16 128,142 255,244
CURVE_G   0,12 128,130 255,243
CURVE_B   0,12 64,64 128,124 192,184 255,236
SAT       90
HUE_SAT   98,104,86,98,100,102
HUE_ROT   5,3,-8,-2,0,-1
HUE_LUM   104,108,92,98,98,102
SPLIT_SH  10,5,-2
SPLIT_HI  12,7,-6
HALATION  52 224 17
HAL_TINT  255,98,48
GRAIN     26
GRAIN_SIZE 34

