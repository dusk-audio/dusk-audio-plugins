#!/usr/bin/env python3
"""
WCS Microcode Engine — Python implementation for coefficient optimization.

Mirrors the C++ Suede200Reverb class exactly:
  - 128-step WCS microcode execution per sample
  - Single circular delay memory (64K at 20480Hz, scaled for host SR)
  - 8-register accumulator file
  - 16 coefficient codes mapped to float values
  - Input injection and output extraction at program-specific steps

This runs entirely in Python (no plugin subprocess needed), enabling
fast scipy.optimize searches over the 16 coefficient values.
"""

import os
import ctypes
import numpy as np
from dataclasses import dataclass

# Original hardware sample rate
ORIGINAL_SR = 20480

# Try to load native C engine for ~100x speedup
_native_lib = None
try:
    _LIB_DIR = os.path.dirname(os.path.abspath(__file__))
    for _ext in ('.dylib', '.so'):
        _path = os.path.join(_LIB_DIR, f'wcs_native{_ext}')
        if os.path.exists(_path):
            _native_lib = ctypes.CDLL(_path)
            _native_lib.wcs_generate_ir.argtypes = [
                ctypes.POINTER(ctypes.c_uint),    # microcode (128 uint32)
                ctypes.POINTER(ctypes.c_double),   # coefficients (16 double)
                ctypes.c_double,                   # sr
                ctypes.c_int,                      # n_samples
                ctypes.c_double,                   # rolloff_hz
                ctypes.POINTER(ctypes.c_double),   # output_l
                ctypes.POINTER(ctypes.c_double),   # output_r
            ]
            _native_lib.wcs_generate_ir.restype = None
            break
except Exception:
    _native_lib = None

# ROM-extracted microcode data for all 6 programs (128 × 32-bit words each)
# Identical to WCSData::microcode in Suede200Reverb.h
MICROCODE = [
    # Program 0: Concert Hall (Algorithm A)
    [
        0xF2FFDF5F, 0x50FA0738, 0xFBFADD3F, 0x55F5E7BB, 0xFBFA15BF, 0x55F5C13B, 0x50FAFEBF, 0x54F5FF3F,
        0x4FFA0F38, 0x40FBD83F, 0x55F5EFBB, 0x40FB10BF, 0xD9F80F38, 0x30FAD83F, 0x4FFAEFBB, 0x30FA10BF,
        0x57F90738, 0x2FFADD3F, 0xD9F8E7BB, 0x2FFA15BF, 0xADF90738, 0x2EFADD3F, 0x57F9E7BB, 0xFFFF15BF,
        0xFEFF4F39, 0xFDFF103C, 0xADF94FBF, 0xFFFFF0FF, 0xFFFFEFFF, 0xFFFF59FF, 0x9EFE0F38, 0xF2FED83F,
        0x2DFAEFBB, 0xF2FE10BF, 0x13FE0238, 0x9DFEDD3F, 0x9EFEE2BB, 0x9DFE15BF, 0xFBFEC53B, 0xB7FBFEBF,
        0x5CF8093C, 0xFFFFFDFF, 0xFFFF07FB, 0xFFFF3FFF, 0xA87AFB3F, 0xFCFEC9BF, 0xFFFFFDFF, 0xFFFFE7FF,
        0xFFFFF6FF, 0xFDFEFD3F, 0xD0F8C73B, 0xFEFEFFB7, 0xFFFFFFFF, 0xFFFFFFFF, 0xB6FBF83F, 0x4EFAFA3F,
        0xDEF6F43F, 0xA87AFF3F, 0x54F5FF3F, 0xFFFFFFFB, 0xF1FFDFDF, 0xFFFFFFFB, 0xFFFFFFFF, 0xFFFFFFFF,
        0xF2FFDF5F, 0xE0F60738, 0x92F7DD3F, 0xA97AE7B3, 0x92F715BF, 0xFFFFFFFF, 0xA97AC13B, 0xE0F6FEBF,
        0xA87AFF3F, 0xFFFFFFFF, 0xDFF60F38, 0xC1F7D83F, 0xA97AEFBB, 0xC1F710BF, 0x7EFF4F39, 0x7DFF103C,
        0xDFF64FBF, 0xFFFFF0FF, 0xFFFFEFFF, 0xFFFF59FF, 0x57F50F38, 0xBFF6D83F, 0xBCF6EFBB, 0xBFF610BF,
        0xDCF50738, 0xBEF6DD3F, 0x57F5E7BB, 0xBEF615BF, 0x28F60738, 0xBDF6DD3F, 0xDCF5E7BB, 0x80FF15BF,
        0xCAFD0F38, 0xFDFED83F, 0x28F6EFBB, 0x12FE10BF, 0x55FD0238, 0xC9FDDD3F, 0xCAFDE2BB, 0xC9FD15BF,
        0xF9FEC53B, 0x4FF8FEBF, 0x57F53E3C, 0xFFFF07FB, 0xFFFF3FFF, 0x54F5FB3F, 0xFAFEC9BF, 0xFFFFFDFF,
        0xFFFFE7FF, 0xFFFFFEFF, 0xF3FEFD3F, 0x57F5C73B, 0xF4FEFFBF, 0x43F8F83F, 0xC0F6FA3F, 0x31FAC13F,
        0xFFFFF5FF, 0x54F5FF3F, 0xA87AFF3F, 0xFFFFFFFB, 0xF1FFDFDF, 0xFFFFFFFB, 0xFFFFFFEF, 0xFFFFFFFF,
    ],
    # Program 1: Plate (Algorithm B)
    [
        0xF2FFDF5F, 0x75FFC73B, 0x01EAFEBF, 0x74FFFD3F, 0x51FF0738, 0x74FFDC3F, 0x75FFE7BB, 0x74FF14BF,
        0xE2FE0A38, 0x50FFDC3F, 0x51FFEABB, 0x50FF14BF, 0xBAFD0F38, 0xE1FED83F, 0xE2FEEFBB, 0xE1FE10BF,
        0x30FB0738, 0x7CFCDD3F, 0xBAFDE7BB, 0xFFFF15BF, 0x54F80738, 0xECF9DD3F, 0x30FBE7BB, 0xECF915BF,
        0x4DF40F38, 0x1EFAD83F, 0x54F8EFBB, 0x1EFA10BF, 0x06EA0738, 0x4DF43EBF, 0x77FF7D3D, 0xFFFF47FB,
        0x78FFFFBF, 0x7DFFF93F, 0xFFFF4BFB, 0xFFFFFEFF, 0xB9FDF03F, 0x7EFFCDBF, 0xFFFFFDFF, 0x52FACA3B,
        0x7DFCFFBF, 0xB9FDF83F, 0xFEFF4F39, 0xFDFF103C, 0x50FA4FBF, 0xFFFFF0FF, 0xFFFFEEFF, 0xFFFF5DFF,
        0x5DF7CA3B, 0x7CFCF7BF, 0xB9FDF83F, 0xB9FDCF3B, 0x30F7F2BF, 0x2EFBF33F, 0x84EEFB3F, 0x33F4FB3F,
        0x00EAFF3F, 0xFE74FF3F, 0x00EAFF3F, 0xFFFFFFFB, 0xF1FFDFDF, 0xFFFFFFFB, 0xFFFFFFFF, 0xFFFFFFF7,
        0xF2FFDF5F, 0xFFFFFFFF, 0x00EACF3B, 0xFF74FABF, 0xFE74CF3B, 0x76FFFABF, 0xFFFFE7FB, 0xFFFFFEFF,
        0xB7FDFD3F, 0x9FFD0738, 0xB7FDDC3F, 0xB8FDE7BB, 0xB7FD14BF, 0x51FD0938, 0x9EFDDC3F, 0x9FFDE9BB,
        0x9EFD14BF, 0x7FFC0C38, 0x50FDDC3F, 0x51FDECBB, 0x50FD14FF, 0xE5F10738, 0x55F3DD3F, 0x7EFCE7B3,
        0x55F315BF, 0xFFFFFFFF, 0xC5EE0738, 0x89F0DD3F, 0xE5F1E7BB, 0x89F015BF, 0x4DEB0F38, 0xBBF0D83F,
        0xC5EEEFBB, 0xBBF010BF, 0x57F30738, 0x4DEB3EBF, 0x7BFF3D3C, 0xFFFF07FB, 0x7CFFFFBF, 0x79FFF93F,
        0xFFFF0BFB, 0xFFFFFEFF, 0x7EFCF03F, 0x7AFFCDBF, 0xFFFFFDFF, 0xEFF0CA3B, 0x56F3FFBF, 0x7EFCF83F,
        0xA8EDCA3B, 0xEDF0FFBF, 0x7EFCF83F, 0x7FFCCF3B, 0xA6EDFABF, 0xCBF1F33F, 0x39F8FB3F, 0xE2EAFB3F,
        0xFE74FF3F, 0x00EAFF3F, 0xFE74FF3F, 0xFFFFFFFB, 0xF1FFDFDF, 0xFFFFFFFB, 0xFFFFFFEF, 0xFFFFFFFF,
    ],
    # Program 2: Chamber (Algorithm C)
    [
        0xD7E1CB3B, 0xD8E1FEBF, 0x47FF0738, 0xFFFFD4FF, 0xFFFFE7FB, 0x74FF1CBF, 0xFCFE0A38, 0x46FFD43F,
        0x47FFEABB, 0x46FF1CBF, 0xDAE10038, 0xFFFF3EFF, 0xFFFFB8FE, 0x45F50F38, 0xFFFFD8FF, 0xFFFFEFFB,
        0xC6F510BF, 0x74F20338, 0x45F53EBF, 0x7CFF7E3D, 0xFFFF4BFB, 0x7DFF7FBF, 0x7AFF383C, 0xFFFF4BFB,
        0xFFFF7EFF, 0x7BFFFFBF, 0x90F10F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x74F210BF, 0x22EF0038, 0x90F13EBF,
        0x05EE0F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x22EF10BF, 0x2CF5CF3B, 0x05EEFABF, 0xFEF3F33F, 0xDEF2FC3F,
        0x9AF1F33F, 0x0CF0FC3F, 0xE5EEFB3F, 0x56EDF33F, 0x07ECFC3F, 0xAEEAF33F, 0x4CE9FC3F, 0x31E8FB3F,
        0xD3E6F33F, 0x6CE5FC3F, 0x39E4F33F, 0xCFE2FC3F, 0xD7E1FF3F, 0xEA70F73F, 0xD7E1FF3F, 0xFFFFBFFF,
        0x75FFCB3B, 0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x76FFFFBF, 0xF1FFDFDF, 0xF2FFDF5B, 0xFFFFFFFF,
        0xEA70CB3B, 0xEB70FEBF, 0xD4FE0738, 0xFFFFD4FF, 0xFFFFE7FB, 0xFBFE1CBF, 0x95FE0A38, 0xD3FED43F,
        0xD4FEEABB, 0xD3FE1CBF, 0x4AEC0038, 0xFFFF3EFF, 0xFFFF83FE, 0xFFFFFCFF, 0xFFFFE3FF, 0xFFFFFEFF,
        0xFEFFFE3F, 0xD5EB0F38, 0xFFFFD8BF, 0xFFFFEFFB, 0x4AEC10BF, 0x7EE80038, 0xD5EB3EBF, 0x9CE70F38,
        0xFFFFD8FF, 0xFFFFEFFB, 0x7DE810BF, 0x2CE50038, 0x9CE73EBF, 0x80E30F38, 0xFFFFD8FF, 0xFFFFEFF3,
        0x2CE510BF, 0xFFFFFFFF, 0x76F4CF3B, 0x80E3F2BF, 0x6DF3FC3F, 0x33F2FB3F, 0x00F1F33F, 0x33EFFC3F,
        0x13EEF33F, 0xA3ECFC3F, 0x7DEBFB3F, 0x13EAF33F, 0x9AE8FC3F, 0x7FE7F33F, 0x18E6FC3F, 0xDFE4FB3F,
        0x80E3F337, 0x3CE2FC3F, 0xFFFFFFFF, 0xEA70FF3F, 0xD7E1F73F, 0xEA70FF3F, 0xFFFFBFFF, 0x7EFFCB3B,
        0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x7FFFFFBF, 0xFFFFFFFF, 0xF1FFDFDF, 0xF2FFDF4B, 0xFFFFFFFF,
    ],
    # Program 3: Rich Plate (Algorithm C variant)
    [
        0x0FE8CF3B, 0x10E8FABF, 0xE8FF0738, 0xF5FFD4BF, 0xFFFFE7FB, 0xEFFF1CBF, 0xC5FF0A38, 0xFFFFD4FF,
        0xE8FFEABB, 0xE7FF1CBF, 0x8DFF0A38, 0xFFFFD4FF, 0xC5FFEABB, 0xC4FF1CBF, 0x11E80038, 0xFFFF3EFF,
        0xFFFFB8FE, 0x7BF80F38, 0xFFFFD8FF, 0xFFFFEFFB, 0xABF810BF, 0x59F50338, 0x7BF83EBF, 0xFEFF7E3D,
        0xFFFF4BFB, 0xFFFFFFBF, 0xFCFF383C, 0xFFFF4BFB, 0xFFFF7EFF, 0xFDFFFFBF, 0xE1F40F38, 0xFFFFD8FF,
        0xFFFFEFFB, 0x59F510BF, 0x0DF8CF3B, 0xE1F4FABF, 0xFDF6F33F, 0xECF5FC3F, 0xE4F4F33F, 0xC1F3FC3F,
        0x8CF2FC3F, 0x91F1FB3F, 0x5DF0F43F, 0x3DEFFC3F, 0x10EEF33F, 0x1BEDFB3F, 0x07ECFC3F, 0xBEEAFB3F,
        0xBCE9F43F, 0xA7E8FC3F, 0x0FE8FF3B, 0x0774F73F, 0xFFFFBFFF, 0xFAFFCB3B, 0xFFFFFFFF, 0xFFFFF8FF,
        0xFFFFFFFB, 0xFBFFFFBF, 0xF1FFDFDF, 0xF2FFDF5B, 0xFFFFFFFF, 0x0774CF3B, 0x0874FABF, 0x82FF0738,
        0xFFFFD4FF, 0xFFFFE7FB, 0x8CFF1CBF, 0x64FF0738, 0xFFFFD4FF, 0x82FFE7BB, 0x81FF1CBF, 0x35FF0A38,
        0xFFFFD4FF, 0x64FFEABB, 0x63FF1CBF, 0x2FEF0038, 0xFFFF3EFF, 0xFFFFB8FE, 0xFFEE0F38, 0xFFFFD8FF,
        0xFFFFEFFB, 0x2FEF10BF, 0x63EB0038, 0xFFEE3EBF, 0xFFFF23FC, 0xFFFF3EFF, 0xF6FFFE3F, 0xD4E90F38,
        0xF7FFD8BF, 0xFFFFEFFB, 0x62EB10BF, 0x07F20038, 0xD4E93EBF, 0xF0F00F38, 0xFFFFD8FF, 0xFFFFEFF3,
        0x07F210BF, 0xFFFFFFFF, 0x7BF7CF3B, 0xF0F0F2BF, 0x8BF6FB3F, 0x6BF5FC3F, 0x53F4F33F, 0x22F3FC3F,
        0x11F2F43F, 0xE0F0FB3F, 0xC3EFFC3F, 0xCBEEFB3F, 0x82EDFB3F, 0x6DECF43F, 0x6CEBFC3F, 0x4EEAF33F,
        0x40E9FC37, 0x20E8FC3F, 0xFFFFFFFF, 0x0774F73B, 0x0FE8F03F, 0x0774FF3F, 0xFFFFBFFF, 0xF8FFCB3B,
        0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0xF9FFFFBF, 0xFFFFFFFF, 0xF1FFDFDF, 0xF2FFDF4B, 0xFFFFFFFF,
    ],
    # Program 4: Rich Splits (Algorithm C)
    [
        0xD7E1CB3B, 0xD8E1FEBF, 0x47FF0738, 0xFFFFD4FF, 0x94FEE7BB, 0x74FF1CBF, 0xFCFE0A38, 0x46FFD43F,
        0x47FFEABB, 0x46FF1CBF, 0x4AEC0038, 0xFFFF3EFF, 0xFFFFB8FE, 0xFFFFFFFF, 0xFFFFE3FF, 0xFFFFFEFF,
        0x7CFFFE3F, 0x45F50F38, 0x7DFFD8BF, 0xFFFFEFFB, 0xC6F510BF, 0x74F24B39, 0x45F57FBF, 0x7AFFF83F,
        0xFFFF4BFB, 0xFFFF7EFF, 0x7BFFFFBF, 0x90F10F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x74F210BF, 0x22EF0038,
        0x90F13EBF, 0x05EE0F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x22EF10BF, 0x2CF5CF3B, 0x05EEFABF, 0xFEF3F33F,
        0xDEF2FC3F, 0x9AF1F33F, 0x0CF0FC3F, 0xE5EEFB3F, 0x56EDF33F, 0x4BECFC3F, 0x76F4F33F, 0x6DF3FC3F,
        0x33F2FB3F, 0x00F1F33F, 0x33EFFC3F, 0x13EEF33F, 0xA3ECFC3F, 0xD7E1FF3F, 0xFFFFBFFF, 0x75FFCB3B,
        0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x76FFFFBF, 0xF1FFDFDF, 0xF2FFDF5B, 0xFFFFFFFF, 0xEA70CB3B,
        0xEB70FEBF, 0xD4FE0738, 0xFFFFD4FF, 0xFFFFE7FB, 0xFBFE1CBF, 0x95FE0A38, 0xD3FED43F, 0xD4FEEABB,
        0xD3FE1CBF, 0xDAE10038, 0xFFFF3EFF, 0xFFFFB8FE, 0xFFFFFFFF, 0xFFFFE3FF, 0xFFFFFEFF, 0xFEFFFE3F,
        0xD5EB0F38, 0xFFFFD8BF, 0xFFFFEFFB, 0x4AEC10BF, 0x7EE84B39, 0xD5EB7FBF, 0xFDFFF83F, 0xFFFF4BFB,
        0xFFFF7EFF, 0xFEFFFFBF, 0x9CE70F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x7DE810BF, 0x2CE50038, 0x9CE73EBF,
        0x80E30F38, 0xFFFFD8FF, 0xFFFFEFF3, 0x2CE510BF, 0xFFFFFFFF, 0xAEEACF3B, 0x80E3F2BF, 0x4CE9FC3F,
        0x31E8FB3F, 0xD3E6F33F, 0x6CE5FC3F, 0x39E4F33F, 0xCFE2FC3F, 0x7DEBFB3F, 0x13EAF33F, 0x9AE8FC3F,
        0x7FE7F33F, 0x18E6FC3F, 0xDFE4FB3F, 0x80E3F337, 0x3CE2FC3F, 0xFFFFFFFF, 0xEA70FF3F, 0xFFFFBFFF,
        0x7EFFCB3B, 0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x7FFFFFBF, 0xF1FFDFDF, 0xF2FFDF4B, 0xFFFFFFFF,
    ],
    # Program 5: Inverse Rooms (Algorithm C)
    [
        0xD7E1C73B, 0xD8E1FEBF, 0x47FF0738, 0xFFFFD4FF, 0xFFFFE7FB, 0x74FF1CBF, 0xFCFE0A38, 0x46FFD43F,
        0x47FFEABB, 0x46FF1CBF, 0xFFFFE7FB, 0xFFFFF4FF, 0xFFFFFEFF, 0x45F50F38, 0xFFFF98FE, 0xFFFFEFFB,
        0xC6F510BF, 0x74F20338, 0x45F53EBF, 0x7CFF7E3D, 0xFFFF4BFB, 0x7DFF7FBF, 0x7AFF383C, 0xFFFF4BFB,
        0xFFFF7EFF, 0xFFFF79FF, 0x7BFFFFBF, 0x90F10F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x74F210BF, 0x22EF0038,
        0x90F13EBF, 0xFFFF39FF, 0x05EE0F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x22EF10BF, 0x2CF5CF3B, 0x05EEFABF,
        0xFEF3F33F, 0xDEF2FB3F, 0x9AF1F33F, 0x0CF0FB3F, 0xE5EEFB3F, 0x56EDF33F, 0x07ECFB3F, 0xAEEAF33F,
        0x4CE9FB3F, 0x31E8FB3F, 0xD3E6F33F, 0x6CE5FB3F, 0x39E4F33F, 0xCFE2FB3F, 0xFFFFFFFF, 0x75FFCB3B,
        0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x76FFFFBF, 0xF1FFDFDF, 0xF2FFDF5B, 0xFFFFFFFF, 0xEA70C73B,
        0xEB70FEBF, 0xD4FE0738, 0xFFFFD4FF, 0xFFFFE7FB, 0xFBFE1CBF, 0x95FE0A38, 0xD3FED43F, 0xD4FEEABB,
        0xD3FE1CBF, 0xFFFFE7FB, 0xFFFFF4FF, 0xFFFFFEFF, 0x5BEB0F38, 0xFFFF98FE, 0xFFFFEFFB, 0xD0EB10BF,
        0x7EE80338, 0x5BEB3EBF, 0xFEFF7E3D, 0xFFFF4BFB, 0xFFFF7FBF, 0xFCFF383C, 0xFFFF4BFB, 0xFFFF7EFF,
        0xFFFF79FF, 0xFDFFFFBF, 0x9CE70F38, 0xFFFFD8FF, 0xFFFFEFFB, 0x7DE810BF, 0x2CE50038, 0x9CE73EBF,
        0xFFFF39FF, 0x80E30F38, 0xFFFFD8FF, 0xFFFFEFF3, 0x2CE510BF, 0xFFFFFFFF, 0x76F4CF3B, 0x80E3F2BF,
        0x6DF3FB3F, 0x33F2FB3F, 0x00F1F33F, 0x33EFFB3F, 0x13EEF33F, 0xA3ECFB3F, 0x7DEBFB3F, 0x13EAF33F,
        0x9AE8FB3F, 0x7FE7F33F, 0x18E6FB3F, 0xDFE4FB3F, 0x80E3F337, 0x3CE2FB3F, 0xFFFFFFFF, 0x7EFFCB3B,
        0xFFFFFFFF, 0xFFFFF8FF, 0xFFFFFFFB, 0x7FFFFFBF, 0xFFFFFFFF, 0xF1FFDFDF, 0xF2FFDF4B, 0xFFFFFFFF,
    ],
]


@dataclass
class DecodedStep:
    """Decoded WCS micro-instruction — matches C++ struct exactly."""
    ofst: int       # Memory offset (in original 20480Hz samples)
    cCode: int      # 4-bit coefficient code (0-15)
    acc0: bool      # true = accumulate, false = load fresh
    rad: int        # Register read address (0-3)
    rai: bool       # Read address input: true = memory, false = register
    wai: int        # Write address (register 0-7)
    ctrl: int       # Control field (5 bits)
    hasCoeff: bool  # Step has valid coefficient
    isNop: bool     # Step is a no-operation

    @staticmethod
    def decode(word: int) -> 'DecodedStep':
        mi31_24 = (word >> 24) & 0xFF
        mi23_16 = (word >> 16) & 0xFF
        mi15_8 = (word >> 8) & 0xFF
        mi7_0 = word & 0xFF

        wai = mi31_24 & 7
        ctrl = (mi31_24 >> 3) & 0x1F
        ofst = (mi15_8 << 8) | mi7_0
        hasCoeff = (mi23_16 != 0xFF)
        isNop = (mi31_24 == 0xFF and mi23_16 == 0xFF)

        if hasCoeff:
            c8 = (mi23_16 >> 0) & 1
            c1 = (mi23_16 >> 1) & 1
            c2 = (mi23_16 >> 2) & 1
            c3 = (mi23_16 >> 3) & 1
            cCode = (c8 << 3) | (c3 << 2) | (c2 << 1) | c1
            acc0 = ((mi23_16 >> 4) & 1) != 0
            rad = (mi23_16 >> 5) & 3
            rai = ((mi23_16 >> 7) & 1) != 0
        else:
            cCode = 0
            acc0 = False
            rad = 0
            rai = True

        return DecodedStep(ofst, cCode, acc0, rad, rai, wai, ctrl, hasCoeff, isNop)


class WCSEngine:
    """
    Python WCS Microcode Engine — mirrors C++ Suede200Reverb exactly.

    Usage:
        engine = WCSEngine(program=0, sr=44100)
        ir = engine.generate_ir(duration_s=3.0, coefficients=coeff_array)
    """

    def __init__(self, program: int = 0, sr: int = 44100):
        self.sr = sr
        self.sr_ratio = sr / ORIGINAL_SR
        self.program = program

        # Decode microcode
        self.steps = [DecodedStep.decode(w) for w in MICROCODE[program]]

        # Find output steps
        self.output_step_l = 60  # default
        self.output_step_r = 124
        for i in range(64):
            s = self.steps[i]
            if s.ctrl == 0x1E and s.wai == 1 and not s.hasCoeff:
                self.output_step_l = i
                break
        for i in range(64, 128):
            s = self.steps[i]
            if s.ctrl == 0x1E and s.wai == 1 and not s.hasCoeff:
                self.output_step_r = i
                break

        # Circular delay memory
        self.memory_size = int(65536 * self.sr_ratio) + 16
        self.memory = np.zeros(self.memory_size, dtype=np.float64)
        self.write_ptr = 0

        # Register file
        self.regs = np.zeros(8, dtype=np.float64)

        # LFO
        self.lfo_phase = 0.0
        self.lfo_value = 0.0

    def reset(self):
        self.memory[:] = 0.0
        self.write_ptr = 0
        self.regs[:] = 0.0
        self.lfo_phase = 0.0
        self.lfo_value = 0.0

    def _generate_ir_native(self, n_samples: int, coefficients: np.ndarray,
                            rolloff_hz: float) -> np.ndarray:
        """Generate IR using native C engine — ~100x faster than Python."""
        microcode = (ctypes.c_uint * 128)(*MICROCODE[self.program])
        coeffs = (ctypes.c_double * 16)(*np.asarray(coefficients, dtype=np.float64))
        output_l = (ctypes.c_double * n_samples)()
        output_r = (ctypes.c_double * n_samples)()

        _native_lib.wcs_generate_ir(microcode, coeffs,
                                    ctypes.c_double(float(self.sr)),
                                    ctypes.c_int(n_samples),
                                    ctypes.c_double(rolloff_hz),
                                    output_l, output_r)

        result = np.zeros((2, n_samples), dtype=np.float32)
        result[0] = np.frombuffer(output_l, dtype=np.float64).astype(np.float32)
        result[1] = np.frombuffer(output_r, dtype=np.float64).astype(np.float32)
        return result

    def _execute_step(self, step: DecodedStep, coefficients: np.ndarray):
        """Execute a single WCS step — mirrors C++ executeStep exactly."""
        if step.isNop:
            return

        # Scale offset for host sample rate
        scaled_ofst = int(step.ofst * self.sr_ratio + 0.5)

        # LFO modulation on long delays
        if scaled_ofst > int(5000 * self.sr_ratio) and self.lfo_value != 0.0:
            mod_amount = int(self.lfo_value * self.sr_ratio * 1.5)
            scaled_ofst += mod_amount

        scaled_ofst = max(0, min(scaled_ofst, self.memory_size - 1))

        read_pos = self.write_ptr - scaled_ofst
        if read_pos < 0:
            read_pos += self.memory_size

        if step.hasCoeff:
            # Get multiplier input
            if step.rai:
                mul_input = self.memory[read_pos]
            else:
                mul_input = self.regs[step.rad]

            # Coefficient multiply
            result = mul_input * coefficients[step.cCode]

            # Soft clamp (16-bit saturation)
            result = max(-4.0, min(4.0, result))

            # Accumulate or load
            if step.acc0:
                self.regs[step.wai] += result
            else:
                self.regs[step.wai] = result

            # Register clamp
            self.regs[step.wai] = max(-8.0, min(8.0, self.regs[step.wai]))

        # Memory write
        do_mem_write = (step.ctrl & 0x10) and (step.ctrl != 0x1F)

        if do_mem_write:
            write_val = self.regs[step.wai]
            if write_val > 1.5 or write_val < -1.5:
                write_val = np.tanh(write_val * 0.667) * 1.5
            self.memory[read_pos] = write_val
        elif not step.hasCoeff and step.ctrl != 0x1F:
            self.regs[step.wai] = self.memory[read_pos]

    def generate_ir(self, duration_s: float, coefficients: np.ndarray,
                    rolloff_hz: float = 10000.0,
                    pre_delay_ms: float = 0.0,
                    input_gain: float = 0.25) -> np.ndarray:
        """
        Generate a stereo impulse response.

        Args:
            duration_s: IR length in seconds
            coefficients: 16-element float array of C-code values
            rolloff_hz: Lowpass cutoff before reverb
            pre_delay_ms: Pre-delay in milliseconds
            input_gain: Input scaling (0.25 matches C++)

        Returns:
            Stereo IR as (2, N) numpy array
        """
        n_samples = int(self.sr * duration_s)

        # Use native C engine if available (~100x faster)
        if _native_lib is not None and pre_delay_ms == 0.0:
            return self._generate_ir_native(n_samples, coefficients, rolloff_hz)

        self.reset()
        output = np.zeros((2, n_samples), dtype=np.float64)

        coefficients = np.asarray(coefficients, dtype=np.float64)
        assert len(coefficients) == 16

        # Rolloff filter state (one-pole LP)
        w = np.exp(-2.0 * np.pi * rolloff_hz / self.sr)
        lp_a1 = w
        lp_b0 = 1.0 - w
        lp_z_l = 0.0
        lp_z_r = 0.0

        # DC blocker state
        dc_x1_l, dc_y1_l = 0.0, 0.0
        dc_x1_r, dc_y1_r = 0.0, 0.0

        # Pre-delay buffer
        pd_samples = int(pre_delay_ms * 0.001 * self.sr)
        if pd_samples > 0:
            pd_buf_l = np.zeros(pd_samples + 1)
            pd_buf_r = np.zeros(pd_samples + 1)
            pd_ptr = 0

        output_gain = 1.0 / input_gain

        for n in range(n_samples):
            # Input: impulse at sample 0
            inp_l = 1.0 if n == 0 else 0.0
            inp_r = 1.0 if n == 0 else 0.0

            # Rolloff filter
            lp_z_l = inp_l * lp_b0 + lp_z_l * lp_a1
            lp_z_r = inp_r * lp_b0 + lp_z_r * lp_a1
            filt_l = lp_z_l
            filt_r = lp_z_r

            # Pre-delay
            if pd_samples > 0:
                read_idx = (pd_ptr - pd_samples) % (pd_samples + 1)
                pd_l = pd_buf_l[read_idx]
                pd_r = pd_buf_r[read_idx]
                pd_buf_l[pd_ptr] = filt_l
                pd_buf_r[pd_ptr] = filt_r
                pd_ptr = (pd_ptr + 1) % (pd_samples + 1)
            else:
                pd_l = filt_l
                pd_r = filt_r

            # Scale input
            pd_l *= input_gain
            pd_r *= input_gain

            # === WCS Execution ===
            captured_l = 0.0
            captured_r = 0.0

            # Left half (steps 0-63)
            self.regs[2] = pd_l
            for s in range(64):
                self._execute_step(self.steps[s], coefficients)
                if s == self.output_step_l:
                    captured_l = self.regs[1]

            # Right half (steps 64-127)
            self.regs[2] = pd_r
            for s in range(64, 128):
                self._execute_step(self.steps[s], coefficients)
                if s == self.output_step_r:
                    captured_r = self.regs[1]

            # Advance write pointer
            self.write_ptr = (self.write_ptr + 1) % self.memory_size

            # LFO
            self.lfo_phase += 0.37 / self.sr
            if self.lfo_phase >= 1.0:
                self.lfo_phase -= 1.0
            self.lfo_value = np.sin(self.lfo_phase * 2.0 * np.pi)

            # DC blocking
            dc_y_l = captured_l - dc_x1_l + 0.9975 * dc_y1_l
            dc_x1_l = captured_l
            dc_y1_l = dc_y_l

            dc_y_r = captured_r - dc_x1_r + 0.9975 * dc_y1_r
            dc_x1_r = captured_r
            dc_y1_r = dc_y_r

            # Output (wet only — 100% wet for IR capture)
            output[0, n] = dc_y_l * output_gain
            output[1, n] = dc_y_r * output_gain

        return output.astype(np.float32)


# Mapping from IR filename prefix to program index
PROGRAM_MAP = {
    'Hall': 0,
    'Plate': 1,
    'Chamber': 2,
    'Rich Plate': 3,
    'Rich Split': 4,
    'Inverse Room': 5,
}

PROGRAM_NAMES = [
    'Concert Hall', 'Plate', 'Chamber',
    'Rich Plate', 'Rich Splits', 'Inverse Rooms',
]


def ir_name_to_program(filename: str) -> int:
    """Map an IR filename to a program index."""
    name = filename.replace('_dc.wav', '').replace('_dc', '')
    # Try longest prefix first
    for prefix in sorted(PROGRAM_MAP.keys(), key=len, reverse=True):
        if name.startswith(prefix):
            return PROGRAM_MAP[prefix]
    raise ValueError(f"Cannot map '{filename}' to a program")


if __name__ == '__main__':
    # Quick test: generate an IR with default coefficients
    engine = WCSEngine(program=0, sr=44100)
    # Use the hand-tuned coefficients from the C++ code as baseline
    coeffs = np.array([
        0.45, 0.47, 0.50, 0.75,  # C0-C3
        0.35, 0.50, 0.71, 0.55,  # C4-C7
        0.45, 0.38, 0.73, 0.75,  # C8-CB
        0.35, 0.15, 0.70, 0.39,  # CC-CF
    ])
    ir = engine.generate_ir(duration_s=2.0, coefficients=coeffs)
    peak = np.max(np.abs(ir))
    print(f"Concert Hall IR: shape={ir.shape}, peak={peak:.4f}")
    print(f"Output steps: L={engine.output_step_l}, R={engine.output_step_r}")
