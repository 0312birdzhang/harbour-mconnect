#!/usr/bin/env python3
"""Real PulseAudio test using only digital silence; restores the original step.

Keep regular music paused. Run as the Sailfish session user with the native
library path. Tests the system media step at zero/low/zero and app gain changes.
"""
import ctypes as c
import dbus
import json
from pathlib import Path
import tempfile
import time
import wave
import sys

lib=c.CDLL(str(Path(sys.argv[1]).resolve()))
def bind(name, result, *args):
    fn=getattr(lib,name); fn.restype=result; fn.argtypes=list(args); return fn
create=bind('mc_audio_create',c.c_void_p)
destroy=bind('mc_audio_destroy',None,c.c_void_p)
open_audio=bind('mc_audio_open',c.c_int,c.c_void_p,c.c_char_p)
state=bind('mc_audio_state',c.c_int,c.c_void_p,c.c_int)
volume=bind('mc_audio_volume',None,c.c_void_p,c.c_double)
connection=dbus.connection.Connection('unix:path=/run/user/100000/pulse/dbus-socket')
def props(path):
    return dbus.Interface(connection.get_object(None,path),'org.freedesktop.DBus.Properties')
main=props('/com/meego/mainvolume2')
iface='com.Meego.MainVolume2'
original=int(main.Get(iface,'CurrentStep'))
core=props('/org/pulseaudio/core1')
before=set(core.Get('org.PulseAudio.Core1','PlaybackStreams'))
player=create(); assert player
try:
    with tempfile.TemporaryDirectory(prefix='mconnect-volume-') as directory:
        path=Path(directory)/'digital-zero.wav'
        with wave.open(str(path),'wb') as out:
            out.setparams((1,2,16000,0,'NONE','not compressed'))
            out.writeframes(bytes(16000*2*30))
        assert open_audio(player,path.as_uri().encode())
        assert state(player,2)
        time.sleep(1)
        streams=set(core.Get('org.PulseAudio.Core1','PlaybackStreams'))-before
        assert len(streams)==1, streams
        stream=props(next(iter(streams)))
        def read():
            values=stream.GetAll('org.PulseAudio.Core1.Stream')
            properties={str(k):bytes(v).rstrip(b'\0').decode(errors='replace') for k,v in values['PropertyList'].items()}
            return {'volume':[int(v) for v in values['Volume']], 'role':properties.get('media.role'), 'restore':properties.get('module-stream-restore.id')}
        results=[]
        for step in (0,2,0):
            main.Set(iface,'CurrentStep',dbus.UInt32(step))
            time.sleep(.7)
            baseline=read()
            assert baseline['role']=='x-maemo',baseline
            assert baseline['restore']=='sink-input-by-media-role:x-maemo',baseline
            if step==0: assert max(baseline['volume'])==0,baseline
            else: assert max(baseline['volume'])>0,baseline
            for gain in (0.0,.1,.5,1.0):
                volume(player,gain); time.sleep(.15)
                current=read()
                assert current['volume']==baseline['volume'],(step,gain,baseline,current)
            results.append({'step':step,**baseline,'gain_changes_preserve_system_volume':True})
        print(json.dumps({'result':'PASS','source':'digital-zero PCM only','checks':results,'original_step':original},indent=2))
finally:
    state(player,0)
    destroy(player)
    main.Set(iface,'CurrentStep',dbus.UInt32(original))
