import miniaudio, pathlib, struct
SRC = pathlib.Path(r'C:\Users\Mill\Desktop\prize-wheel\media\mp3')
OUT = pathlib.Path(r'C:\Users\Mill\Desktop\prize-wheel\work\playlist_20260922\playlist\samples.h')
names = {1:'tick',2:'ratchet',3:'drumroll',4:'fanfare',5:'ambience',6:'guest'}
SR = 22050
lines = ['// auto-generated: 16-bit mono PCM @ %d Hz from media/mp3' % SR, '#pragma once', '#include <stdint.h>', '#define SAMPLE_RATE %d' % SR]
table = []
total = 0
for n, name in names.items():
    dec = miniaudio.decode_file(str(SRC / f'{n:04d}.mp3'), output_format=miniaudio.SampleFormat.SIGNED16, nchannels=1, sample_rate=SR)
    pcm = dec.samples  # array('h')
    total += len(pcm) * 2
    print(f'{n} {name}: {len(pcm)/SR:.2f} s, {len(pcm)*2} bytes')
    lines.append(f'static const int16_t pcm_{name}[{len(pcm)}] = {{')
    for i in range(0, len(pcm), 32):
        lines.append(','.join(str(v) for v in pcm[i:i+32]) + ',')
    lines.append('};')
    table.append((name, len(pcm)))
lines.append('struct Sample { const char* name; const int16_t* data; uint32_t n; };')
lines.append('static const Sample SAMPLES[] = {')
for name, n in table:
    lines.append(f'  {{"{name}", pcm_{name}, {n}}},')
lines.append('};')
lines.append('#define NUM_SAMPLES %d' % len(table))
OUT.write_text('\n'.join(lines))
print('total PCM bytes', total)
