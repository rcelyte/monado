// Copyright 2022, Collabora, Ltd.
// Copyright 2022 Max Thomas
// SPDX-License-Identifier: BSL-1.0
/*!
 * @file
 * @brief  quest_link XRSP hostinfo packets
 * @author Max Thomas <mtinc2@gmail.com>
 * @ingroup drv_quest_link
 */

#include <stdlib.h>
#include <stdio.h>

#include "ql_xrsp_hostinfo.h"
#include "ql_xrsp_types.h"
#include "ql_types.h"
#include "ql_utils.h"

typedef struct xrsp_hostinfo_header
{
    uint32_t message_type:4;
    uint32_t result:10;
    uint32_t stream_size_words:18;

    uint32_t unk_4;
    //uint32_t unk_8;
    //uint32_t len_u64s;
} __attribute__((packed)) xrsp_hostinfo_header;

typedef struct xrsp_capnp_payload
{
    uint32_t unk;
    uint32_t len_u64s;
} xrsp_capnp_payload;

int32_t ql_xrsp_hostinfo_pkt_create(struct ql_xrsp_hostinfo_pkt* pkt, struct ql_xrsp_topic_pkt* topic_pkt, struct ql_xrsp_host* host)
{
    *pkt = (struct ql_xrsp_hostinfo_pkt){0};

    if (topic_pkt->payload_valid < sizeof(xrsp_hostinfo_header)) {
        return -1;
    }

    pkt->recv_ns = topic_pkt->recv_ns;

    xrsp_hostinfo_header* header = (xrsp_hostinfo_header*)topic_pkt->payload;

    if (header->message_type == BUILTIN_ECHO) {
        pkt->payload = topic_pkt->payload + 8;
        pkt->payload_size = topic_pkt->payload_size - 8;
    }
    else {
        pkt->payload = topic_pkt->payload + 0x8;
        pkt->payload_size = topic_pkt->payload_size - 0x8;
    }

    pkt->message_type = header->message_type;
    pkt->result = header->result;
    pkt->stream_size = header->stream_size_words << 2;

    pkt->unk_4 = header->unk_4;
    return 0;
}

void ql_xrsp_hostinfo_pkt_destroy(struct ql_xrsp_hostinfo_pkt* pkt)
{
    if (pkt->payload) {
        //free(pkt->payload);
    }

    *pkt = (struct ql_xrsp_hostinfo_pkt){0};
}

void ql_xrsp_hostinfo_pkt_dump(struct ql_xrsp_hostinfo_pkt* pkt)
{

    printf("type: %s (%x)\n", xrsp_builtin_type_str(pkt->message_type), pkt->message_type);
    printf("result: %x\n",  pkt->result);
    printf("stream size: %x\n",  pkt->stream_size);

    printf("unk_4: %x\n",  pkt->unk_4);
    printf("------\n");
}

uint8_t* ql_xrsp_craft_echo(uint16_t result, uint32_t echo_id, int64_t org, int64_t recv, int64_t xmt, int64_t offset, int32_t* out_len)
{
    ql_xrsp_echo_payload payload = {org, recv, xmt, offset};
    return ql_xrsp_craft_basic(BUILTIN_ECHO, result, echo_id, (const uint8_t*)&payload, sizeof(payload), out_len);
}

uint8_t* ql_xrsp_craft_basic(uint8_t message_type, uint16_t result, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len)
{
    return ql_xrsp_craft(message_type, result, payload_size + 8, unk_4, payload, payload_size, out_len);
}

uint8_t* ql_xrsp_craft_capnp(uint8_t message_type, uint16_t result, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len)
{
    xrsp_capnp_payload payload_header = {0, (uint32_t)payload_size >> 3};

    int32_t tmp_size = payload_size + sizeof(payload_header);
    uint8_t* tmp = (uint8_t*)malloc(tmp_size);
    memcpy(tmp, &payload_header, sizeof(payload_header));
    if (payload) {
        memcpy(tmp + sizeof(payload_header), payload, payload_size);
    }

    uint8_t* ret = ql_xrsp_craft(message_type, result, tmp_size + 8, unk_4, tmp, tmp_size, out_len);

    free(tmp);

    return ret;
}

uint8_t* ql_xrsp_craft(uint8_t message_type, uint16_t result, uint32_t stream_size, uint32_t unk_4, const uint8_t* payload, size_t payload_size, int32_t* out_len)
{
    int32_t total_size = sizeof(xrsp_hostinfo_header) + payload_size;
    uint8_t* out = (uint8_t*)malloc(total_size);
    memset(out, 0, total_size);

    xrsp_hostinfo_header* header = (xrsp_hostinfo_header*)out;
    header->message_type = message_type;
    header->result = result;
    header->stream_size_words = stream_size >> 2;
    header->unk_4 = unk_4;

    if (payload) {
        memcpy(header+1, payload, payload_size);
    }
    
    if (out_len) {
        *out_len = total_size;
    }
    return out;
}