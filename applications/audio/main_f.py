import numpy as np
import sounddevice as sd
import matplotlib.pyplot as plt

from scipy.signal import stft
from scipy.fft import fft, ifft


# ============================================================
# Configuration
# ============================================================

FS = 48000

CHANNELS = 2

BLOCK_SIZE = 512

# STFT parameters
N_FFT = 2048
HOP = 512

# How much history to display
DISPLAY_SECONDS = 5

# Frequency range shown in spectrograms
MAX_FREQ = 10000

# Maximum microphone spacing we expect
# Used to define the possible TDOA range.
MAX_MIC_DISTANCE = 0.30       # meters

# Speed of sound
SPEED_OF_SOUND = 343.0        # m/s


BUFFER_SIZE = int(FS * DISPLAY_SECONDS)


# ============================================================
# Rolling audio buffer
# ============================================================

audio_buffer = np.zeros(
    (BUFFER_SIZE, CHANNELS),
    dtype=np.float32
)


# ============================================================
# Audio callback
# ============================================================

def audio_callback(indata, frames, time, status):

    global audio_buffer

    if status:
        print(status)

    # Shift old samples toward the beginning
    audio_buffer[:-frames] = audio_buffer[frames:]

    # Append new samples
    audio_buffer[-frames:] = indata


# ============================================================
# Start microphone
# ============================================================

stream = sd.InputStream(samplerate=FS, channels=CHANNELS, blocksize=BLOCK_SIZE, dtype="float32", callback=audio_callback)
stream.start()


# ============================================================
# Create figure
# ============================================================

plt.ion()

fig, axes = plt.subplots(4, 1, figsize=(13, 12))

fig.suptitle("Real-Time Stereo Spatial Audio Analyzer")


# ============================================================
# Helper function: STFT
# ============================================================

def calculate_stft(x):

    f, t, Z = stft(x, fs=FS, window="hann", nperseg=N_FFT, noverlap=N_FFT - HOP, boundary=None)

    return f, t, Z


# ============================================================
# Initial calculation
# ============================================================

x = audio_buffer.copy()

f, t, XL = calculate_stft(x[:, 0])
_, _, XR = calculate_stft(x[:, 1])


# Frequency mask

freq_mask = f <= MAX_FREQ

f_display = f[freq_mask]


# Magnitudes

mag_L = np.abs(XL[freq_mask, :])
mag_R = np.abs(XR[freq_mask, :])


# dB
db_L = 20 * np.log10(mag_L + 1e-10)
db_R = 20 * np.log10(mag_R + 1e-10)


# ============================================================
# Create initial spectrograms
# ============================================================

image_L = axes[0].pcolormesh(t, f_display, db_L, shading="auto")
image_R = axes[1].pcolormesh(t, f_display, db_R, shading="auto")


# ============================================================
# IPD
# ============================================================

cross_spectrum = (XL[freq_mask, :]*np.conj(XR[freq_mask, :]))

IPD = np.angle(cross_spectrum)

image_IPD = axes[2].pcolormesh(t, f_display, IPD, shading="auto", vmin=-np.pi, vmax=np.pi)


# ============================================================
# ILD
# ============================================================

ILD = 20 * np.log10((mag_L + 1e-10)/(mag_R + 1e-10))

image_ILD = axes[3].pcolormesh(t, f_display, ILD, shading="auto")


# ============================================================
# Plot configuration
# ============================================================

axes[0].set_title("Left microphone STFT")
axes[1].set_title("Right microphone STFT")
axes[2].set_title("Inter-channel Phase Difference (IPD)")
axes[3].set_title("Inter-channel Level Difference (ILD)")


for ax in axes:
    ax.set_ylim(0, MAX_FREQ)
    ax.set_xlim(0, DISPLAY_SECONDS)


axes[0].set_ylabel("Frequency [Hz]")
axes[1].set_ylabel("Frequency [Hz]")
axes[2].set_ylabel("Frequency [Hz]")
axes[3].set_ylabel("Frequency [Hz]")
axes[3].set_xlabel("Time [s]")

fig.colorbar(image_L, ax=axes[0], label="Magnitude [dB]")
fig.colorbar(image_R, ax=axes[1], label="Magnitude [dB]")
fig.colorbar(image_IPD, ax=axes[2], label="Phase [rad]")
fig.colorbar(image_ILD, ax=axes[3], label="Level difference [dB]")

plt.tight_layout()

# ============================================================
# GCC-PHAT
# ============================================================

def gcc_phat(x, y, fs, max_tau=None):

    """
    GCC-PHAT time-delay estimation.

    Returns:

        tau      estimated delay [seconds]
        cc       cross-correlation
        lags     lag axis [seconds]
    """

    n = len(x)

    # FFT

    X = fft(x)
    Y = fft(y)

    # Cross spectrum

    R = X * np.conj(Y)

    # PHAT normalization

    R /= (
        np.abs(R) + 1e-12
    )

    # GCC-PHAT correlation

    cc = np.real(
        ifft(R)
    )

    # Rearrange correlation so zero lag is in center

    cc = np.concatenate(
        (
            cc[-(n // 2):],
            cc[:n // 2]
        )
    )

    # Lag axis

    lags = (
        np.arange(-n // 2, n // 2)
        / fs
    )

    # Limit physically possible delays

    if max_tau is not None:

        mask = np.abs(lags) <= max_tau

        cc_search = cc[mask]
        lags_search = lags[mask]

    else:

        cc_search = cc
        lags_search = lags

    # Find maximum correlation

    index = np.argmax(
        np.abs(cc_search)
    )

    tau = lags_search[index]

    return tau, cc, lags


# ============================================================
# Real-time loop
# ============================================================

try:

    while plt.fignum_exists(fig.number):

        # ----------------------------------------------------
        # Copy current audio buffer
        # ----------------------------------------------------

        x = audio_buffer.copy()


        # ----------------------------------------------------
        # STFT
        # ----------------------------------------------------

        f, t, XL = calculate_stft(x[:, 0])

        _, _, XR = calculate_stft(x[:, 1])


        # ----------------------------------------------------
        # Frequency mask
        # ----------------------------------------------------

        freq_mask = (f <= MAX_FREQ)

        f_display = f[freq_mask]


        # ----------------------------------------------------
        # Magnitude
        # ----------------------------------------------------

        mag_L = np.abs(XL[freq_mask, :])

        mag_R = np.abs(XR[freq_mask, :])


        # ----------------------------------------------------
        # dB
        # ----------------------------------------------------

        db_L = 20 * np.log10(mag_L + 1e-10)

        db_R = 20 * np.log10(mag_R + 1e-10)


        # ----------------------------------------------------
        # Cross spectrum
        #
        # G_LR(f,t) =
        #       XL(f,t) * conj(XR(f,t))
        # ----------------------------------------------------

        cross_spectrum = (XL[freq_mask, :]*np.conj(XR[freq_mask, :]))


        # ----------------------------------------------------
        # Inter-channel phase difference
        #
        # IPD = angle(XL * conj(XR))
        # ----------------------------------------------------

        IPD = np.angle(cross_spectrum)


        # ----------------------------------------------------
        # Inter-channel level difference
        #
        # ILD = 20 log10(|XL| / |XR|)
        # ----------------------------------------------------

        ILD = 20 * np.log10((mag_L + 1e-10)/(mag_R + 1e-10))


        # ----------------------------------------------------
        # Update spectrograms
        # ----------------------------------------------------

        image_L.set_array(db_L.ravel())
        image_R.set_array(db_R.ravel())
        image_IPD.set_array(IPD.ravel())
        image_ILD.set_array(ILD.ravel())


        # ----------------------------------------------------
        # GCC-PHAT
        #
        # Use the complete time-domain buffer.
        # ----------------------------------------------------

        tau, cc, lags = gcc_phat(x[:, 0], x[:, 1], FS, max_tau=(MAX_MIC_DISTANCE/SPEED_OF_SOUND))


        # ----------------------------------------------------
        # Convert TDOA to distance difference
        # ----------------------------------------------------

        distance_difference = (tau * SPEED_OF_SOUND)


        # ----------------------------------------------------
        # Update figure title
        # ----------------------------------------------------

        fig.suptitle(
            "Real-Time Stereo Spatial Audio Analyzer\n"
            f"TDOA = {tau * 1e6:+.1f} µs    "
            f"Path difference = "
            f"{distance_difference * 100:+.1f} cm"
        )


        # ----------------------------------------------------
        # Redraw
        # ----------------------------------------------------

        fig.canvas.draw_idle()

        fig.canvas.flush_events()

        plt.pause(0.01)


finally:

    stream.stop()
    stream.close()

    plt.close(fig)