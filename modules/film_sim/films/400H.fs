# Fujifilm Pro 400H: pastel, minty greens, light cyan, soft highlights.
#
# 400H's party trick is that it barely clips: the shoulder sits at 234 on a
# curve that is flat across the top, so a wedding dress keeps its folds.  The
# mint cast is not a global shift -- HUE_SAT 84 on the yellow-green sector with
# -6 degrees of rotation -- and the blue curve starts at 16, which is where the
# pale cyan in the shadows comes from.
#
VERSION   3
NAME      PRO 400H
DESC      PASTEL, MINTY GREENS, LIGHT CYAN, SOFT HIGHLIGHTS
WB        AS_SHOT
CURVE     0,22 64,84 128,140 192,190 255,234
CURVE_R   0,16 128,138 255,240
CURVE_G   0,12 128,133 255,245
CURVE_B   0,16 64,72 128,132 192,190 255,240
SAT       88
HUE_SAT   96,84,106,102,98,100
HUE_ROT   -4,-6,6,2,-2,0
HUE_LUM   102,104,94,98,96,103
SPLIT_SH  2,6,-2
SPLIT_HI  6,4,-2
HALATION  60 220 20
HAL_TINT  255,110,70
GRAIN     16
GRAIN_SIZE 30

