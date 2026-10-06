"""
Direction of Arrival (DOA) Estimation using Stereo Microphone
==============================================================

Uses GCC-PHAT (Generalized Cross-Correlation with Phase Transform) to estimate
the time difference of arrival (TDOA) between left and right audio channels,
then converts it to an azimuth angle.

Requirements:
    pip install numpy scipy sounddevice matplotlib

Usage:
    python doa_stereo.py [--duration 5] [--sample-rate 48000] [--mic-spacing 0.17]
"""

import argparse
import sys
import threading
import queue
import time
from dataclasses import dataclass
from typing import Optional, Tuple, List

import numpy as np

try:
    import sounddevice as sd
except ImportError:
    print("Warning: sounddevice not installed. Live recording disabled.")
    print("Install with: pip install sounddevice")
    sd = None

try:
    import matplotlib.pyplot as plt
    HAS_MPL = True
except ImportError:
    HAS_MPL = False


# ============================================================================
# Configuration & Data Classes
# ============================================================================

@dataclass
class DOAConfig:
    """Configuration parameters for DOA estimation."""
    sample_rate: int = 48000          # Hz
    mic_spacing_m: float = 0.17       # Distance between mics in meters (~human head width)
    speed_of_sound: float = 343.0     # m/s at ~20°C
    window_size_samples: int = 4096   # Analysis window size
    hop_size_samples: int = 2048      # Hop size between windows
    max_tdoa_samples: int = 256       # Maximum TDOA to search (in samples)
    min_angle_deg: float = -90.0      # Minimum detectable angle
    max_angle_deg: float = 90.0       # Maximum detectable angle
    smoothing_alpha: float = 0.3      # Exponential smoothing factor for angle output
    frequency_band_low: float = 300.0   # Lower bound for bandpass filtering (Hz)
    frequency_band_high: float = 4000.0 # Upper bound for bandpass filtering (Hz)


@dataclass
class DOAResult:
    """Result of a single DOA estimation."""
    tdoa_seconds: float
    tdoa_samples: float
    angle_degrees: float
    confidence: float
    timestamp: float


# ============================================================================
# Signal Preprocessing
# ============================================================================

def bandpass_filter(signal: np.ndarray, fs: int, low_freq: float, high_freq: float,
                    order: int = 4) -> np.ndarray:
    """
    Apply a Butterworth bandpass filter to the signal.
    """
    nyquist = 0.5 * fs
    low = max(1e-6, low_freq / nyquist)
    high = min(0.999, high_freq / nyquist)

    if low >= high:
        raise ValueError(f"Invalid frequency band: {low_freq}-{high_freq} Hz "
                         f"(Nyquist={nyquist} Hz)")

    try:
        sos = butter(order, [low, high], btype='band', output='sos')
        filtered = sosfilt(sos, signal, axis=-1)
        return filtered
    except Exception as e:
        # Fallback if scipy filtering fails due to edge cases
        return signal


def normalize_signal(signal: np.ndarray) -> np.ndarray:
    """Normalize signal to [-1, 1] range."""
    max_val = np.max(np.abs(signal))
    if max_val > 0:
        return signal / max_val
    return signal


def apply_window(signal: np.ndarray, window_type: str = 'hann') -> np.ndarray:
    """Apply a window function to reduce spectral leakage."""
    n = len(signal)
    if window_type == 'hann':
        window = np.hanning(n)
    elif window_type == 'hamming':
        window = np.hamming(n)
    elif window_type == 'blackman':
        window = np.blackman(n)
    else:
        window = np.ones(n)
    return signal * window


# ============================================================================
# GCC-PHAT Core Algorithm
# ============================================================================

def gcc_phat(x1: np.ndarray, x2: np.ndarray, max_lag: int,
             fs: int, epsilon: float = 1e-8) -> Tuple[np.ndarray, np.ndarray]:
    """
    Generalized Cross-Correlation with Phase Transform (GCC-PHAT).
    """
    n = len(x1)

    # Compute FFTs
    X1 = np.fft.rfft(x1, n=2*n)
    X2 = np.fft.rfft(x2, n=2*n)

    # Cross-spectrum
    G = X1 * np.conj(X2)

    # Phase Transform: divide by magnitude (whitening)
    magnitude = np.abs(G) + epsilon
    G_ph = G / magnitude

    # Inverse FFT to get cross-correlation
    cc = np.fft.irfft(G_ph, n=2*n)

    # Extract relevant lags around zero
    lags = np.arange(-max_lag, max_lag + 1)
    indices = lags % (2*n)  # Handle wrap-around
    correlation = cc[indices]

    return lags, correlation


def find_peak_with_interpolation(correlation: np.ndarray, lags: np.ndarray,
                                  sigma: float = 1.5) -> Tuple[float, float]:
    """
    Find the peak of the correlation function with parabolic interpolation.
    """
    # Smooth correlation to reduce noise effects
    smoothed = gaussian_filter1d(correlation, sigma=sigma)

    # Find global maximum
    idx_max = np.argmax(smoothed)

    # Parabolic interpolation for sub-sample accuracy
    if 0 < idx_max < len(smoothed) - 1:
        y_minus = smoothed[idx_max - 1]
        y_zero = smoothed[idx_max]
        y_plus = smoothed[idx_max + 1]

        denominator = y_minus - 2*y_zero + y_plus
        if abs(denominator) > 1e-10:
            delta = 0.5 * (y_minus - y_plus) / denominator
            peak_lag = lags[idx_max] + delta
        else:
            peak_lag = float(lags[idx_max])
    else:
        peak_lag = float(lags[idx_max])

    peak_value = float(smoothed[idx_max])

    return peak_lag, peak_value


def compute_confidence(correlation: np.ndarray, peak_idx: int) -> float:
    """
    Estimate confidence based on the sharpness of the correlation peak.
    """
    peak_height = np.max(correlation)
    if peak_height <= 0:
        return 0.0

    n = len(correlation)
    margin = max(5, n // 10)
    start = max(0, peak_idx - margin)
    end = min(n, peak_idx + margin + 1)

    mask = np.ones(n, dtype=bool)
    mask[start:end] = False

    sidelobe_energy = np.mean(correlation[mask]**2) if np.any(mask) else 0.0

    if sidelobe_energy > 0:
        snr_ratio = peak_height**2 / sidelobe_energy
        confidence = 1.0 - 1.0 / (1.0 + snr_ratio / 10.0)
    else:
        confidence = 1.0

    return np.clip(confidence, 0.0, 1.0)


# ============================================================================
# TDOA to Angle Conversion
# ============================================================================

def tdoa_to_angle(tdoa_seconds: float, mic_spacing_m: float,
                  speed_of_sound: float = 343.0) -> Optional[float]:
    """
    Convert Time Difference of Arrival to azimuth angle.
    """
    argument = speed_of_sound * tdoa_seconds / mic_spacing_m

    if abs(argument) > 1.0:
        return np.sign(argument) * 90.0

    angle_rad = np.arcsin(argument)
    angle_deg = np.degrees(angle_rad)

    return angle_deg


def angle_to_tdoa(angle_degrees: float, mic_spacing_m: float,
                  speed_of_sound: float = 343.0) -> float:
    """Convert azimuth angle back to TDOA in seconds."""
    angle_rad = np.radians(angle_degrees)
    return mic_spacing_m * np.sin(angle_rad) / speed_of_sound


# ============================================================================
# Main DOA Estimator Class
# ============================================================================

class StereoDOAEstimator:
    """
    Real-time Direction of Arrival estimator using a stereo microphone pair.
    """

    def __init__(self, config: Optional[DOAConfig] = None):
        self.config = config or DOAConfig()
        self._smoothed_angle = None
        self._lock = threading.Lock()

    def process_block(self, block_left: np.ndarray, block_right: np.ndarray,
                      timestamp: float = 0.0) -> Optional[DOAResult]:
        """
        Process a stereo audio block and return DOA result.
        """
        cfg = self.config

        # Ensure same length
        min_len = min(len(block_left), len(block_right))
        if min_len < cfg.window_size_samples:
            return None

        block_left = block_left[:min_len].astype(np.float64)
        block_right = block_right[:min_len].astype(np.float64)

        # Bandpass filter both channels
        try:
            left_filtered = bandpass_filter(
                block_left, cfg.sample_rate,
                cfg.frequency_band_low, cfg.frequency_band_high
            )
            right_filtered = bandpass_filter(
                block_right, cfg.sample_rate,
                cfg.frequency_band_low, cfg.frequency_band_high
            )
        except Exception as e:
            print(f"Filtering error: {e}")
            return None

        # Normalize
        left_norm = normalize_signal(left_filtered)
        right_norm = normalize_signal(right_filtered)

        # Use only the last `window_size` samples for analysis
        win = cfg.window_size_samples
        left_win = left_norm[-win:]
        right_win = right_norm[-win:]

        # Apply window function
        left_win = apply_window(left_win, 'hann')
        right_win = apply_window(right_win, 'hann')

        # Compute GCC-PHAT
        lags, correlation = gcc_phat(
            left_win, right_win,
            max_lag=cfg.max_tdoa_samples,
            fs=cfg.sample_rate
        )

        # Find peak with sub-sample interpolation
        peak_lag_samples, peak_value = find_peak_with_interpolation(
            correlation, lags
        )

        # Convert lag to TDOA
        tdoa_seconds = peak_lag_samples / cfg.sample_rate

        # Compute confidence
        peak_idx = np.argmin(np.abs(lags - peak_lag_samples))
        confidence = compute_confidence(correlation, peak_idx)

        # Convert TDOA to angle
        angle_deg = tdoa_to_angle(
            tdoa_seconds, cfg.mic_spacing_m, cfg.speed_of_sound
        )

        if angle_deg is None:
            return None

        # Apply exponential smoothing
        with self._lock:
            if self._smoothed_angle is None:
                self._smoothed_angle = angle_deg
            else:
                alpha = cfg.smoothing_alpha
                self._smoothed_angle = (alpha * angle_deg +
                                        (1 - alpha) * self._smoothed_angle)

            smoothed_angle = self._smoothed_angle

        result = DOAResult(
            tdoa_seconds=tdoa_seconds,
            tdoa_samples=peak_lag_samples,
            angle_degrees=smoothed_angle,
            confidence=confidence,
            timestamp=timestamp
        )

        return result

    def reset(self):
        """Reset internal state."""
        with self._lock:
            self._smoothed_angle = None


# ============================================================================
# Audio Recording Backend
# ============================================================================

class StereoRecorder:
    """
    Records stereo audio from default input device using sounddevice.
    """

    def __init__(self, sample_rate: int, block_size: int, callback):
        self.sample_rate = sample_rate
        self.block_size = block_size
        self.callback = callback
        self.stream = None
        self.running = False
        self._total_samples = 0

    def start(self):
        """Start recording."""
        if sd is None:
            raise RuntimeError("sounddevice library is required for live recording")

        self.running = True
        self._total_samples = 0

        def audio_callback(indata, frames, time_info, status):
            if status:
                print(f"Audio callback status: {status}", file=sys.stderr)

            # indata shape: (frames, channels)
            left = indata[:, 0].copy()
            right = indata[:, 1].copy()

            # FIX: Use wall-clock time via sd.time() instead of accessing CFFI struct fields
            # This avoids AttributeError: 'struct PaStreamCallbackTimeInfo' has no field 'current_time'
            timestamp = sd.time()

            self.callback(left, right, timestamp)

        self.stream = sd.InputStream(
            samplerate=self.sample_rate,
            channels=2,
            blocksize=self.block_size,
            dtype='float32',
            callback=audio_callback
        )
        self.stream.start()
        print("Recording started. Press Ctrl+C to stop.")

    def stop(self):
        """Stop recording."""
        self.running = False
        if self.stream:
            self.stream.stop()
            self.stream.close()
            self.stream = None
        print("Recording stopped.")


# ============================================================================
# Visualization
# ============================================================================

class DOAVisualizer:
    """Optional real-time visualization of DOA results."""

    def __init__(self):
        self.fig = None
        self.ax_angle = None
        self.ax_spectrum = None
        self._initialized = False
        self._time_hist: List[float] = []
        self._angle_hist: List[float] = []
        self._conf_hist: List[float] = []

    def initialize(self):
        """Set up matplotlib figures."""
        if not HAS_MPL:
            return False

        try:
            self.fig, (self.ax_angle, self.ax_spectrum) = plt.subplots(2, 1, figsize=(10, 8))
            self.fig.suptitle('Stereo DOA Estimation', fontsize=14)

            # Angle plot
            self.ax_angle.set_xlim(0, 10)
            self.ax_angle.set_ylim(-90, 90)
            self.ax_angle.set_ylabel('Azimuth Angle (degrees)')
            self.ax_angle.set_xlabel('Time (seconds)')
            self.ax_angle.grid(True, alpha=0.3)
            self.ax_angle.axhline(y=0, color='gray', linestyle='--', linewidth=0.5)

            # Spectrum plot
            self.ax_spectrum.set_title('Cross-Spectrum Magnitude')
            self.ax_spectrum.set_xlabel('Frequency (Hz)')
            self.ax_spectrum.set_ylabel('Magnitude')
            self.ax_spectrum.grid(True, alpha=0.3)

            self._initialized = True
            return True
        except Exception as e:
            print(f"Matplotlib initialization failed: {e}")
            return False

    def update(self, result: DOAResult, freqs: Optional[np.ndarray] = None,
               csd_mag: Optional[np.ndarray] = None):
        """Update plots with new result."""
        if not self._initialized:
            return

        self._time_hist.append(result.timestamp)
        self._angle_hist.append(result.angle_degrees)
        self._conf_hist.append(result.confidence)

        # Keep only recent history
        max_points = 500
        if len(self._time_hist) > max_points:
            self._time_hist.pop(0)
            self._angle_hist.pop(0)
            self._conf_hist.pop(0)

        # Update angle plot
        self.ax_angle.clear()
        if len(self._time_hist) > 0:
            self.ax_angle.plot(self._time_hist, self._angle_hist, 'b-', linewidth=1.5, label='Angle')
            self.ax_angle.fill_between(self._time_hist,
                                        np.array(self._angle_hist) - 10,
                                        np.array(self._angle_hist) + 10,
                                        alpha=0.1, color='blue')
            self.ax_angle.set_xlim(max(0, self._time_hist[-1] - 10), self._time_hist[-1])
            self.ax_angle.legend(loc='upper right')
            self.ax_angle.text(0.02, 0.95,
                              f'Angle: {result.angle_degrees:.1f}° | Conf: {result.confidence:.2f}',
                              transform=self.ax_angle.transAxes,
                              fontsize=10, verticalalignment='top',
                              bbox=dict(boxstyle='round', facecolor='wheat', alpha=0.5))

        # Update spectrum if provided
        if freqs is not None and csd_mag is not None:
            self.ax_spectrum.clear()
            self.ax_spectrum.semilogy(freqs, csd_mag + 1e-10, 'g-', linewidth=0.8)

        self.fig.canvas.draw_idle()

    def show(self):
        """Display the visualization window."""
        if self._initialized and HAS_MPL:
            plt.show()


# ============================================================================
# Command-Line Interface & Main Loop
# ============================================================================

def parse_args():
    parser = argparse.ArgumentParser(
        description='Estimate Direction of Arrival using stereo microphone'
    )
    parser.add_argument('--duration', type=float, default=None,
                        help='Recording duration in seconds (None = indefinite)')
    parser.add_argument('--sample-rate', type=int, default=48000,
                        help='Audio sample rate in Hz')
    parser.add_argument('--mic-spacing', type=float, default=0.17,
                        help='Distance between microphones in meters')
    parser.add_argument('--window-size', type=int, default=4096,
                        help='Analysis window size in samples')
    parser.add_argument('--hop-size', type=int, default=2048,
                        help='Hop size in samples')
    parser.add_argument('--max-lag', type=int, default=256,
                        help='Maximum TDOA lag to search in samples')
    parser.add_argument('--freq-low', type=float, default=300.0,
                        help='Lower bandpass frequency (Hz)')
    parser.add_argument('--freq-high', type=float, default=4000.0,
                        help='Upper bandpass frequency (Hz)')
    parser.add_argument('--no-viz', action='store_true',
                        help='Disable visualization')
    parser.add_argument('--test', action='store_true',
                        help='Run synthetic test instead of live recording')
    return parser.parse_args()


def run_synthetic_test(config: DOAConfig):
    """
    Test the DOA estimator with synthetic signals at known angles.
    """
    print("=" * 60)
    print("Running Synthetic DOA Test")
    print("=" * 60)

    estimator = StereoDOAEstimator(config)

    # Generate test tones at different angles
    test_angles = [-60, -30, 0, 30, 60]
    tone_freq = 1000.0  # Hz
    duration = 0.5      # seconds
    n_samples = int(config.sample_rate * duration)

    t = np.linspace(0, duration, n_samples, endpoint=False)

    for true_angle in test_angles:
        # Generate source signal
        source = np.sin(2 * np.pi * tone_freq * t)

        # Add some noise
        noise_level = 0.1
        source_noisy = source + noise_level * np.random.randn(n_samples)

        # Compute expected TDOA for this angle
        expected_tdoa = angle_to_tdoa(true_angle, config.mic_spacing_m,
                                       config.speed_of_sound)
        delay_samples = int(round(expected_tdoa * config.sample_rate))

        # Create delayed versions for left/right
        if delay_samples >= 0:
            left = np.concatenate([np.zeros(delay_samples), source_noisy[:-delay_samples]]) \
                   if delay_samples < n_samples else np.zeros(n_samples)
            right = source_noisy.copy()
        else:
            shift = -delay_samples
            left = source_noisy.copy()
            right = np.concatenate([np.zeros(shift), source_noisy[:-shift]]) \
                    if shift < n_samples else np.zeros(n_samples)

        # Pad to ensure sufficient length
        min_needed = config.window_size_samples
        if len(left) < min_needed:
            pad = min_needed - len(left)
            left = np.pad(left, (0, pad), mode='constant')
            right = np.pad(right, (0, pad), mode='constant')

        # Process
        result = estimator.process_block(left, right, timestamp=0.0)

        if result:
            print(f"True angle: {true_angle:+6.1f}° | "
                  f"Estimated: {result.angle_degrees:+6.1f}° | "
                  f"TDOA: {result.tdoa_seconds*1e6:+8.2f} µs | "
                  f"Confidence: {result.confidence:.3f}")
        else:
            print(f"True angle: {true_angle:+6.1f}° | No valid detection")

        # Reset for next test
        estimator.reset()

    print("\nSynthetic test complete.")


def run_live_recording(args, config: DOAConfig):
    """Run live stereo recording and DOA estimation."""
    if sd is None:
        print("ERROR: sounddevice library is required for live recording.")
        print("Install with: pip install sounddevice")
        sys.exit(1)

    estimator = StereoDOAEstimator(config)
    visualizer = DOAVisualizer()

    if not args.no_viz:
        if not visualizer.initialize():
            print("Visualization disabled due to missing dependencies.")
            args.no_viz = True

    # Print device info
    print("Available audio input devices:")
    try:
        devices = sd.query_devices()
        input_devices = [(i, d) for i, d in enumerate(devices)
                         if d['max_input_channels'] >= 2]
        if input_devices:
            print(f"Default input: {devices[sd.default.device[0]]['name']}")
            for idx, dev in input_devices:
                print(f"  [{idx}] {dev['name']} ({dev['max_input_channels']} ch)")
        else:
            print("WARNING: No stereo input devices found!")
    except Exception as e:
        print(f"Could not query devices: {e}")

    # Block size should be at least window_size
    block_size = max(config.window_size_samples, config.hop_size_samples)

    # Callback for each audio block
    def on_audio_block(left: np.ndarray, right: np.ndarray, timestamp: float):
        result = estimator.process_block(left, right, timestamp)

        if result and result.confidence > 0.1:
            # Print periodically to avoid spam
            if not hasattr(on_audio_block, '_last_print'):
                on_audio_block._last_print = 0
            if timestamp - on_audio_block._last_print > 0.5:
                print(f"[{timestamp:8.2f}s] Angle: {result.angle_degrees:+6.1f}° | "
                      f"TDOA: {result.tdoa_seconds*1e6:+8.2f} µs | "
                      f"Conf: {result.confidence:.3f}")
                on_audio_block._last_print = timestamp

            # Update visualization
            if not args.no_viz:
                freqs = None
                csd_mag = None
                try:
                    # Quick cross-spectral density estimate
                    f_csd, Pxy = welch(left.astype(np.float64), right.astype(np.float64),
                                        fs=config.sample_rate, nperseg=min(1024, len(left)),
                                        noverlap=512, detrend='linear')
                    freqs = f_csd
                    csd_mag = np.abs(Pxy)
                except Exception:
                    pass

                visualizer.update(result, freqs, csd_mag)

    recorder = StereoRecorder(
        sample_rate=config.sample_rate,
        block_size=block_size,
        callback=on_audio_block
    )

    try:
        recorder.start()

        if args.duration:
            time.sleep(args.duration)
            recorder.stop()
        else:
            while True:
                time.sleep(0.1)

    except KeyboardInterrupt:
        print("\nInterrupted by user.")
    finally:
        recorder.stop()

    if not args.no_viz:
        visualizer.show()


def main():
    args = parse_args()

    config = DOAConfig(
        sample_rate=args.sample_rate,
        mic_spacing_m=args.mic_spacing,
        window_size_samples=args.window_size,
        hop_size_samples=args.hop_size,
        max_tdoa_samples=args.max_lag,
        frequency_band_low=args.freq_low,
        frequency_band_high=args.freq_high,
    )

    print("Stereo DOA Estimator")
    print(f"  Sample rate:     {config.sample_rate} Hz")
    print(f"  Mic spacing:     {config.mic_spacing_m*100:.1f} cm")
    print(f"  Window size:     {config.window_size_samples} samples "
          f"({config.window_size_samples/config.sample_rate*1000:.1f} ms)")
    print(f"  Freq band:       {config.frequency_band_low}-{config.frequency_band_high} Hz")
    print(f"  Max TDOA lag:    {config.max_tdoa_samples} samples "
          f"({config.max_tdoa_samples/config.sample_rate*1e6:.1f} µs)")
    print()

    if args.test:
        run_synthetic_test(config)
    else:
        run_live_recording(args, config)


if __name__ == '__main__':
    main()