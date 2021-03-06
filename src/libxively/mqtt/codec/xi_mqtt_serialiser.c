/* Copyright (c) 2003-2016, LogMeIn, Inc. All rights reserved.
 *
 * This is part of the Xively C Client library,
 * it is licensed under the BSD 3-Clause license.
 */

#include <string.h>

#include "xi_debug.h"
#include "xi_macros.h"
#include "xi_mqtt_message.h"
#include "xi_mqtt_serialiser.h"

#define WRITE_8( out, byte ) XI_CHECK_STATE( xi_data_desc_append_byte( out, byte ) )

#define WRITE_16( out, value )                                                           \
    WRITE_8( out, value >> 8 );                                                          \
    WRITE_8( out, value & 0xFF )

#define WRITE_STRING( out, str )                                                         \
    if ( NULL != str )                                                                   \
    {                                                                                    \
        WRITE_16( out, str->length );                                                    \
        XI_CHECK_STATE( xi_data_desc_append_data( out, str ) );                          \
    }                                                                                    \
    else                                                                                 \
    {                                                                                    \
        WRITE_16( out, 0 );                                                              \
    }

#define WRITE_DATA( out, data ) XI_CHECK_STATE( xi_data_desc_append_data( out, data ) )

void xi_mqtt_serialiser_init( xi_mqtt_serialiser_t* serialiser )
{
    memset( serialiser, 0, sizeof( xi_mqtt_serialiser_t ) );
}

static uint8_t xi_mqtt_get_remaining_length_bytes( const size_t remaining_length )
{
    if ( remaining_length <= 127 )
    {
        return 1;
    }
    else if ( remaining_length <= 16383 )
    {
        return 2;
    }
    else if ( remaining_length <= 2097151 )
    {
        return 3;
    }
    else if ( remaining_length <= 268435455 )
    {
        return 4;
    }

    /* terrible failure */
    assert( 0 == 1 );
    return 0;
}

xi_state_t xi_mqtt_serialiser_size( size_t* msg_len,
                                    size_t* remaining_len,
                                    size_t* publish_payload_len,
                                    xi_mqtt_serialiser_t* serialiser,
                                    const xi_mqtt_message_t* message )
{
    if ( NULL == msg_len || NULL == remaining_len || NULL == publish_payload_len )
    {
        return XI_INVALID_PARAMETER;
    }

    /* reset the values */
    *msg_len             = 0;
    *remaining_len       = 0;
    *publish_payload_len = 0;

    /* this is currently unused, however it may be used
     * to save the state of serialiser in case of serialializing message
     * in turns */
    ( void )serialiser;

    /* first byte stays for fixed header */
    *msg_len = 1;

    if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_CONNECT )
    {
        /* @TODO: Should get this first len from the actual protocol_name
         * variable, instead of hardcoding it */
        *msg_len += 6; /* protocol name */
        *msg_len += 1; /* protocol version */
        *msg_len += 1; /* connect flags */
        *msg_len += 2; /* keep alive timer */

        *msg_len += 2; /* size of client id length */

        if ( NULL != message->connect.client_id )
        {
            *msg_len += message->connect.client_id->length;
        }

        if ( message->connect.flags_u.flags_bits.username_follows )
        {
            *msg_len += 2; /* size of username length */
            *msg_len += message->connect.username->length;
        }

        if ( message->connect.flags_u.flags_bits.password_follows )
        {
            *msg_len += 2; /* size of password length */
            *msg_len += message->connect.password->length;
        }

        if ( message->connect.flags_u.flags_bits.will )
        {
            *msg_len += 4; /* size of will topic length and size of
                            * will message length */
            *msg_len += message->connect.will_topic->length;
            *msg_len += message->connect.will_message->length;
        }
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_CONNACK )
    {
        *msg_len += 2;
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_PUBLISH )
    {
        *msg_len += 2; /* size */
        *msg_len += message->publish.topic_name->length;

        if ( message->publish.common.common_u.common_bits.qos > 0 )
        {
            *msg_len += 2; /* size */
        }

        *msg_len += message->publish.content->length;
        *publish_payload_len += message->publish.content->length;
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_PUBACK )
    {
        *msg_len += 2; /* size of the msg id */
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_DISCONNECT )
    {
        /* empty */
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_SUBSCRIBE )
    {
        *msg_len += 2; /* size msgid */
        *msg_len += 2; /* size of topic */

        /* @TODO add support for multiple topics per request */
        *msg_len += message->subscribe.topics->name->length;
        *msg_len += 1; /* qos */
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_SUBACK )
    {
        *msg_len += 2; /* size of the msg id */
        *msg_len += 1; /* qos */
    }
    else if ( message->common.common_u.common_bits.type == XI_MQTT_TYPE_PINGREQ )
    {
        /* just a fixed header */
    }
    else
    {
        return XI_MQTT_SERIALIZER_ERROR;
    }

    *remaining_len = *msg_len - 1; /* because remianing length does not contain
                                    * the size of fixed header */
    *msg_len += xi_mqtt_get_remaining_length_bytes( *remaining_len );

    return XI_STATE_OK;
}

xi_mqtt_serialiser_rc_t xi_mqtt_serialiser_write( xi_mqtt_serialiser_t* serialiser,
                                                  const xi_mqtt_message_t* message,
                                                  xi_data_desc_t* buffer,
                                                  const size_t message_len,
                                                  const size_t remaining_len )
{
    XI_UNUSED( message_len );

    size_t tmp_remaining_len = 0;
    uint32_t value           = 0;

    WRITE_8( buffer, message->common.common_u.common_value );

    tmp_remaining_len = remaining_len;

    do
    {
        /* mask the least significant 7 bits */
        value = tmp_remaining_len & 0x7f;

        /* shift so the rest of the number become availible */
        tmp_remaining_len >>= 7;

        /* if the value is greater set the continuation bit */
        WRITE_8( buffer, value | ( tmp_remaining_len > 0 ? 0x80 : 0x0 ) );
    } while ( tmp_remaining_len > 0 );

    switch ( message->common.common_u.common_bits.type )
    {
        case XI_MQTT_TYPE_CONNECT:
        {
            WRITE_STRING( buffer, message->connect.protocol_name );

            WRITE_8( buffer, message->connect.protocol_version );
            WRITE_8( buffer, message->connect.flags_u.flags_value );

            WRITE_16( buffer, message->connect.keepalive );

            WRITE_STRING( buffer, message->connect.client_id );

            if ( message->connect.flags_u.flags_bits.will )
            {
                WRITE_STRING( buffer, message->connect.will_topic );
                WRITE_STRING( buffer, message->connect.will_message );
            }

            if ( message->connect.flags_u.flags_bits.username_follows )
            {
                WRITE_STRING( buffer, message->connect.username );
            }

            if ( message->connect.flags_u.flags_bits.password_follows )
            {
                WRITE_STRING( buffer, message->connect.password );
            }

            break;
        }

        case XI_MQTT_TYPE_CONNACK:
        {
            WRITE_8( buffer, message->connack._unused );
            WRITE_8( buffer, message->connack.return_code );

            break;
        }

        case XI_MQTT_TYPE_PUBLISH:
        {
            WRITE_STRING( buffer, message->publish.topic_name );

            if ( message->common.common_u.common_bits.qos > 0 )
            {
                WRITE_16( buffer, message->publish.message_id );
            }

            /* Because the publish payload is being sent in separation by the
             * mqtt codec layer. Writing the payloads data is no longer part
             * of a serialisation process so that line is obsolete in current
             * version.
             *
             * This comment is to remember about that very important line of code
             * whenever we will change the implementation of the mqtt_codec_layer
             * WRITE_DATA( buffer,message->publish.content ) */

            break;
        }

        case XI_MQTT_TYPE_PUBACK:
        {
            WRITE_16( buffer, message->puback.message_id );

            break;
        }

        case XI_MQTT_TYPE_SUBSCRIBE:
        {
            /* write the message identifier the subscribe is using
             * the QoS 1 anyway */

            WRITE_16( buffer, message->subscribe.message_id );

            WRITE_STRING( buffer, message->subscribe.topics->name );

            WRITE_8( buffer,
                     message->subscribe.topics->xi_mqtt_topic_pair_payload_u.qos & 0xFF );
            break;
        }

        case XI_MQTT_TYPE_SUBACK:
        {
            WRITE_16( buffer, message->suback.message_id );

            WRITE_8( buffer,
                     message->subscribe.topics->xi_mqtt_topic_pair_payload_u.status &
                         0xFF );
            break;
        }

        case XI_MQTT_TYPE_DISCONNECT:
        {
            /* empty */
            break;
        }

        case XI_MQTT_TYPE_PINGREQ:
        {
            /* empty */
            break;
        }

        default:
        {
            serialiser->error = XI_MQTT_ERROR_SERIALISER_INVALID_MESSAGE_ID;
            return XI_MQTT_SERIALISER_RC_ERROR;
        }
    }

    return XI_MQTT_SERIALISER_RC_SUCCESS;

err_handling:
    return XI_MQTT_SERIALISER_RC_ERROR;
}
