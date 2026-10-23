#!/usr/bin/env python3
"""Device-only GStreamer smoke test. Uses fakesink and never emits sound."""
import ctypes as c
import json
from pathlib import Path
import struct
import sys
import tempfile
import time
import wave

library = c.CDLL(str(Path(sys.argv[1]).resolve()))

def bind(name, result, *arguments):
    function = getattr(library, name)
    function.restype = result
    function.argtypes = list(arguments)
    return function

create = bind('mc_audio_create_with_sink', c.c_void_p, c.c_char_p)
destroy = bind('mc_audio_destroy', None, c.c_void_p)
open_audio = bind('mc_audio_open', c.c_int, c.c_void_p, c.c_char_p)
state = bind('mc_audio_state', c.c_int, c.c_void_p, c.c_int)
status = bind('mc_audio_status', c.c_int, c.c_void_p)
position = bind('mc_audio_position', c.c_int64, c.c_void_p)
duration = bind('mc_audio_duration', c.c_int64, c.c_void_p)
seek = bind('mc_audio_seek', c.c_int, c.c_void_p, c.c_int64)
rate = bind('mc_audio_rate', c.c_int, c.c_void_p, c.c_double)
volume = bind('mc_audio_volume', None, c.c_void_p, c.c_double)
equalizer = bind('mc_audio_equalizer', c.c_int, c.c_void_p, c.POINTER(c.c_double))
error = bind('mc_audio_error', c.c_char_p, c.c_void_p)
player = create(b'fakesink')
assert player, 'Cannot create GStreamer playbin with silent fakesink'

def wait_for(predicate):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        current = status(player)
        if predicate(current): return current
        if current == 5: raise RuntimeError(error(player).decode())
        time.sleep(0.05)
    raise TimeoutError('GStreamer did not reach expected state')

try:
    with tempfile.TemporaryDirectory(prefix='mconnect-audio-') as temporary:
        source = Path(temporary) / 'silent.wav'
        with wave.open(str(source), 'wb') as output:
            output.setparams((1, 2, 16000, 0, 'NONE', 'not compressed'))
            output.writeframes(struct.pack('<h', 0) * 48000)
        assert open_audio(player, source.as_uri().encode()) == 1
        assert wait_for(lambda value: value == 2) == 2
        length = duration(player)
        assert 2900000 <= length <= 3100000, length
        assert state(player, 2) == 1
        wait_for(lambda value: value == 3)
        assert state(player, 1) == 1
        wait_for(lambda value: value == 2)
        assert seek(player, 1000000) == 1
        wait_for(lambda _: abs(position(player) - 1000000) < 200000)
        assert rate(player, 1.25) == 1
        volume(player, 0.1)
        eq = equalizer(player, (c.c_double * 5)(1, 0, -1, 0, 1))
        assert eq == 1, 'GStreamer equalizer unavailable'
        assert state(player, 2) == 1
        wait_for(lambda value: value == 4)
        # Seek after EOS must clear completion so replay works.
        assert seek(player, 0) == 1
        assert state(player, 2) == 1
        wait_for(lambda value: value == 3)
        state(player, 0)
        open_audio(player, b'file:///mconnect-test-file-does-not-exist.wav')
        wait_for(lambda value: value == 5)
        print(json.dumps({'result': 'PASS', 'sink': 'fakesink', 'audible_output': False,
            'duration_us': length, 'play_pause_seek_rate_equalizer': True,
            'eos_and_replay': True, 'error_reporting': error(player).decode()}))
finally:
    destroy(player)
