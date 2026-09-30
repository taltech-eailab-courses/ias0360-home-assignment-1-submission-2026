#!/usr/bin/env python3
"""Train a compact digit CNN and export a fully-int8 TFLite Micro model.

Example: train the small filter configuration and save it separately:
        python3 tools/train_quantized_model.py \\
            --conv-filters 4 8 \\
            --dense-units 32 \\
            --model-output models/digit_model_small.tflite \\
            --cpp-output models/mnist_model_data_small.cpp
"""

import argparse
from pathlib import Path

import numpy as np
import tensorflow as tf
from sklearn.datasets import load_digits
from sklearn.model_selection import train_test_split

PROJECT_ROOT = Path(__file__).resolve().parents[1]
MODEL_PATH = PROJECT_ROOT / "models" / "digit_model.tflite"
CPP_PATH = PROJECT_ROOT / "models" / "mnist_model_data.cpp"


def load_dataset():
    digits = load_digits()
    images = (digits.images.astype(np.float32) / 16.0)[..., np.newaxis]
    images = tf.image.resize(images, (28, 28), method="bilinear").numpy()
    labels = digits.target.astype(np.int32)
    return train_test_split(images, labels, test_size=0.2, random_state=42, stratify=labels)


def write_cpp_array(model_bytes, cpp_path):
    values_per_line = 12
    rows = []
    for start in range(0, len(model_bytes), values_per_line):
        row = ", ".join(f"0x{value:02x}" for value in model_bytes[start:start + values_per_line])
        rows.append(f"  {row},")

    cpp_path.write_text(
        '#include "mnist_model_data.h"\n\n'
        'alignas(8) const unsigned char mnist_model_data[] = {\n'
        + "\n".join(rows)
        + '\n};\n'
        + f'const int mnist_model_data_len = {len(model_bytes)};\n',
        encoding="ascii",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--conv-filters",
        type=int,
        nargs=2,
        default=(8, 16),
        metavar=("FIRST", "SECOND"),
        help="number of filters in the two convolution layers (default: 8 16)",
    )
    parser.add_argument(
        "--dense-units",
        type=int,
        default=32,
        help="number of units in the hidden dense layer (default: 32)",
    )
    parser.add_argument(
        "--model-output",
        type=Path,
        default=MODEL_PATH,
        help=f"output TFLite model path (default: {MODEL_PATH})",
    )
    parser.add_argument(
        "--cpp-output",
        type=Path,
        default=CPP_PATH,
        help=f"output embedded C++ model path (default: {CPP_PATH})",
    )
    args = parser.parse_args()
    if any(count <= 0 for count in args.conv_filters) or args.dense_units <= 0:
        parser.error("filter counts and dense units must be positive integers")

    x_train, x_test, y_train, y_test = load_dataset()
    model = tf.keras.Sequential([
        tf.keras.layers.Input(batch_size=1, shape=(28, 28, 1)),
        tf.keras.layers.Conv2D(args.conv_filters[0], 3, activation="relu"),
        tf.keras.layers.MaxPooling2D(),
        tf.keras.layers.Conv2D(args.conv_filters[1], 3, activation="relu"),
        tf.keras.layers.MaxPooling2D(),
        tf.keras.layers.Reshape((5 * 5 * args.conv_filters[1],)),
        tf.keras.layers.Dense(args.dense_units, activation="relu"),
        tf.keras.layers.Dense(10, activation="softmax"),
    ])
    print(
        f"Training CNN with filters={args.conv_filters} and "
        f"dense_units={args.dense_units}"
    )
    model.compile(optimizer="adam", loss="sparse_categorical_crossentropy", metrics=["accuracy"])
    model.fit(x_train, y_train, validation_data=(x_test, y_test), epochs=20, batch_size=64, verbose=2)

    converter = tf.lite.TFLiteConverter.from_keras_model(model)
    converter.optimizations = [tf.lite.Optimize.DEFAULT]

    def representative_dataset():
        for image in x_train[:200]:
            yield [image[np.newaxis, ...]]

    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8
    model_bytes = converter.convert() # conversion modelo como bytes
    args.model_output.parent.mkdir(parents=True, exist_ok=True)
    args.cpp_output.parent.mkdir(parents=True, exist_ok=True)
    args.model_output.write_bytes(model_bytes)
    write_cpp_array(model_bytes, args.cpp_output)

    interpreter = tf.lite.Interpreter(model_path=str(args.model_output))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()[0]
    output_details = interpreter.get_output_details()[0]
    print(f"Wrote {args.model_output} ({len(model_bytes)} bytes)")
    print(f"Wrote {args.cpp_output}")
    print("Input:", input_details["shape"], input_details["dtype"], input_details["quantization"])
    print("Output:", output_details["shape"], output_details["dtype"], output_details["quantization"])


if __name__ == "__main__":
    main()