/*
  ==============================================================================

    Suede200Reverb.h
    Suede 200 — Vintage Digital Reverberator DSP Engine

    WCS Microcode Engine — ROM-Accurate Implementation

    Implements the original Suede 200 architecture:
      - 128-step Writable Control Store (WCS) microcode execution per sample
      - Single 64K×16-bit circular delay memory (scaled for actual sample rate)
      - 8-register accumulator/routing file
      - 3 algorithm topologies with 6 programs extracted from ROM

    Hardware operation per WCS step:
      1. Read from delay memory at CPC + OFST → data_in
      2. If RAI=1: multiply data_in by coefficient; if RAI=0: multiply reg[RAD]
      3. If ACC0=0: ACC = result (load); if ACC0=1: ACC += result (accumulate)
      4. Store result to register file at WAI
      5. If CTRL bit 4 set (and not 0x1F): write register to memory

    Algorithm A — Concert Hall: 97 active steps, 7 diffusion + 25 FDN taps
    Algorithm B — Plate: 104 active steps, 10 diffusion + 32 FDN taps
    Algorithm C — Chamber family: 83-89 active steps, 6-7 diffusion + 19-26 FDN taps

    Copyright (c) 2025 Dusk Audio - All rights reserved.

  ==============================================================================
*/

#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

namespace Suede200
{

//==============================================================================
// ROM-extracted microcode data for all 6 programs (128 × 32-bit words each)
// Extracted from original firmware v1.3 ROMs
namespace WCSData
{
    static constexpr uint32_t microcode[6][128] =
    {
        // Program 1: Concert Hall (Algorithm A)
        {
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
        },
        // Program 2: Plate (Algorithm B)
        {
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
        },
        // Program 3: Chamber (Algorithm C)
        {
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
        },
        // Program 4: Random Hall (Algorithm C variant)
        {
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
        },
        // Program 5: Church (Algorithm C)
        {
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
        },
        // Program 6: Cathedral (Algorithm C)
        {
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
        },
    };
} // namespace WCSData

//==============================================================================
// Decoded WCS micro-instruction
struct DecodedStep
{
    uint16_t ofst;      // Memory offset (in original 20480Hz samples)
    uint8_t cCode;      // 4-bit coefficient code (0-15)
    bool acc0;          // true = accumulate, false = load fresh
    uint8_t rad;        // Register read address (0-3)
    bool rai;           // Read address input: true = memory, false = register
    uint8_t wai;        // Write address (register 0-7)
    uint8_t ctrl;       // Control field (5 bits)
    bool hasCoeff;      // Step has valid coefficient (MI16-23 != 0xFF)
    bool isNop;         // Step is a no-operation

    static DecodedStep decode(uint32_t word)
    {
        DecodedStep s{};

        auto mi31_24 = static_cast<uint8_t>((word >> 24) & 0xFF);
        auto mi23_16 = static_cast<uint8_t>((word >> 16) & 0xFF);
        auto mi15_8  = static_cast<uint8_t>((word >> 8) & 0xFF);
        auto mi7_0   = static_cast<uint8_t>(word & 0xFF);

        s.wai = mi31_24 & 7;
        s.ctrl = (mi31_24 >> 3) & 0x1F;
        s.ofst = static_cast<uint16_t>((mi15_8 << 8) | mi7_0);
        s.hasCoeff = (mi23_16 != 0xFF);
        s.isNop = (mi31_24 == 0xFF && mi23_16 == 0xFF);

        if (s.hasCoeff)
        {
            uint8_t c8 = (mi23_16 >> 0) & 1;
            uint8_t c1 = (mi23_16 >> 1) & 1;
            uint8_t c2 = (mi23_16 >> 2) & 1;
            uint8_t c3 = (mi23_16 >> 3) & 1;
            s.cCode = static_cast<uint8_t>((c8 << 3) | (c3 << 2) | (c2 << 1) | c1);
            s.acc0  = ((mi23_16 >> 4) & 1) != 0;
            s.rad   = static_cast<uint8_t>((mi23_16 >> 5) & 3);
            s.rai   = ((mi23_16 >> 7) & 1) != 0;
        }
        else
        {
            s.cCode = 0;
            s.acc0 = false;
            s.rad = 0;
            s.rai = true;
        }

        return s;
    }
};

//==============================================================================
// One-pole lowpass filter
struct OnePoleLP
{
    float b0 = 1.0f, a1 = 0.0f;
    float z1 = 0.0f;

    void setFrequency(float freqHz, float sampleRate)
    {
        float w = std::exp(-juce::MathConstants<float>::twoPi * freqHz / sampleRate);
        a1 = w;
        b0 = 1.0f - w;
    }

    float process(float x)
    {
        z1 = x * b0 + z1 * a1;
        return z1;
    }

    void reset() { z1 = 0.0f; }
};

//==============================================================================
// DC blocking highpass filter
struct DCBlock
{
    float x1 = 0.0f, y1 = 0.0f;

    float process(float x)
    {
        float y = x - x1 + 0.9975f * y1;
        x1 = x;
        y1 = y;
        return y;
    }

    void reset() { x1 = 0.0f; y1 = 0.0f; }
};

//==============================================================================
/**
    WCS Microcode Engine — faithful reproduction of the original hardware's
    DSP architecture using ROM-extracted microcode data.

    Architecture:
      - Single circular delay memory (64K samples at 20.48kHz, scaled for host SR)
      - 128 microcode steps per sample, processing two stereo halves
      - 8 accumulator registers shared between halves
      - Coefficient multiplier with 16 C-codes mapped to parameter controls
      - Input injection and output extraction at algorithm-specific step positions
*/
class Suede200Reverb
{
public:
    Suede200Reverb() = default;

    void prepare(double sampleRate, int maxBlockSize)
    {
        sr = sampleRate;
        srRatio = sampleRate / 20480.0;

        // Scale circular delay memory for host sample rate
        // Original: 65536 samples at 20480 Hz ≈ 3.2 seconds
        memorySize = static_cast<int>(65536.0 * srRatio) + 16;
        memory.resize(static_cast<size_t>(memorySize), 0.0f);

        // Pre-delay buffer (up to 999ms)
        maxPreDelaySamples = static_cast<int>(sampleRate * 1.0) + 1;
        preDelayBufL.resize(static_cast<size_t>(maxPreDelaySamples), 0.0f);
        preDelayBufR.resize(static_cast<size_t>(maxPreDelaySamples), 0.0f);

        // Rolloff filter initialization
        updateRolloff();

        reset();
        loadProgram(currentProgram >= 0 ? currentProgram : 0);

        juce::ignoreUnused(maxBlockSize);
    }

    void reset()
    {
        std::fill(memory.begin(), memory.end(), 0.0f);
        writePtr = 0;
        preDelayWritePtr = 0;
        for (auto& r : regs) r = 0.0f;
        std::fill(preDelayBufL.begin(), preDelayBufL.end(), 0.0f);
        std::fill(preDelayBufR.begin(), preDelayBufR.end(), 0.0f);
        rolloffLP[0].reset();
        rolloffLP[1].reset();
        dcBlocker[0].reset();
        dcBlocker[1].reset();
        capturedOutL = 0.0f;
        capturedOutR = 0.0f;
        lfoPhase = 0.0;
    }

    //==============================================================================
    // Parameter setters — same API as Phase 2

    void setProgram(int program)
    {
        int p = juce::jlimit(0, 5, program);
        if (p != currentProgram)
            loadProgram(p);
    }

    void setPreDelay(float ms)       { preDelayMs = ms; }
    void setReverbTime(float seconds) { reverbTimeSec = juce::jlimit(0.6f, 70.0f, seconds); }
    void setSize(float meters)        { sizeMeters = juce::jlimit(8.0f, 90.0f, meters); }
    void setMix(float mix01)          { wetMix = juce::jlimit(0.0f, 1.0f, mix01); }

    void setDiffusion(int level)
    {
        diffusionLevel = juce::jlimit(0, 2, level);
    }

    void setPreEchoes(bool enabled) { preEchoesOn = enabled; }

    void setRTContourLow(int level)
    {
        rtLow = juce::jlimit(0, 2, level);
    }

    void setRTContourHigh(int level)
    {
        rtHigh = juce::jlimit(0, 2, level);
    }

    void setRolloff(int level)
    {
        rolloffLevel = juce::jlimit(0, 2, level);
        updateRolloff();
    }

    float getTargetRT60() const { return reverbTimeSec; }

    /** Load optimized coefficients directly (from IR-matched presets).
        When set, these override the formula-based updateCoefficients().
        Call clearOptimizedCoefficients() to return to formula mode. */
    void setOptimizedCoefficients(const float* coeffs, int numCoeffs, float rolloffHz)
    {
        jassert(numCoeffs == 16);
        for (int i = 0; i < 16; ++i)
            coefficients[i] = juce::jlimit(-0.998f, 0.998f, coeffs[i]);
        useOptimizedCoeffs = true;

        // Override rolloff with optimized value
        rolloffLP[0].setFrequency(rolloffHz, static_cast<float>(sr));
        rolloffLP[1].setFrequency(rolloffHz, static_cast<float>(sr));
    }

    void clearOptimizedCoefficients()
    {
        useOptimizedCoeffs = false;
    }

    bool isUsingOptimizedCoefficients() const { return useOptimizedCoeffs; }

    //==============================================================================
    // Per-sample processing (called from processBlock)

    void process(float inputL, float inputR, float& outputL, float& outputR)
    {
        // Update coefficients (skip if using optimized preset coefficients)
        if (!useOptimizedCoeffs)
            updateCoefficients();

        // Rolloff filter (input LPF, before reverb as in original hardware)
        float filtL = rolloffLP[0].process(inputL);
        float filtR = rolloffLP[1].process(inputR);

        // Pre-delay
        float pdL, pdR;
        int pdSamples = juce::jlimit(0, maxPreDelaySamples - 1,
                                     static_cast<int>(preDelayMs * 0.001f * static_cast<float>(sr)));
        if (pdSamples > 0)
        {
            int readIdx = preDelayWritePtr - pdSamples;
            if (readIdx < 0) readIdx += maxPreDelaySamples;
            pdL = preDelayBufL[static_cast<size_t>(readIdx)];
            pdR = preDelayBufR[static_cast<size_t>(readIdx)];
            preDelayBufL[static_cast<size_t>(preDelayWritePtr)] = filtL;
            preDelayBufR[static_cast<size_t>(preDelayWritePtr)] = filtR;
            preDelayWritePtr = (preDelayWritePtr + 1) % maxPreDelaySamples;
        }
        else
        {
            pdL = filtL;
            pdR = filtR;
        }

        // Scale input for headroom (original uses 16-bit fixed point)
        constexpr float inputGain = 0.25f;
        pdL *= inputGain;
        pdR *= inputGain;

        // === WCS Microcode Execution ===

        // Pre-load register 2 with left channel input
        // (hardware injects input before step 0)
        regs[2] = pdL;

        // Execute first half (steps 0-63)
        for (int s = 0; s < 64; s++)
        {
            executeStep(steps[s]);
            if (s == outputStepL)
                capturedOutL = regs[1];
        }

        // Pre-load register 2 with right channel input
        regs[2] = pdR;

        // Execute second half (steps 64-127)
        for (int s = 64; s < 128; s++)
        {
            executeStep(steps[s]);
            if (s == outputStepR)
                capturedOutR = regs[1];
        }

        // Advance circular buffer write pointer
        writePtr = (writePtr + 1) % memorySize;

        // Slow LFO for time-variant modulation
        lfoPhase += 0.37 / sr;
        if (lfoPhase >= 1.0) lfoPhase -= 1.0;
        lfoValue = static_cast<float>(std::sin(lfoPhase * juce::MathConstants<double>::twoPi));

        // DC blocking
        float wetL = dcBlocker[0].process(capturedOutL);
        float wetR = dcBlocker[1].process(capturedOutR);

        // Compensate for input gain scaling
        constexpr float outputGain = 4.0f; // 1/inputGain
        wetL *= outputGain;
        wetR *= outputGain;

        // Dry/wet mix
        outputL = inputL * (1.0f - wetMix) + wetL * wetMix;
        outputR = inputR * (1.0f - wetMix) + wetR * wetMix;
    }

private:
    //==============================================================================
    // WCS step executor — the core of the Suede 200 architecture
    void executeStep(const DecodedStep& step)
    {
        if (step.isNop) return;

        // Scale offset for host sample rate
        int scaledOfst = static_cast<int>(step.ofst * srRatio + 0.5);

        // Add subtle modulation to long delays (time-variant behavior)
        // Only modulate long delays to avoid pitch artifacts on short diffusion taps
        if (scaledOfst > static_cast<int>(5000 * srRatio) && lfoValue != 0.0f)
        {
            int modAmount = static_cast<int>(lfoValue * srRatio * 1.5f);
            scaledOfst += modAmount;
        }

        if (scaledOfst >= memorySize)
            scaledOfst = memorySize - 1;
        if (scaledOfst < 0)
            scaledOfst = 0;

        int readPos = writePtr - scaledOfst;
        if (readPos < 0) readPos += memorySize;

        if (step.hasCoeff)
        {
            // Get multiplier input: memory (RAI=1) or register file (RAI=0)
            float mulInput;
            if (step.rai)
                mulInput = memory[static_cast<size_t>(readPos)];
            else
                mulInput = regs[step.rad];

            // Coefficient multiply
            float result = mulInput * coefficients[step.cCode];

            // Soft clamp (emulates 16-bit arithmetic saturation)
            result = juce::jlimit(-4.0f, 4.0f, result);

            // Accumulate or load fresh
            if (step.acc0)
                regs[step.wai] += result;
            else
                regs[step.wai] = result;

            // Clamp register to prevent unbounded growth
            regs[step.wai] = juce::jlimit(-8.0f, 8.0f, regs[step.wai]);
        }

        // Memory write: CTRL bit 4 set (0x10) and not the NOP pattern (0x1F)
        bool doMemWrite = (step.ctrl & 0x10) && (step.ctrl != 0x1F);

        if (doMemWrite)
        {
            // Write current register value to delay memory
            float writeVal = regs[step.wai];

            // Soft saturation on memory write (emulates 16-bit fixed-point overflow)
            if (writeVal > 1.5f || writeVal < -1.5f)
                writeVal = std::tanh(writeVal * 0.667f) * 1.5f;

            memory[static_cast<size_t>(readPos)] = writeVal;
        }
        else if (!step.hasCoeff && step.ctrl != 0x1F)
        {
            // No coefficient, no memory write: route memory value to register
            regs[step.wai] = memory[static_cast<size_t>(readPos)];
        }
    }

    //==============================================================================
    void loadProgram(int prog)
    {
        currentProgram = juce::jlimit(0, 5, prog);

        // Decode microcode for this program
        for (int i = 0; i < 128; i++)
            steps[i] = DecodedStep::decode(WCSData::microcode[currentProgram][i]);

        // Find output extraction steps
        // Signature: CTRL=0x1E, WAI=1, no coefficient (writes reg[1] to memory)
        outputStepL = 60;  // Default for Algorithm A
        outputStepR = 124;

        for (int i = 0; i < 64; i++)
        {
            if (steps[i].ctrl == 0x1E && steps[i].wai == 1 && !steps[i].hasCoeff)
            {
                outputStepL = i;
                break;
            }
        }
        for (int i = 64; i < 128; i++)
        {
            if (steps[i].ctrl == 0x1E && steps[i].wai == 1 && !steps[i].hasCoeff)
            {
                outputStepR = i;
                break;
            }
        }

        updateCoefficients();
    }

    //==============================================================================
    void updateCoefficients()
    {
        // Map C-codes (0-15) to float coefficient values based on front-panel parameters.
        //
        // Based on frequency analysis of C-code usage across all 3 algorithm
        // topologies (signal_flow.py analysis):
        //   C3: Main FDN feedback writes (MEM_WRITE_ACC)
        //   C5: Structural tap gains — most common coefficient (MAC + writes)
        //   C7: Allpass diffusion paths (FEEDBACK_READ)
        //   CA: Secondary feedback/decay
        //   CB: LF-dependent decay, CE: Output stage, CC: Cross-coupling
        //   CD: Damping/pre-echo, C4: Auxiliary routing
        //   C0/C1/C2/C8: Algorithm-specific structural gains

        float rtNorm  = juce::jlimit(0.0f, 1.0f, (reverbTimeSec - 0.6f) / 69.4f);
        float sizeNorm = juce::jlimit(0.0f, 1.0f, (sizeMeters - 8.0f) / 82.0f);

        // Diffusion coefficient (allpass gain)
        float diffCoeff = (diffusionLevel == 0) ? 0.35f
                        : (diffusionLevel == 1) ? 0.55f : 0.75f;

        // RT contour multipliers
        static constexpr float rtLowMults[]  = { 0.5f, 1.0f, 1.5f };
        static constexpr float rtHighMults[] = { 0.25f, 0.5f, 1.0f };
        float rtLowMult  = rtLowMults[rtLow];
        float rtHighMult = rtHighMults[rtHigh];

        // Feedback gain — direct mapping from RT parameter.
        // sqrt curve gives musically useful response: gentle low end, faster at high RT.
        // Short RT (0.6s) → 0.55, Long RT (70s) → 0.97
        float feedbackGain = 0.55f + 0.42f * std::sqrt(rtNorm);
        feedbackGain = juce::jlimit(0.4f, 0.97f, feedbackGain);

        // Size scaling — larger rooms need slightly more gain to sustain
        float sizeScale = 0.9f + 0.15f * sizeNorm;

        float preEchoGain = preEchoesOn ? 0.35f : 0.0f;

        coefficients[0x0] = 0.45f * sizeScale;                              // Baseline structural
        coefficients[0x1] = diffCoeff * 0.85f;                              // Diffusion variant A
        coefficients[0x2] = diffCoeff * 0.9f;                               // Diffusion variant B
        coefficients[0x3] = feedbackGain;                                    // Main FDN feedback
        coefficients[0x4] = 0.35f + 0.15f * rtNorm;                         // Auxiliary routing
        coefficients[0x5] = 0.5f * sizeScale;                               // Structural tap gain
        coefficients[0x6] = feedbackGain * 0.95f;                            // Secondary feedback
        coefficients[0x7] = diffCoeff;                                       // Allpass diffusion
        coefficients[0x8] = 0.45f * sizeScale;                              // Size-dependent (Algo C)
        coefficients[0x9] = feedbackGain * rtHighMult;                       // HF-dependent decay
        coefficients[0xA] = feedbackGain * 0.97f;                            // Decay variant A
        coefficients[0xB] = feedbackGain * juce::jlimit(0.4f, 1.0f, rtLowMult); // LF-dependent decay
        coefficients[0xC] = 0.35f * sizeScale;                              // Cross-coupling
        coefficients[0xD] = 0.15f + preEchoGain;                            // Pre-echo / damping
        coefficients[0xE] = feedbackGain * 0.93f;                            // Output stage
        coefficients[0xF] = diffCoeff * 0.7f;                               // Reserved/variant

        // Safety clamp — prevent runaway
        for (auto& c : coefficients)
            c = juce::jlimit(-0.998f, 0.998f, c);
    }

    //==============================================================================
    void updateRolloff()
    {
        static constexpr float rolloffFreqs[] = { 3000.0f, 7000.0f, 10000.0f };
        float freq = rolloffFreqs[rolloffLevel];
        rolloffLP[0].setFrequency(freq, static_cast<float>(sr));
        rolloffLP[1].setFrequency(freq, static_cast<float>(sr));
    }

    //==============================================================================
    // WCS engine state
    std::vector<float> memory;          // Circular delay memory
    int memorySize = 0;
    int writePtr = 0;

    float regs[8] = {};                 // Hardware register file (W0-W7)
    DecodedStep steps[128] = {};        // Current program's decoded microcode

    int outputStepL = 60;               // Step that produces L output
    int outputStepR = 124;              // Step that produces R output

    float capturedOutL = 0.0f;
    float capturedOutR = 0.0f;

    // Parameters
    int currentProgram = -1;
    float preDelayMs = 39.0f;
    float reverbTimeSec = 2.5f;
    float sizeMeters = 26.0f;
    int diffusionLevel = 1;             // 0=Lo, 1=Med, 2=Hi
    bool preEchoesOn = false;
    int rtLow = 1;                      // 0=×0.5, 1=×1.0, 2=×1.5
    int rtHigh = 1;                     // 0=×0.25, 1=×0.5, 2=×1.0
    int rolloffLevel = 2;               // 0=3kHz, 1=7kHz, 2=10kHz
    float wetMix = 0.35f;

    // Coefficients — 16 C-code values mapped from parameters
    float coefficients[16] = {};
    bool useOptimizedCoeffs = false;  // true when using IR-matched preset coefficients

    // DSP components
    double sr = 44100.0;
    double srRatio = 1.0;

    OnePoleLP rolloffLP[2];
    DCBlock dcBlocker[2];

    // Pre-delay
    std::vector<float> preDelayBufL, preDelayBufR;
    int maxPreDelaySamples = 0;
    int preDelayWritePtr = 0;

    // Time-variant modulation
    double lfoPhase = 0.0;
    float lfoValue = 0.0f;
};

} // namespace Suede200
