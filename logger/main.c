#include "icm20948.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

#include "f_util.h"
#include "ff.h"
#include "hw_config.h"
#include "sd_card.h"

#define SAMPLE_RATE_HZ 500
#define PERIOD_US (1000000 / SAMPLE_RATE_HZ)
#define BUF_BYTES 4096

/* Both settable from the build so one capture run needs no source edit:
 *   cmake -DOUT_NAME=still -DRUN_SECONDS=15 ..   */
#ifndef RUN_SECONDS
#define RUN_SECONDS 60
#endif
#ifndef OUT_NAME
#define OUT_NAME "imu"
#endif
#ifndef START_DELAY_MS
#define START_DELAY_MS 3000   /* time to get into position before logging */
#endif

typedef struct {
  int16_t ax, ay, az;
  int16_t gx, gy, gz;
  uint32_t t_us; // timestamp (lower 32 bits is fine for short runs)
} __attribute__((packed)) sample_t;

static FATFS fs6;
static uint8_t buf[BUF_BYTES];
static size_t buf_used = 0;

/* Variant 6 - Stage 3: single core. Deadline-paced sampling, 16-byte binary
 * records batched into a 4 KB buffer, one f_write per full buffer. Kept as the
 * baseline that variant 7 is measured against. */
int main_6(void) {
  stdio_init_all();
  sleep_ms(START_DELAY_MS);
  IMU_EN_SENSOR_TYPE type;

  imuInit(&type);
  printf("imu: %s\n",
         type == IMU_EN_SENSOR_TYPE_ICM20948 ? "ICM-20948" : "NOT FOUND");

  if (!sd_init_driver()) {
    printf("sd_init_driver() FAILED\n");
    while (1)
      tight_loop_contents();
  }

  sd_card_t *sd = sd_get_by_num(0);
  if (!sd) {
    printf("sd_get_by_num(0) returned NULL\n");
    while (1)
      tight_loop_contents();
  }

  const char *drive = sd_get_drive_prefix(sd);
  printf("drive = \"%s\"\n", drive);

  FRESULT fr = f_mount(&fs6, drive, 1);
  printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);
  if (fr != FR_OK) {
    while (1)
      tight_loop_contents();
  }

  /* --- open the output file ------------------------------------------- */
  char path[64];
  snprintf(path, sizeof path, "%s/" OUT_NAME ".bin", drive);

  FIL f;
  fr = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
  printf("f_open(%s) -> %s (%d)\n", path, FRESULT_str(fr), fr);
  if (fr != FR_OK) {
    while (1)
      tight_loop_contents();
  }

  /* --- log ------------------------------------------------------------- */
  printf("logging %d s at %d Hz, %u-byte records, %d-byte buffer\n",
         RUN_SECONDS, SAMPLE_RATE_HZ, (unsigned)sizeof(sample_t), BUF_BYTES);

  uint32_t n_samples = 0;   /* records appended to the buffer  */
  uint32_t n_writes  = 0;   /* f_write calls made              */
  uint32_t n_late    = 0;   /* iterations that missed deadline */
  uint32_t worst_us  = 0;   /* slowest single f_write          */

  uint64_t t_start = time_us_64();
  absolute_time_t next      = get_absolute_time();
  absolute_time_t next_sync = make_timeout_time_ms(1000);

  while (time_us_64() - t_start < (uint64_t)RUN_SECONDS * 1000000ull) {

    /* Advance from the previous deadline, not from now: a slow iteration is
     * then absorbed by the next one waiting less, instead of drifting. */
    next = delayed_by_us(next, PERIOD_US);

    /* 1. read one sample */
    sample_t s;
    icm20948AccelFastRead(&s.ax, &s.ay, &s.az);
    icm20948GyroFastRead(&s.gx, &s.gy, &s.gz);
    s.t_us = (uint32_t)time_us_64();

    /* 2. append, flushing first if another record would not fit */
    if (buf_used + sizeof(sample_t) > BUF_BYTES) {
      UINT bw = 0;
      uint64_t w0 = time_us_64();
      fr = f_write(&f, buf, (UINT)buf_used, &bw);
      uint32_t w_us = (uint32_t)(time_us_64() - w0);
      if (w_us > worst_us)
        worst_us = w_us;

      if (fr != FR_OK || bw != buf_used) {
        printf("f_write -> %s (%d), %u of %u bytes\n", FRESULT_str(fr), fr, bw,
               (unsigned)buf_used);
        break;
      }
      buf_used = 0;
      n_writes++;
    }
    memcpy(buf + buf_used, &s, sizeof s);
    buf_used += sizeof s;
    n_samples++;

    /* 3. sync on a timer, not per write */
    if (absolute_time_diff_us(get_absolute_time(), next_sync) < 0) {
      f_sync(&f);
      next_sync = make_timeout_time_ms(1000);
    }

    /* 4. wait out the period. If we are already past it we never wait, and
     * that iteration counts as late. */
    if (absolute_time_diff_us(get_absolute_time(), next) < 0)
      n_late++;
    busy_wait_until(next);
  }

  /* --- flush and close -------------------------------------------------- */
  if (buf_used) {
    UINT bw = 0;
    fr = f_write(&f, buf, (UINT)buf_used, &bw);
    printf("final f_write -> %s (%d), %u bytes\n", FRESULT_str(fr), fr, bw);
    buf_used = 0;
    n_writes++;
  }
  fr = f_close(&f);
  printf("f_close -> %s (%d)\n", FRESULT_str(fr), fr);

  uint64_t elapsed = time_us_64() - t_start;
  double   secs    = (double)elapsed / 1000000.0;

  printf("\n--- result ---\n");
  printf("stored   %lu samples in %.2f s = %.1f Hz\n", (unsigned long)n_samples,
         secs, (double)n_samples / secs);
  printf("bytes    %lu\n", (unsigned long)n_samples * sizeof(sample_t));
  printf("writes   %lu (%u bytes each)\n", (unsigned long)n_writes, BUF_BYTES);
  printf("late     %lu of %lu iterations (%.1f%%)\n", (unsigned long)n_late,
         (unsigned long)n_samples, 100.0 * n_late / (double)n_samples);
  printf("worst    f_write %lu us\n", (unsigned long)worst_us);

  while (1)
    tight_loop_contents();
  return 0;
}

/* ===========================================================================
 * Variant 7 - Stage 4 + 5: multicore acquisition with a touch drawing UI.
 *
 *   core 1  sampler only. Reads the IMU on a fixed deadline and pushes into a
 *           lock-free ring. Never touches the SD card, the LCD or printf, so
 *           nothing the storage side does can stretch a sampling interval.
 *   core 0  everything else: drains the ring to the card, runs the touch UI,
 *           updates the status indicator.
 *
 * The two cores share a single-producer/single-consumer ring. Only core 1
 * writes ring_head, only core 0 writes ring_tail, so no lock is needed - just
 * a barrier so the slot is visible before the index that publishes it.
 * ======================================================================== */

#define RING_SLOTS 1024u /* 1024 * 16 B = 16 KB = ~2 s of slack at 500 Hz */
#define RING_MASK (RING_SLOTS - 1u)

static sample_t ring[RING_SLOTS];
static volatile uint32_t ring_head;  /* written by core 1 only */
static volatile uint32_t ring_tail;  /* written by core 0 only */
static volatile uint32_t n_produced; /* samples core 1 took          */
static volatile uint32_t n_dropped;  /* samples lost to a full ring  */
static volatile uint32_t n_late;     /* iterations that missed their deadline */
static volatile uint32_t ring_peak;  /* high-water mark               */
static volatile bool run_sampler;

/* --- core 1: the sampler ------------------------------------------------- */
static void core1_sampler(void) {
  absolute_time_t next = get_absolute_time();

  while (run_sampler) {
    next = delayed_by_us(next, PERIOD_US);

    sample_t s;
    icm20948AccelFastRead(&s.ax, &s.ay, &s.az);
    icm20948GyroFastRead(&s.gx, &s.gy, &s.gz);
    s.t_us = (uint32_t)time_us_64();

    uint32_t h = ring_head;
    uint32_t used = h - ring_tail; /* unsigned: correct across wraparound */

    if (used < RING_SLOTS) {
      ring[h & RING_MASK] = s;
      __dmb(); /* publish the slot before the index that exposes it */
      ring_head = h + 1;
      if (used + 1 > ring_peak)
        ring_peak = used + 1;
    } else {
      n_dropped++; /* never block the sampler - count it and move on */
    }
    n_produced++;

    if (absolute_time_diff_us(get_absolute_time(), next) < 0)
      n_late++;
    busy_wait_until(next);
  }

  while (1)
    tight_loop_contents();
}

/* --- core 0 -------------------------------------------------------------- */
int main_7(void) {
  stdio_init_all();
  sleep_ms(START_DELAY_MS);

  IMU_EN_SENSOR_TYPE type;
  imuInit(&type);
  printf("imu: %s\n",
         type == IMU_EN_SENSOR_TYPE_ICM20948 ? "ICM-20948" : "NOT FOUND");

  if (!sd_init_driver()) {
    printf("sd_init_driver() FAILED\n");
    while (1)
      tight_loop_contents();
  }
  sd_card_t *sd = sd_get_by_num(0);
  const char *drive = sd_get_drive_prefix(sd);

  FRESULT fr = f_mount(&fs6, drive, 1);
  printf("f_mount -> %s (%d)\n", FRESULT_str(fr), fr);
  if (fr != FR_OK) {
    while (1)
      tight_loop_contents();
  }

  char path[64];
  snprintf(path, sizeof path, "%s/" OUT_NAME ".bin", drive);
  FIL f;
  fr = f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS);
  printf("f_open(%s) -> %s (%d)\n", path, FRESULT_str(fr), fr);
  if (fr != FR_OK) {
    while (1)
      tight_loop_contents();
  }

  /* --- start the sampler on core 1 --------------------------------------- */
  printf("logging %d s at %d Hz, %u-byte records, %d-byte buffer, %u-slot ring\n",
         RUN_SECONDS, SAMPLE_RATE_HZ, (unsigned)sizeof(sample_t), BUF_BYTES,
         RING_SLOTS);

  run_sampler = true;
  multicore_launch_core1(core1_sampler);

  uint32_t n_stored = 0, n_writes = 0, worst_us = 0;
  uint64_t t_start = time_us_64();
  absolute_time_t next_sync = make_timeout_time_ms(1000);

  while (time_us_64() - t_start < (uint64_t)RUN_SECONDS * 1000000ull) {

    /* 1. drain the ring into the block buffer */
    while (ring_tail != ring_head) {
      if (buf_used + sizeof(sample_t) > BUF_BYTES) {
        UINT bw = 0;
        uint64_t w0 = time_us_64();
        fr = f_write(&f, buf, (UINT)buf_used, &bw);
        uint32_t w_us = (uint32_t)(time_us_64() - w0);
        if (w_us > worst_us)
          worst_us = w_us;
        if (fr != FR_OK || bw != buf_used) {
          printf("f_write -> %s (%d), %u of %u\n", FRESULT_str(fr), fr, bw,
                 (unsigned)buf_used);
          goto done;
        }
        buf_used = 0;
        n_writes++;
      }
      uint32_t t = ring_tail;
      memcpy(buf + buf_used, &ring[t & RING_MASK], sizeof(sample_t));
      __dmb(); /* finish reading the slot before freeing it */
      ring_tail = t + 1;
      buf_used += sizeof(sample_t);
      n_stored++;
    }

    /* 2. sync on a timer, not per write */
    if (absolute_time_diff_us(get_absolute_time(), next_sync) < 0) {
      f_sync(&f);
      next_sync = make_timeout_time_ms(1000);
    }

  }

done:
  run_sampler = false;

  if (buf_used) {
    UINT bw = 0;
    f_write(&f, buf, (UINT)buf_used, &bw);
    buf_used = 0;
    n_writes++;
  }
  f_close(&f);

  uint64_t elapsed = time_us_64() - t_start;
  double secs = (double)elapsed / 1000000.0;

  printf("\n--- result ---\n");
  printf("sampled  %lu (core 1)\n", (unsigned long)n_produced);
  printf("stored   %lu samples in %.2f s = %.1f Hz\n", (unsigned long)n_stored,
         secs, (double)n_stored / secs);
  printf("dropped  %lu (%.3f%%)\n", (unsigned long)n_dropped,
         100.0 * n_dropped / (double)(n_produced ? n_produced : 1));
  printf("late     %lu (%.3f%%)\n", (unsigned long)n_late,
         100.0 * n_late / (double)(n_produced ? n_produced : 1));
  printf("ring max %lu of %u slots\n", (unsigned long)ring_peak, RING_SLOTS);
  printf("writes   %lu, worst f_write %lu us\n", (unsigned long)n_writes,
         (unsigned long)worst_us);

  while (1)
    tight_loop_contents();
  return 0;
}


#ifndef IMU_VARIANT
#define IMU_VARIANT 7
#endif

int main(void) {
#if IMU_VARIANT == 6
  return main_6();
#elif IMU_VARIANT == 7
  return main_7();
#else
#error "IMU_VARIANT must be 6 (single core) or 7 (two cores)"
#endif
}
