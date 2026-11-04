# OLYMPUS NATURAL -- an authored look in the spirit of Olympus/OM's "Natural"
# picture mode, not a measurement of one.
#
# What that family of cameras is known for, and what this tries to put in the
# frame: clean, slightly cool whites; greens that stay green instead of being
# pushed toward acid; skin that is left alone; a gentle S rather than a film
# curve; almost no grain, because the look is a digital one.
#
# So the recipe is deliberately the opposite of Velvia or Classic Chrome:
# nothing per-hue is switched far from 100, there is no colour cast worth
# naming, and the tone curve is the whole of it. If the HUE_* lines look
# timid, that is the point -- a look with no opinion still has to be a look.
#
# Authored here; no Olympus camera was used to derive these numbers.

VERSION   3
NAME      OLYMPUS NATURAL
DESC      CLEAN COOL WHITES, HONEST GREENS, GENTLE S
WB        AS_SHOT

CURVE     0,4 32,30 96,96 160,163 224,227 255,246
CURVE_R   0,3 128,130 255,250
CURVE_G   0,4 128,129 255,248
CURVE_B   0,7 64,66 128,128 192,190 255,241

SAT       104
HUE_SAT   104,92,88,98,108,100
HUE_ROT   2,-4,-8,-6,-3,0
HUE_LUM   100,98,96,98,100,100

SPLIT_SH  -4,0,8
SPLIT_HI  4,2,-2

HALATION  24 238 10
HAL_TINT  255,120,80
GRAIN     5
GRAIN_SIZE 8
