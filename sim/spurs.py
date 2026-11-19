# How clean a pure tone is at 48 kHz: the energy outside the tone (and its
# neighbouring bins), relative to the tone, for each s16le mono file.
import sys
import numpy as np
for path in sys.argv[1:]:
    x = np.fromfile(path, dtype=np.int16).astype(float)
    x -= x.mean()
    X = np.abs(np.fft.rfft(x * np.blackman(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / 48000)
    k = int(np.argmax(X))
    tone = X[k - 8:k + 9].sum()
    rest = X.sum() - tone
    print(f"{path}: tone {f[k]:.0f} Hz, everything else {10 * np.log10(rest / tone):.1f} dB")
