#include "pico/stdlib.h"   
#include "pico/multicore.h"
#include "sensor.h"
#include "sampling.h"
#include "buffer.h"
#include "stdio.h"
#include "stats.h"
#include "math.h"
#include "features.h"
#include "led.h"
#include "storage.h"

#define TEST_PUSH_COUNT 350
#define EPSILON 0.0001f // Defines the accepted error of roundings coming from math operators in comparison to the expected values.

int main() {
    stdio_init_all();
    sleep_ms(2000); //Give the microcontroller some bootup time. If I use screen to check for output then I have time to open it before the outputs are printed.
    buffer_init();
    
    
    uint32_t success = 0;
    uint32_t failure = 0;
    for(int i = 0; i < TEST_PUSH_COUNT; i++){
        bool push = buffer_push((float)i); // Index is cast to float. Push to buffer is performed as many times as test parameter specifies.
        if(push == 1){ // If push is successful then the success count is appended.
            success++;
        }else{ // If push is unsuccessful then the failure count is appended.
            failure++;
        }
    } // Altogether pushed floats are just numbers that are equal to index. push member 1 is equal to 1 for ease of testing the buffer.

    float popped_value; // Define a float.

    for(uint32_t i = 0; i < (success+1); i++){
        bool pop = buffer_pop(&popped_value); // Pop values stored on the buffer from the buffer
        
        if(pop == 1){ //if pop is successful
            if(popped_value == i){ //and the popped value is equal to the index of the member prints pass.
                //printf("PASS\n");
                //printf("the popped float value is: %f\n", popped_value); // Prints the popped value.
            }else{
                printf("FAIL- an unexpected pop value\n"); // if the popped value is not equal to its index then print FAIL.  
        }
        }else{
            if (i < success){ // success count represents accurately the value of members on the buffer.
                printf("FAIL- i is smaller than success count\n"); // Print failed if a POP operation failed. Meaning that all members were not popped from the buffer.
            }else if(i == success){ // If i is equal to success count that means that all the members of buffer were popped.
                printf("PASS- queue empty as expected\n");
            }
        }
    }
    printf("The success count is following: %lu\n", success);
    printf("The failure count is following: %lu\n", failure);
    printf("The overflow count is the following: %lu\n", buffer_overflow_count());


    float test_data[] = {0.0f, 2.0f, 4.0f};
    // TODO 1: get the element count WITHOUT hardcoding "3" — there's a classic
    // C idiom for this: sizeof(an_array) gives the array's total size in
    // bytes, sizeof(an_array[0]) gives one element's size in bytes, and
    // dividing the two gives you the element count, computed at compile
    // time. Same "derive it, don't hardcode it" habit as IterationsForP
    // back in Phase 3 — if you ever add a 4th test value, this keeps working
    // without you remembering to update a separate count by hand.
        uint32_t ArraySize = sizeof(test_data);
        uint32_t ElementSize = sizeof(test_data[0]);
        uint32_t count = (ArraySize/ElementSize); // Sizeof gives me the size of the array in bytes. Using sizeof on a member of the array gives me the size of the member.
    // TODO 2: call mean_f32 on test_data and your count, store the result.
        float Calc_Mean = mean_f32(test_data, count);
    // TODO 3: call variance_f32, passing test_data, the count, AND the
    // you just computed — remember variance_f32 doesn't compute its own
    // mean, it's handed one
        float Variance = variance_f32(test_data, count, Calc_Mean);
        //printf("CHECKPOINT D, variance=%f\n", Variance);
    // TODO 4: call stddev_f32 on the variance you just computed
        float Stddev = stddev_f32(Variance);
        //printf("CHECKPOINT E, stddev=%f\n", Stddev);
        // TODO 5: compare each of the three results against its expected va
        // (2, 4, 2) and print PASS/FAIL, same style as your buffer checkpoint.
        if (fabsf(Calc_Mean - 2) < EPSILON){
            // If value within error limits = PASS
            printf("Mean is correct and inside of allowed error and its value is: %f\n", Calc_Mean);
        } else {
        // FAIL
            printf("Mean is incorrect and not inside of allowed error, its value is %f\n", Calc_Mean);
        }
        if (fabsf(Variance - 4) < EPSILON){
            // If value within error limits = PASS
            printf("Variance is correct and inside of allowed error and its value is: %f\n", Variance);
        } else {
            // If value within error limits = PASS
            printf("Variance is incorrect and not inside of allowed error, its value is %f\n", Variance);
        }
        if (fabsf(Stddev - 2) < EPSILON){
            // If value within error limits = PASS
            printf("Standard deviation is correct and inside of allowed error and its value is: %f\n", Stddev);
        } else {
        // FAIL
            printf("Standard deviation is incorrect and not inside of allowed error, its value is %f\n", Stddev);
        }
            fflush(stdout); // Force USB buffer, to see what is printed beyond buffer logs.

        sensor_init(); // Initialize IMU.
        if (!storage_init()) {
            printf("WARNING: SD storage init failed, logging disabled.\n"); // If failed then print a statement.
        }
        led_init(); // Initialize LED.
        sleep_ms(500); // Give time for IMU initialization to finish.
        multicore_launch_core1(sampling_core1_entry); // Launch core1.
        motion_state_t current_state = MOTION_STILL; // Start as motion state.
        absolute_time_t measure_start = get_absolute_time(); // Establish timestamp for start.
        uint32_t last_known_overflow_count = 0; // Initialize last known overflow count at 0.
        uint32_t last_known_bad_sample_count = 0; // Initialize bad sample count at 0.

    while (true) {
        motion_state_t state;
        motion_state_t debounced_state;
        float stddev;
        
        if (features_classify_window(&state, &stddev)) { // State based print statements.
            debounced_state = features_debounce(state);
            if(debounced_state == MOTION_STILL){
                printf("stddev = %f -> STILL\n", stddev);
            }else if(debounced_state == MOTION_MOVING){
                printf("stddev = %f -> MOVING\n", stddev);
            }else if(debounced_state == MOTION_WALKING){
                printf("stddev = %f -> WALKING\n", stddev);
            }else if(debounced_state == MOTION_RUNNING){
                printf("stddev = %f -> RUNNING\n", stddev);
        }

        uint64_t measured_count = sampling_get_core1_count(); // Core1 sampling count gets called and storaged.
        float elapsed_s = absolute_time_diff_us(measure_start, get_absolute_time()) / 1000000.0f; // Call a function that returns how much time has passed since sampling has started.
        printf("Core 1 achieved Hz: %.2f\n", measured_count / elapsed_s);
        current_state = debounced_state;
        storage_log_transition(debounced_state, stddev);

        uint32_t current_overflow_count = buffer_overflow_count(); // Buffer overflow count call and storing.
        if(current_overflow_count != last_known_overflow_count){ // If the overflow count differs from the last one then print a statement and update the overflow count.
            printf("Warning, overflow is present, current overflow count: %lu\n", current_overflow_count);
            last_known_overflow_count = current_overflow_count;
        }

        uint32_t current_bad_sample_count = sampling_get_bad_sample_count();

        if(current_bad_sample_count != last_known_bad_sample_count){
            printf("Warning, faulty sampling is present, current bad sample count: %lu\n", current_bad_sample_count);
            last_known_bad_sample_count = current_bad_sample_count;
        }
    }
    led_update(current_state);
}
}