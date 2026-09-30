#include "model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "mnist_model_data.h"
#include "model_settings.h"
#include "tensorflow/lite/micro/tflite_bridge/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"

bool Model::setup() {
  if (mnist_model_data_len < 8) {
    printf("Model data is missing. Export and embed the quantized .tflite file.\n");
    return false;
  }

  static tflite::MicroErrorReporter error_reporter;
  model_ = tflite::GetModel(mnist_model_data);
  if (model_->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(&error_reporter, "Unsupported model schema version.");
    return false;
  }

  static tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddConv2D();
  resolver.AddMaxPool2D();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();

  static uint8_t tensor_arena[kTensorArenaSize];
  static tflite::MicroInterpreter interpreter(
      model_, resolver, tensor_arena, kTensorArenaSize);
  interpreter_ = &interpreter;

  if (interpreter_->AllocateTensors() != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(&error_reporter, "Tensor allocation failed.");
    return false;
  }

  input_ = interpreter_->input(0);
  if (input_ == nullptr || input_->type != kTfLiteInt8 ||
      input_->bytes != kImagePixels) {
    TF_LITE_REPORT_ERROR(&error_reporter, "Unexpected input tensor.");
    return false;
  }

  input_scale_ = input_->params.scale;
  input_zero_point_ = input_->params.zero_point;
  return true;
}

bool Model::set_input_image(const uint8_t* pixels) {
  if (input_ == nullptr || pixels == nullptr || input_scale_ <= 0.0f) {
    return false;
  }

  for (int index = 0; index < kImagePixels; ++index) {
    const float normalized = static_cast<float>(pixels[index]) / 255.0f;
    const int quantized = static_cast<int>(std::round(normalized / input_scale_)) +
                          input_zero_point_;
    input_->data.int8[index] =
        static_cast<int8_t>(std::clamp(quantized, -128, 127));
  }
  return true;
}

int Model::predict() {
  if (interpreter_ == nullptr || interpreter_->Invoke() != kTfLiteOk) {
    return -1;
  }

  TfLiteTensor* output = interpreter_->output(0);
  if (output == nullptr || output->type != kTfLiteInt8 ||
      output->bytes < kDigitCount) {
    return -1;
  }

  int prediction = 0;
  for (int index = 1; index < kDigitCount; ++index) {
    if (output->data.int8[index] > output->data.int8[prediction]) {
      prediction = index;
    }
  }
  return prediction;
}

bool Model::get_output_scores(float* scores, int count) const {
  if (interpreter_ == nullptr || scores == nullptr || count < kDigitCount) {
    return false;
  }

  TfLiteTensor* output = interpreter_->output(0);
  if (output == nullptr || output->type != kTfLiteInt8 ||
      output->bytes < kDigitCount) {
    return false;
  }

  for (int index = 0; index < kDigitCount; ++index) {
    scores[index] = (static_cast<int>(output->data.int8[index]) -
                     output->params.zero_point) * output->params.scale;
  }
  return true;
}
