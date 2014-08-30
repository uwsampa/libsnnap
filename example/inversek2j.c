/*
 * inversek2j.c
 *
 *  Created on: Dec 6, 2011
 *      Authors: Hadi Esmaeilzadeh <hadianeh@cs.washington.edu>
 *               Thierry Moreau <moreau@cs.washington.edu>
 *               Adrian Sampson <asampson@cs.washington.edu>
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "snnap.h"
#include <string.h>

#define PI 3.14159265

// Kernel parameters.
#define NUM_INPUTS      2
#define NUM_OUTPUTS     2
#define BUFFER_SIZE     64

// Kinematics functions.
const float l1 = 0.5;
const float l2 = 0.5;
void forwardk2j(float theta1, float theta2, float* x, float* y) {
    *x = l1 * cos(theta1) + l2 * cos(theta1 + theta2);
    *y = l1 * sin(theta1) + l2 * sin(theta1 + theta2);
}
void inversek2j(float x, float y, float* theta1, float* theta2) {
    *theta2 = acos(((x * x) + (y * y) - (l1 * l1) - (l2 * l2))/(2 * l1 * l2));
    *theta1 = asin((y * (l1 + l2 * cos(*theta2)) - x * l2 * sin(*theta2))/(x * x + y * y));
}

float * srcData;
float * dstData;

#ifdef NPU_STREAM
// NPU data consumption callback.
void callback(const volatile void *data) {
    memcpy(dstData, (const float *)data, NUM_OUTPUTS * sizeof(float));
    dstData += NUM_INPUTS;
}
#endif

int main (int argc, const char* argv[]) {
    // Performance counters
    unsigned int t_precise;
    unsigned int t_approx;
    unsigned int evt_counter[1] = {0x68};

    // Inversek2j variables
    int i;
    int j;
    int x;
    int n = 4096;

    // Init rand number generator:
    srand (1);

    // Allocate input and output arrays
    float* xy           = (float*)malloc(n * 2 * sizeof (float));
    float* xy_approx    = (float*)malloc(n * 2 * sizeof (float));
    float* t1t2_precise = (float*)malloc(n * 2 * sizeof (float));
    float* t1t2_approx  = (float*)malloc(n * 2 * sizeof (float));

    // Ensure memory allocation was successful
    if(t1t2_approx == NULL || t1t2_precise == NULL || xy == NULL) {
        printf("Cannot allocate memory!\n");
        return -1;
    }

    // Initialize input data
    for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS) {
        x = rand();
        t1t2_precise[i] = (((float)x)/RAND_MAX) * PI / 2;
        x = rand();
        t1t2_precise[i + 1] = (((float)x)/RAND_MAX) *  PI / 2;
        forwardk2j(t1t2_precise[i + 0], t1t2_precise[i + 1], xy + (i + 0), xy + (i + 1));
    }

    printf("\n\nRunning inversek2j benchmark on %u inputs\n\n", n);


    /*** BASELINE (PRECISE) EXECUTION ***/

    for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS) {
        inversek2j(xy[i + 0], xy[i + 1], t1t2_precise + (i + 0), t1t2_precise + (i + 1));
    }


    /*** NPU EXECUTION ***/

#ifdef NPU_STREAM

    // Simpler streaming interface.

    dstData = t1t2_approx;  // For the callback.
    snnap_init();
    struct snnap_stream *stream = snnap_stream_new(NUM_INPUTS * sizeof(float),
            NUM_OUTPUTS * sizeof(float), callback);

    for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS) {
        srcData = xy + i;
        volatile float *iBuff = snnap_stream_write(stream);
        memcpy((float *)iBuff, srcData, NUM_INPUTS * sizeof(float));
        snnap_stream_send(stream);
    }

    snnap_stream_barrier(stream);
    free(stream);

#else

    // Lower-level interface with explicit buffer management.

    for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS * BUFFER_SIZE){
        srcData = (xy + i);
        dstData = (t1t2_approx + i);
        volatile float *iBuff = snnap_writebuf();
        for(j = 0; j < NUM_INPUTS * BUFFER_SIZE; j++) {
            *(iBuff++) = *(srcData ++);
        }
        snnap_sendbuf();
        snnap_block();
        volatile const float *oBuff = snnap_readbuf();
        for(j = 0; j < NUM_OUTPUTS * BUFFER_SIZE; j++) {
            *(dstData++) = *(oBuff ++);
        }
        snnap_consumebuf();
    }

#endif



    /*** REPORTING ***/

    // Perform forward kinematics on approx thetas
    for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS) {
        forwardk2j(t1t2_approx[i + 0], t1t2_approx[i + 1], xy_approx + (i + 0), xy_approx + (i + 1));
    }

    // Compute RMSE and NRMSE
    double RMSE = 0;
    double NRMSE = 0;
    double diff0, diff1;
    float min0 = xy[0];
    float min1 = xy[1];
    float max0 = xy[0];
    float max1 = xy[1];
    unsigned int total = n;
    for (i = 0; i < n * NUM_OUTPUTS; i += NUM_OUTPUTS){
        // Check for nan errors
        if (isnan(t1t2_approx[i+0])||isnan(t1t2_approx[i+1])) {
            printf("ERROR: NaN @ address 0x%X\n", (unsigned int) (t1t2_approx+i));
            exit(0);
        }
        if (isnan(t1t2_precise[i+0])||isnan(t1t2_precise[i+1])) {
            total --;
        } else {
            min0 = (xy[i+0] < min0) ? xy[i+0] : min0;
            min1 = (xy[i+1] < min1) ? xy[i+1] : min1;
            max0 = (xy[i+0] > max0) ? xy[i+0] : max0;
            max1 = (xy[i+1] > max1) ? xy[i+1] : max1;
            diff0 = xy[i+0] - xy_approx[i+0];
            diff1 = xy[i+1] - xy_approx[i+1];
            RMSE += (diff0*diff0+diff1*diff1);
        }
    }
    RMSE = RMSE/total;
    diff0 = max0 - min0;
    diff1 = max1 - min1;
    RMSE = sqrt(RMSE);
    NRMSE = RMSE/(sqrt(diff0+diff1));

    printf("==> RMSE = %.4f (NRMSE = %.2f%%)\n", (float) RMSE, (float) ((100.*NRMSE)));

    free(t1t2_precise);
    free(t1t2_approx);
    free(xy);
    free(xy_approx);

    return 0;
}


