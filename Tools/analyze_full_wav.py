#!/usr/bin/env python3
"""Analyze a full WAV file with the same frame layout used by the STM32 tuner.

The STM32 firmware embeds only a short excerpt because the whole WAV does not
fit comfortably in flash. This host tool analyzes the complete source file and
writes a frame-by-frame CSV plus a compact Markdown summary.
"""

from __future__ import annotations

import argparse
import csv
import math
from pathlib import Path
import wave

import numpy as np


SAMPLE_RATE_HZ = 16000
FRAME_LENGTH = 4096
HOP_LENGTH = 1024
CORR_FRAME_LENGTH = 2048
MIN_FREQ_HZ = 70.0
MAX_FREQ_HZ = 400.0
IN_TUNE_LIMIT_X10 = 50
CONFIDENT_THRESHOLD = 0.70

STRINGS = [
    ("low E", 82.41),
    ("A", 110.00),
    ("D", 146.83),
    ("G", 196.00),
    ("B", 246.94),
    ("high E", 329.63),
]


def read_pcm16_wav(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as wav:
        sample_rate = wav.getframerate()
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        frames = wav.getnframes()
        raw = wav.readframes(frames)

    if sample_width != 2:
        raise ValueError(f"Only 16-bit PCM WAV is supported, got {sample_width * 8}-bit")

    samples = np.frombuffer(raw, dtype="<i2").astype(np.float32)
    if channels > 1:
        samples = samples.reshape((-1, channels)).mean(axis=1)

    return samples, sample_rate


def downsample_integer(samples: np.ndarray, input_rate: int, output_rate: int) -> np.ndarray:
    if input_rate == output_rate:
        return samples.copy()

    if input_rate % output_rate != 0:
        raise ValueError(
            f"Input rate {input_rate} Hz is not an integer multiple of {output_rate} Hz"
        )

    ratio = input_rate // output_rate
    return samples[::ratio].copy()


def nearest_string(frequency_hz: float) -> tuple[int, str, int]:
    if frequency_hz <= 0.0:
        return 255, "unknown", 0

    best_index = min(
        range(len(STRINGS)),
        key=lambda i: abs((frequency_hz - STRINGS[i][1]) / STRINGS[i][1]),
    )
    target_hz = STRINGS[best_index][1]
    relative_error = (frequency_hz - target_hz) / target_hz
    cents_x10 = int(round(relative_error * 17312.34))
    return best_index, STRINGS[best_index][0], cents_x10


def tuning_state(cents_x10: int, confidence: float) -> tuple[int, str]:
    if confidence < CONFIDENT_THRESHOLD:
        return 4, "unknown"

    if cents_x10 > IN_TUNE_LIMIT_X10:
        return 3, "sharp"

    if cents_x10 < -IN_TUNE_LIMIT_X10:
        return 2, "flat"

    return 1, "in tune"


def parabolic_peak(values: np.ndarray, index: int) -> float:
    if index <= 0 or index >= len(values) - 1:
        return float(index)

    denominator = values[index - 1] - (2.0 * values[index]) + values[index + 1]
    if abs(float(denominator)) < 1.0e-12:
        return float(index)

    return float(index) + (0.5 * float(values[index - 1] - values[index + 1]) / float(denominator))


def yin_pitch(frame: np.ndarray) -> tuple[float, float, int]:
    mean = float(frame.mean())
    centered = frame - mean
    min_lag = int(SAMPLE_RATE_HZ / MAX_FREQ_HZ)
    max_lag = int(SAMPLE_RATE_HZ / MIN_FREQ_HZ)
    analysis_length = len(frame) - max_lag
    yin = np.ones(max_lag + 2, dtype=np.float32)
    running_sum = 0.0
    best_tau = 0
    best_cmnd = 1.0e9

    for tau in range(1, max_lag + 1):
        delta = centered[:analysis_length] - centered[tau : tau + analysis_length]
        difference = float(np.dot(delta, delta))
        running_sum += difference
        yin[tau] = difference * tau / running_sum if running_sum > 0.0 else 1.0

    for tau in range(min_lag, max_lag + 1):
        cmnd = float(yin[tau])
        if cmnd < best_cmnd:
            best_cmnd = cmnd
            best_tau = tau

        if cmnd < 0.18:
            while tau + 1 <= max_lag and yin[tau + 1] < yin[tau]:
                tau += 1
            best_tau = tau
            best_cmnd = float(yin[tau])
            break

    if best_tau == 0:
        return 0.0, 0.0, 0

    better_tau = parabolic_peak(yin, best_tau)
    detected_hz = SAMPLE_RATE_HZ / better_tau
    return detected_hz, max(0.0, 1.0 - best_cmnd), best_tau


def corr_pitch(frame: np.ndarray) -> tuple[float, float, int]:
    corr_input = frame[:CORR_FRAME_LENGTH] - frame[:CORR_FRAME_LENGTH].mean()
    corr = np.correlate(corr_input, corr_input, mode="full")[CORR_FRAME_LENGTH - 1 :]
    zero_lag = float(corr[0])

    if zero_lag <= 1.0e-6:
        return 0.0, 0.0, 0

    min_lag = int(SAMPLE_RATE_HZ / MAX_FREQ_HZ)
    max_lag = int(SAMPLE_RATE_HZ / MIN_FREQ_HZ)
    scores = np.zeros(max_lag + 2, dtype=np.float32)

    for tau in range(min_lag - 1, max_lag + 2):
        expected_energy = zero_lag * float(CORR_FRAME_LENGTH - tau) / float(CORR_FRAME_LENGTH)
        scores[tau] = float(corr[tau]) / expected_energy if expected_energy > 1.0e-6 else 0.0

    peaks = [
        tau
        for tau in range(min_lag, max_lag + 1)
        if scores[tau] >= scores[tau - 1] and scores[tau] >= scores[tau + 1]
    ]

    if not peaks:
        return 0.0, 0.0, 0

    best_tau = max(peaks, key=lambda tau: scores[tau])
    best_score = float(scores[best_tau])
    threshold = max(0.30, best_score * 0.75)

    for tau in peaks:
        if scores[tau] >= threshold:
            best_tau = tau
            best_score = float(scores[tau])
            break

    better_tau = parabolic_peak(scores, best_tau)
    return SAMPLE_RATE_HZ / better_tau, best_score, best_tau


def fft_peak(frame: np.ndarray) -> float:
    centered = frame - frame.mean()
    windowed = centered * np.hanning(len(frame))
    spectrum = np.abs(np.fft.rfft(windowed))
    freqs = np.fft.rfftfreq(len(frame), 1.0 / SAMPLE_RATE_HZ)
    mask = (freqs >= MIN_FREQ_HZ) & (freqs <= 500.0)
    masked_indices = np.where(mask)[0]
    peak_index = masked_indices[np.argmax(spectrum[masked_indices])]
    better_index = parabolic_peak(spectrum, int(peak_index))
    return better_index * SAMPLE_RATE_HZ / len(frame)


def analyze(samples: np.ndarray) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for frame_index, start in enumerate(range(0, len(samples) - FRAME_LENGTH + 1, HOP_LENGTH)):
        frame = samples[start : start + FRAME_LENGTH]
        detected_hz, confidence, yin_tau = yin_pitch(frame)
        string_id, string_name, cents_x10 = nearest_string(detected_hz)
        state_id, state_name = tuning_state(cents_x10, confidence)

        if confidence < CONFIDENT_THRESHOLD:
            string_id = 255
            string_name = "unknown"

        corr_hz, corr_confidence, corr_tau = corr_pitch(frame)
        corr_string_id, corr_string_name, corr_cents_x10 = nearest_string(corr_hz)

        rows.append(
            {
                "frame": frame_index,
                "time_ms": int(round(start * 1000 / SAMPLE_RATE_HZ)),
                "detected_hz": detected_hz,
                "string_id": string_id,
                "string_name": string_name,
                "state_id": state_id,
                "state_name": state_name,
                "cents_x10": cents_x10,
                "confidence": confidence,
                "yin_tau": yin_tau,
                "corr_hz": corr_hz,
                "corr_string_id": corr_string_id,
                "corr_string_name": corr_string_name,
                "corr_cents_x10": corr_cents_x10,
                "corr_confidence": corr_confidence,
                "corr_tau": corr_tau,
                "fft_peak_hz": fft_peak(frame),
                "signal_amplitude": int(frame.max() - frame.min()),
            }
        )

    return rows


def grouped_segments(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    segments: list[dict[str, object]] = []
    if not rows:
        return segments

    start_index = 0
    current = rows[0]["string_name"]

    for index, row in enumerate(rows[1:], start=1):
        if row["string_name"] != current:
            segment_rows = rows[start_index:index]
            segments.append(make_segment(current, segment_rows))
            start_index = index
            current = row["string_name"]

    segments.append(make_segment(current, rows[start_index:]))
    return segments


def make_segment(name: object, segment_rows: list[dict[str, object]]) -> dict[str, object]:
    confident = [row for row in segment_rows if row["string_name"] != "unknown"]
    hz_values = [float(row["detected_hz"]) for row in confident]
    return {
        "string_name": name,
        "start_ms": segment_rows[0]["time_ms"],
        "end_ms": segment_rows[-1]["time_ms"],
        "frames": len(segment_rows),
        "avg_hz": sum(hz_values) / len(hz_values) if hz_values else 0.0,
    }


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        for row in rows:
            formatted = row.copy()
            for key in ("detected_hz", "confidence", "corr_hz", "corr_confidence", "fft_peak_hz"):
                formatted[key] = f"{float(formatted[key]):.6f}"
            writer.writerow(formatted)


def write_summary(path: Path, input_path: Path, samples: np.ndarray, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    counts = {name: 0 for name, _ in STRINGS}
    counts["unknown"] = 0
    for row in rows:
        counts[str(row["string_name"])] += 1

    segments = grouped_segments(rows)

    lines = [
        "# Full WAV Analysis",
        "",
        f"- Input: `{input_path}`",
        f"- Downsampled sample rate: `{SAMPLE_RATE_HZ} Hz`",
        f"- Downsampled duration: `{len(samples) / SAMPLE_RATE_HZ:.3f} s`",
        f"- Frame length: `{FRAME_LENGTH}` samples / `{FRAME_LENGTH * 1000 // SAMPLE_RATE_HZ} ms`",
        f"- Hop length: `{HOP_LENGTH}` samples / `{HOP_LENGTH * 1000 // SAMPLE_RATE_HZ} ms`",
        f"- Frames analyzed: `{len(rows)}`",
        "",
        "## String Counts",
        "",
        "| String | Frames |",
        "| --- | ---: |",
    ]

    for name, _ in STRINGS:
        lines.append(f"| {name} | {counts[name]} |")
    lines.append(f"| unknown | {counts['unknown']} |")

    lines.extend(
        [
            "",
            "## Contiguous Detected Segments",
            "",
            "| String | Start ms | End ms | Frames | Avg Hz |",
            "| --- | ---: | ---: | ---: | ---: |",
        ]
    )

    for segment in segments:
        lines.append(
            f"| {segment['string_name']} | {segment['start_ms']} | {segment['end_ms']} | "
            f"{segment['frames']} | {float(segment['avg_hz']):.2f} |"
        )

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", default="Samples/static_tuner/gc.wav")
    parser.add_argument("--csv", default="Samples/static_tuner/analysis/gc_full_analysis.csv")
    parser.add_argument("--summary", default="Samples/static_tuner/analysis/gc_full_analysis.md")
    args = parser.parse_args()

    input_path = Path(args.input)
    samples, input_rate = read_pcm16_wav(input_path)
    downsampled = downsample_integer(samples, input_rate, SAMPLE_RATE_HZ)
    rows = analyze(downsampled)

    if not rows:
        raise RuntimeError("No complete analysis frames were produced")

    write_csv(Path(args.csv), rows)
    write_summary(Path(args.summary), input_path, downsampled, rows)
    print(f"Analyzed {len(rows)} frames from {input_path}")
    print(f"CSV: {args.csv}")
    print(f"Summary: {args.summary}")


if __name__ == "__main__":
    main()
