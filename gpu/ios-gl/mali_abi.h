#ifndef MALI_ABI_H
#define MALI_ABI_H

#include <stdint.h>

#define MALI_CMD_INFO       0   /* out: [0]=gpu_id(0x400), [1]=num_pp(4), [2]=work_pa, [3]=mem_size */
#define MALI_CMD_CLEAR      1   /* in:  [0]=fb_pa, [1]=w, [2]=h, [3]=stride, [4]=rgba */
#define MALI_CMD_MAP_VA     2   /* in:  [0]=va, [1]=pa, [2]=size */
#define MALI_CMD_SUBMIT_GP    3 /* struct_in: uint32_t gp[6] */
#define MALI_CMD_SUBMIT_PP    4 /* struct_in: struct mali_pp_submit */
#define MALI_CMD_SUBMIT_GP_PP 8 /* struct_in: struct mali_gp_pp_submit */
#define MALI_CMD_WAIT_IDLE    9 /* scalar: wait for in-flight async PP */

struct mali_pp_submit {
    uint32_t num_pp;
    uint32_t plbu[4];
    uint32_t stack[4];
    uint32_t frame[23];
    uint32_t wb[36];
};

struct mali_gp_pp_submit {
    uint32_t gp[6];
    struct mali_pp_submit pp;
};

#endif
