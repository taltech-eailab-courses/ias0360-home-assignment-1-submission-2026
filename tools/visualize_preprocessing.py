#!/usr/bin/env python3
"""Generate visual comparisons of the digit image preprocessing pipeline."""

import argparse
import csv
import math
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image
from sklearn.datasets import load_digits

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT = PROJECT_ROOT / "report" / "images" / "preprocessing"


def resize_image(image, size):
    image_uint8 = np.clip(image * 255.0, 0, 255).astype(np.uint8)
    resized = Image.fromarray(image_uint8, mode="L").resize(
        size, Image.Resampling.BILINEAR
    )
    return np.asarray(resized, dtype=np.float32) / 255.0


def average_reduce(image, kernel_size):
    height, width = image.shape
    reduced_height = height // kernel_size
    reduced_width = width // kernel_size
    cropped = image[:reduced_height * kernel_size, :reduced_width * kernel_size]
    return cropped.reshape(
        reduced_height, kernel_size, reduced_width, kernel_size
    ).mean(axis=(1, 3))


def resize_to_84(image):
    return resize_image(image, (84, 84))


def filter_and_resize(image, kernel_size):
    filtered = average_reduce(image, kernel_size)
    return resize_to_84(filtered)


def signal_metrics(reference, estimate):
    error = reference.astype(np.float64) - estimate.astype(np.float64)
    mse = float(np.mean(error ** 2))
    rmse = math.sqrt(mse)
    mae = float(np.mean(np.abs(error)))
    max_absolute_error = float(np.max(np.abs(error)))
    psnr = float("inf") if mse == 0 else 10.0 * math.log10(1.0 / mse)
    reference_flat = reference.ravel()
    estimate_flat = estimate.ravel()
    correlation = float(np.corrcoef(reference_flat, estimate_flat)[0, 1])
    reference_gradient = np.gradient(reference.astype(np.float64))
    estimate_gradient = np.gradient(estimate.astype(np.float64))
    gradient_error = np.concatenate([
        reference_gradient[0].ravel() - estimate_gradient[0].ravel(),
        reference_gradient[1].ravel() - estimate_gradient[1].ravel(),
    ])
    gradient_rmse = float(np.sqrt(np.mean(gradient_error ** 2)))
    return mse, rmse, mae, max_absolute_error, psnr, correlation, gradient_rmse


def prepare_stages(image):
    normalized_8 = image.astype(np.float32) / 16.0
    resized_28 = resize_image(normalized_8, (28, 28))
    touch_like_84 = resize_to_84(resized_28)
    reduced_28 = average_reduce(touch_like_84, 3)
    quantized_int8 = np.round(reduced_28 * 255.0 - 128.0).astype(np.int8)
    dequantized = (quantized_int8.astype(np.float32) + 128.0) / 255.0
    return normalized_8, resized_28, touch_like_84, reduced_28, dequantized


def draw_pipeline(index, image, label, output_path):
    stages = prepare_stages(image)
    titles = [
        "Original 8 x 8",
        "Resized 28 x 28",
        "Touch buffer 84 x 84",
        "Reduced by 3 x 3 average",
        "After 8-bit round trip",
    ]

    figure, axes = plt.subplots(1, len(stages), figsize=(14, 3.2))
    for axis, title, stage in zip(axes, titles, stages):
        axis.imshow(stage, cmap="gray", vmin=0, vmax=1, interpolation="nearest")
        axis.set_title(title)
        axis.set_xticks([])
        axis.set_yticks([])
    figure.suptitle(f"Preprocessing comparison: sample {index}, digit {label}")
    figure.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=180, bbox_inches="tight")
    plt.close(figure)


def draw_all_digits(images, labels, output_path):
    figure, axes = plt.subplots(2, 5, figsize=(10, 5))
    for axis, image, label in zip(axes.flat, images, labels):
        _, _, _, reduced_28, _ = prepare_stages(image)
        axis.imshow(reduced_28, cmap="gray", vmin=0, vmax=1, interpolation="nearest")
        axis.set_title(f"Digit {label}")
        axis.set_xticks([])
        axis.set_yticks([])
    figure.suptitle("28 x 28 images after 84 x 84 to 28 x 28 reduction")
    figure.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=180, bbox_inches="tight")
    plt.close(figure)


def draw_filter_comparison(touch_image, output_path):
    kernel_sizes = (2, 3, 5)
    figure, axes = plt.subplots(1, len(kernel_sizes), figsize=(10, 3.6))
    rows = []
    for axis, kernel_size in zip(axes, kernel_sizes):
        reconstructed = filter_and_resize(touch_image, kernel_size)
        metrics = signal_metrics(touch_image, reconstructed)
        axis.imshow(reconstructed, cmap="gray", vmin=0, vmax=1, interpolation="nearest")
        axis.set_title(f"Average {kernel_size} x {kernel_size}")
        axis.set_xticks([])
        axis.set_yticks([])
        rows.append((kernel_size, *metrics))
    figure.suptitle("Spatial filtering comparison on the 84 x 84 image")
    figure.tight_layout()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=180, bbox_inches="tight")
    plt.close(figure)
    return rows


def write_metrics(rows, output_path):
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="", encoding="ascii") as file:
        writer = csv.writer(file)
        writer.writerow([
            "kernel_size",
            "mse",
            "rmse",
            "mae",
            "max_absolute_error",
            "psnr_db",
            "correlation",
            "gradient_rmse",
        ])
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--digit", type=int, default=7, help="digit label to visualize")
    parser.add_argument("--index", type=int, default=None, help="dataset index override")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()

    digits = load_digits()
    if args.index is None:
        candidates = np.flatnonzero(digits.target == args.digit)
        if len(candidates) == 0:
            parser.error(f"no sample found for digit {args.digit}")
        index = int(candidates[0])
    else:
        index = args.index
    if not 0 <= index < len(digits.images):
        parser.error("index is outside the dataset")

    image = digits.images[index]
    label = int(digits.target[index])
    draw_pipeline(index, image, label, args.output_dir / f"digit_{label}_pipeline.png")
    draw_all_digits(
        digits.images[:10],
        digits.target[:10],
        args.output_dir / "digits_reduced_comparison.png",
    )
    filter_rows = draw_filter_comparison(
        prepare_stages(image)[2],
        args.output_dir / "filter_comparison.png",
    )
    write_metrics(filter_rows, args.output_dir / "filter_metrics.csv")
    print(f"Wrote preprocessing images to {args.output_dir}")
    for kernel_size, mse, rmse, mae, max_error, psnr, correlation, gradient_rmse in filter_rows:
        print(
            f"{kernel_size}x{kernel_size}: MSE={mse:.6f}, MAE={mae:.6f}, "
            f"PSNR={psnr:.2f} dB, correlation={correlation:.4f}, "
            f"gradient_RMSE={gradient_rmse:.6f}"
        )


if __name__ == "__main__":
    main()
