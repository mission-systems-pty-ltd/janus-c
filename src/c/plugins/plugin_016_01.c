//*************************************************************************
// JANUS is a simple, robust, open standard signalling method for         *
// underwater communications. See <http://www.januswiki.org> for details. *
//*************************************************************************
// Example software implementations provided by STO CMRE are subject to   *
// Copyright (C) 2008-2018 STO Centre for Maritime Research and           *
// Experimentation (CMRE)                                                 *
//                                                                        *
// This is free software: you can redistribute it and/or modify it        *
// under the terms of the GNU General Public License version 3 as         *
// published by the Free Software Foundation.                             *
//                                                                        *
// This program is distributed in the hope that it will be useful, but    *
// WITHOUT ANY WARRANTY; without even the implied warranty of FITNESS     *
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for       *
// more details.                                                          *
//                                                                        *
// You should have received a copy of the GNU General Public License      *
// along with this program. If not, see <http://www.gnu.org/licenses/>.   *
//*************************************************************************
// Author: Luigi Elia D'Amaro                                             *
//*************************************************************************

// For more info see:
// https://www.januswiki.com/tiki-index.php?page=Class+User+Id%3A+016+NATO+JANUS+reference+Implementation

// TODO: NEED TO ADD ENCODE FUNCTION FOR CRC16 etc

// ISO C headers.
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>

// JANUS headers.
#include <janus/defaults.h>
#include <janus/codec/codec.h>
#include <janus/error.h>
#include <janus/crc.h>
#include <janus/packet.h>

#define STATION_ID_LABEL "StationIdentifier"
#define PSET_ID_LABEL "ParameterSetIdentifier"
#define PAYLOAD_SIZE_LABEL "PayloadSize"
#define PAYLOAD_LABEL "Payload"
#define DESTINATION_ID_LABEL "DestinationIdentifier"
#define ACK_REQUEST "AckRequest"
#define TX_RX_FLAG  "TxRxFlag"
#define CRC_BYTES 2

#define BMASK(var, bit)  ((var >> (64 - bit)) & 0x00000001U)
#define LMASK(var, bits) (var & (0xFFFFFFFFFFFFFFFFU >> (64 - bits)))
#define HMASK(var, bits) (var & (0xFFFFFFFFFFFFFFFFU << (64 - bits)))

static inline unsigned
cargo_lookup_index(unsigned index)
{
  // bitshift index to include CRC16
  if (index == 0)
    return 0;
  else
    return index;
}

static inline unsigned
cargo_lookup_size(unsigned dsize, unsigned* esize)
{
  unsigned index = 0;

  if (dsize < 3)
  {
    index = dsize;
    *esize = dsize;
  }
  else if (dsize < 5)
  {
    index = 3;
    *esize = 4;
  }
  else if (dsize <= 480)
  {
    *esize = ((dsize - 1) & 0xfff8) + 8;
    index = 4 + ((*esize - 8) / 8);
  }
  else
  {
    *esize = 0;
  }

  return index;
}

static inline void
app_data_decode_station_id(janus_uint64_t app_data, janus_app_fields_t app_fields)
{
  char name[] = STATION_ID_LABEL;
  char value[4];

  janus_uint8_t station_id = (janus_uint8_t)((app_data >> 18) & (0xFFU));
  sprintf(value, "%u", station_id);
  janus_app_fields_add_field(app_fields, name, value);
}

static inline void
app_data_decode_destination_id(janus_uint64_t app_data, janus_app_fields_t app_fields)
{
  char name[] = DESTINATION_ID_LABEL;
  char value[4];

  janus_uint8_t destination_id = (janus_uint8_t)((app_data >> 10) & (0xFFU));
  sprintf(value, "%u", destination_id);
  janus_app_fields_add_field(app_fields, name, value);
}

static inline void
app_data_decode_ack_request(janus_uint64_t app_data, janus_app_fields_t app_fields)
{
  char name[] = ACK_REQUEST;
  char value[4];

  bool ack_request = (janus_uint8_t)((app_data >> 9) & (0x1U));
  sprintf(value, "%u", ack_request);
  janus_app_fields_add_field(app_fields, name, value);
}

static inline void
app_data_decode_pset_id(janus_uint64_t app_data, janus_app_fields_t app_fields)
{
  char name[] = PSET_ID_LABEL;
  char value[5];

  janus_uint16_t pset_id = (janus_uint16_t)((app_data >> 6) & (0xFFFU));
  sprintf(value, "%u", pset_id);
  janus_app_fields_add_field(app_fields, name, value);
}

static inline unsigned
app_data_decode_cargo_size(janus_uint64_t app_data)
{
  unsigned cargo_size_index = (unsigned)(app_data & (0xFFU));
  return cargo_lookup_index(cargo_size_index);
}

static inline void
app_fields_encode_station_id(janus_uint64_t* app_data, janus_app_field_t app_data_field)
{
  janus_uint64_t station_id = atoi(app_data_field->value);
  *app_data = HMASK(*app_data, 38) | (station_id << 18) | LMASK(*app_data, 18);
}

static inline void
app_fields_encode_destination_id(janus_uint64_t* app_data, janus_app_field_t app_data_field)
{
  janus_uint64_t destination_id = atoi(app_data_field->value);
  *app_data = HMASK(*app_data, 46) | (destination_id << 10) | LMASK(*app_data, 10);
}

static inline void
app_fields_encode_ack_request(janus_uint64_t* app_data, janus_app_field_t app_data_field)
{
  janus_uint64_t ack_request = atoi(app_data_field->value);
  *app_data = HMASK(*app_data, 54) | (ack_request << 9) | LMASK(*app_data, 9);
}

static inline void
app_fields_encode_pset_id(janus_uint64_t* app_data, janus_app_field_t app_data_field)
{
  janus_uint64_t pset_id = atoi(app_data_field->value);
  *app_data = HMASK(*app_data, 46) | (pset_id << 6) | LMASK(*app_data, 6);
}

static inline unsigned 
app_fields_encode_cargo_size(janus_uint64_t* app_data, unsigned desired_cargo_size)
{
  unsigned cargo_size;
  janus_uint64_t cargo_size_index = cargo_lookup_size(desired_cargo_size, &cargo_size);
  *app_data = HMASK(*app_data, 58) | ( desired_cargo_size );
  return cargo_size;
}

JANUS_PLUGIN_EXPORT int
app_data_decode(janus_uint64_t app_data, janus_uint8_t app_data_size, unsigned* cargo_size, janus_app_fields_t app_fields)
{
  // Station Identifier (8 bits).
  app_data_decode_station_id(app_data, app_fields);

  // Destination Identifier (8 bits)
  app_data_decode_destination_id(app_data, app_fields);

  // Ack Request (1 bit)
  app_data_decode_ack_request(app_data, app_fields);

  // Parameter Set Identifier (12 bits).
  //app_data_decode_pset_id(app_data, app_fields); // ? This might be needed but unlikely

  // Cargo Size (6 bits).
  *cargo_size = app_data_decode_cargo_size(app_data);

  return 0;
}

JANUS_PLUGIN_EXPORT int
app_data_encode(unsigned desired_cargo_size, janus_app_fields_t app_fields, janus_uint8_t app_data_size, unsigned* cargo_size, janus_uint64_t* app_data)
{
  *app_data = 0;

  // Check cargo size validity.
  if (desired_cargo_size > 480)
    return JANUS_ERROR_CARGO_SIZE;

  // Cargo Size (6 bits).
  *cargo_size = app_fields_encode_cargo_size(app_data, desired_cargo_size);

  if (app_fields != 0)
  {
    int i;
    for (i = 0; i < app_fields->field_count; ++i)
    {
      if (strcmp(app_fields->fields[i].name, STATION_ID_LABEL) == 0)
      {
        // Station Identifier (8 bits).
        app_fields_encode_station_id(app_data, app_fields->fields + i);
      }
      else if (strcmp(app_fields->fields[i].name, DESTINATION_ID_LABEL) == 0)
      {
        // Destination Identifier (8 bits)
        app_fields_encode_destination_id(app_data, app_fields->fields + i);
      }
      else if (strcmp(app_fields->fields[i].name, ACK_REQUEST) == 0)
      {
        // Ack Request (1 bit)
        app_fields_encode_ack_request(app_data, app_fields->fields + i);
      }
      else if (strcmp(app_fields->fields[i].name, PSET_ID_LABEL) == 0)
      {
        // Parameter Set Identifier (12 bits).
        app_fields_encode_pset_id(app_data, app_fields->fields + i);
      }
      else
      {
        //return -1;
      }
    }
  }
  
  return 0;
}

JANUS_PLUGIN_EXPORT janus_uint16_t
janus_packet_get_crc16(janus_uint8_t* cargo, unsigned cargo_size)
{
  janus_uint8_t MSB = cargo[cargo_size - CRC_BYTES];
  janus_uint8_t LSB = cargo[cargo_size - 1];
  janus_uint16_t crc = (MSB << 8) + LSB;
  return crc;
}

JANUS_PLUGIN_EXPORT int
cargo_decode(janus_uint8_t* cargo, unsigned cargo_size, janus_app_fields_t* app_fields)
{
  int rv = 0;

  if (*app_fields == 0)
  {
    *app_fields = janus_app_fields_new();
  }

  char size_string[4];
  sprintf(size_string, "%3u", cargo_size);

  janus_app_fields_add_field(*app_fields, PAYLOAD_SIZE_LABEL, size_string);
  janus_app_fields_add_blob(*app_fields, PAYLOAD_LABEL, cargo, cargo_size-2);

  janus_uint16_t ccrc = janus_crc_16(cargo, cargo_size-2, 0);
  janus_uint16_t pcrc = janus_packet_get_crc16(cargo, cargo_size);

  if (ccrc != pcrc)
  {
    printf("Cargo CRC Failed\n");
    return JANUS_ERROR_CARGO_CORRUPTED;
  }

  return rv;
}

JANUS_PLUGIN_EXPORT int
cargo_encode(janus_app_fields_t app_fields, janus_uint8_t** cargo, unsigned* cargo_size)
{
  int rv = JANUS_ERROR_FIELDS;
  int cargo_size_found = 0;

  unsigned i;
  for (i = 0; i != app_fields->field_count; ++i)
  {
    if (strcmp(app_fields->fields[i].name, PAYLOAD_SIZE_LABEL) == 0)
    {
      *cargo_size = atoi(app_fields->fields[i].value);
      cargo_size_found = 1;
      break;
    }
  }

  for (i = 0; i != app_fields->field_count; ++i)
  {
    if (strcmp(app_fields->fields[i].name, PAYLOAD_LABEL) == 0)
    {
      if (! cargo_size_found)
      {
        *cargo_size = strlen(app_fields->fields[i].value);
      }
      if (*cargo_size > JANUS_MAX_PKT_CARGO_SIZE)
      {
        return JANUS_ERROR_CARGO_SIZE;
      }

      char* payload = app_fields->fields[i].value;
      *cargo = JANUS_UTILS_MEMORY_REALLOC(*payload, janus_uint8_t, *cargo_size);
      rv = 0;
      
      break;
    }
  }
  if (*cargo_size == 0)
    return 0;
  else
    return rv;
}     