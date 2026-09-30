#ifndef HOME_ASSIGNMENT_1_MODEL_H_
#define HOME_ASSIGNMENT_1_MODEL_H_

#include <cstdint>

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

class Model {
 public:
  bool setup();
  bool set_input_image(const uint8_t* pixels);
  int predict();
  bool get_output_scores(float* scores, int count) const;

 private:
  const tflite::Model* model_ = nullptr;
  tflite::MicroInterpreter* interpreter_ = nullptr;
  TfLiteTensor* input_ = nullptr;
  float input_scale_ = 1.0f;
  int input_zero_point_ = 0;
};

#endif
