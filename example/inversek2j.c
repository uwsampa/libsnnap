/*
 * inversek2j.c
 *
 *  Created on: Dec 6, 2011
 *      Authors: Hadi Esmaeilzadeh <hadianeh@cs.washington.edu>
 *               Thierry Moreau <moreau@cs.washington.edu>
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include "snnap.h"

#include "kinematics.h"

#define PI 3.14159265

// Inversek2j parameters
#define NUM_INPUTS      2
#define NUM_OUTPUTS     2
#define BUFFER_SIZE     64
// TIMER:
// 0 - Use ARM performance counters
// 1 - Use FPGA clock counter
#define TIMER           1 
// POWER MODE:
// 0 - Normal operation
// 1 - Loop on precise execution
// 2 - Loop on approximate execution
#define POWER_MODE      0 

// Global variables
long long int t_kernel_precise;
long long int t_kernel_approx;
long long int dynInsn_kernel_approx;


int main (int argc, const char* argv[]) {

    // Performance counters
    unsigned int t_precise;
    unsigned int t_approx;
    unsigned int dynInsn_precise;
    unsigned int dynInsn_approx;
    unsigned int evt_counter[1] = {0x68};

    // Inversek2j variables
    int i;
    int j;
    int x;
    int n;


    ///////////////////////////////
    // 1 - Initialization
    ///////////////////////////////

    // Init performance counters:
    t_kernel_precise = 0;
    t_kernel_approx = 0;
    dynInsn_kernel_approx = 0;
    
    // Init rand number generator:
    srand (1);
    
    // Set input size to 100000 if not set
    if (argc < 2) {
        n = 4096;
    } else {
        n = atoi(argv[1]);
    }
    assert (n%(BUFFER_SIZE)==0);
    
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

    
    ///////////////////////////////
    // 2 - Precise execution
    ///////////////////////////////
    
    

        for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS) {
        

            inversek2j(xy[i + 0], xy[i + 1], t1t2_precise + (i + 0), t1t2_precise + (i + 1));


        }
    

    


    ///////////////////////////////
    // 3 - Approximate execution
    ///////////////////////////////
    
    // Pointers NPU based inversek2j
    float * srcData;
    float * dstData;
    

    
    snnap_init();
    
        
        // NPU OFFLOADING
        for (i = 0; i < n * NUM_INPUTS; i += NUM_INPUTS * BUFFER_SIZE){
        
            
            srcData = (xy + i);
            dstData = (t1t2_approx + i);
            volatile float *iBuff = snnap_writebuf();
            for(j = 0; j < NUM_INPUTS * BUFFER_SIZE; j++) {
                *(iBuff++) = *(srcData ++);
            }
            snnap_block();
            volatile const float *oBuff = snnap_readbuf();
            for(j = 0; j < NUM_OUTPUTS * BUFFER_SIZE; j++) {
                *(dstData++) = *(oBuff ++);
            }
            snnap_consumebuf();
            

        }
        
/*
int k;
for (k = 0; k < n * NUM_INPUTS; k += NUM_INPUTS)
          printf("\n%f\t%f", *(t1t2_approx + (k + 0)), *(t1t2_approx + (k + 1)));
          */

    //andreolb
    //dynInsn_approx = get_eventcount(0) - dynInsn_approx; 
    
    
    
    ///////////////////////////////
    // 4 - Compute RMSE
    ///////////////////////////////
    
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
            //printf("Hello Andre, the RMSE is %f\n", RMSE);
        }
    }
    RMSE = RMSE/total;
    diff0 = max0 - min0;
    diff1 = max1 - min1;
    RMSE = sqrt(RMSE);
    NRMSE = RMSE/(sqrt(diff0+diff1));
    
    
    ///////////////////////////////
    // 5 - Report results
    ///////////////////////////////

    printf("==> RMSE = %.4f (NRMSE = %.2f%%)\n", (float) RMSE, (float) ((100.*NRMSE)));
    

    ///////////////////////////////
    // 6 - Free memory
    ///////////////////////////////
    
    free(t1t2_precise);
    free(t1t2_approx);
    free(xy);
    free(xy_approx);

    return 0;
}


