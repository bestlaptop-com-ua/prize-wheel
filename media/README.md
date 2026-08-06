# Party audio

Run `python media/generate_mp3.py` from the repository root after installing
`python -m pip install -r media/requirements.txt`. The generator creates six
original, deterministic, 44.1 kHz mono MP3 files in `media/mp3/`.

Copy the complete `media/mp3` directory to `/mp3` on a FAT32-formatted
microSD card so the DFPlayer sees `/mp3/0001.mp3` through `/mp3/0006.mp3`.
The two three-second loop sources are periodic at their file boundaries, but
some DFPlayer clones add a decoder pause; verify the ratchet seam by ear.
