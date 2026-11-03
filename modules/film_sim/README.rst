Film Simulation
===============

:Author: Magic Lantern contributors
:License: GPL
:Summary: Real-time film look and in-camera Picture Style controls

Overview
--------
The module rewrites LiveView frames through a display filter:
filmic tone curve, chroma matrix (hue/saturation) with highlight
desaturation, split toning and monochrome grain.

Presets include Neutral, Fuji Superia 400 (Classic Neg),
Fuji Pro 400H, Kodak Portra 400, Fuji Velvia 50, Cinema 800T,
B&W Tri-X 400, and Tungsten 64T.

Picture Style Controls
----------------------
Switch Canon Picture Styles (User 1, 2, 3 for custom styles)
and tune hardware JPEG Contrast, Saturation, and Color Tone.

Picture Styles: what renders
----------------------------
A User Def slot is a 16,704-byte blob: a 4-byte header, 68 bytes of settings
(base style, name, four sliders), then a 16,632-byte body.

The body carries the real rendering data, and the camera derives it: a .pf2 is
only 1-4 KB, expanded over USB into a body that is whitened and cannot be
authored outside the camera.

So sliders render, and a hand-converted body does not.  A body the camera made
itself clones byte-exact - the only route to a real film look here.

Registering and cloning
-----------------------
    # camera on USB, EOS Utility closed
    python3 tools/register_pf2.py --slot 2 --shortlist tools/pf2_shortlist.txt

That drives EDSDK exactly as EOS Utility does, then archives each expanded
16,704-byte body as ``STYLE2_<name>.BIN``.  Copy those to
``ML/FILMSIM/STYLES/`` on the card and use **Developer > JPEG: Load Style** to
put a body into any slot; the copy is byte-exact.

Raw Develop
-----------
**Film > Develop Shot** writes ``IMG_xxxx.FLM.JPG`` beside the raw and leaves
the CR2 alone.  **Develop Stock** picks any ``.fs`` file in ``ML/FILM``.

The route is the Soup Engine camera path: the full-size JPEG DIGIC already
embedded in the CR2 is decoded, graded and re-encoded.  Canon did the demosaic
and the colour; the stock owns tone, per-hue saturation, halation and grain.
That is how a film look reaches a JPEG.

The CR2 is never held in memory: a 256 KB header window is read and the
preview is streamed in 8 KB steps, so peak RAM is about 560 KB.  Level 3
decodes either payload -- a mosaic frame, or the mRAW this body actually
writes.  Stocks come from the card.

Film Shelf and Darkroom
-----------------------
Both screens are drawn by gui/fsg.c, so the desktop preview
(tools/gui_preview) renders the same pixels the camera draws.  The shelf
shows the selected stock's own canister photograph, read from
``ML/data/FILMCAN.DAT`` -- the artwork shipped with the FilmRoll module.
Only the user's own copy is read, never a copy carried here; without it the
shelf draws a cassette in the film's own colours.

That file is an indexed-image container ("FCAN", 15 records, image plus
thumbnail, 255 = transparent), and its palette is loaded into overlay
entries 80..254, the range ML leaves free.  The film strip uses the
thumbnails, taken from the heap when the shelf opens and freed when it
closes; without them, or for a film the file does not cover, the strip
shows names.

Tools
-----
- se/hosttest: runs the camera develop on a desktop and diffs the result
  against the engine reference render.
- tools/filmsim_lut.py: Export matching 3D .cube LUTs.
- tools/lut2pf3.py: Convert LUTs to Canon Picture Styles.
- tools/sim: Host simulation of the LiveView filter.
- tools/register_pf2.py: Register .pf2/.pf3 styles over USB via EDSDK and
  archive the expanded bodies (this is how a real film look gets into a
  picture style).
- tools/pf2_shortlist.txt: the 13 styles worth registering first.
- tools/sim/devsim.c: Host run of the sensor-data path still in fs_core.h;
  synthesises an RGGB frame and exercises demosaic, matrix, curve, LUT and
  TIFF writer.  ``make -C tools/sim devrun``.  Not reachable from the menu:
  on a body the develop goes through the embedded JPEG, see Raw Develop.

Notes
-----
Only one display filter runs at a time: disable RAW video preview,
defishing and focus peaking. Not available with external monitor.

Where the developed file shows up
---------------------------------

The JPEG is valid: correct Exif, 4:2:2, a 160x120 thumbnail, a clean scan. A
byte-identical copy put on the card from a computer plays in Canon's playback,
and so does the module's own file -- but only after the card is re-inserted or
the camera is power-cycled: the body builds its playback list when the card is
mounted and appends to it only for files it writes itself.

Use "View Last Develop" (a develop opens it automatically) to see the result at
once, and remount the card when you want it in Canon's own playback. Do not
touch the encoder or the Exif for this: Mac copies of the module's own output
were verified to play on the body.
