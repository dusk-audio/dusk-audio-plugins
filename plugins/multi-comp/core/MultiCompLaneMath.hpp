// Copyright (C) 2026 Dusk Audio, GNU GPL v3.0 or later (see repository LICENSE).
// Natural logarithm, exponential and power in double precision, for one value or for the two lanes of a
// lanes::D2 (MultiCompLanes.hpp). Both forms perform the same IEEE operations in the same order (separate multiply
// and add, no fusion; the plugin builds with -ffp-contract=off), so a lane's value does not depend on which form
// computed it, on any platform. They exist for loops that would otherwise call the library once per lane: the call
// and the vector registers it clobbers cost more there than the function itself (CPU-parity campaign, 2026-10-21).
//
// log: x = 2^k z with z in [sqrt(1/2), sqrt(2)) split into 128 intervals by its leading mantissa bits; interval i
//      has a centre c_i (1 for the interval holding 1, so ln 1 = 0), 1 / c_i and ln c_i (correctly rounded),
//      r = (z - c_i) / c_i (|r| < 0.0079, the subtraction exact) and ln x = k ln 2 + ln c_i + ln(1 + r), the last to
//      r^7 (truncation under 2e-18).
// exp: y = (k + r / (ln 2 / 128)) ln 2 / 128 with |r| <= ln 2 / 256, e^y = 2^(k / 128) e^r with 2^(j / 128) from a
//      table (correctly rounded) and e^r to r^5 (truncation under 6e-19).
// pow: exp(e log x).
// Measured against the library over 4e7 arguments each (2026-10-21): log within 2.3e-16 (absolute, relative above
// |ln x| = 1), exp within 2.3e-16 relative, pow within 1.1e-14 relative for x in [1e-6, 1e6] and e in [0.1, 4]
// (the rounding of e ln x); the two-lane forms equal the one-lane forms bit for bit.
// Outside log's [2^-1000, 2^1000] and exp's |y| <= 700 (NaN included) every form returns the library's value.
#pragma once

#include "MultiCompLanes.hpp"

#include <cmath>
#include <cstdint>
#include <array>
#include <cstring>

namespace duskaudio::lanes::lanemath
{
struct LogEntry { double c, invc, logc, pad; };

inline constexpr LogEntry kLogTable[128] = {
    {0x1.6b09e667f3bcdp-1, 0x1.690a9aed41336p+0, -0x1.60111baa659bdp-2, 0.0},
    {0x1.6d09e667f3bcdp-1, 0x1.67103630e798dp+0, -0x1.5a70e7e2f8ad9p-2, 0.0},
    {0x1.6f09e667f3bcdp-1, 0x1.651b563da9bf9p+0, -0x1.54d8929d2d59bp-2, 0.0},
    {0x1.7109e667f3bcdp-1, 0x1.632be41b48593p+0, -0x1.4f4805f3fa86bp-2, 0.0},
    {0x1.7309e667f3bcdp-1, 0x1.6141c9504cf0fp+0, -0x1.49bf2c5d395ffp-2, 0.0},
    {0x1.7509e667f3bcdp-1, 0x1.5f5cefdea3da8p+0, -0x1.443df0a7b107bp-2, 0.0},
    {0x1.7709e667f3bcdp-1, 0x1.5d7d424051f7dp+0, -0x1.3ec43df92fa99p-2, 0.0},
    {0x1.7909e667f3bcdp-1, 0x1.5ba2ab64454d3p+0, -0x1.3951ffccb07ffp-2, 0.0},
    {0x1.7b09e667f3bcdp-1, 0x1.59cd16ab3f661p+0, -0x1.33e721f08e632p-2, 0.0},
    {0x1.7d09e667f3bcdp-1, 0x1.57fc6fe4d8978p+0, -0x1.2e839084c27cbp-2, 0.0},
    {0x1.7f09e667f3bcdp-1, 0x1.5630a34c9b387p+0, -0x1.292737f92ebdfp-2, 0.0},
    {0x1.8109e667f3bcdp-1, 0x1.54699d8735f00p+0, -0x1.23d2050bf3bacp-2, 0.0},
    {0x1.8309e667f3bcdp-1, 0x1.52a74b9fc443dp+0, -0x1.1e83e4c7d18e5p-2, 0.0},
    {0x1.8509e667f3bcdp-1, 0x1.50e99b052c9acp+0, -0x1.193cc4829372ep-2, 0.0},
    {0x1.8709e667f3bcdp-1, 0x1.4f30798792edap+0, -0x1.13fc91db85b7ap-2, 0.0},
    {0x1.8909e667f3bcdp-1, 0x1.4d7bd555df6aap+0, -0x1.0ec33ab9f5c36p-2, 0.0},
    {0x1.8b09e667f3bcdp-1, 0x1.4bcb9cfb58562p+0, -0x1.0990ad4bbbd6cp-2, 0.0},
    {0x1.8d09e667f3bcdp-1, 0x1.4a1fbf5d4e7b1p+0, -0x1.0464d803ce407p-2, 0.0},
    {0x1.8f09e667f3bcdp-1, 0x1.48782bb8db83bp+0, -0x1.fe7f5331bb79bp-3, 0.0},
    {0x1.9109e667f3bcdp-1, 0x1.46d4d1a0b19c6p+0, -0x1.f4422207f5712p-3, 0.0},
    {0x1.9309e667f3bcdp-1, 0x1.4535a0fafbc46p+0, -0x1.ea11fafe8666dp-3, 0.0},
    {0x1.9509e667f3bcdp-1, 0x1.439a89ff4e3acp+0, -0x1.dfeebd093202ep-3, 0.0},
    {0x1.9709e667f3bcdp-1, 0x1.42037d34a6793p+0, -0x1.d5d84798c0567p-3, 0.0},
    {0x1.9909e667f3bcdp-1, 0x1.40706b6f7a338p+0, -0x1.cbce7a988a5f3p-3, 0.0},
    {0x1.9b09e667f3bcdp-1, 0x1.3ee145cfd4da7p+0, -0x1.c1d1366c15d99p-3, 0.0},
    {0x1.9d09e667f3bcdp-1, 0x1.3d55fdbf83227p+0, -0x1.b7e05becbfedap-3, 0.0},
    {0x1.9f09e667f3bcdp-1, 0x1.3bce84f04c163p+0, -0x1.adfbcc6776498p-3, 0.0},
    {0x1.a109e667f3bcdp-1, 0x1.3a4acd5a37403p+0, -0x1.a423699a7e3ebp-3, 0.0},
    {0x1.a309e667f3bcdp-1, 0x1.38cac939df7b7p+0, -0x1.9a5715b3497b4p-3, 0.0},
    {0x1.a509e667f3bcdp-1, 0x1.374e6b0ed1ff8p+0, -0x1.9096b34c57fadp-3, 0.0},
    {0x1.a709e667f3bcdp-1, 0x1.35d5a599f940cp+0, -0x1.86e2256b26d04p-3, 0.0},
    {0x1.a909e667f3bcdp-1, 0x1.34606bdc13400p+0, -0x1.7d394f7e2b69cp-3, 0.0},
    {0x1.ab09e667f3bcdp-1, 0x1.32eeb11432eafp+0, -0x1.739c155adaf66p-3, 0.0},
    {0x1.ad09e667f3bcdp-1, 0x1.318068be4c2fcp+0, -0x1.6a0a5b3bbd953p-3, 0.0},
    {0x1.af09e667f3bcdp-1, 0x1.30158691ca6b9p+0, -0x1.608405be8cfaap-3, 0.0},
    {0x1.b109e667f3bcdp-1, 0x1.2eadfe8030ddep+0, -0x1.5708f9e25e396p-3, 0.0},
    {0x1.b309e667f3bcdp-1, 0x1.2d49c4b3c4cebp+0, -0x1.4d991d05d6616p-3, 0.0},
    {0x1.b509e667f3bcdp-1, 0x1.2be8cd8e4116ep+0, -0x1.443454e569a76p-3, 0.0},
    {0x1.b709e667f3bcdp-1, 0x1.2a8b0da792bfap+0, -0x1.3ada8799a4cc2p-3, 0.0},
    {0x1.b909e667f3bcdp-1, 0x1.293079cc9e6dfp+0, -0x1.318b9b95807abp-3, 0.0},
    {0x1.bb09e667f3bcdp-1, 0x1.27d906fe0e535p+0, -0x1.284777a4be599p-3, 0.0},
    {0x1.bd09e667f3bcdp-1, 0x1.2684aa6f285f8p+0, -0x1.1f0e02ea4f8aep-3, 0.0},
    {0x1.bf09e667f3bcdp-1, 0x1.25335984ac702p+0, -0x1.15df24dec45afp-3, 0.0},
    {0x1.c109e667f3bcdp-1, 0x1.23e509d3ba3fap+0, -0x1.0cbac54ec4de3p-3, 0.0},
    {0x1.c309e667f3bcdp-1, 0x1.2299b120bed51p+0, -0x1.03a0cc5992417p-3, 0.0},
    {0x1.c509e667f3bcdp-1, 0x1.2151455e6939bp+0, -0x1.f52244df2122ep-4, 0.0},
    {0x1.c709e667f3bcdp-1, 0x1.200bbcaca63adp+0, -0x1.e31760a1b180dp-4, 0.0},
    {0x1.c909e667f3bcdp-1, 0x1.1ec90d57a3019p+0, -0x1.d120be17a5640p-4, 0.0},
    {0x1.cb09e667f3bcdp-1, 0x1.1d892dd6d649cp+0, -0x1.bf3e2ff7ae2fdp-4, 0.0},
    {0x1.cd09e667f3bcdp-1, 0x1.1c4c14cc10057p+0, -0x1.ad6f898fb1d5fp-4, 0.0},
    {0x1.cf09e667f3bcdp-1, 0x1.1b11b9028f3b8p+0, -0x1.9bb49ec22c9c4p-4, 0.0},
    {0x1.d109e667f3bcdp-1, 0x1.19da116e1df02p+0, -0x1.8a0d4403a1541p-4, 0.0},
    {0x1.d309e667f3bcdp-1, 0x1.18a5152a32ea2p+0, -0x1.78794e5817a3ep-4, 0.0},
    {0x1.d509e667f3bcdp-1, 0x1.1772bb791927cp+0, -0x1.66f89350a806fp-4, 0.0},
    {0x1.d709e667f3bcdp-1, 0x1.1642fbc31cd79p+0, -0x1.558ae90915297p-4, 0.0},
    {0x1.d909e667f3bcdp-1, 0x1.1515cd95bdab9p+0, -0x1.443026257249ep-4, 0.0},
    {0x1.db09e667f3bcdp-1, 0x1.13eb28a2e65f2p+0, -0x1.32e821cfd64b4p-4, 0.0},
    {0x1.dd09e667f3bcdp-1, 0x1.12c304c02946fp+0, -0x1.21b2b3b61b26fp-4, 0.0},
    {0x1.df09e667f3bcdp-1, 0x1.119d59e601c75p+0, -0x1.108fb407a96d9p-4, 0.0},
    {0x1.e109e667f3bcdp-1, 0x1.107a202f1a8a5p+0, -0x1.fefdf6e69f159p-5, 0.0},
    {0x1.e309e667f3bcdp-1, 0x1.0f594fd798544p+0, -0x1.dd00c64a49007p-5, 0.0},
    {0x1.e509e667f3bcdp-1, 0x1.0e3ae13c69530p+0, -0x1.bb278988eb9e8p-5, 0.0},
    {0x1.e709e667f3bcdp-1, 0x1.0d1eccda98c85p+0, -0x1.9971f4e38095dp-5, 0.0},
    {0x1.e909e667f3bcdp-1, 0x1.0c050b4ea6ee7p+0, -0x1.77dfbd896719ep-5, 0.0},
    {0x1.eb09e667f3bcdp-1, 0x1.0aed9553e4f84p+0, -0x1.567099947f975p-5, 0.0},
    {0x1.ed09e667f3bcdp-1, 0x1.09d863c3d50f9p+0, -0x1.352440055b9fap-5, 0.0},
    {0x1.ef09e667f3bcdp-1, 0x1.08c56f958e344p+0, -0x1.13fa68bf81967p-5, 0.0},
    {0x1.f109e667f3bcdp-1, 0x1.07b4b1dd23e05p+0, -0x1.e5e5990b874cap-6, 0.0},
    {0x1.f309e667f3bcdp-1, 0x1.06a623cb1155cp+0, -0x1.a41a49ed530cdp-6, 0.0},
    {0x1.f509e667f3bcdp-1, 0x1.0599beaba87c5p+0, -0x1.62925911d939dp-6, 0.0},
    {0x1.f709e667f3bcdp-1, 0x1.048f7be684357p+0, -0x1.214d3d100c1c4p-6, 0.0},
    {0x1.f909e667f3bcdp-1, 0x1.038754fdfe0e1p+0, -0x1.c094dc43392c8p-7, 0.0},
    {0x1.fb09e667f3bcdp-1, 0x1.0281438ea7364p+0, -0x1.3f12cc38b6d54p-7, 0.0},
    {0x1.fd09e667f3bcdp-1, 0x1.017d414ec4a7ap+0, -0x1.7c2681aeef80bp-8, 0.0},
    {0x1.0000000000000p+0, 0x1.0000000000000p+0, 0x0.0p+0, 0.0},
    {0x1.0109e667f3bcdp+0, 0x1.fdee5952551eap-1, 0x1.095cafdf35de6p-8, 0.0},
    {0x1.0309e667f3bcdp+0, 0x1.f9fe734bfba8dp-1, 0x1.82a8e46f2452bp-7, 0.0},
    {0x1.0509e667f3bcdp+0, 0x1.f61dff0bf581bp-1, 0x1.3f57af16fa04ap-6, 0.0},
    {0x1.0709e667f3bcdp+0, 0x1.f24ca2626886ap-1, 0x1.bc64b3b2d2378p-6, 0.0},
    {0x1.0909e667f3bcdp+0, 0x1.ee8a05d85e704p-1, 0x1.1c3f9d79ce23bp-5, 0.0},
    {0x1.0b09e667f3bcdp+0, 0x1.ead5d495abff0p-1, 0x1.59d5751c3ec70p-5, 0.0},
    {0x1.0d09e667f3bcdp+0, 0x1.e72fbc480224fp-1, 0x1.96f5a8fde70a7p-5, 0.0},
    {0x1.0f09e667f3bcdp+0, 0x1.e3976d0b19c73p-1, 0x1.d3a1f738c7af3p-5, 0.0},
    {0x1.1109e667f3bcdp+0, 0x1.e00c9951eb9a3p-1, 0x1.07ee0a0803fb5p-4, 0.0},
    {0x1.1309e667f3bcdp+0, 0x1.dc8ef5d0e6739p-1, 0x1.25d2d51cbf364p-4, 0.0},
    {0x1.1509e667f3bcdp+0, 0x1.d91e39691734bp-1, 0x1.43802d9247dadp-4, 0.0},
    {0x1.1709e667f3bcdp+0, 0x1.d5ba1d14362fdp-1, 0x1.60f6df9f4069cp-4, 0.0},
    {0x1.1909e667f3bcdp+0, 0x1.d2625bd18e988p-1, 0x1.7e37b31a2c697p-4, 0.0},
    {0x1.1b09e667f3bcdp+0, 0x1.cf16b293b5373p-1, 0x1.9b436b99363bfp-4, 0.0},
    {0x1.1d09e667f3bcdp+0, 0x1.cbd6e02f0430cp-1, 0x1.b81ac890d6969p-4, 0.0},
    {0x1.1f09e667f3bcdp+0, 0x1.c8a2a548d246fp-1, 0x1.d4be8571699dbp-4, 0.0},
    {0x1.2109e667f3bcdp+0, 0x1.c579c4475c7adp-1, 0x1.f12f59c3bd139p-4, 0.0},
    {0x1.2309e667f3bcdp+0, 0x1.c25c0142597eep-1, 0x1.06b6fca2513dap-3, 0.0},
    {0x1.2509e667f3bcdp+0, 0x1.bf4921f42eda7p-1, 0x1.14bd89ffc7c51p-3, 0.0},
    {0x1.2709e667f3bcdp+0, 0x1.bc40edabc0124p-1, 0x1.22abab342b632p-3, 0.0},
    {0x1.2909e667f3bcdp+0, 0x1.b9432d3ed094fp-1, 0x1.3081b4ba00db4p-3, 0.0},
    {0x1.2b09e667f3bcdp+0, 0x1.b64faafcf18b3p-1, 0x1.3e3ff9586acccp-3, 0.0},
    {0x1.2d09e667f3bcdp+0, 0x1.b36632a2f50aap-1, 0x1.4be6ca2ec596cp-3, 0.0},
    {0x1.2f09e667f3bcdp+0, 0x1.b086914ee0822p-1, 0x1.597676bfe0d62p-3, 0.0},
    {0x1.3109e667f3bcdp+0, 0x1.adb0957458889p-1, 0x1.66ef4cfcda60fp-3, 0.0},
    {0x1.3309e667f3bcdp+0, 0x1.aae40ed180849p-1, 0x1.7451994f9e727p-3, 0.0},
    {0x1.3509e667f3bcdp+0, 0x1.a820ce6448eeep-1, 0x1.819da6a51092dp-3, 0.0},
    {0x1.3709e667f3bcdp+0, 0x1.a566a6602733ep-1, 0x1.8ed3be76e09a6p-3, 0.0},
    {0x1.3909e667f3bcdp+0, 0x1.a2b56a24327b8p-1, 0x1.9bf428d50f0a8p-3, 0.0},
    {0x1.3b09e667f3bcdp+0, 0x1.a00cee31a0db4p-1, 0x1.a8ff2c6f23d1ap-3, 0.0},
    {0x1.3d09e667f3bcdp+0, 0x1.9d6d0822a0b15p-1, 0x1.b5f50e9d1a6bdp-3, 0.0},
    {0x1.3f09e667f3bcdp+0, 0x1.9ad58ea18a1c0p-1, 0x1.c2d6136806318p-3, 0.0},
    {0x1.4109e667f3bcdp+0, 0x1.9846596064b77p-1, 0x1.cfa27d927185ap-3, 0.0},
    {0x1.4309e667f3bcdp+0, 0x1.95bf4110bdf92p-1, 0x1.dc5a8ea07a767p-3, 0.0},
    {0x1.4509e667f3bcdp+0, 0x1.93401f5bccb19p-1, 0x1.e8fe86dfaf47dp-3, 0.0},
    {0x1.4709e667f3bcdp+0, 0x1.90c8cedade65ep-1, 0x1.f58ea56ead439p-3, 0.0},
    {0x1.4909e667f3bcdp+0, 0x1.8e592b100b5cfp-1, 0x1.0105942242088p-2, 0.0},
    {0x1.4b09e667f3bcdp+0, 0x1.8bf1105f2e635p-1, 0x1.073a261befdefp-2, 0.0},
    {0x1.4d09e667f3bcdp+0, 0x1.89905c071d6c9p-1, 0x1.0d652682fdc57p-2, 0.0},
    {0x1.4f09e667f3bcdp+0, 0x1.8736ec1b205c5p-1, 0x1.1386b2acafbbbp-2, 0.0},
    {0x1.5109e667f3bcdp+0, 0x1.84e49f7ca3623p-1, 0x1.199ee7683539dp-2, 0.0},
    {0x1.5309e667f3bcdp+0, 0x1.829955d52272ep-1, 0x1.1fade101d585ap-2, 0.0},
    {0x1.5509e667f3bcdp+0, 0x1.8054ef904b85dp-1, 0x1.25b3bb4604249p-2, 0.0},
    {0x1.5709e667f3bcdp+0, 0x1.7e174dd6555bfp-1, 0x1.2bb091845e402p-2, 0.0},
    {0x1.5909e667f3bcdp+0, 0x1.7be0528688acep-1, 0x1.31a47e9291cacp-2, 0.0},
    {0x1.5b09e667f3bcdp+0, 0x1.79afe031f9b30p-1, 0x1.378f9ccf2f2b1p-2, 0.0},
    {0x1.5d09e667f3bcdp+0, 0x1.7785da167024cp-1, 0x1.3d720624662adp-2, 0.0},
    {0x1.5f09e667f3bcdp+0, 0x1.756224197bc28p-1, 0x1.434bd40aaedf2p-2, 0.0},
    {0x1.6109e667f3bcdp+0, 0x1.7344a2c3b3b2bp-1, 0x1.491d1f8b5f398p-2, 0.0},
    {0x1.6309e667f3bcdp+0, 0x1.712d3b3c1efdcp-1, 0x1.4ee601432de93p-2, 0.0},
    {0x1.6509e667f3bcdp+0, 0x1.6f1bd343c48bep-1, 0x1.54a69164a32d5p-2, 0.0},
    {0x1.6709e667f3bcdp+0, 0x1.6d105131611afp-1, 0x1.5a5ee7ba78344p-2, 0.0},
    {0x1.6909e667f3bcdp+0, 0x1.6b0a9bed41b36p-1, 0x1.600f1ba9e59bdp-2, 0.0},
};
inline constexpr double kExpTable[128] = {
    0x1.0000000000000p+0,
    0x1.0163da9fb3335p+0,
    0x1.02c9a3e778061p+0,
    0x1.04315e86e7f85p+0,
    0x1.059b0d3158574p+0,
    0x1.0706b29ddf6dep+0,
    0x1.0874518759bc8p+0,
    0x1.09e3ecac6f383p+0,
    0x1.0b5586cf9890fp+0,
    0x1.0cc922b7247f7p+0,
    0x1.0e3ec32d3d1a2p+0,
    0x1.0fb66affed31bp+0,
    0x1.11301d0125b51p+0,
    0x1.12abdc06c31ccp+0,
    0x1.1429aaea92de0p+0,
    0x1.15a98c8a58e51p+0,
    0x1.172b83c7d517bp+0,
    0x1.18af9388c8deap+0,
    0x1.1a35beb6fcb75p+0,
    0x1.1bbe084045cd4p+0,
    0x1.1d4873168b9aap+0,
    0x1.1ed5022fcd91dp+0,
    0x1.2063b88628cd6p+0,
    0x1.21f49917ddc96p+0,
    0x1.2387a6e756238p+0,
    0x1.251ce4fb2a63fp+0,
    0x1.26b4565e27cddp+0,
    0x1.284dfe1f56381p+0,
    0x1.29e9df51fdee1p+0,
    0x1.2b87fd0dad990p+0,
    0x1.2d285a6e4030bp+0,
    0x1.2ecafa93e2f56p+0,
    0x1.306fe0a31b715p+0,
    0x1.32170fc4cd831p+0,
    0x1.33c08b26416ffp+0,
    0x1.356c55f929ff1p+0,
    0x1.371a7373aa9cbp+0,
    0x1.38cae6d05d866p+0,
    0x1.3a7db34e59ff7p+0,
    0x1.3c32dc313a8e5p+0,
    0x1.3dea64c123422p+0,
    0x1.3fa4504ac801cp+0,
    0x1.4160a21f72e2ap+0,
    0x1.431f5d950a897p+0,
    0x1.44e086061892dp+0,
    0x1.46a41ed1d0057p+0,
    0x1.486a2b5c13cd0p+0,
    0x1.4a32af0d7d3dep+0,
    0x1.4bfdad5362a27p+0,
    0x1.4dcb299fddd0dp+0,
    0x1.4f9b2769d2ca7p+0,
    0x1.516daa2cf6642p+0,
    0x1.5342b569d4f82p+0,
    0x1.551a4ca5d920fp+0,
    0x1.56f4736b527dap+0,
    0x1.58d12d497c7fdp+0,
    0x1.5ab07dd485429p+0,
    0x1.5c9268a5946b7p+0,
    0x1.5e76f15ad2148p+0,
    0x1.605e1b976dc09p+0,
    0x1.6247eb03a5585p+0,
    0x1.6434634ccc320p+0,
    0x1.6623882552225p+0,
    0x1.68155d44ca973p+0,
    0x1.6a09e667f3bcdp+0,
    0x1.6c012750bdabfp+0,
    0x1.6dfb23c651a2fp+0,
    0x1.6ff7df9519484p+0,
    0x1.71f75e8ec5f74p+0,
    0x1.73f9a48a58174p+0,
    0x1.75feb564267c9p+0,
    0x1.780694fde5d3fp+0,
    0x1.7a11473eb0187p+0,
    0x1.7c1ed0130c132p+0,
    0x1.7e2f336cf4e62p+0,
    0x1.80427543e1a12p+0,
    0x1.82589994cce13p+0,
    0x1.8471a4623c7adp+0,
    0x1.868d99b4492edp+0,
    0x1.88ac7d98a6699p+0,
    0x1.8ace5422aa0dbp+0,
    0x1.8cf3216b5448cp+0,
    0x1.8f1ae99157736p+0,
    0x1.9145b0b91ffc6p+0,
    0x1.93737b0cdc5e5p+0,
    0x1.95a44cbc8520fp+0,
    0x1.97d829fde4e50p+0,
    0x1.9a0f170ca07bap+0,
    0x1.9c49182a3f090p+0,
    0x1.9e86319e32323p+0,
    0x1.a0c667b5de565p+0,
    0x1.a309bec4a2d33p+0,
    0x1.a5503b23e255dp+0,
    0x1.a799e1330b358p+0,
    0x1.a9e6b5579fdbfp+0,
    0x1.ac36bbfd3f37ap+0,
    0x1.ae89f995ad3adp+0,
    0x1.b0e07298db666p+0,
    0x1.b33a2b84f15fbp+0,
    0x1.b59728de5593ap+0,
    0x1.b7f76f2fb5e47p+0,
    0x1.ba5b030a1064ap+0,
    0x1.bcc1e904bc1d2p+0,
    0x1.bf2c25bd71e09p+0,
    0x1.c199bdd85529cp+0,
    0x1.c40ab5fffd07ap+0,
    0x1.c67f12e57d14bp+0,
    0x1.c8f6d9406e7b5p+0,
    0x1.cb720dcef9069p+0,
    0x1.cdf0b555dc3fap+0,
    0x1.d072d4a07897cp+0,
    0x1.d2f87080d89f2p+0,
    0x1.d5818dcfba487p+0,
    0x1.d80e316c98398p+0,
    0x1.da9e603db3285p+0,
    0x1.dd321f301b460p+0,
    0x1.dfc97337b9b5fp+0,
    0x1.e264614f5a129p+0,
    0x1.e502ee78b3ff6p+0,
    0x1.e7a51fbc74c83p+0,
    0x1.ea4afa2a490dap+0,
    0x1.ecf482d8e67f1p+0,
    0x1.efa1bee615a27p+0,
    0x1.f252b376bba97p+0,
    0x1.f50765b6e4540p+0,
    0x1.f7bfdad9cbe14p+0,
    0x1.fa7c1819e90d8p+0,
    0x1.fd3c22b8f71f1p+0,
};
inline constexpr double kLn2Hi = 0x1.62e42fee00000p-1, kLn2Lo = 0x1.a39ef35793c76p-33;
inline constexpr double kLn2NHi = 0x1.62e42fef80000p-8, kLn2NLo = 0x1.1cf79abc9e3b4p-43;
inline constexpr double kInvLn2N = 0x1.71547652b82fep+7;

inline constexpr std::uint64_t kOff = 0x3FE6A09E667F3BCDull;   // the bits of sqrt(1/2)
inline constexpr double kA2 = -0.5, kA3 = 0x1.5555555555555p-2, kA4 = -0.25, kA5 = 0x1.999999999999ap-3,
                        kA6 = -0x1.5555555555555p-3, kA7 = 0x1.2492492492492p-3;
inline constexpr double kE2 = 0.5, kE3 = 0x1.5555555555555p-3, kE4 = 0x1.5555555555555p-5, kE5 = 0x1.1111111111111p-7;
inline constexpr double kLogLow = 0x1p-1000, kLogHigh = 0x1p+1000, kExpLimit = 700.0;

inline double bitsToDouble(std::uint64_t b) noexcept { double d; std::memcpy(&d, &b, sizeof d); return d; }
inline std::uint64_t doubleToBits(double d) noexcept { std::uint64_t b; std::memcpy(&b, &d, sizeof b); return b; }

// ln x
inline double log(double x) noexcept
{
    if (!(x >= kLogLow && x <= kLogHigh)) return std::log(x);
    const std::uint64_t ix = doubleToBits(x);
    const std::uint64_t tmp = ix - kOff;
    const LogEntry& e = kLogTable[(tmp >> 45) & 127u];
    const double kd = static_cast<double>(static_cast<std::int64_t>(tmp) >> 52);
    const double z = bitsToDouble(ix - (tmp & (0xfffull << 52)));
    const double r = (z - e.c) * e.invc;
    const double r2 = r * r;
    const double q = (kA2 + r * kA3) + r2 * ((kA4 + r * kA5) + r2 * (kA6 + r * kA7));
    return (kd * kLn2Hi + e.logc) + (r + (r2 * q + kd * kLn2Lo));
}

// e^y
inline double exp(double y) noexcept
{
    if (!(std::abs(y) <= kExpLimit)) return std::exp(y);
    const double kd = std::nearbyint(y * kInvLn2N);
    const std::int64_t k = static_cast<std::int64_t>(kd);
    const double r = (y - kd * kLn2NHi) - kd * kLn2NLo;
    const double r2 = r * r;
    const double p = r + r2 * ((kE2 + r * kE3) + r2 * (kE4 + r * kE5));
    const double scaled = bitsToDouble(doubleToBits(kExpTable[static_cast<std::uint64_t>(k) & 127u])
                                       + (static_cast<std::uint64_t>(k >> 7) << 52));
    return scaled + scaled * p;
}

// x^e for x > 0 (x <= 0 or not finite: the library's)
inline double pow(double x, double e) noexcept
{
    if (!(x >= kLogLow && x <= kLogHigh)) return std::pow(x, e);
    const double y = e * lanemath::log(x);
    if (!(std::abs(y) <= kExpLimit)) return std::pow(x, e);
    return lanemath::exp(y);
}

#if defined(__aarch64__)
// The two lanes at once, lane by lane identical to the forms above.
inline D2 log(D2 x) noexcept
{
    const float64x2_t v = x.v;
    const uint64x2_t inRange = vandq_u64(vcgeq_f64(v, vdupq_n_f64(kLogLow)), vcleq_f64(v, vdupq_n_f64(kLogHigh)));
    if (vminvq_u32(vreinterpretq_u32_u64(inRange)) == 0)
        return {float64x2_t{lanemath::log(vgetq_lane_f64(v, 0)), lanemath::log(vgetq_lane_f64(v, 1))}};
    const uint64x2_t ix = vreinterpretq_u64_f64(v);
    const uint64x2_t tmp = vsubq_u64(ix, vdupq_n_u64(kOff));
    const uint64x2_t index = vshrq_n_u64(vshlq_n_u64(tmp, 12), 57);   // (tmp >> 45) & 127
    const LogEntry& e0 = kLogTable[vgetq_lane_u64(index, 0)];
    const LogEntry& e1 = kLogTable[vgetq_lane_u64(index, 1)];
    const float64x2_t a0 = vld1q_f64(&e0.c), a1 = vld1q_f64(&e1.c);   // {c, 1 / c}
    const float64x2_t c = vzip1q_f64(a0, a1), invc = vzip2q_f64(a0, a1);
    const float64x2_t logc = vld1q_lane_f64(&e1.logc, vld1q_dup_f64(&e0.logc), 1);
    const float64x2_t kd = vcvtq_f64_s64(vshrq_n_s64(vreinterpretq_s64_u64(tmp), 52));
    const float64x2_t z = vreinterpretq_f64_u64(vsubq_u64(ix, vandq_u64(tmp, vdupq_n_u64(0xfffull << 52))));
    const float64x2_t r = vmulq_f64(vsubq_f64(z, c), invc);
    const float64x2_t r2 = vmulq_f64(r, r);
    const float64x2_t q = vaddq_f64(vaddq_f64(vdupq_n_f64(kA2), vmulq_f64(r, vdupq_n_f64(kA3))),
        vmulq_f64(r2, vaddq_f64(vaddq_f64(vdupq_n_f64(kA4), vmulq_f64(r, vdupq_n_f64(kA5))),
                                vmulq_f64(r2, vaddq_f64(vdupq_n_f64(kA6), vmulq_f64(r, vdupq_n_f64(kA7)))))));
    const float64x2_t hi = vaddq_f64(vmulq_f64(kd, vdupq_n_f64(kLn2Hi)), logc);
    return {vaddq_f64(hi, vaddq_f64(r, vaddq_f64(vmulq_f64(r2, q), vmulq_f64(kd, vdupq_n_f64(kLn2Lo)))))};
}

inline D2 exp(D2 y) noexcept
{
    const float64x2_t v = y.v;
    const uint64x2_t inRange = vcleq_f64(vabsq_f64(v), vdupq_n_f64(kExpLimit));
    if (vminvq_u32(vreinterpretq_u32_u64(inRange)) == 0)
        return {float64x2_t{lanemath::exp(vgetq_lane_f64(v, 0)), lanemath::exp(vgetq_lane_f64(v, 1))}};
    const float64x2_t kd = vrndnq_f64(vmulq_f64(v, vdupq_n_f64(kInvLn2N)));
    const int64x2_t k = vcvtq_s64_f64(kd);
    const float64x2_t r = vsubq_f64(vsubq_f64(v, vmulq_f64(kd, vdupq_n_f64(kLn2NHi))), vmulq_f64(kd, vdupq_n_f64(kLn2NLo)));
    const float64x2_t r2 = vmulq_f64(r, r);
    const float64x2_t p = vaddq_f64(r, vmulq_f64(r2,
        vaddq_f64(vaddq_f64(vdupq_n_f64(kE2), vmulq_f64(r, vdupq_n_f64(kE3))),
                  vmulq_f64(r2, vaddq_f64(vdupq_n_f64(kE4), vmulq_f64(r, vdupq_n_f64(kE5)))))));
    const uint64x2_t j = vandq_u64(vreinterpretq_u64_s64(k), vdupq_n_u64(127));
    const float64x2_t t = vld1q_lane_f64(&kExpTable[vgetq_lane_u64(j, 1)],
                                         vld1q_dup_f64(&kExpTable[vgetq_lane_u64(j, 0)]), 1);
    const float64x2_t scaled = vreinterpretq_f64_u64(vaddq_u64(vreinterpretq_u64_f64(t),
        vreinterpretq_u64_s64(vshlq_n_s64(vshrq_n_s64(k, 7), 52))));
    return {vaddq_f64(scaled, vmulq_f64(scaled, p))};
}

inline D2 pow(D2 x, double e) noexcept
{
    const float64x2_t v = x.v;
    const uint64x2_t inRange = vandq_u64(vcgeq_f64(v, vdupq_n_f64(kLogLow)), vcleq_f64(v, vdupq_n_f64(kLogHigh)));
    if (vminvq_u32(vreinterpretq_u32_u64(inRange)) == 0)
        return {float64x2_t{lanemath::pow(vgetq_lane_f64(v, 0), e), lanemath::pow(vgetq_lane_f64(v, 1), e)}};
    const float64x2_t y = vmulq_f64(vdupq_n_f64(e), lanemath::log(x).v);
    const uint64x2_t yInRange = vcleq_f64(vabsq_f64(y), vdupq_n_f64(kExpLimit));
    if (vminvq_u32(vreinterpretq_u32_u64(yInRange)) == 0)
        return {float64x2_t{lanemath::pow(vgetq_lane_f64(v, 0), e), lanemath::pow(vgetq_lane_f64(v, 1), e)}};
    return lanemath::exp(D2{y});
}
#endif
// x^e for one exponent fixed ahead (setExponent), the two lanes of a D2 bit for bit as one value: x = 2^k m with
// m in [1, 2) gives x^e = 2^(k e) c^e (1 + r)^e, c the centre of m's 1/256 interval and r = (m - c) / c (|r| <= 1/512,
// the subtraction exact), 2^(k e) (k in [-64, 63]) and c^e from tables, (1 + r)^e to r^5 (truncation under 2e-18).
// Within 4 ulp of the library (2e7 arguments over e^+-40, 2026-10-21) at about 2.5 x pow()'s throughput. Outside
// that range of x, x <= 0 or not finite: pow() above.
class FixedPower
{
public:
    static constexpr int kMinK = -64, kMaxK = 63;
    struct Entry { double c, invc, ce, pad; };

    void setExponent(double exponent) noexcept
    {
        e = exponent;
        // 2^(k e) with k e split so the integer part is exact: k (e_hi + e_lo), e_hi with 40 significant bits.
        double eHi = e;
        {
            std::uint64_t b; std::memcpy(&b, &eHi, 8); b &= ~((1ull << 13) - 1); std::memcpy(&eHi, &b, 8);
        }
        const double eLo = e - eHi;
        for (int k = kMinK; k <= kMaxK; ++k)
        {
            const double p = static_cast<double>(k) * eHi;          // exact
            const double whole = std::floor(p);
            twoPow[static_cast<size_t>(k - kMinK)] =
                std::ldexp(std::exp2(p - whole) * std::exp2(static_cast<double>(k) * eLo), static_cast<int>(whole));
        }
        for (int i = 0; i < 256; ++i)
        {
            const double lo = 1.0 + i / 256.0, hi = 1.0 + (i + 1) / 256.0;
            const double c = 0.5 * (lo + hi);
            table[static_cast<size_t>(i)] = Entry{c, 1.0 / c, std::pow(c, e), 0.0};
        }
        c1 = e;
        c2 = e * (e - 1.0) / 2.0;
        c3 = c2 * (e - 2.0) / 3.0;
        c4 = c3 * (e - 3.0) / 4.0;
        c5 = c4 * (e - 4.0) / 5.0;
    }

    double operator()(double x) const noexcept
    {
        std::uint64_t bits; std::memcpy(&bits, &x, 8);
        const int expo = static_cast<int>((bits >> 52) & 0x7ffu) - 1023;
        if (!(x > 0.0) || expo < kMinK || expo > kMaxK) return pow(x, e);
        const Entry& t = table[(bits >> 44) & 255u];
        const std::uint64_t mbits = (bits & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
        double m; std::memcpy(&m, &mbits, 8);
        const double r = (m - t.c) * t.invc;
        const double poly = 1.0 + r * (c1 + r * (c2 + r * (c3 + r * (c4 + r * c5))));
        return twoPow[static_cast<size_t>(expo - kMinK)] * (t.ce * poly);
    }

#if defined(__aarch64__)
    D2 operator()(D2 x) const noexcept
    {
        const float64x2_t v = x.v;
        const uint64x2_t bits = vreinterpretq_u64_f64(v);
        const int64x2_t expo = vsubq_s64(vreinterpretq_s64_u64(vandq_u64(vshrq_n_u64(bits, 52), vdupq_n_u64(0x7ff))),
                                         vdupq_n_s64(1023));
        const uint64x2_t ok = vandq_u64(vcgtq_f64(v, vdupq_n_f64(0.0)),
            vandq_u64(vcgeq_s64(expo, vdupq_n_s64(kMinK)), vcleq_s64(expo, vdupq_n_s64(kMaxK))));
        if (vminvq_u32(vreinterpretq_u32_u64(ok)) == 0)
            return {float64x2_t{(*this)(vgetq_lane_f64(v, 0)), (*this)(vgetq_lane_f64(v, 1))}};
        const uint64x2_t index = vandq_u64(vshrq_n_u64(bits, 44), vdupq_n_u64(255));
        const Entry& t0 = table[vgetq_lane_u64(index, 0)];
        const Entry& t1 = table[vgetq_lane_u64(index, 1)];
        const float64x2_t a0 = vld1q_f64(&t0.c), a1 = vld1q_f64(&t1.c);   // {c, 1 / c}
        const float64x2_t c = vzip1q_f64(a0, a1), invc = vzip2q_f64(a0, a1);
        const float64x2_t ce = vld1q_lane_f64(&t1.ce, vld1q_dup_f64(&t0.ce), 1);
        const float64x2_t scale = vld1q_lane_f64(&twoPow[static_cast<size_t>(vgetq_lane_s64(expo, 1) - kMinK)],
            vld1q_dup_f64(&twoPow[static_cast<size_t>(vgetq_lane_s64(expo, 0) - kMinK)]), 1);
        const float64x2_t m = vreinterpretq_f64_u64(vorrq_u64(vandq_u64(bits, vdupq_n_u64(0x000fffffffffffffull)),
                                                              vdupq_n_u64(0x3ff0000000000000ull)));
        const float64x2_t r = vmulq_f64(vsubq_f64(m, c), invc);
        float64x2_t poly = vaddq_f64(vdupq_n_f64(c4), vmulq_f64(r, vdupq_n_f64(c5)));
        poly = vaddq_f64(vdupq_n_f64(c3), vmulq_f64(r, poly));
        poly = vaddq_f64(vdupq_n_f64(c2), vmulq_f64(r, poly));
        poly = vaddq_f64(vdupq_n_f64(c1), vmulq_f64(r, poly));
        poly = vaddq_f64(vdupq_n_f64(1.0), vmulq_f64(r, poly));
        return {vmulq_f64(scale, vmulq_f64(ce, poly))};
    }
#endif

private:
    double e = 1.0, c1 = 1.0, c2 = 0.0, c3 = 0.0, c4 = 0.0, c5 = 0.0;
    std::array<double, kMaxK - kMinK + 1> twoPow{};
    alignas(16) std::array<Entry, 256> table{};
};
} // namespace duskaudio::lanes::lanemath
