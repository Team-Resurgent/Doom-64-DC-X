#!/usr/bin/env python3
"""Hold the music in audio_test's output to a direct decode of the file.

The Dreamcast plays a Doom 64 track (data/mus/*.adpcm, stereo Yamaha ADPCM,
44.1 kHz, the channels interleaved a nibble at a time: low nibble right,
high nibble left, as KOS's snd_adpcm_split takes them) through two AICA
channels panned hard left and right. This decodes the file the same way and
finds, for each output channel, where the decode starts in it, how loud it
is, and every sample that differs.

    python tools/xbox/hosttest/audio_check.py <track.adpcm> <out.wav>
"""
import struct
import sys

STEP = [230, 230, 230, 230, 307, 409, 512, 614]


def decode(nibbles):
    pred, scale, out = 0, 127, []
    for code in nibbles:
        m = code & 7
        move = ((2 * m + 1) * scale) >> 3
        pred = pred - move if code & 8 else pred + move
        pred = max(-32768, min(32767, pred))
        scale = max(127, min(24576, (scale * STEP[m]) >> 8))
        out.append(pred)
    return out


def read_wav(path):
    d = open(path, "rb").read()
    p = 12
    while p < len(d):
        cid, n = d[p:p + 4], struct.unpack("<I", d[p + 4:p + 8])[0]
        if cid == b"data":
            pcm = d[p + 8:p + 8 + n]
            break
        p += 8 + n
    s = struct.unpack("<%dh" % (len(pcm) // 2), pcm)
    return list(s[0::2]), list(s[1::2])


def align(ref, got, maxlag=44100):
    """The lag at which `ref` best matches `got` (first loud stretch)."""
    start = next(i for i, v in enumerate(ref) if abs(v) > 2000)
    probe = ref[start:start + 64]
    best, lag = None, None
    for L in range(0, maxlag):
        j = start + L
        if j + 64 > len(got):
            break
        err = sum(abs(a - b) for a, b in zip(probe, got[j:j + 64]))
        if best is None or err < best:
            best, lag = err, L
            if err == 0:
                break
    return lag


def main():
    raw = open(sys.argv[1], "rb").read()
    n = min(len(raw), 44100 * 40)
    right = decode([b & 15 for b in raw[:n]])
    left = decode([b >> 4 for b in raw[:n]])
    gl, gr = read_wav(sys.argv[2])
    for name, ref, got in (("left", left, gl), ("right", right, gr)):
        lag = align(ref, got)
        pairs = [(ref[i], got[i + lag]) for i in range(len(got) - lag) if i < len(ref)]
        loud = [(a, b) for a, b in pairs if abs(a) > 1000]
        gain = sum(b / a for a, b in loud) / len(loud) if loud else 0
        bad = [i for i, (a, b) in enumerate(pairs) if abs(b - a * gain) > max(64, abs(a) * 0.02)]
        print("%s: decode starts at output sample %d, gain %.4f, %d of %d samples off"
              % (name, lag, gain, len(bad), len(pairs)))
        if bad:
            runs, s0 = [], bad[0]
            for a, b in zip(bad, bad[1:] + [None]):
                if b != a + 1:
                    runs.append((s0, a))
                    s0 = b
            print("   first off-runs (track sample, length): %s" %
                  ", ".join("%d+%d" % (r0, r1 - r0 + 1) for r0, r1 in runs[:12]))
    # the two channels against each other, in time
    print("left/right lag difference:", align(left, gl) - align(right, gr))


if __name__ == "__main__":
    main()
