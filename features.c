#include "features.h"
#include "buffer.h"
#include "stats.h"
#include "stdio.h"

#define MOTION_THRESHOLD 20.0f // Calibrated.
#define MOTION_WALK_THRESHOLD 100.0f // Calibrated.
#define MOTION_RUN_THRESHOLD 400.0f //Calibrated.

#define DEBOUNCE_THRESHOLD 3 // 3 consecutive measurements need to have a result that is same in order for the state to change.



bool features_classify_window(motion_state_t *out_state, float *out_stddev){
    float window[WINDOW_SIZE];
    // TODO 1: try to pop WINDOW_SIZE values from the buffer into `window`.
    if (buffer_level() < WINDOW_SIZE){ // If buffer is smaller than window size, return false for the bool
        return false;
    }
    for(int16_t i = 0; i < WINDOW_SIZE; i++){ 
        bool WindowPop = buffer_pop(&window[i]);
        if(WindowPop != 1){
            printf("Buffer wasn't full, partial window error.");
            return 0;
        }
    }
    
    // If any single pop fails (buffer didn't have enough yet), return
    // false immediately — don't classify a partial window.
    
    float MeanWindow = mean_f32(window, WINDOW_SIZE);
    float VarianceWindow = variance_f32(window, WINDOW_SIZE, MeanWindow);
    float StddevWindow = stddev_f32(VarianceWindow);
    *out_stddev = StddevWindow;
    if(*out_stddev < MOTION_THRESHOLD){
        *out_state = MOTION_STILL;
    } else if (*out_stddev < MOTION_WALK_THRESHOLD){
        *out_state = MOTION_MOVING;
    } else if(*out_stddev < MOTION_RUN_THRESHOLD){
        *out_state = MOTION_WALKING;
    } else {
        *out_state = MOTION_RUNNING;
    }
    //*out_state = (*out_stddev < MOTION_THRESHOLD) ? MOTION_STILL : MOTION_MOVING; // Ternary statement for motion state evaluation and storing. Only fine for one state.
    return 1;
}

    static motion_state_t candidate_state = MOTION_STILL;
    static motion_state_t stable_state = MOTION_STILL;
    static uint8_t consecutive_count = 0;

motion_state_t features_debounce(motion_state_t raw_state){
        // TODO: if raw_state == candidate_state, increment consecutive_count
    //       (another window agreeing with the current candidate)
            if(raw_state == candidate_state){
                consecutive_count++;
            }else{
                candidate_state = raw_state;
                consecutive_count = 1;
            }
            if(consecutive_count == DEBOUNCE_THRESHOLD){
                stable_state = candidate_state;
            }
            return stable_state;
    
    }