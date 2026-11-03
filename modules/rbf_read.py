#!/usr/bin/env python3
import struct, sys

# from http://stackoverflow.com/questions/35988/c-like-structures-in-python
class Bunch:
    def __init__(self, **kwds):
        self.__dict__.update(kwds)

# RBF loading code from rbfEditor, rebUtils.py
# http://freecode.com/projects/rbfeditor
_FNT_HDR_MAGIC  = b'\xE0\x0E\xF0\x0D\x03\x00\x00\x00'
_FNT_HDR_SIZE   = 0x74
_FNT_MAX_NAME   = 64

def rbf_load(file):
    '''
        Load rebFont instance from file.
        <file> - python file object
    '''
    self = Bunch()

    file.seek(0)
    magic = file.read(len(_FNT_HDR_MAGIC))
    if magic == _FNT_HDR_MAGIC :
        fmt = "=" + str(_FNT_MAX_NAME) + 's'
        self.name       = struct.unpack(fmt,file.read(_FNT_MAX_NAME))[0]
        fmt = '=l'
        fmt_size = 4
        self._charSize  = struct.unpack(fmt,file.read(fmt_size))[0]
        self.points     = struct.unpack(fmt,file.read(fmt_size))[0]
        self.height     = struct.unpack(fmt,file.read(fmt_size))[0]
        self.maxWidth   = struct.unpack(fmt,file.read(fmt_size))[0]
        self.charFirst  = struct.unpack(fmt,file.read(fmt_size))[0]
        self._charLast  = struct.unpack(fmt,file.read(fmt_size))[0]
        self._unknown4  = struct.unpack(fmt,file.read(fmt_size))[0]
        self._wmapAddr  = struct.unpack(fmt,file.read(fmt_size))[0]
        self._cmapAddr  = struct.unpack(fmt,file.read(fmt_size))[0]
        self.descent    = struct.unpack(fmt,file.read(fmt_size))[0]
        self.intline    = struct.unpack(fmt,file.read(fmt_size))[0]
        self.wTable     = []
        self.cTable     = []

        self.width = int(8 * self._charSize // self.height)
        self.charCount = self._charLast - self.charFirst + 1

        charlist = list(range(0, self.charCount))
        file.seek(self._wmapAddr)
        for char in charlist:
            self.wTable.append(struct.unpack('=B', file.read(1))[0])
        file.seek(self._cmapAddr)
        for char in charlist:
            self.cTable.append(file.read(self._charSize))
    
    return self

font = None

def rbf_init_font(file):
    global font
    font = rbf_load(open(file, "rb"))

def extent_func(string):
    # Characters outside the font's table have no glyph metrics; they must
    # measure as zero rather than raise. argnor23.rbf only covers 32..159, and
    # html2text passes whole paragraphs -- including their newlines -- to the
    # wrapper, so a README with any wrapped line used to abort the module
    # build with "IndexError: list index out of range" from here.
    w = 0
    for c in string:
        idx = ord(c) - font.charFirst
        if 0 <= idx < len(font.wTable):
            w += font.wTable[idx]
    return (w, 0)
