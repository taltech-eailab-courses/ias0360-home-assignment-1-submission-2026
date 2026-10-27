#include "buffer.h"
#include "stdio.h"
#include "pico/util/queue.h"

    // TODO 1: derive from rate x stall tolerance; show the arithmetic in a comment. 500Hz*0.5seconds=250

static queue_t sample_queue;
static volatile uint32_t overflow_count = 0;

// TODO 2: buffer_init - initialise sample_queue. Read queue_init's signature
//         (queue.h line 70): what should element_size be? Use sizeof, not a bare 4.
void buffer_init(void){
    bool initialized = queue_init(&sample_queue, sizeof(float), BUFFER_CAPACITY); //float holds 4 bytes here, but queue init needs to know that. first is the address of hte queue to be set up. next is the size of one element in the queue and third is the number of entries in the queue.
        if(initialized == true){
            printf("Queue initialized successfully. \n");
        }
        else{
            printf("Queue initialization failed.");
        }
}

// TODO 3: buffer_push - try to add WITHOUT ever blocking. If it fails,
//         increment overflow_count and report failure to the caller.
    bool buffer_push(float value){
        bool queue_add = queue_try_add(&sample_queue, &value);
        if(queue_add != 1){
            overflow_count ++;
            return 0;
        }else{
            return 1;
        }
    }
// TODO 4: buffer_pop - try to remove one element into *out, return whether it worked.
    bool buffer_pop(float *out){
        bool queue_remove = queue_try_remove(&sample_queue, out);
        if(queue_remove != 1){
            return 0;
        }else{
            return 1;
        }
    }
uint32_t buffer_level(void){
    return queue_get_level(&sample_queue);
}
// TODO 5: buffer_overflow_count - return the counter.
uint32_t buffer_overflow_count(void){
    return overflow_count;
}

