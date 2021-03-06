/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2018 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef STDC_HEADERS
#include <stddef.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <errno.h>

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-dcerpc.h"
#include "libsmb2-raw.h"
#include "libsmb2-private.h"

#define container_of(ptr, type, member) ({                      \
        const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
        (type *)( (char *)__mptr - offsetof(type,member) );})

struct dcerpc_deferred_pointer {
        dcerpc_coder coder;
        void *ptr;
};

#define MAX_DEFERRED_PTR 1024

#define SRVSVC_UUID    0x4b324fc8, 0x1670, 0x01d3, 0x12785a47bf6ee188
#define NDR32_UUID     0x8a885d04, 0x1ceb, 0x11c9, 0x9fe808002b104860
#define NDR64_UUID     0x71710533, 0xbeba, 0x4937, 0x8319b5dbef9ccc36

p_syntax_id_t srvsvc_interface = {
        {SRVSVC_UUID}, 3, 0
};

p_syntax_id_t ndr32_syntax = {
        {NDR32_UUID}, 2
};

p_syntax_id_t ndr64_syntax = {
        {NDR64_UUID}, 1
};

struct dcerpc_context {
        struct smb2_context *smb2;
        const char *path;
        p_syntax_id_t *syntax;
        smb2_file_id file_id;

        uint8_t tctx_id; /* 0:NDR32 1:NDR64 */
        uint32_t call_id;
};

struct dcerpc_header {
        uint8_t  rpc_vers;
        uint8_t  rpc_vers_minor;
        uint8_t  PTYPE;
        uint8_t  pfc_flags;
        uint8_t  packed_drep[4];
        uint16_t frag_length;
        uint16_t auth_length;
        uint32_t call_id;
};

struct dcerpc_bind_pdu {
        uint16_t max_xmit_frag;
        uint16_t max_recv_frag;
        uint32_t assoc_group_id;

        /* presentation context list */
        p_syntax_id_t *abstract_syntax;
};

#define ACK_RESULT_ACCEPTANCE         0
#define ACK_RESULT_USER_REJECTION     1
#define ACK_RESULT_PROVIDER_REJECTION 2

#define ACK_REASON_REASON_NOT_SPECIFIED                     0
#define ACK_REASON_ABSTRACT_SYNTAX_NOT_SUPPORTED            1
#define ACK_REASON_PROPOSED_TRANSFER_SYNTAXES_NOT_SUPPORTED 2
#define ACK_REASON_PROTOCOL_VERSION_NOT_SUPPORTED           4

struct dcerpc_bind_context_results {
        uint16_t ack_result;
        uint16_t ack_reason;
        struct dcerpc_uuid uuid;
        uint32_t syntax_version;
};

#define MAX_ACK_RESULTS 4
struct dcerpc_bind_ack_pdu {
        uint16_t max_xmit_frag;
        uint16_t max_recv_frag;
        uint32_t assoc_group_id;

        /* presentation context list */
        uint8_t num_results;
        struct dcerpc_bind_context_results results[MAX_ACK_RESULTS];
};

struct dcerpc_request_pdu {
        uint32_t alloc_hint;
        uint16_t context_id;
        uint16_t opnum;

      /* optional field for request, only present if the PFC_OBJECT_UUID
         * field is non-zero */
      /*  uuid_t  object;              24:16 object UID */

      /* stub data, 8-octet aligned 
                   .
                   .
                   .                 */
};

struct dcerpc_response_pdu {
        uint32_t alloc_hint;
        uint16_t context_id;
        uint8_t cancel_count;
        uint8_t reserverd;
      /* stub data, 8-octet aligned 
                   .
                   .
                   .                 */
};

/* PDU Types */
#define PDU_TYPE_REQUEST             0
#define PDU_TYPE_PING                1
#define PDU_TYPE_RESPONSE            2
#define PDU_TYPE_FAULT               3
#define PDU_TYPE_WORKING             4
#define PDU_TYPE_NOCALL              5
#define PDU_TYPE_REJECT              6
#define PDU_TYPE_ACK                 7
#define PDU_TYPE_CL_CANCEL           8
#define PDU_TYPE_FACK                9
#define PDU_TYPE_CANCEL_ACK         10
#define PDU_TYPE_BIND               11
#define PDU_TYPE_BIND_ACK           12
#define PDU_TYPE_BIND_NAK           13
#define PDU_TYPE_ALTER_CONTEXT      14
#define PDU_TYPE_ALTER_CONTEXT_RESP 15
#define PDU_TYPE_SHUTDOWN           17
#define PDU_TYPE_CO_CANCEL          18
#define PDU_TYPE_ORPHANED           19


/* Flags */
#define PFC_FIRST_FRAG      0x01
#define PFC_LAST_FRAG       0x02
#define PFC_PENDING_CANCEL  0x04
#define PFC_RESERVED_1      0x08
#define PFC_CONC_MPX        0x10
#define PFC_DID_NOT_EXECUTE 0x20
#define PFC_MAYBE           0x40
#define PFC_OBJECT_UUID     0x80

/* Data representation */
/* Integer */
#define DCERPC_DR_BYTE_ORDER_BIG_ENDIAN         0x00
#define DCERPC_DR_BYTE_ORDER_LITTLE_ENDIAN      0x10
/* Character */
#define DCERPC_DR_CHARACTER_ASCII               0x00
#define DCERPC_DR_CHARACTER_EBCDIC              0x01

#define NSE_BUF_SIZE 128*1024

struct dcerpc_cb_data {
        struct dcerpc_context *dce;
        dcerpc_cb cb;
        void *cb_data;
};

struct dcerpc_pdu {
        struct dcerpc_header hdr;

        union {
                struct dcerpc_bind_pdu bind;
                struct dcerpc_bind_ack_pdu bind_ack;
                struct dcerpc_request_pdu req;
                struct dcerpc_response_pdu rsp;
        };

        /* optional authentication verifier */
        /* following fields present iff auth_length != 0 */
        /*auth_verifier_co_t   auth_verifier; */

        struct dcerpc_context *dce;
        dcerpc_cb cb;
        void *cb_data;

        dcerpc_coder decoder;
        int decode_size;
        void *payload;

        int top_level;
        uint64_t ptr_id;

        int cur_ptr;
        int max_ptr;
        struct dcerpc_deferred_pointer ptrs[MAX_DEFERRED_PTR];
};

struct smb2_context *
dcerpc_get_smb2_context(struct dcerpc_context *dce)
{
        return dce->smb2;
}

void *
dcerpc_get_pdu_payload(struct dcerpc_pdu *pdu)
{
        return pdu->payload;
}

struct dcerpc_context *
dcerpc_create_context(struct smb2_context *smb2, const char *path,
                      p_syntax_id_t *syntax)
{
        struct dcerpc_context *ctx;

        ctx = malloc(sizeof(struct dcerpc_context));
        if (ctx == NULL) {
                smb2_set_error(smb2, "Failed to allcoate dcercp context.");
                return NULL;
        }
        memset(ctx, 0, sizeof(struct dcerpc_context));

        ctx->smb2 = smb2;
        ctx->call_id = 2;
        ctx->path = strdup(path);
        if (ctx->path == NULL) {
                smb2_set_error(smb2, "Failed to allocate path for "
                               "dcercp context.");
                free(ctx);
                return NULL;
        }
        ctx->syntax = syntax;

        return ctx;
}

void
dcerpc_destroy_context(struct dcerpc_context *dce)
{
        if (dce == NULL) {
                return;
        }
        free(discard_const(dce->path));
        free(dce);
}

static void
dcerpc_free_pdu(struct dcerpc_context *dce, struct dcerpc_pdu *pdu)
{
        if (pdu == NULL) {
                return;
        }

        if (pdu->payload) {
                smb2_free_data(dce->smb2, pdu->payload);
        }
        free(pdu);
}

static struct dcerpc_pdu *
dcerpc_allocate_pdu(struct dcerpc_context *dce)
{
        struct dcerpc_pdu *pdu;

        pdu = malloc(sizeof(struct dcerpc_pdu));
        if (pdu == NULL) {
                smb2_set_error(dce->smb2, "Failed to allocate DCERPC PDU");
                return NULL;
        }

        memset(pdu, 0, sizeof(struct dcerpc_pdu));
        pdu->dce = dce;
        pdu->hdr.call_id = dce->call_id++;

        return pdu;
}

void
dcerpc_add_deferred_pointer(struct dcerpc_context *ctx,
                            struct dcerpc_pdu *pdu,
                            dcerpc_coder coder, void *ptr)
{
        pdu->ptrs[pdu->max_ptr].coder = coder;
        pdu->ptrs[pdu->max_ptr].ptr = ptr;
        pdu->max_ptr++;
}

int
dcerpc_process_deferred_pointers(struct dcerpc_context *ctx,
                                 struct dcerpc_pdu *pdu,
                                 struct smb2_iovec *iov,
                                 int offset)
{
        struct dcerpc_deferred_pointer *dp;
        int idx;

        while (pdu->cur_ptr != pdu->max_ptr) {
                idx = pdu->cur_ptr++;
                dp = &pdu->ptrs[idx];
                offset = dp->coder(ctx, pdu, iov, offset, dp->ptr);
        }
        return offset;
}

int
dcerpc_decode_32(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                 struct smb2_iovec *iov, int offset, void *ptr)
{
        if (offset < 0) {
                return offset;
        }

        smb2_get_uint32(iov, offset, ptr);
        offset += 4;

        return offset;
}

int
dcerpc_encode_32(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                 struct smb2_iovec *iov, int offset, void *ptr)
{
        uint32_t val = *((uint32_t *)ptr);

        if (offset < 0) {
                return offset;
        }

        offset = (offset + 3) & ~3;

        smb2_set_uint32(iov, offset, val);
        offset += 4;

        return offset;
}

int
dcerpc_encode_16(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                 struct smb2_iovec *iov,
                 int offset, void *ptr)
{
        uint16_t val = *((uint16_t *)ptr);

        if (offset < 0) {
                return offset;
        }

        offset = (offset + 1) & ~1;

        smb2_set_uint16(iov, offset, val);
        offset += 2;

        return offset;
}

/* Encode words that vary in size depending on the tranport syntax */
int
dcerpc_encode_3264(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                   struct smb2_iovec *iov,
                   int offset, uint64_t val)
{
        if (offset < 0) {
                return offset;
        }

        if (ctx->tctx_id) {
                offset = (offset + 7) & ~7;

                /* NDR64 */
                smb2_set_uint64(iov, offset, val);
                offset += 8;
        } else {
                offset = (offset + 3) & ~3;

                /* NDR32 */
                smb2_set_uint32(iov, offset, val);
                offset += 4;
        }
        return offset;
}

int
dcerpc_decode_3264(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                   struct smb2_iovec *iov, int offset, void *ptr)
{
        uint32_t u32;
        uint64_t u64;

        if (offset < 0) {
                return offset;
        }

        if (ctx->tctx_id) {
                offset = (offset + 7) & ~7;

                /* NDR64 */
                smb2_get_uint64(iov, offset, &u64);
                *(uint64_t *)ptr = u64;
                offset += 8;
        } else {
                offset = (offset + 3) & ~3;

                /* NDR32 */
                smb2_get_uint32(iov, offset, &u32);
                *(uint64_t *)ptr = u32;
                offset += 4;
        }
        return offset;
}

int
dcerpc_encode_ptr(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                  struct smb2_iovec *iov,
                  int offset, void *ptr,
                  enum ptr_type type, dcerpc_coder coder)
{
        int top_level = pdu->top_level;

        if (offset < 0) {
                return offset;
        }

        if (dce->tctx_id) {
                offset = (offset + 7) & ~7;
        } else {
                offset = (offset + 3) & ~3;
        }

        switch (type) {
        case PTR_REF:
                if (pdu->top_level) {
                        pdu->top_level = 0;
                        offset = coder(dce, pdu, iov, offset, ptr);
                        pdu->top_level = top_level;
                        return offset;
                }
                
                pdu->ptr_id++;
                offset = dcerpc_encode_3264(dce, pdu, iov, offset, pdu->ptr_id);
                dcerpc_add_deferred_pointer(dce, pdu, coder, ptr);
                break;
        case PTR_UNIQUE:
                if (ptr == NULL) {
                        offset = dcerpc_encode_3264(dce, pdu, iov, offset, 0);
                        return offset;
                }
                
                pdu->ptr_id++;
                offset = dcerpc_encode_3264(dce, pdu, iov, offset, pdu->ptr_id);
                if (pdu->top_level) {
                        pdu->top_level = 0;
                        offset = coder(dce, pdu, iov, offset, ptr);
                        pdu->top_level = top_level;
                } else {
                        dcerpc_add_deferred_pointer(dce, pdu, coder, ptr);
                }
                break;
        }

        return offset;
}

int
dcerpc_decode_ptr(struct dcerpc_context *dce, struct dcerpc_pdu *pdu,
                  struct smb2_iovec *iov, int offset, void *ptr,
                  enum ptr_type type, dcerpc_coder coder)
{
        int top_level = pdu->top_level;
        uint64_t p;

        if (offset < 0) {
                return offset;
        }

        if (dce->tctx_id) {
                offset = (offset + 7) & ~7;
        } else {
                offset = (offset + 3) & ~3;
        }

        switch (type) {
        case PTR_REF:
                if (pdu->top_level) {
                        pdu->top_level = 0;
                        offset = coder(dce, pdu, iov, offset, ptr);
                        pdu->top_level = top_level;
                        return offset;
                }
                
                offset = dcerpc_decode_3264(dce, pdu, iov, offset, &p);
                dcerpc_add_deferred_pointer(dce, pdu, coder, ptr);
                break;
        case PTR_UNIQUE:
                offset = dcerpc_decode_3264(dce, pdu, iov, offset, &p);
                if (p == 0) {
                        return offset;
                }
                
                if (pdu->top_level) {
                        pdu->top_level = 0;
                        offset = coder(dce, pdu, iov, offset, ptr);
                        pdu->top_level = top_level;
                } else {
                        dcerpc_add_deferred_pointer(dce, pdu, coder, ptr);
                }
                break;
        }

        return offset;
}

/* Encode a \0 terminated ucs2 string */
int dcerpc_encode_ucs2z(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                        struct smb2_iovec *iov, int offset,
                        void *ptr)
{
        struct ucs2 *str = ptr;
        uint16_t zero = 0;
        int i;

        offset = dcerpc_encode_3264(ctx, pdu, iov, offset, str->len + 1);
        offset = dcerpc_encode_3264(ctx, pdu, iov, offset, 0);
        offset = dcerpc_encode_3264(ctx, pdu, iov, offset, str->len + 1);
        for (i = 0; i < str->len; i++) {
                offset = dcerpc_encode_16(ctx, pdu, iov, offset, &str->val[i]);
        }
        offset = dcerpc_encode_16(ctx, pdu, iov, offset, &zero);
        return offset;
}

int dcerpc_decode_ucs2z(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                        struct smb2_iovec *iov, int offset,
                        void *ptr)
{
        uint64_t max, off, actual;
        char *str, **dst;
        const char *tmp;

        if (offset < 0) {
                return offset;
        }

        offset = dcerpc_decode_3264(ctx, pdu, iov, offset, &max);
        offset = dcerpc_decode_3264(ctx, pdu, iov, offset, &off);
        offset = dcerpc_decode_3264(ctx, pdu, iov, offset, &actual);

        if (offset + actual * 2 > iov->len) {
                return -1;
        }
        tmp = ucs2_to_utf8((uint16_t *)(&iov->buf[offset]), actual);
        offset += actual * 2;

        str = smb2_alloc_data(ctx->smb2, pdu->payload, strlen(tmp) + 1);
        if (str == NULL) {
                free(discard_const(tmp));
                return -1;
        }
        strcat(str, tmp);
        free(discard_const(tmp));

        dst = ptr;
        *dst = str;

        return offset;
}

static int
dcerpc_encode_header(struct smb2_iovec *iov, struct dcerpc_header *hdr)
{
        /* Major Version */
        smb2_set_uint8(iov, 0, hdr->rpc_vers);
        /* Minor Version */
        smb2_set_uint8(iov, 1, hdr->rpc_vers_minor);
        /* Packet Type */
        smb2_set_uint8(iov, 2, hdr->PTYPE);
        /* Flags */
        smb2_set_uint8(iov, 3, hdr->pfc_flags);

        /* Data Representation */
        smb2_set_uint8(iov, 4, hdr->packed_drep[0]);
        smb2_set_uint8(iov, 5, hdr->packed_drep[1]);
        smb2_set_uint8(iov, 6, hdr->packed_drep[2]);
        smb2_set_uint8(iov, 7, hdr->packed_drep[3]);

        /* Fragment len */
        smb2_set_uint16(iov, 8, hdr->frag_length);

        /* Auth len */
        smb2_set_uint16(iov, 10, hdr->auth_length);

        /* Call id */
        smb2_set_uint32(iov, 12, hdr->call_id);

        return 16;
}

static int
dcerpc_decode_header(struct smb2_iovec *iov, struct dcerpc_header *hdr)
{
        /* Major Version */
        smb2_get_uint8(iov, 0, &hdr->rpc_vers);
        /* Minor Version */
        smb2_get_uint8(iov, 1, &hdr->rpc_vers_minor);
        /* Packet Type */
        smb2_get_uint8(iov, 2, &hdr->PTYPE);
        /* Flags */
        smb2_get_uint8(iov, 3, &hdr->pfc_flags);

        /* Data Representation */
        smb2_get_uint8(iov, 4, &hdr->packed_drep[0]);
        smb2_get_uint8(iov, 5, &hdr->packed_drep[1]);
        smb2_get_uint8(iov, 6, &hdr->packed_drep[2]);
        smb2_get_uint8(iov, 7, &hdr->packed_drep[3]);

        /* Fragment len */
        smb2_get_uint16(iov, 8, &hdr->frag_length);

        /* Auth len */
        smb2_get_uint16(iov, 10, &hdr->auth_length);

        /* Call id */
        smb2_get_uint32(iov, 12, &hdr->call_id);

        return 16;
}

static int
dcerpc_encode_uuid(struct dcerpc_context *ctx,
                   struct smb2_iovec *iov, int offset,
                   struct dcerpc_uuid *uuid)
{
        if (offset < 0) {
                return offset;
        }

        smb2_set_uint32(iov, offset, uuid->v1);
        offset += 4;
        smb2_set_uint16(iov, offset, uuid->v2);
        offset += 2;
        smb2_set_uint16(iov, offset, uuid->v3);
        offset += 2;

        smb2_set_uint64(iov, offset, (uuid->v4 >> 56) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >> 48) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >> 40) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >> 32) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >> 24) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >> 16) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4 >>  8) & 0xff);
        offset += 1;
        smb2_set_uint64(iov, offset, (uuid->v4      ) & 0xff);
        offset += 1;

        return offset;
}

static int
dcerpc_decode_uuid(struct dcerpc_context *ctx,
                   struct smb2_iovec *iov, int offset,
                   struct dcerpc_uuid *uuid)
{
        uint8_t ch;
        int i;

        if (offset < 0) {
                return offset;
        }

        smb2_get_uint32(iov, offset, &uuid->v1);
        offset += 4;
        smb2_get_uint16(iov, offset, &uuid->v2);
        offset += 2;
        smb2_get_uint16(iov, offset, &uuid->v3);
        offset += 2;

        uuid->v4 = 0;
        for (i = 0; i < 8; i++) {
                smb2_get_uint8(iov, offset, &ch);
                uuid->v4 = (uuid->v4 << 8) | ch;
                offset += 1;
        }

        return offset;
}

static int
dcerpc_encode_bind(struct dcerpc_context *ctx,
                   struct dcerpc_bind_pdu *bind,
                   struct smb2_iovec *iov, int offset)
{
        if (offset < 0) {
                return offset;
        }

        /* Max Xmit Frag */
        smb2_set_uint16(iov, offset, bind->max_xmit_frag);
        offset += 2;

        /* Max Recv Frag */
        smb2_set_uint16(iov, offset, bind->max_recv_frag);
        offset += 2;

        /* Association Group */
        smb2_set_uint32(iov, offset, bind->assoc_group_id);
        offset += 4;

        /* Number Of Context Items */
        smb2_set_uint8(iov, offset, 2);
        offset += 4;

        /* Context Id[0]: NDR32 */
        smb2_set_uint16(iov, offset, 0);
        offset += 2;
        /* Num Trans Items */
        smb2_set_uint8(iov, offset, 1);
        offset += 2;
        /* Abstract Syntax */
        offset = dcerpc_encode_uuid(ctx, iov, offset, &bind->abstract_syntax->uuid);
        if (offset < 0) {
                return offset;
        }

        smb2_set_uint16(iov, offset, bind->abstract_syntax->vers);
        offset += 2;
        smb2_set_uint16(iov, offset, bind->abstract_syntax->vers_minor);
        offset += 2;
        /* Transport Syntax */
        offset = dcerpc_encode_uuid(ctx, iov, offset, &ndr32_syntax.uuid);
        if (offset < 0) {
                return offset;
        }
        smb2_set_uint16(iov, offset, ndr32_syntax.vers);
        offset += 4;
        /* Context Id[1]: NDR64 */
        smb2_set_uint16(iov, offset, 1);
        offset += 2;
        /* Num Trans Items */
        smb2_set_uint8(iov, offset, 1);
        offset += 2;
        /* Abstract Syntax */
        offset = dcerpc_encode_uuid(ctx, iov, offset, &bind->abstract_syntax->uuid);
        if (offset < 0) {
                return offset;
        }
        smb2_set_uint16(iov, offset, bind->abstract_syntax->vers);
        offset += 2;
        smb2_set_uint16(iov, offset, bind->abstract_syntax->vers_minor);
        offset += 2;
        /* Transport Syntax */
        offset = dcerpc_encode_uuid(ctx, iov, offset, &ndr64_syntax.uuid);
        if (offset < 0) {
                return offset;
        }
        smb2_set_uint16(iov, offset, ndr64_syntax.vers);
        offset += 4;
        
        /* Fixup fragment length */
        smb2_set_uint16(iov, 8, offset);
        
        return offset;
}

static int
dcerpc_encode_request(struct dcerpc_context *ctx,
                      struct dcerpc_request_pdu *req,
                      struct smb2_iovec *iov, int offset)
{
        if (offset < 0) {
                return offset;
        }

        /* Alloc Hint */
        smb2_set_uint32(iov, offset, req->alloc_hint);
        offset += 4;

        /* Context ID */
        smb2_set_uint16(iov, offset, req->context_id);
        offset += 2;
        
        /* Opnum */
        smb2_set_uint16(iov, offset, req->opnum);
        offset += 2;

        return offset;
}

static int
dcerpc_encode_pdu(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                  struct smb2_iovec *iov)
{
        int offset = 0;

        offset = dcerpc_encode_header(iov, &pdu->hdr);
        if (offset < 0) {
                return offset;
        }

        switch (pdu->hdr.PTYPE) {
        case PDU_TYPE_BIND:
                offset = dcerpc_encode_bind(ctx, &pdu->bind, iov, offset);
                break;
        case PDU_TYPE_REQUEST:
                offset = dcerpc_encode_request(ctx, &pdu->req, iov, offset);
                break;
        default:
                smb2_set_error(ctx->smb2, "DCERPC No encoder for PDU type %d",
                               pdu->hdr.PTYPE);
                return -1;
        }

        return offset;
}

static int
dcerpc_decode_bind_ack(struct dcerpc_context *ctx,
                       struct dcerpc_bind_ack_pdu *bind_ack,
                       struct smb2_iovec *iov, int offset)
{
        int i;
        uint16_t sec_addr_len;

        if (offset < 0) {
                return offset;
        }

        /* Max Xmit Frag */
        smb2_get_uint16(iov, offset, &bind_ack->max_xmit_frag);
        offset += 2;

        /* Max Recv Frag */
        smb2_get_uint16(iov, offset, &bind_ack->max_recv_frag);
        offset += 2;

        /* Association Group */
        smb2_get_uint32(iov, offset, &bind_ack->assoc_group_id);
        offset += 4;

        /* Secondary Addres Length */
        smb2_get_uint16(iov, offset, &sec_addr_len);
        offset += 2;

        /* Skip the secondary address and realign to 32bit */
        offset += sec_addr_len;
        offset = (offset + 3) & ~3;

        /* Number Of Results */
        smb2_get_uint8(iov, offset, &bind_ack->num_results);
        offset += 4;

        for (i = 0; i < bind_ack->num_results; i++) {
                smb2_get_uint16(iov, offset, &bind_ack->results[i].ack_result);
                offset += 2;

                smb2_get_uint16(iov, offset, &bind_ack->results[i].ack_reason);
                offset += 2;

                offset = dcerpc_decode_uuid(ctx, iov, offset,
                                            &bind_ack->results[i].uuid);

                smb2_get_uint32(iov, offset,
                                &bind_ack->results[i].syntax_version);
                offset += 4;
        }

        return offset;
}

static int
dcerpc_decode_response(struct dcerpc_context *ctx,
                       struct dcerpc_response_pdu *rsp,
                       struct smb2_iovec *iov, int offset)
{
#ifndef _MSC_VER
        struct dcerpc_pdu *pdu = container_of(rsp, struct dcerpc_pdu, rsp);
#else
        const char* __mptr = rsp;
        struct dcerpc_pdu *pdu = (struct smb2_alloc_header*)((char *)__mptr - offsetof(struct dcerpc_pdu, rsp));
#endif // !_MSC_VER
   
        if (offset < 0) {
                return offset;
        }

        /* Alloc Hint */
        smb2_get_uint32(iov, offset, &rsp->alloc_hint);
        offset += 4;

        if (rsp->alloc_hint < 0 || rsp->alloc_hint > 16*1024*1024) {
                smb2_set_error(ctx->smb2, "DCERPC RESPONSE alloc_hint out "
                               "of range.");
                return -1;
        }

        /* Context Id */
        smb2_get_uint16(iov, offset, &rsp->context_id);
        offset += 2;
        
        /* Cancel Count */
        smb2_get_uint8(iov, offset, &rsp->cancel_count);
        offset += 2;


        /* decode the blob */
        pdu->top_level = 1;
        if (pdu->decoder(ctx, pdu, iov, offset, pdu->payload) < 0) {
                return -1;
        }

        offset += rsp->alloc_hint;

        return offset;
}

static int
dcerpc_decode_pdu(struct dcerpc_context *ctx, struct dcerpc_pdu *pdu,
                  struct smb2_iovec *iov)
{
        int offset = 0;

        offset = dcerpc_decode_header(iov, &pdu->hdr);

        switch (pdu->hdr.PTYPE) {
        case PDU_TYPE_BIND_ACK:
                offset = dcerpc_decode_bind_ack(ctx, &pdu->bind_ack, iov, offset);
                break;
        case PDU_TYPE_RESPONSE:
                offset = dcerpc_decode_response(ctx, &pdu->rsp, iov, offset);
                break;
        default:
                smb2_set_error(ctx->smb2, "DCERPC No decoder for PDU type %d",
                               pdu->hdr.PTYPE);
                dcerpc_free_pdu(ctx, pdu);
                return -1;
        }

        return offset;
}

static void
dce_unfragment_ioctl(struct smb2_iovec *iov)
{
        int offset = 0;
        int unfragment_len;
        struct dcerpc_header hdr, next_hdr;
        struct smb2_iovec tmpiov;

        dcerpc_decode_header(iov, &hdr);
        if (hdr.rpc_vers != 5 || hdr.rpc_vers_minor != 0 ||
            hdr.PTYPE != PDU_TYPE_RESPONSE) {
                return;
        }

        if (hdr.pfc_flags & PFC_LAST_FRAG) {
                return;
        }

        offset += hdr.frag_length;
        unfragment_len = hdr.frag_length;
        do {
                /* We must have at least a DCERPC header plus a
                 * RESPONSE header
                 */
                if (iov->len - offset < 24) {
                        return;
                }

                tmpiov.buf = iov->buf + offset;
                tmpiov.len = iov->len - offset;
                dcerpc_decode_header(&tmpiov, &next_hdr);

                memmove(iov->buf + unfragment_len, iov->buf + offset + 24,
                        next_hdr.frag_length - 24);
                unfragment_len += next_hdr.frag_length - 24;
                offset += next_hdr.frag_length;

                hdr.frag_length += next_hdr.frag_length;
                if (next_hdr.pfc_flags & PFC_LAST_FRAG) {
                        hdr.pfc_flags |= PFC_LAST_FRAG;
                }
                dcerpc_encode_header(iov, &hdr);
        } while (!(next_hdr.pfc_flags & PFC_LAST_FRAG));
        iov->len = unfragment_len;
}

static void
dcerpc_call_cb(struct smb2_context *smb2, int status,
               void *command_data, void *private_data)
{
        struct dcerpc_pdu *pdu = private_data;
        struct dcerpc_context *dce = pdu->dce;
        struct smb2_iovec iov;
        struct smb2_ioctl_reply *rep = command_data;
        int ret;

        if (status != SMB2_STATUS_SUCCESS) {
                pdu->cb(dce, -nterror_to_errno(status), NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        smb2_free_data(dce->smb2, pdu->payload);
        pdu->payload = NULL;

        pdu->payload = smb2_alloc_init(dce->smb2, pdu->decode_size);
        if (pdu->payload == NULL) {
                pdu->cb(dce, -ENOMEM, NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        iov.buf = rep->output;
        iov.len = rep->output_count;
        iov.free = NULL;

        dce_unfragment_ioctl(&iov);

        ret = dcerpc_decode_pdu(dce, pdu, &iov);
        if (ret < 0) {
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                smb2_free_data(dce->smb2, rep->output);
                dcerpc_free_pdu(dce, pdu);
                return;
        }
        smb2_free_data(dce->smb2, rep->output);

        if (pdu->hdr.PTYPE != PDU_TYPE_RESPONSE) {
                smb2_set_error(dce->smb2, "DCERPC response was not a RESPONSE");
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        pdu->cb(dce, 0, pdu->payload, pdu->cb_data);
        pdu->payload = NULL;
        dcerpc_free_pdu(dce, pdu);
}

int
dcerpc_call_async(struct dcerpc_context *dce,
                  int opnum,
                  dcerpc_coder encoder, void *ptr,
                  dcerpc_coder decoder, int decode_size,
                  dcerpc_cb cb, void *cb_data)
{
        struct dcerpc_pdu *pdu;
        struct smb2_pdu *smb2_pdu;
        struct smb2_ioctl_request req;
        struct smb2_iovec iov;
        int offset;

        pdu = dcerpc_allocate_pdu(dce);
        if (pdu == NULL) {
                return -ENOMEM;
        }

        pdu->hdr.rpc_vers = 5;
        pdu->hdr.rpc_vers_minor = 0;
        pdu->hdr.PTYPE = PDU_TYPE_REQUEST;
        pdu->hdr.pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
        pdu->hdr.packed_drep[0] = DCERPC_DR_BYTE_ORDER_LITTLE_ENDIAN |
                DCERPC_DR_CHARACTER_ASCII;
        pdu->hdr.frag_length = 0;
        pdu->hdr.auth_length = 0;
        pdu->req.alloc_hint = 0;
        pdu->req.context_id = dce->tctx_id;
        pdu->req.opnum = opnum;

        pdu->payload = smb2_alloc_init(dce->smb2, NSE_BUF_SIZE);
        if (pdu->payload == NULL) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }

        pdu->decoder = decoder;
        pdu->decode_size = decode_size;
        pdu->cb = cb;
        pdu->cb_data = cb_data;

        iov.buf = pdu->payload;
        iov.len = NSE_BUF_SIZE;
        iov.free = NULL;
        offset = dcerpc_encode_pdu(dce, pdu, &iov);
        if (offset < 0) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }

        /* encode the blob */
        pdu->top_level = 1;
        offset = encoder(dce, pdu, &iov, offset, ptr);

        iov.len = offset;

        /* Fixup frag_length and alloc_hint */
        smb2_set_uint16(&iov,  8, offset);
        smb2_set_uint32(&iov, 16, offset - 24);

        memset(&req, 0, sizeof(struct smb2_ioctl_request));
        req.ctl_code = SMB2_FSCTL_PIPE_TRANSCEIVE;
        memcpy(req.file_id, dce->file_id, SMB2_FD_SIZE);
        req.input_count = iov.len;
        req.input = iov.buf;
        req.flags = SMB2_0_IOCTL_IS_FSCTL;

        smb2_pdu = smb2_cmd_ioctl_async(dce->smb2, &req, dcerpc_call_cb, pdu);
        if (smb2_pdu == NULL) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }
        smb2_queue_pdu(dce->smb2, smb2_pdu);
 
        return 0;
}

static void
dcerpc_bind_cb(struct smb2_context *smb2, int status,
               void *command_data, void *private_data)
{
        struct dcerpc_pdu *pdu = private_data;
        struct dcerpc_context *dce = pdu->dce;
        struct smb2_iovec iov;
        struct smb2_ioctl_reply *rep = command_data;
        int i;

        if (status != SMB2_STATUS_SUCCESS) {
                pdu->cb(dce, -nterror_to_errno(status), NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        iov.buf = rep->output;
        iov.len = rep->output_count;
        iov.free = NULL;
        if (dcerpc_decode_pdu(dce, pdu, &iov) < 0) {
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                smb2_free_data(dce->smb2, rep->output);
                dcerpc_free_pdu(dce, pdu);
                return;
        }
        smb2_free_data(dce->smb2, rep->output);

        if (pdu->hdr.PTYPE != PDU_TYPE_BIND_ACK) {
                smb2_set_error(dce->smb2, "DCERPC response was not a BIND_ACK");
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        if (pdu->bind_ack.num_results < 1) {
                smb2_set_error(smb2, "No results in BIND ACK");
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }
        for (i = 0; i < pdu->bind_ack.num_results; i++) {
                if (pdu->bind_ack.results[i].ack_result !=
                    ACK_RESULT_ACCEPTANCE) {
                        continue;
                }

                dce->tctx_id = i;
                break;
        }
        if (i == pdu->bind_ack.num_results) {
                smb2_set_error(smb2, "Bind rejected all contexts");
                pdu->cb(dce, -EINVAL, NULL, pdu->cb_data);
                dcerpc_free_pdu(dce, pdu);
                return;
        }

        pdu->cb(dce, 0, NULL, pdu->cb_data);
        dcerpc_free_pdu(dce, pdu);
}

int
dcerpc_bind_async(struct dcerpc_context *dce, dcerpc_cb cb,
                  void *cb_data)
{
        struct dcerpc_pdu *pdu;
        struct smb2_pdu *smb2_pdu;
        struct smb2_ioctl_request req;
        struct smb2_iovec iov;
        int offset;

        pdu = dcerpc_allocate_pdu(dce);
        if (pdu == NULL) {
                return -ENOMEM;
        }

        pdu->hdr.rpc_vers = 5;
        pdu->hdr.rpc_vers_minor = 0;
        pdu->hdr.PTYPE = PDU_TYPE_BIND;
        pdu->hdr.pfc_flags = PFC_FIRST_FRAG | PFC_LAST_FRAG;
        pdu->hdr.packed_drep[0] = DCERPC_DR_BYTE_ORDER_LITTLE_ENDIAN |
                DCERPC_DR_CHARACTER_ASCII;
        pdu->hdr.frag_length = 0;
        pdu->hdr.auth_length = 0;
        pdu->bind.max_xmit_frag = 32768;
        pdu->bind.max_recv_frag = 32768;
        pdu->bind.assoc_group_id = 0;
        pdu->bind.abstract_syntax = dce->syntax;

        pdu->payload = smb2_alloc_init(dce->smb2, NSE_BUF_SIZE);
        if (pdu->payload == NULL) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }

        pdu->cb = cb;
        pdu->cb_data = cb_data;

        iov.buf = pdu->payload;
        iov.len = NSE_BUF_SIZE;
        iov.free = NULL;
        offset = dcerpc_encode_pdu(dce, pdu, &iov);
        if (offset < 0) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }
        iov.len = offset;

        memset(&req, 0, sizeof(struct smb2_ioctl_request));
        req.ctl_code = SMB2_FSCTL_PIPE_TRANSCEIVE;
        memcpy(req.file_id, dce->file_id, SMB2_FD_SIZE);
        req.input_count = iov.len;
        req.input = iov.buf;
        req.flags = SMB2_0_IOCTL_IS_FSCTL;

        smb2_pdu = smb2_cmd_ioctl_async(dce->smb2, &req, dcerpc_bind_cb, pdu);
        if (smb2_pdu == NULL) {
                dcerpc_free_pdu(dce, pdu);
                return -ENOMEM;
        }
        smb2_queue_pdu(dce->smb2, smb2_pdu);
 
        return 0;
}

static void
dcerpc_open_cb(struct smb2_context *smb2, int status,
               void *command_data, void *private_data)
{
        struct dcerpc_cb_data *cb_data = private_data;
        struct smb2_create_reply *rep = command_data;
        struct dcerpc_context *dce = cb_data->dce;

        if (status != SMB2_STATUS_SUCCESS) {
                cb_data->cb(dce, -nterror_to_errno(status),
                            NULL, cb_data->cb_data);
                free(cb_data);
                return;
        }
        
        memcpy(dce->file_id, rep->file_id, SMB2_FD_SIZE);

        cb_data->cb(dce, 0, NULL, cb_data->cb_data);
        free(cb_data);
}

int
dcerpc_open_async(struct dcerpc_context *dce, dcerpc_cb cb,
                  void *cb_data)
{
        struct smb2_create_request req;
        struct smb2_pdu *pdu;
        struct dcerpc_cb_data *data;

        data = malloc(sizeof(struct dcerpc_cb_data));
        if (data == NULL) {
                smb2_set_error(dce->smb2, "Failed to allocate dcerpc callback "
                               "data");
                return -ENOMEM;
        }
        data->dce = dce;
        data->cb = cb;
        data->cb_data = cb_data;

        memset(&req, 0, sizeof(struct smb2_create_request));
        req.requested_oplock_level = SMB2_OPLOCK_LEVEL_NONE;
        req.impersonation_level = SMB2_IMPERSONATION_IMPERSONATION;
        req.desired_access = SMB2_FILE_READ_DATA |
                SMB2_FILE_WRITE_DATA |
                SMB2_FILE_APPEND_DATA |
                SMB2_FILE_READ_EA |
                SMB2_FILE_READ_ATTRIBUTES |
                SMB2_FILE_WRITE_EA |
                SMB2_FILE_WRITE_ATTRIBUTES |
                SMB2_READ_CONTROL |
                SMB2_SYNCHRONIZE;
        req.file_attributes = 0;
        req.share_access = SMB2_FILE_SHARE_READ |
                SMB2_FILE_SHARE_WRITE |
                SMB2_FILE_SHARE_DELETE;
        req.create_disposition = SMB2_FILE_OPEN;
        req.create_options = 0;
        req.name = dce->path;

        pdu = smb2_cmd_create_async(dce->smb2, &req, dcerpc_open_cb, data);
        if (pdu == NULL) {
                free(data);
                return -ENOMEM;
        }
        smb2_queue_pdu(dce->smb2, pdu);

        return 0;
}
