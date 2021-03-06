/*-----------------------------------------*\
|  RGBController_LEDStrip.cpp               |
|                                           |
|  Generic RGB Interface for OpenAuraSDK    |
|  E1.31 Streaming ACN interface            |
|                                           |
|  Adam Honse (CalcProgrammer1) 10/18/2019  |
\*-----------------------------------------*/

#include "RGBController_E131.h"
#include <e131.h>
#include <math.h>

using namespace std::chrono_literals;

RGBController_E131::RGBController_E131(std::vector<E131Device> device_list)
{
    name        = "E1.31 Streaming ACN Device";
    type        = DEVICE_TYPE_LEDSTRIP;
    description = "E1.31 Streaming ACN Device";

    devices = device_list;

    mode Direct;
    Direct.name       = "Direct";
    Direct.value      = 0;
    Direct.flags      = MODE_FLAG_HAS_PER_LED_COLOR;
    Direct.color_mode = MODE_COLORS_PER_LED;
    modes.push_back(Direct);

    sockfd = e131_socket();
    
    keepalive_delay = 0ms;

    SetupZones();

    for (std::size_t device_idx = 0; device_idx < devices.size(); device_idx++)
    {
        /*-----------------------------------------*\
        | Update keepalive delay                    |
        \*-----------------------------------------*/
        if(devices[device_idx].keepalive_time > 0)
        {
            if(keepalive_delay.count() == 0 || keepalive_delay.count() > devices[device_idx].keepalive_time)
            {
                keepalive_delay = std::chrono::milliseconds(devices[device_idx].keepalive_time);
            }
        }

        /*-----------------------------------------*\
        | Add Universes                             |
        \*-----------------------------------------*/
        unsigned int total_universes = ceil( ( ( devices[device_idx].num_leds * 3 ) + devices[device_idx].start_channel ) / 512.0f );

        for (unsigned int univ_idx = 0; univ_idx < total_universes; univ_idx++)
        {
            unsigned int universe = devices[device_idx].start_universe + univ_idx;
            bool universe_exists = false;

            for (std::size_t pkt_idx = 0; pkt_idx < packets.size(); pkt_idx++)
            {
                if(universes[pkt_idx] == universe)
                {
                    universe_exists = true;
                }
            }

            if(!universe_exists)
            {
                e131_packet_t   packet;
                e131_addr_t     dest_addr;

                e131_pkt_init(&packet, universe, 512);
                e131_multicast_dest(&dest_addr, universe, E131_DEFAULT_PORT);

                packets.push_back(packet);
                universes.push_back(universe);
                dest_addrs.push_back(dest_addr);
            }
        }

        /*-----------------------------------------*\
        | Generate matrix maps                      |
        \*-----------------------------------------*/
        if(devices[device_idx].type == ZONE_TYPE_MATRIX)
        {
            unsigned int led_idx = 0;
            matrix_map_type * new_map = new matrix_map_type;

            new_map->width = devices[device_idx].matrix_width;
            new_map->height = devices[device_idx].matrix_height;
            new_map->map = new unsigned int[devices[device_idx].matrix_width * devices[device_idx].matrix_height];

            switch(devices[device_idx].matrix_order)
            {
                case E131_MATRIX_ORDER_HORIZONTAL_TOP_LEFT:
                    for(unsigned int y = 0; y < new_map->height; y++)
                    {
                        for(unsigned int x = 0; x < new_map->width; x++)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_HORIZONTAL_TOP_RIGHT:
                    for(unsigned int y = 0; y < new_map->height; y++)
                    {
                        for(int x = new_map->width - 1; x >= 0; x--)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_HORIZONTAL_BOTTOM_LEFT:
                    for(int y = new_map->height; y >= 0; y--)
                    {
                        for(unsigned int x = 0; x < new_map->width; x++)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_HORIZONTAL_BOTTOM_RIGHT:
                    for(int y = new_map->height; y >= 0; y--)
                    {
                        for(int x = new_map->width - 1; x >= 0; x--)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_VERTICAL_TOP_LEFT:
                    for(unsigned int x = 0; x < new_map->width; x++)
                    {
                        for(unsigned int y = 0; y < new_map->height; y++)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_VERTICAL_TOP_RIGHT:
                    for(int x = new_map->width - 1; x >= 0; x--)
                    {
                        for(unsigned int y = 0; y < new_map->height; y++)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_VERTICAL_BOTTOM_LEFT:
                    for(unsigned int x = 0; x < new_map->width; x++)
                    {
                        for(int y = new_map->height - 1; y >= 0; y--)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
                case E131_MATRIX_ORDER_VERTICAL_BOTTOM_RIGHT:
                    for(int x = new_map->width - 1; x >= 0; x--)
                    {
                        for(int y = new_map->height - 1; y >= 0; y--)
                        {
                            new_map->map[(y * new_map->width) + x] = led_idx;
                            led_idx++;
                        }
                    }
                    break;
            }
            zones[device_idx].matrix_map = new_map;
        }
	}

    if(keepalive_delay.count() > 0)
    {
        KeepaliveThread = new std::thread(&RGBController_E131::KeepaliveThreadFunction, this);
    }
}

void RGBController_E131::SetupZones()
{
    /*-----------------------------------------*\
    | Add Zones                                 |
    \*-----------------------------------------*/
    for(std::size_t zone_idx = 0; zone_idx < devices.size(); zone_idx++)
    {
        zone led_zone;
        led_zone.name           = devices[zone_idx].name;
        led_zone.type           = devices[zone_idx].type;
        led_zone.leds_min       = devices[zone_idx].num_leds;
        led_zone.leds_max       = devices[zone_idx].num_leds;
        led_zone.leds_count     = devices[zone_idx].num_leds;
        led_zone.matrix_map     = NULL;

        zones.push_back(led_zone);
    }

    /*-----------------------------------------*\
    | Add LEDs                                  |
    \*-----------------------------------------*/
    for(std::size_t zone_idx = 0; zone_idx < zones.size(); zone_idx++)
    {
        for(std::size_t led_idx = 0; led_idx < zones[zone_idx].leds_count; led_idx++)
        {
            led new_led;

            new_led.name = zones[zone_idx].name + " LED ";
            new_led.name.append(std::to_string(led_idx));

            leds.push_back(new_led);
        }
    }

    SetupColors();
}

void RGBController_E131::ResizeZone(int /*zone*/, int /*new_size*/)
{
    /*---------------------------------------------------------*\
    | This device does not support resizing zones               |
    \*---------------------------------------------------------*/
}

void RGBController_E131::DeviceUpdateLEDs()
{
    int color_idx = 0;

    last_update_time = std::chrono::steady_clock::now();
    
    for(std::size_t device_idx = 0; device_idx < devices.size(); device_idx++)
    {
        unsigned int total_universes = ceil( ( ( devices[device_idx].num_leds * 3 ) + devices[device_idx].start_channel ) / 512.0f );
        unsigned int channel_idx = devices[device_idx].start_channel;
        unsigned int led_idx = 0;
        unsigned int rgb_idx = 0;
        bool         done = false;

        for (unsigned int univ_idx = 0; univ_idx < total_universes; univ_idx++)
        {
            unsigned int universe = devices[device_idx].start_universe + univ_idx;

            for(std::size_t packet_idx = 0; packet_idx < packets.size(); packet_idx++)
            {
                if(!done && (universes[packet_idx] == universe))
                {
                    while(!done && (channel_idx <= 512))
                    {
                        switch(rgb_idx)
                        {
                            case 0:
                                packets[packet_idx].dmp.prop_val[channel_idx] = RGBGetRValue( colors[color_idx] );
                                rgb_idx = 1;
                                break;
                            case 1:
                                packets[packet_idx].dmp.prop_val[channel_idx] = RGBGetGValue( colors[color_idx] );
                                rgb_idx = 2;
                                break;
                            case 2:
                                packets[packet_idx].dmp.prop_val[channel_idx] = RGBGetBValue( colors[color_idx] );
                                rgb_idx = 0;
                                led_idx++;
                                color_idx++;
                                break;
                        }

                        if(led_idx >= devices[device_idx].num_leds)
                        {
                            done = true;
                        }

                        channel_idx++;
                    }
                }
            }

            channel_idx = 1;
        }
    }

    for(std::size_t packet_idx = 0; packet_idx < packets.size(); packet_idx++)
    {
        e131_send(sockfd, &packets[packet_idx], &dest_addrs[packet_idx]);
        packets[packet_idx].frame.seq_number++;
    }
}

void RGBController_E131::UpdateZoneLEDs(int /*zone*/)
{
	DeviceUpdateLEDs();
}

void RGBController_E131::UpdateSingleLED(int /*led*/)
{
	DeviceUpdateLEDs();
}

void RGBController_E131::SetCustomMode()
{

}

void RGBController_E131::DeviceUpdateMode()
{

}

void RGBController_E131::KeepaliveThreadFunction()
{
    while(1)
    {
        if((std::chrono::steady_clock::now() - last_update_time) > ( keepalive_delay * 0.95f ) )
        {
            UpdateLEDs();
        }
        std::this_thread::sleep_for(keepalive_delay / 2);
    }
}
