# Home Assignment 1: Image Preprocessing

This project implements and evaluates an image-preprocessing pipeline. The main deliverable is the visualization and comparison of its intermediate stages. The CNN and Raspberry Pi Pico firmware are optional extensions.

## Main Work: Preprocessing

The pipeline starts with handwritten-digit images from scikit-learn and demonstrates:

1. Normalization of pixel values to the range [0, 1].
2. Bilinear resizing from 8 x 8 to 28 x 28 pixels.
3. Resizing to an 84 x 84 touch-like buffer.
4. Reduction to 28 x 28 using 3 x 3 block averaging.
5. An 8-bit representation and its reconstructed image.

## Generate Visual Comparisons

Run this inside the course Docker container, starting from the workspace root:

```sh
cd submission/ias0360-home-assignment-1-submission-2026
python3 tools/visualize_preprocessing.py --digit 7
```

The script writes the results to `report/images/preprocessing/`:

- `digit_7_pipeline.png` shows the five intermediate image stages.
- `digits_reduced_comparison.png` compares ten digit images after reduction.
- `filter_comparison.png` compares 2 x 2, 3 x 3, and 5 x 5 average filters.
- `filter_metrics.csv` records MSE, RMSE, MAE, maximum absolute error, PSNR, correlation, and gradient RMSE.

The plots are reproducible preprocessing results, not photographs of the Pico demonstration. Use `--digit` to select another digit, `--index` to select a dataset sample directly, or `--output-dir` to write results elsewhere. The default digit is 7.

### Filter Experiment

Each filter is applied to the 84 x 84 image, then resized back to 84 x 84 for comparison with the original. The default experiment produced:

| Filter | MAE | PSNR (dB) | Correlation | Gradient RMSE |
| --- | ---: | ---: | ---: | ---: |
| 2 x 2 | 0.002862 | 44.54 | 0.9998 | 0.002820 |
| 3 x 3 | 0.004856 | 41.89 | 0.9998 | 0.002336 |
| 5 x 5 | 0.065237 | 20.41 | 0.9516 | 0.013683 |

The 2 x 2 filter has the lowest pixel error, while the 3 x 3 filter gives the lowest gradient error and stronger smoothing. The 5 x 5 filter loses noticeably more image structure. These measurements make the trade-off between smoothing and detail preservation explicit.

## Optional Extension: CNN Model

The repository includes a fully int8-quantized model at `models/digit_model.tflite` and its embedded C array at `models/mnist_model_data.cpp`. The model was trained on the scikit-learn handwritten-digit dataset and reached 96.39% held-out validation accuracy. This classifier is an extension; it is not required to generate or evaluate the preprocessing comparisons.

To retrain and regenerate the model in the course Docker container:

```sh
python3 tools/train_quantized_model.py
```

The script overwrites the default model files. Use `--model-output` and `--cpp-output` to keep alternate experiments separate. The model uses `CONV_2D`, `MAX_POOL_2D`, `RESHAPE`, `FULLY_CONNECTED`, and `SOFTMAX` operators.

## Optional Extension: Raspberry Pi Pico

The firmware runs the CNN on a touchscreen drawing. Build the `digit_recognition` target from the workspace root inside the course Docker container:

```sh
cd submission/ias0360-home-assignment-1-submission-2026
cmake -S . -B build
cmake --build build --target digit_recognition -j$(nproc)
cd ../../..
./flash.sh submission/ias0360-home-assignment-1-submission-2026/build/digit_recognition.uf2
```

The expected LCD output is:

```text
Prediction: <0-9>  Confidence: <0-100>%
```

Tap `CLEAR` to erase the drawing and classify another digit. The build output is ignored by Git. The required Pico SDK libraries, LCD/touch code, and TFLite Micro dependency are included under `lib/`.
