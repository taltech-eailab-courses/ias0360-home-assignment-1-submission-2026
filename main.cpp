#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"

#include "DEV_Config.h"
#include "LCD_Driver.h"
#include "LCD_GUI.h"
#include "LCD_Touch.h"
#include "model.h"
#include "model_settings.h"

int main() {
  System_Init();
  sleep_ms(5000);

  mutex_t inference_mutex;
  mutex_init(&inference_mutex);

  LCD_Init(SCAN_DIR_DFT, 1000);
  TP_Init(SCAN_DIR_DFT);
  TP_GetAdFac();

  INFERENCE inference;
  reset_inference(&inference);
  init_gui();

  Model model;
  if (!model.setup()) {
    while (true) {
      tight_loop_contents();
    }
  }

  multicore_launch_core1([] {
    while (true) {
      LCD_SetBackLight(1000);
      TP_DrawBoard();
    }
  });

  while (true) {
    const uint32_t signal = multicore_fifo_pop_blocking();
    if (signal != MULTICORE_RUN_INFERENCE_FLAG) {
      continue;
    }

    mutex_enter_blocking(&inference_mutex);
    inference.IsProcessing = true;
    for (int index = 0; index < DIGIT_INPUT_COUNT; ++index) {
      const uint8_t* pixels = inference.UserInputs[index].InputData;
      if (model.set_input_image(pixels)) {
        const absolute_time_t start = get_absolute_time();
        const int prediction = model.predict();
        inference.UserInputs[index].InferenceTimeUs =
            absolute_time_diff_us(start, get_absolute_time());
        inference.UserInputs[index].PredictedDigit =
            prediction < 0 ? UNKNOWN_PREDICTION : prediction;
        float scores[kDigitCount];
        inference.UserInputs[index].PredictionScore =
            prediction >= 0 && model.get_output_scores(scores, kDigitCount)
                ? scores[prediction]
                : 0.0f;
      } else {
        inference.UserInputs[index].PredictedDigit = UNKNOWN_PREDICTION;
        inference.UserInputs[index].PredictionScore = 0.0f;
        inference.UserInputs[index].InferenceTimeUs = 0;
      }
    }
    inference.IsProcessing = false;
    mutex_exit(&inference_mutex);
  }
}
