# Florin FilmRoll 6D (beta 11) Reverse-Engineering Findings

Disassembled from `tools/florin_filmroll_1.1/FilmRoll-6D-beta11/ML/modules/filmroll.mo` using `arm-none-eabi-objdump`.

---

## 1. Hook Sites & Calling Convention

Florin patches two entry points:
* `0xFF16E930` (`sssCompleteSsrawToYuv`)
* `0xFF1640E8` (on 6D 1.1.6 this is `cmp r0, #0` and fails patch, but `0xFF16E930` succeeds)

### Register Inspection in `yuv_hook` (`0x31FC`):
```arm
000031fc <yuv_hook>:
    31fc: push {r0, r1, r4, r5, r6, r7, r8, r9, sl, lr}
    3200: mov  r5, r2         ; r5 = pc (the hooked function address)
    3204: mov  r4, r0         ; r4 = regs pointer
    ...
    3228: ldr  r6, [r4, #4]   ; r6 = regs[1]!
    ...
    3264: cmp  r6, #0x40000000 ; Check if r6 is in DRAM
    3274: cmp  r6, #0x60000000
    3280: beq  3298           ; If in DRAM, jump to grade!
```

### Key Observation:
* Florin reads **`regs[1]`** (not `regs[0]`).
* For `0xFF16E930` (`sssCompleteSsrawToYuv`):
  * `r0` = job context (low RAM `0x000Fxxxx`).
  * `r1` = YUV frame buffer (`0x4xxxxxxx` in DRAM).
* Florin has **ZERO heuristic sniffers** (`fs_yuv_picture`, `fs_yuv_detect_stride`). If `regs[1]` is in `0x40000000..0x60000000`, he immediately grades it.

---

## 2. Geometry & Stride Detection

Florin does **not** detect stride by measuring pixel autocorrelation. He reads ML's `pic_quality`:
* Large: 5472 × 3648, stride = 10944 bytes (or uncropped 5760 × 3840 on some DIGIC 5 paths).
* Medium: 3648 × 2432, stride = 7296 bytes.
* Small: 2736 × 1824, stride = 5472 bytes.

In `yuv_hook` (`0x3310 - 0x3338`):
```arm
3314: moveq sl, #5760       ; width
3334: moveq r3, #3840       ; height
3338: movne r3, #3648       ; height (5472x3648)
```

---

## 3. Data Structure (`filmyuv_init` at `0xBAF4`)

Florin compiles a `.fs` / `.CUB` recipe into a compact 2304-byte (2.25 KB) table context:

| Offset | Size | Purpose | Formula / Content |
|---|---|---|---|
| `+0x000` | 256 B | Luma Tone Curve | `Y' = ctx[Y]` |
| `+0x100` | 1024 B (256×4) | Split-Tone & Luma Gain | `byte 0`: chroma gain (Q6)<br>`byte 1`: $U$ offset (signed int8)<br>`byte 2`: $V$ offset (signed int8)<br>`byte 3`: reserved |
| `+0x500` | 1024 B (32×32) | Chroma Gain per $(U,V)$ | `cg = ctx[0x500 + idx]`, where `idx = ((V^0x80)>>3)*32 + ((U^0x80)>>3)` |

---

## 4. Per-Pixel Processing (`work_000` at `0xA0F8`)

### Input Packing: UYVY (32 bits per 2 pixels)
```arm
a154: ldr  lr, [r5, #4]!    ; lr = [Y1 << 24] | [V << 16] | [Y0 << 8] | U
```

### Table Indexing:
```arm
a15c: eor  r1, lr, #0x00800000 ; V ^ 0x80
a160: eor  r1, r1, #0x00000080 ; U ^ 0x80
...
a178: ldrb r7, [r0, lr, lsr #24] ; Y1' = ctx[Y1]
a180: ldrb r8, [r0, r3]          ; Y0' = ctx[Y0]
a188: add  r3, r8, r7            ; Y_avg = (Y0' + Y1') / 2
a18c: ldrb r2, [r1, r2]          ; cg = ctx[0x500 + ((V^80)>>3)*32 + (U^80)>>3]
a198: ldr  ip, [r1, r3, lsl #2]  ; ip = ctx[0x100 + Y_avg * 4]
```

### Chroma Math:
```arm
a1a4: mul  r2, r1, r2       ; total_gain = cg * luma_gain (Q12)
...
a1c0: mul  r1, r2, r1       ; U * total_gain
a1c4: mul  lr, r2, lr       ; V * total_gain
a1d4: add  r2, r2, r1, asr #6 ; U' = U_offset + (U * total_gain) >> 6
a1ec: add  ip, ip, lr, asr #6 ; V' = V_offset + (V * total_gain) >> 6
```
Clamped to `-128..127`.

### Writeback:
```arm
a28c: orr  r2, r2, r8, lsl #8   ; [Y0' << 8] | U'
a294: orr  r2, r2, r7, lsl #24  ; [Y1' << 24] | ...
a29c: orr  r2, r2, ip, lsl #16  ; [V' << 16] | ...
a2a4: str  r2, [r5]             ; Store 32-bit word back into buffer
```

Total CPU time: **~200 ms** on DIGIC 5 ARM Cortex-A9 for 20 MP.

