/* packet-pn.c
 * Common functions for other PROFINET protocols like IO, CBA, DCP, ...
 *
 * Wireshark - Network traffic analyzer
 * By Gerald Combs <gerald@wireshark.org>
 * Copyright 1999 Gerald Combs
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#include "config.h"

#include <string.h>

#include <epan/packet.h>
#include <epan/expert.h>
#include <epan/wmem/wmem.h>
#include <epan/dissectors/packet-dcerpc.h>

#include "packet-pn.h"



static int hf_pn_padding = -1;
static int hf_pn_undecoded_data = -1;
static int hf_pn_user_data = -1;
static int hf_pn_user_bytes = -1;
static int hf_pn_frag_bytes = -1;
static int hf_pn_malformed = -1;

static expert_field ei_pn_undecoded_data = EI_INIT;

/* Initialize PNIO RTC1 stationInfo memory */
void
init_pnio_rtc1_station(stationInfo *station_info) {
    station_info->iocs_data_in = wmem_list_new(wmem_file_scope());
    station_info->iocs_data_out = wmem_list_new(wmem_file_scope());
    station_info->ioobject_data_in = wmem_list_new(wmem_file_scope());
    station_info->ioobject_data_out = wmem_list_new(wmem_file_scope());
    station_info->diff_module = wmem_list_new(wmem_file_scope());
}

/* dissect an 8 bit unsigned integer */
int
dissect_pn_uint8(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                  proto_tree *tree, int hfindex, guint8 *pdata)
{
    guint8 data;

    data = tvb_get_guint8 (tvb, offset);
    proto_tree_add_uint(tree, hfindex, tvb, offset, 1, data);
    if (pdata)
        *pdata = data;
    return offset + 1;
}

/* dissect a 16 bit unsigned integer; return the item through a pointer as well */
int
dissect_pn_uint16_ret_item(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                       proto_tree *tree, int hfindex, guint16 *pdata, proto_item ** new_item)
{
    guint16     data;
    proto_item *item = NULL;

    data = tvb_get_ntohs (tvb, offset);

    item = proto_tree_add_uint(tree, hfindex, tvb, offset, 2, data);
    if (pdata)
        *pdata = data;
    if (new_item)
        *new_item = item;
    return offset + 2;
}

/* dissect a 16 bit unsigned integer */
int
dissect_pn_uint16(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                       proto_tree *tree, int hfindex, guint16 *pdata)
{
    guint16 data;

    data = tvb_get_ntohs (tvb, offset);

    proto_tree_add_uint(tree, hfindex, tvb, offset, 2, data);
    if (pdata)
        *pdata = data;
    return offset + 2;
}

/* dissect a 32 bit unsigned integer */
int
dissect_pn_uint32(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                       proto_tree *tree, int hfindex, guint32 *pdata)
{
    proto_tree_add_item_ret_uint(tree, hfindex,
            tvb, offset, 4, ENC_BIG_ENDIAN, pdata);

    return offset+4;
}

/* dissect a 16 bit signed integer */
int
dissect_pn_int16(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                       proto_tree *tree, int hfindex, gint16 *pdata)
{
    gint16 data;

    data = tvb_get_ntohs (tvb, offset);

    proto_tree_add_int(tree, hfindex, tvb, offset, 2, data);
    if (pdata)
        *pdata = data;
    return offset + 2;
}

/* dissect a 32 bit signed integer */
int
dissect_pn_int32(tvbuff_t *tvb, gint offset, packet_info *pinfo _U_,
                       proto_tree *tree, int hfindex, gint32 *pdata)
{
    proto_tree_add_item_ret_int(tree, hfindex,
            tvb, offset, 4, ENC_BIG_ENDIAN, pdata);

    return offset + 4;
}

/* dissect a 24bit OUI (IEC organizational unique id) */
int
dissect_pn_oid(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, int hfindex, guint32 *pdata)
{
    guint32 data;

    data = tvb_get_ntoh24(tvb, offset);

    proto_tree_add_uint(tree, hfindex, tvb, offset, 3, data);
    if (pdata)
        *pdata = data;
    return offset + 3;
}

/* dissect a 6 byte MAC address */
int
dissect_pn_mac(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, int hfindex, guint8 *pdata)
{
    guint8 data[6];

    tvb_memcpy(tvb, data, offset, 6);
    proto_tree_add_ether(tree, hfindex, tvb, offset, 6, data);

    if (pdata)
        memcpy(pdata, data, 6);

    return offset + 6;
}

/* dissect an IPv4 address */
int
dissect_pn_ipv4(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, int hfindex, guint32 *pdata)
{
    guint32 data;

    data = tvb_get_ipv4(tvb, offset);
    proto_tree_add_ipv4(tree, hfindex, tvb, offset, 4, data);

    if (pdata)
        *pdata = data;

    return offset + 4;
}

/* dissect a 16 byte UUID address */
int
dissect_pn_uuid(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, int hfindex, e_guid_t *uuid)
{
    guint8 drep[2] = { 0,0 };

    offset = dissect_dcerpc_uuid_t(tvb, offset, pinfo, tree, drep,
                    hfindex, uuid);

    return offset;
}

/* "dissect" some bytes still undecoded (with Expert warning) */
int
dissect_pn_undecoded(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, guint32 length)
{
    proto_item *item;


    item = proto_tree_add_string_format(tree, hf_pn_undecoded_data, tvb, offset, length, "data",
        "Undecoded Data: %d bytes", length);

    expert_add_info_format(pinfo, item, &ei_pn_undecoded_data,
                           "Undecoded Data, %u bytes", length);

    return offset + length;
}

/* "dissect" some user bytes */
int
dissect_pn_user_data_bytes(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, guint32 length, int iSelect)
{
    if(iSelect == FRAG_DATA)
        proto_tree_add_item(tree, hf_pn_frag_bytes, tvb, offset, length, ENC_NA);
    else
        proto_tree_add_item(tree, hf_pn_user_bytes, tvb, offset, length, ENC_NA);

    return offset + length;
}

int
dissect_pn_user_data(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, guint32 length, const char *text)
{
    if (length != 0) {
        proto_tree_add_string_format(tree, hf_pn_user_data, tvb, offset, length, "data",
            "%s: %d bytes", text, length);
    }
    return offset + length;
}

/* packet is malformed, mark it as such */
int
dissect_pn_malformed(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, guint32 length)
{
    proto_tree_add_item(tree, hf_pn_malformed, tvb, 0, 10000, ENC_NA);

    return offset + length;
}


/* dissect some padding data (with the given length) */
int
dissect_pn_padding(tvbuff_t *tvb, int offset, packet_info *pinfo _U_,
                    proto_tree *tree, int length)
{
    proto_tree_add_string_format(tree, hf_pn_padding, tvb, offset, length, "data",
        "Padding: %u byte", length);

    return offset + length;
}

/* align offset to 4 */
int
dissect_pn_align4(tvbuff_t *tvb, int offset, packet_info *pinfo _U_, proto_tree *tree)
{
    guint padding = 0;


    if (offset % 4) {
        padding = 4 - (offset % 4);

        proto_tree_add_string_format(tree, hf_pn_padding, tvb, offset, padding, "data",
            "Padding: %u byte", padding);
    }

    return offset + padding;
}

/* append the given info text to item and column */
void
pn_append_info(packet_info *pinfo, proto_item *dcp_item, const char *text)
{
    col_append_str(pinfo->cinfo, COL_INFO, text);

    proto_item_append_text(dcp_item, "%s", text);
}



void
init_pn (int proto)
{
    static hf_register_info hf[] = {
        { &hf_pn_padding,
          { "Padding", "pn.padding",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }},

        { &hf_pn_undecoded_data,
          { "Undecoded Data", "pn.undecoded",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }},

        { &hf_pn_user_data,
          { "User Data", "pn.user_data",
            FT_STRING, BASE_NONE, NULL, 0x0,
            NULL, HFILL }},

        { &hf_pn_user_bytes,
          { "Substitute Data", "pn.user_bytes",
            FT_BYTES, BASE_NONE, NULL, 0x0,
            NULL, HFILL }},

        { &hf_pn_frag_bytes,
          { "Fragment Data", "pn.frag_bytes",
            FT_BYTES, BASE_NONE, NULL, 0x0,
            NULL, HFILL }},

        { &hf_pn_malformed,
          { "Malformed", "pn_rt.malformed",
            FT_BYTES, BASE_NONE, NULL, 0x0,
            NULL, HFILL }}

    };

    /*static gint *ett[] = {
      };*/

    static ei_register_info ei[] = {
        { &ei_pn_undecoded_data, { "pn.undecoded_data", PI_UNDECODED, PI_WARN, "Undecoded Data", EXPFILL }},
    };

    expert_module_t* expert_pn;


    proto_register_field_array (proto, hf, array_length (hf));
    /*proto_register_subtree_array (ett, array_length (ett));*/
    expert_pn = expert_register_protocol(proto);
    expert_register_field_array(expert_pn, ei, array_length(ei));
}

/* Read a string from an "xml" file, dropping xml comment blocks */
char *pn_fgets(char *str, int n, FILE *stream)
{
    const char XML_COMMENT_START[] = "<!--";
    const char XML_COMMENT_END[] = "-->";

    char *retVal = fgets(str, n, stream);
    if (retVal == NULL) {
        /* No input, we're done */
        return retVal;
    }

    /* Search for the XML begin comment marker */
    char *comment_start = strstr(str, XML_COMMENT_START);
    char *common_start_end = comment_start + sizeof(XML_COMMENT_START) - 1;
    if(comment_start == NULL) {
        /* No comment start, we're done */
        return retVal;
    }

    /* Terminate the input buffer at the comment start */
    *comment_start = '\0';
    size_t used_space = comment_start - str;
    size_t remaining_space = n - used_space;

    /* Read more data looking for the comment end */
    char *comment_end = strstr(common_start_end, XML_COMMENT_END);
    if (comment_end == NULL) {
      // Not found in this line, read more lines until we do find it */
      char *temp = (char*)wmem_alloc(wmem_packet_scope(), MAX_LINE_LENGTH);
      char *next_line = temp;
      while((comment_end == NULL) && (next_line != NULL)) {
          next_line = fgets(temp, MAX_LINE_LENGTH, stream);
          if (next_line == NULL) {
              /* No more data, exit now */
              break;
          }
          comment_end = strstr(next_line, XML_COMMENT_END);
      }
    }

    if (comment_end == NULL) {
        /* We didn't find the comment end, return what we have */
        return retVal;
    }

    /* We did find a comment end, skip past the comment */
    char *comment_end_end = comment_end + sizeof(XML_COMMENT_END) - 1;

    /* Check we have space left in the buffer to move the trailing bytes after the comment end */
    size_t remaining_bytes = strlen(comment_end_end) + 1;
    if (remaining_bytes < remaining_space) {
        g_strlcat(str, comment_end_end, n);
    }
    else {
      /* Seek the file back to the comment end so the next read picks it up */
      fseek(stream, -(long)(remaining_bytes), SEEK_CUR);
    }

    return retVal;
}

/*
 * Editor modelines  -  http://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 4
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=4 tabstop=8 expandtab:
 * :indentSize=4:tabSize=8:noTabs=true:
 */
