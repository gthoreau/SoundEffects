#!/usr/bin/env python3
"""Hybrid 68k + 68881 disassembler.

Capstone's M68K mode does not decode 68881 coprocessor (F-line, 0xF2xx)
instructions, which is exactly where the floating-point math lives in the
FPU builds (`EFCF`) of the Norris modules. This walks instructions, decoding
0xF2xx by hand and delegating everything else to capstone.

68881 cpGEN extension word:
  bit15   R/M      0 = source is FPm, 1 = source is <ea>
  bit14   0
  b13-10  source specifier (R/M=1) or FPm register (R/M=0, bits 12-10)
  b9-7    destination FP register
  b6-0    opmode
"""
import struct
from capstone import Cs, CS_ARCH_M68K, CS_MODE_M68K_040

md = Cs(CS_ARCH_M68K, CS_MODE_M68K_040)

OPM = {0x00:'fmove',0x01:'fint',0x02:'fsinh',0x03:'fintrz',0x04:'fsqrt',0x06:'flognp1',
       0x08:'fetoxm1',0x09:'ftanh',0x0a:'fatan',0x0c:'fasin',0x0d:'fatanh',0x0e:'fsin',
       0x0f:'ftan',0x10:'fetox',0x11:'ftwotox',0x12:'ftentox',0x14:'flogn',0x15:'flog10',
       0x16:'flog2',0x18:'fabs',0x19:'fcosh',0x1a:'fneg',0x1c:'facos',0x1d:'fcos',
       0x1e:'fgetexp',0x1f:'fgetman',0x20:'fdiv',0x21:'fmod',0x22:'fadd',0x23:'fmul',
       0x24:'fsgldiv',0x25:'frem',0x26:'fscale',0x27:'fsglmul',0x28:'fsub',
       0x38:'fcmp',0x3a:'ftst'}
# source specifier -> (name, immediate byte length)
SRC = {0:('l',4),1:('s',4),2:('x',12),3:('p',12),4:('w',2),5:('d',8),6:('b',2)}

def _ea_len(mode, reg, imm_len):
    if mode in (0,1,2,3,4): return 0
    if mode in (5,6): return 2
    if mode == 7:
        return {0:2,1:4,2:2,3:2,4:imm_len}.get(reg,0)
    return 0

def _ea_str(mode, reg, data, off, imm_len):
    if mode==0: return f'd{reg}'
    if mode==1: return f'a{reg}'
    if mode==2: return f'(a{reg})'
    if mode==3: return f'(a{reg})+'
    if mode==4: return f'-(a{reg})'
    if mode==5:
        d=struct.unpack('>h',data[off:off+2])[0]; return f'${d:x}(a{reg})'
    if mode==6: return f'(d8,a{reg},Xn)'
    if mode==7:
        if reg==0: return f'${struct.unpack(">h",data[off:off+2])[0]:x}.w'
        if reg==1: return f'${struct.unpack(">I",data[off:off+4])[0]:x}.l'
        if reg==2:
            d=struct.unpack('>h',data[off:off+2])[0]; return f'${d:x}(pc)'
        if reg==3: return '(d8,pc,Xn)'
        if reg==4:
            raw=data[off:off+imm_len]
            return f'#<{raw.hex()}>'
    return '?'

def decode_fpu(data, i):
    """Return (text, length) for a 0xF2xx instruction at i, or None."""
    w0 = (data[i]<<8)|data[i+1]
    if (w0 & 0xFF00) != 0xF200: return None
    mode=(w0>>3)&7; reg=w0&7
    ext=(data[i+2]<<8)|data[i+3]
    top=(ext>>13)&7
    opm=ext&0x7f
    dst=(ext>>7)&7
    if top in (0,2) and opm in OPM:
        name=OPM[opm]
        if top==0:  # FPm -> FPn
            src=f'fp{(ext>>10)&7}'
            ln=4
        else:       # <ea> -> FPn
            ss=(ext>>10)&7
            sname,ilen=SRC.get(ss,('?',0))
            el=_ea_len(mode,reg,ilen)
            src=_ea_str(mode,reg,data,i+4,ilen)
            name=f'{name}.{sname}'
            ln=4+el
        if opm==0x3a:  # ftst has no dst
            return (f'{name} {src}', ln)
        return (f'{name} {src}, fp{dst}', ln)
    if top==3:  # FMOVE FPn -> <ea>
        ss=(ext>>10)&7
        sname,ilen=SRC.get(ss,('?',0))
        el=_ea_len(mode,reg,ilen)
        dstea=_ea_str(mode,reg,data,i+4,ilen)
        return (f'fmove.{sname} fp{(ext>>7)&7}, {dstea}', 4+el)
    return (f'fpu? ext=0x{ext:04x}', 4)

def disasm(data, start, end, base=0):
    """Yield (addr, text, raw) walking instructions from start to end."""
    i=start
    while i < end and i < len(data)-1:
        f = decode_fpu(data, i)
        if f:
            text,ln = f
            yield (i, text, data[i:i+ln]); i += ln; continue
        got=False
        for insn in md.disasm(data[i:i+16], i):
            yield (i, f'{insn.mnemonic} {insn.op_str}', data[i:i+insn.size])
            i += insn.size; got=True; break
        if not got:
            yield (i, f'dc.w ${(data[i]<<8)|data[i+1]:04x}', data[i:i+2]); i += 2
