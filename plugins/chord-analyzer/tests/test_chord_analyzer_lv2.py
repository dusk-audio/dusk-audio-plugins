#!/usr/bin/env python3
"""Exercise the real headless LV2 ABI and optionally compare a frozen binary."""
import argparse
import ctypes as c
import math
import pathlib
import struct

URI = b"https://dusk-audio.github.io/plugins/chord-analyzer-headless"
MAP = c.CFUNCTYPE(c.c_uint32, c.c_void_p, c.c_char_p)


class UridMap(c.Structure):
    _fields_ = [("handle", c.c_void_p), ("map", MAP)]


class Feature(c.Structure):
    _fields_ = [("uri", c.c_char_p), ("data", c.c_void_p)]


class Descriptor(c.Structure):
    pass


INSTANTIATE = c.CFUNCTYPE(c.c_void_p, c.POINTER(Descriptor), c.c_double, c.c_char_p,
                         c.POINTER(c.POINTER(Feature)))
CONNECT = c.CFUNCTYPE(None, c.c_void_p, c.c_uint32, c.c_void_p)
ACTIVATE = c.CFUNCTYPE(None, c.c_void_p)
RUN = c.CFUNCTYPE(None, c.c_void_p, c.c_uint32)
EXTENSION = c.CFUNCTYPE(c.c_void_p, c.c_char_p)
Descriptor._fields_ = [("uri", c.c_char_p), ("instantiate", INSTANTIATE), ("connect", CONNECT),
                      ("activate", ACTIVATE), ("run", RUN), ("deactivate", ACTIVATE),
                      ("cleanup", ACTIVATE), ("extension", EXTENSION)]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def exercise(path):
    library = c.CDLL(str(path.resolve()))
    library.lv2_descriptor.argtypes = [c.c_uint32]
    library.lv2_descriptor.restype = c.POINTER(Descriptor)
    descriptor = library.lv2_descriptor(0)
    require(bool(descriptor) and descriptor.contents.uri == URI, "shipping LV2 URI is preserved")
    require(not library.lv2_descriptor(1), "the wrapper exports exactly one descriptor")
    identifiers = {}

    @MAP
    def map_uri(_handle, uri):
        if uri not in identifiers:
            identifiers[uri] = len(identifiers) + 1
        return identifiers[uri]

    urid_map = UridMap(None, map_uri)
    feature = Feature(b"http://lv2plug.in/ns/ext/urid#map", c.addressof(urid_map))
    features = (c.POINTER(Feature) * 2)(c.pointer(feature), None)
    api = descriptor.contents
    instance = api.instantiate(descriptor, 48000.0, str(path.parent.resolve()).encode(), features)
    require(bool(instance), "real LV2 instance is created")
    active = False
    try:
        midi_type = identifiers[b"http://lv2plug.in/ns/ext/midi#MidiEvent"]
        sequence_type = map_uri(None, b"http://lv2plug.in/ns/ext/atom#Sequence")
        incoming, outgoing = (c.c_uint64 * 1024)(), (c.c_uint64 * 1024)()
        controls = (c.c_float * 19)(*[float("nan")] * 19)
        for index, value in {2: 0, 3: 0, 4: 2, 5: 1, 10: 1}.items():
            controls[index] = value
        api.connect(instance, 0, c.addressof(incoming))
        api.connect(instance, 1, c.addressof(outgoing))
        for index in range(2, 19):
            api.connect(instance, index, c.addressof(controls) + index * c.sizeof(c.c_float))
        api.activate(instance)
        active = True
        trace = []

        def block(events, expected=None):
            encoded = b"".join(struct.pack("<qII3B5x", frame, 3, midi_type, *message)
                               for frame, message in events)
            require(len(encoded) + 16 <= c.sizeof(incoming), "test MIDI fits the host buffer")
            c.memset(c.addressof(incoming), 0, c.sizeof(incoming))
            struct.pack_into("<IIII", incoming, 0, len(encoded) + 8, sequence_type, 0, 0)
            c.memmove(c.addressof(incoming) + 16, encoded, len(encoded))
            struct.pack_into("<II", outgoing, 0, c.sizeof(outgoing) - 8, sequence_type)
            api.run(instance, 512)
            size, atom_type = struct.unpack_from("<II", outgoing)
            require(size == len(encoded) + 8 and atom_type == sequence_type,
                    "MIDI output preserves the complete atom sequence")
            rendered = bytes(outgoing)[16:8 + size]
            require(rendered == encoded, "MIDI output preserves event payloads and exact timestamps")
            outputs = tuple(controls[index] for index in [6, 7, 8, 9, *range(11, 19)])
            require(all(math.isfinite(v) for v in outputs), "all twelve output controls are initialized")
            if expected is not None:
                require(outputs[:4] == expected, f"detected controls: expected {expected}, got {outputs[:4]}")
            trace.append((rendered, outputs))
            return outputs

        block([], (0, 0, 0, 0))
        major = block([(0, (0x90, 60, 100)), (4, (0x90, 64, 90)), (17, (0x90, 67, 80))],
                      (1, 1, 1, 1))
        require(any(major[4:]), "a detected major chord publishes actual suggestions")
        held = block([(3, (0xB0, 64, 127)), (13, (0x80, 60, 0)),
                      (14, (0x80, 64, 0)), (20, (0x80, 67, 0))], (1, 1, 1, 1))
        require(held == major, "sustain retains the detected chord and suggestions")
        block([(2, (0x90, 64, 90)), (7, (0xB0, 64, 0))], (0, 0, 0, 0))
        block([(1, (0x90, 60, 90)), (15, (0x90, 67, 90))], (1, 1, 1, 1))
        controls[2], controls[3], controls[4] = 2, 1, 0
        changed = block([], (1, 1, 1, 1))
        require(changed[4:] != major[4:], "key/level automation refreshes suggestions without a note change")
        block([(0, (0x80, 67, 0)), (99, (0x80, 60, 0)), (300, (0x80, 64, 0))], (0, 0, 0, 0))
        # Beyond the old wrapper's initial 16-note reserve: preserve the current
        # functional result here; callback-allocation removal is a separate step.
        block([(i, (0x90, 32 + i, 90)) for i in range(64)])
        block([(i, (0x80, 32 + i, 0)) for i in range(64)], (0, 0, 0, 0))
        api.deactivate(instance)
        active = False
        api.activate(instance)
        active = True
        block([], (0, 0, 0, 0))
        return trace
    finally:
        if active:
            api.deactivate(instance)
        api.cleanup(instance)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("plugin", type=pathlib.Path)
    parser.add_argument("--reference", type=pathlib.Path)
    args = parser.parse_args()
    actual = exercise(args.plugin)
    if args.reference:
        require(actual == exercise(args.reference), "all MIDI and output controls match the frozen shipping binary")
    print(f"Chord headless LV2: {len(actual)} blocks pass; timestamps, 12 outputs, sustain, key automation, reactivation"
          + ("; frozen-binary match" if args.reference else ""))


if __name__ == "__main__":
    main()
