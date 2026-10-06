import numpy as np
import sounddevice as sd
import matplotlib.pyplot as plt
from scipy.signal import correlate


# ============================================================
# Configuration
# ============================================================

FS = 48000
CHANNELS = 2

BLOCK_SIZE = 512

DISPLAY_SECONDS = 5.0

BUFFER_SIZE = int(FS * DISPLAY_SECONDS)

# Maximum microphone spacing
# Used to constrain the possible time delay.
MIC_DISTANCE = 0.20       # meters

SPEED_OF_SOUND = 343.0    # m/s

MAX_TDOA = MIC_DISTANCE / SPEED_OF_SOUND

MAX_TDOA_SAMPLES = int(
    MAX_TDOA * FS
)


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

    audio_buffer[:-frames] = audio_buffer[frames:]

    audio_buffer[-frames:] = indata


# ============================================================
# Cross-correlation
# ============================================================

def estimate_tdoa(x_left, x_right):

    # Remove DC
    x_left = x_left - np.mean(x_left)
    x_right = x_right - np.mean(x_right)

    # Cross correlation
    correlation = correlate(
        x_left,
        x_right,
        mode="full"
    )

    # Lag axis
    lags = np.arange(
        -len(x_left) + 1,
        len(x_left)
    )

    # Only search physically possible delays
    mask = (
        np.abs(lags)
        <= MAX_TDOA_SAMPLES
    )

    correlation_search = correlation[mask]
    lags_search = lags[mask]

    # Maximum correlation
    index = np.argmax(
        np.abs(correlation_search)
    )

    best_lag = lags_search[index]

    # Convert samples -> seconds
    tdoa = best_lag / FS

    return (
        tdoa,
        best_lag,
        correlation,
        lags
    )


# ============================================================
# ILD from time-domain RMS
# ============================================================

def calculate_ild(x_left, x_right):

    rms_left = np.sqrt(
        np.mean(x_left ** 2)
    )

    rms_right = np.sqrt(
        np.mean(x_right ** 2)
    )

    ild = 20 * np.log10(
        (rms_left + 1e-12)
        /
        (rms_right + 1e-12)
    )

    return ild


# ============================================================
# Start microphone
# ============================================================

stream = sd.InputStream(
    samplerate=FS,
    channels=CHANNELS,
    blocksize=BLOCK_SIZE,
    dtype="float32",
    callback=audio_callback
)

stream.start()


# ============================================================
# Create figure
# ============================================================

plt.ion()

fig, axes = plt.subplots(
    3,
    1,
    figsize=(12, 9)
)

fig.suptitle(
    "Two-Microphone Time-Domain Analyzer"
)


# ============================================================
# Initial plots
# ============================================================

time_axis = (
    np.arange(BUFFER_SIZE)
    / FS
)

line_left, = axes[0].plot(
    time_axis,
    audio_buffer[:, 0]
)

line_right, = axes[0].plot(
    time_axis,
    audio_buffer[:, 1]
)

axes[0].set_title(
    "Microphone signals"
)

axes[0].set_xlabel(
    "Time [s]"
)

axes[0].set_ylabel(
    "Amplitude"
)

axes[0].set_xlim(
    0,
    DISPLAY_SECONDS
)


# ------------------------------------------------------------
# Cross-correlation
# ------------------------------------------------------------

x = audio_buffer.copy()

tdoa, best_lag, correlation, lags = estimate_tdoa(
    x[:, 0],
    x[:, 1]
)

lag_time = lags / FS

line_corr, = axes[1].plot(
    lag_time * 1000,
    correlation
)

axes[1].set_title(
    "Cross-correlation"
)

axes[1].set_xlabel(
    "Delay [ms]"
)

axes[1].set_ylabel(
    "Correlation"
)

axes[1].set_xlim(
    -MAX_TDOA * 1000,
    MAX_TDOA * 1000
)


# ------------------------------------------------------------
# Correlation peak
# ------------------------------------------------------------

peak_line = axes[1].axvline(
    best_lag / FS * 1000,
    linestyle="--"
)


# ------------------------------------------------------------
# Text information
# ------------------------------------------------------------

info_text = axes[2].text(
    0.5,
    0.5,
    "",
    transform=axes[2].transAxes,
    ha="center",
    va="center",
    fontsize=16
)

axes[2].axis("off")


plt.tight_layout()


# ============================================================
# Real-time processing
# ============================================================

try:

    while plt.fignum_exists(fig.number):

        # ----------------------------------------------------
        # Copy current microphone data
        # ----------------------------------------------------

        x = audio_buffer.copy()

        x_left = x[:, 0]
        x_right = x[:, 1]


        # ----------------------------------------------------
        # Update waveform
        # ----------------------------------------------------

        line_left.set_ydata(
            x_left
        )

        line_right.set_ydata(
            x_right
        )


        # ----------------------------------------------------
        # Calculate TDOA
        # ----------------------------------------------------

        (
            tdoa,
            best_lag,
            correlation,
            lags
        ) = estimate_tdoa(
            x_left,
            x_right
        )


        # ----------------------------------------------------
        # Calculate ILD
        # ----------------------------------------------------

        ild = calculate_ild(
            x_left,
            x_right
        )


        # ----------------------------------------------------
        # Update correlation
        # ----------------------------------------------------

        line_corr.set_ydata(
            correlation
        )

        line_corr.set_xdata(
            lags / FS * 1000
        )


        # ----------------------------------------------------
        # Move peak marker
        # ----------------------------------------------------

        peak_line.set_xdata(
            [tdoa * 1000, tdoa * 1000]
        )


        # ----------------------------------------------------
        # Calculate path difference
        # ----------------------------------------------------

        path_difference = (
            SPEED_OF_SOUND * tdoa
        )


        # ----------------------------------------------------
        # Calculate angle
        # ----------------------------------------------------

        argument = (
            SPEED_OF_SOUND * tdoa
            /
            MIC_DISTANCE
        )

        # Numerical protection
        argument = np.clip(
            argument,
            -1.0,
            1.0
        )

        angle = np.degrees(
            np.arcsin(argument)
        )


        # ----------------------------------------------------
        # Display information
        # ----------------------------------------------------

        info_text.set_text(
            f"TDOA:             {tdoa * 1e6:+.1f} µs\n"
            f"Delay:            {best_lag:+d} samples\n"
            f"Path difference:  {path_difference * 100:+.2f} cm\n"
            f"ILD:              {ild:+.2f} dB\n"
            f"Estimated angle:  {angle:+.1f}°"
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