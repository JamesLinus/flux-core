/*****************************************************************************\
 *  Copyright (c) 2016 Lawrence Livermore National Security, LLC.  Produced at
 *  the Lawrence Livermore National Laboratory (cf, AUTHORS, DISCLAIMER.LLNS).
 *  LLNL-CODE-658032 All rights reserved.
 *
 *  This file is part of the Flux resource manager framework.
 *  For details, see https://github.com/flux-framework.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU Lesser General Public License as published
 *  by the Free Software Foundation; either version 2 of the license,
 *  or (at your option) any later version.
 *
 *  Flux is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the IMPLIED WARRANTY OF MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the terms and conditions of the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software Foundation,
 *  Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *  See also:  http://www.gnu.org/licenses/
\*****************************************************************************/

#ifndef FLUX_PMI2_H_INCLUDED
#define FLUX_PMI2_H_INCLUDED

#define PMI2_SUCCESS                0
#define PMI2_FAIL                   -1
#define PMI2_ERR_INIT               1
#define PMI2_ERR_NOMEM              2
#define PMI2_ERR_INVALID_ARG        3
#define PMI2_ERR_INVALID_KEY        4
#define PMI2_ERR_INVALID_KEY_LENGTH 5
#define PMI2_ERR_INVALID_VAL        6
#define PMI2_ERR_INVALID_VAL_LENGTH 7
#define PMI2_ERR_INVALID_LENGTH     8
#define PMI2_ERR_INVALID_NUM_ARGS   9
#define PMI2_ERR_INVALID_ARGS       10
#define PMI2_ERR_INVALID_NUM_PARSED 11
#define PMI2_ERR_INVALID_KEYVALP    12
#define PMI2_ERR_INVALID_SIZE       13
#define PMI2_ERR_OTHER              14

#define PMI2_MAX_KEYLEN             64
#define PMI2_MAX_VALLEN             1024
#define PMI2_MAX_ATTRVALUE          1024
#define PMI2_ID_NULL                -1


int PMI2_Init (int *spawned, int *size, int *rank, int *appnum);
int PMI2_Finalize (void);
int PMI2_Initialized (void);
int PMI2_Abort (int flag, const char msg[]);


typedef struct PMI2_Connect_comm {
    int (*read)(void *buf, int maxlen, void *ctx);
    int (*write)(const void *buf, int len, void *ctx);
    void *ctx;
    int  isMaster;
} PMI2_Connect_comm_t;

typedef struct MPID_Info {
    int                 handle;
    int                 pobj_mutex;
    int                 ref_count;
    struct MPID_Info    *next;
    char                *key;
    char                *value;
} MPID_Info;

int PMI2_Job_Spawn (int count, const char * cmds[],
                    int argcs[], const char ** argvs[],
                    const int maxprocs[],
                    const int info_keyval_sizes[],
                    const struct MPID_Info *info_keyval_vectors[],
                    int preput_keyval_size,
                    const struct MPID_Info *preput_keyval_vector[],
                    char jobId[], int jobIdSize,
                    int errors[]);
int PMI2_Job_GetId (char jobid[], int jobid_size);
int PMI2_Job_GetRank (int* rank);
int PMI2_Job_Connect (const char jobid[], PMI2_Connect_comm_t *conn);
int PMI2_Job_Disconnect (const char jobid[]);


int PMI2_KVS_Put (const char key[], const char value[]);
int PMI2_KVS_Get (const char *jobid, int src_pmi_id,
                  const char key[], char value [], int maxvalue, int *vallen);
int PMI2_KVS_Fence (void);


int PMI2_Info_GetSize (int* size);
int PMI2_Info_GetNodeAttr (const char name[],
                           char value[], int valuelen, int *found, int waitfor);
int PMI2_Info_GetNodeAttrIntArray (const char name[], int array[],
                                   int arraylen, int *outlen, int *found);
int PMI2_Info_PutNodeAttr (const char name[], const char value[]);
int PMI2_Info_GetJobAttr (const char name[],
                          char value[], int valuelen, int *found);
int PMI2_Info_GetJobAttrIntArray (const char name[], int array[],
                                  int arraylen, int *outlen, int *found);


int PMI2_Nameserv_publish (const char service_name[],
                           const struct MPID_Info *info_ptr, const char port[]);
int PMI2_Nameserv_lookup (const char service_name[],
                          const struct MPID_Info *info_ptr,
                          char port[], int portLen);
int PMI2_Nameserv_unpublish (const char service_name[],
                             const struct MPID_Info *info_ptr);



#endif /* !FLUX_PMI2_H_INCLUDED */

/*
 * vi:tabstop=4 shiftwidth=4 expandtab
 */