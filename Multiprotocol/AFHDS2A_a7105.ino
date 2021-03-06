/*
 This project is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Multiprotocol is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with Multiprotocol.  If not, see <http://www.gnu.org/licenses/>.
 */
// Last sync with hexfet new_protocols/flysky_a7105.c dated 2015-09-28

#ifdef AFHDS2A_A7105_INO

#define AFHDS2A_TXPACKET_SIZE	38
#define AFHDS2A_RXPACKET_SIZE	37
#define AFHDS2A_NUMFREQ			16

enum{
	AFHDS2A_PACKET_STICKS,
	AFHDS2A_PACKET_SETTINGS,
	AFHDS2A_PACKET_FAILSAFE,
};

enum{
	AFHDS2A_BIND1,
	AFHDS2A_BIND2,
	AFHDS2A_BIND3,
	AFHDS2A_BIND4,
	AFHDS2A_DATA,
};

static void AFHDS2A_calc_channels()
{
	uint8_t idx = 0;
	uint32_t rnd = MProtocol_id;
	while (idx < AFHDS2A_NUMFREQ)
	{
		uint8_t i;
		uint8_t count_1_42 = 0, count_43_85 = 0, count_86_128 = 0, count_129_168 = 0;
		rnd = rnd * 0x0019660D + 0x3C6EF35F; // Randomization

		uint8_t next_ch = ((rnd >> (idx%32)) % 0xa8) + 1;
		// Keep the distance 2 between the channels - either odd or even
		if (((next_ch ^ MProtocol_id) & 0x01 )== 0)
			continue;
		// Check that it's not duplicate and spread uniformly
		for (i = 0; i < idx; i++)
		{
			if(hopping_frequency[i] == next_ch)
				break;
			if(hopping_frequency[i] <= 42)
				count_1_42++;
			else if (hopping_frequency[i] <= 85)
				count_43_85++;
			else if (hopping_frequency[i] <= 128)
				count_86_128++;
			else
				count_129_168++;
		}
		if (i != idx)
			continue;
		if ((next_ch <= 42 && count_1_42 < 5)
			||(next_ch >= 43 && next_ch <= 85 && count_43_85 < 5)
			||(next_ch >= 86 && next_ch <=128 && count_86_128 < 5)
			||(next_ch >= 129 && count_129_168 < 5))
			hopping_frequency[idx++] = next_ch;
	}
}

#if defined(TELEMETRY)
// telemetry sensors ID
enum{
	AFHDS2A_SENSOR_RX_VOLTAGE   = 0x00,
	AFHDS2A_SENSOR_RX_ERR_RATE  = 0xfe,
	AFHDS2A_SENSOR_RX_RSSI      = 0xfc,
	AFHDS2A_SENSOR_RX_NOISE     = 0xfb,
	AFHDS2A_SENSOR_RX_SNR       = 0xfa,
};

static void AFHDS2A_update_telemetry()
{
	// AA | TXID | rx_id | sensor id | sensor # | value 16 bit big endian | sensor id ......
	// max 7 sensors per packet

	for(uint8_t sensor=0; sensor<7; sensor++)
	{
		uint8_t index = 9+(4*sensor);
		switch(packet[index])
		{
			case AFHDS2A_SENSOR_RX_VOLTAGE:
				v_lipo = packet[index+3]<<8 | packet[index+2];
				telemetry_link=1;
				break;
			/*case AFHDS2A_SENSOR_RX_ERR_RATE:
				// packet[index+2];
				break;*/
			case AFHDS2A_SENSOR_RX_RSSI:
				RSSI_dBm = -packet[index+2];
				break;
			case 0xff:
				return;
			/*default:
				// unknown sensor ID
				break;*/
		}
	}
}
#endif

static void AFHDS2A_build_bind_packet()
{
	uint8_t ch;
	memcpy( &packet[1], rx_tx_addr, 4);
	memset( &packet[5], 0xff, 4);
	packet[10]= 0x00;
	for(ch=0; ch<AFHDS2A_NUMFREQ; ch++)
		packet[11+ch] = hopping_frequency[ch];
	memset( &packet[27], 0xff, 10);
	packet[37] = 0x00;
	switch(phase)
	{
		case AFHDS2A_BIND1:
			packet[0] = 0xbb;
			packet[9] = 0x01;
			break;
		case AFHDS2A_BIND2:
		case AFHDS2A_BIND3:
		case AFHDS2A_BIND4:
			packet[0] = 0xbc;
			if(phase == AFHDS2A_BIND4)
			{
				memcpy( &packet[5], &rx_id, 4);
				memset( &packet[11], 0xff, 16);
			}
			packet[9] = phase-1;
			if(packet[9] > 0x02)
				packet[9] = 0x02;
			packet[27]= 0x01;
			packet[28]= 0x80;
			break;
	}
}

static void AFHDS2A_build_packet(uint8_t type)
{
	memcpy( &packet[1], rx_tx_addr, 4);
	memcpy( &packet[5], rx_id, 4);
	switch(type)
	{
		case AFHDS2A_PACKET_STICKS:		
			packet[0] = 0x58;
			for(uint8_t ch=0; ch<14; ch++)
			{
				packet[9 +  ch*2] = Servo_data[CH_AETR[ch]]&0xFF;
				packet[10 + ch*2] = (Servo_data[CH_AETR[ch]]>>8)&0xFF;
			}
			break;
		case AFHDS2A_PACKET_FAILSAFE:
			packet[0] = 0x56;
			for(uint8_t ch=0; ch<14; ch++)
			{
				/*if((Model.limits[ch].flags & CH_FAILSAFE_EN))
				{
					packet[9 + ch*2] = Servo_data[CH_AETR[ch]] & 0xff;
					packet[10+ ch*2] = (Servo_data[CH_AETR[ch]] >> 8) & 0xff;
				}
				else*/
				{
					packet[9 + ch*2] = 0xff;
					packet[10+ ch*2] = 0xff;
				}
			}
			break;
		case AFHDS2A_PACKET_SETTINGS:
			packet[0] = 0xaa;
			packet[9] = 0xfd;
			packet[10]= 0xff;
			uint16_t val_hz=5*option+50;			// option value should be between 0 and 70 which gives a value between 50 and 400Hz
			if(val_hz<50 || val_hz>400) val_hz=50;	// default is 50Hz
			packet[11]= val_hz;
			packet[12]= val_hz >> 8;
			if(sub_protocol == PPM_IBUS || sub_protocol == PPM_SBUS)
				packet[13] = 0x01;	// PPM output enabled
			else
				packet[13] = 0x00;
			packet[14]= 0x00;
			for(uint8_t i=15; i<37; i++)
				packet[i] = 0xff;
			packet[18] = 0x05;		// ?
			packet[19] = 0xdc;		// ?
			packet[20] = 0x05;		// ?
			if(sub_protocol == PWM_SBUS || sub_protocol == PPM_SBUS)
				packet[21] = 0xdd;	// SBUS output enabled
			else
				packet[21] = 0xde;	// IBUS
			break;
	}
	packet[37] = 0x00;
}

#define AFHDS2A_WAIT_WRITE 0x80
#ifdef STM32_BOARD
	#define AFHDS2A_DELAY 0
#else
	#define AFHDS2A_DELAY 700
#endif
uint16_t ReadAFHDS2A()
{
	static uint8_t packet_type = AFHDS2A_PACKET_STICKS;
	static uint16_t packet_counter=0;
	
	switch(phase)
	{
		case AFHDS2A_BIND1:
		case AFHDS2A_BIND2:
		case AFHDS2A_BIND3:
			A7105_Strobe(A7105_STANDBY);
			A7105_SetTxRxMode(TX_EN);
			AFHDS2A_build_bind_packet();
			A7105_WriteData(AFHDS2A_TXPACKET_SIZE, packet_count%2 ? 0x0d : 0x8c);
			if(A7105_ReadReg(A7105_00_MODE) == 0x1b)
			{ // todo: replace with check crc+fec
				A7105_Strobe(A7105_RST_RDPTR);
				A7105_ReadData(AFHDS2A_RXPACKET_SIZE);
				if(packet[0] == 0xbc)
				{
					uint8_t temp=50+RX_num*4;
					for(uint8_t i=0; i<4; i++)
					{
						rx_id[i] = packet[5+i];
						eeprom_write_byte((EE_ADDR)(temp+i),rx_id[i]);
					}
					if(packet[9] == 0x01)
						phase = AFHDS2A_BIND4;
				}
			}
			packet_count++;
			phase |= AFHDS2A_WAIT_WRITE;
			return 1700+AFHDS2A_DELAY;
		case AFHDS2A_BIND1|AFHDS2A_WAIT_WRITE:
		case AFHDS2A_BIND2|AFHDS2A_WAIT_WRITE:
		case AFHDS2A_BIND3|AFHDS2A_WAIT_WRITE:
			A7105_SetTxRxMode(RX_EN);
			A7105_Strobe(A7105_RX);
			phase &= ~AFHDS2A_WAIT_WRITE;
			phase++;
			if(phase > AFHDS2A_BIND3)
				phase = AFHDS2A_BIND1;
			return 2150-AFHDS2A_DELAY;
		case AFHDS2A_BIND4:
			A7105_SetTxRxMode(TX_EN);
			A7105_Strobe(A7105_STANDBY);
			AFHDS2A_build_bind_packet();
			A7105_WriteData(AFHDS2A_TXPACKET_SIZE, packet_count%2 ? 0x0d : 0x8c);
			packet_count++;
			bind_phase++;
			if(bind_phase>=4)
			{ 
				packet_counter=0;
				packet_type = AFHDS2A_PACKET_STICKS;
				hopping_frequency_no=1;
				phase = AFHDS2A_DATA;
				BIND_DONE;
			}                        
			phase |= AFHDS2A_WAIT_WRITE;
			return 1700+AFHDS2A_DELAY;
		case AFHDS2A_BIND4|AFHDS2A_WAIT_WRITE:
			A7105_SetTxRxMode(RX_EN);
			A7105_Strobe(A7105_RX);
			phase &= ~AFHDS2A_WAIT_WRITE;
			return 2150-AFHDS2A_DELAY;
		case AFHDS2A_DATA:    
			A7105_SetTxRxMode(TX_EN);
			A7105_Strobe(A7105_STANDBY);
			AFHDS2A_build_packet(packet_type);
			A7105_WriteData(AFHDS2A_TXPACKET_SIZE, hopping_frequency[hopping_frequency_no++]);
			if(hopping_frequency_no >= AFHDS2A_NUMFREQ)
				hopping_frequency_no = 0;
			if(!(packet_counter % 1313))
				packet_type = AFHDS2A_PACKET_SETTINGS;
			else if(!(packet_counter % 1569))
				packet_type = AFHDS2A_PACKET_FAILSAFE;
			else
				packet_type = AFHDS2A_PACKET_STICKS; // todo : check for settings changes
			// got some data from RX ?
			// we've no way to know if RX fifo has been filled
			// as we can't poll GIO1 or GIO2 to check WTR
			// we can't check A7105_MASK_TREN either as we know
			// it's currently in transmit mode.
			if(!(A7105_ReadReg(A7105_00_MODE) & (1<<5 | 1<<6)))
			{ // FECF+CRCF Ok
				A7105_Strobe(A7105_RST_RDPTR);
				A7105_ReadData(1);
				if(packet[0] == 0xaa)
				{
					A7105_Strobe(A7105_RST_RDPTR);
					A7105_ReadData(AFHDS2A_RXPACKET_SIZE);
					if(packet[9] == 0xfc)
						packet_type=AFHDS2A_PACKET_SETTINGS;	// RX is asking for settings
					#if defined(TELEMETRY)
					else
						AFHDS2A_update_telemetry();
					#endif
				}
			}
			packet_counter++;
			phase |= AFHDS2A_WAIT_WRITE;
			return 1700+AFHDS2A_DELAY;
		case AFHDS2A_DATA|AFHDS2A_WAIT_WRITE:
			A7105_SetTxRxMode(RX_EN);
			phase &= ~AFHDS2A_WAIT_WRITE;
			A7105_Strobe(A7105_RX);
			return 2150-AFHDS2A_DELAY;
	}
	return 3850; // never reached, please the compiler
}

uint16_t initAFHDS2A()
{
	A7105_Init();

	AFHDS2A_calc_channels();
	packet_count = 0;
	bind_phase = 0;
	if(IS_AUTOBIND_FLAG_on)
		phase = AFHDS2A_BIND1;
	else
	{
		phase = AFHDS2A_DATA;
		//Read RX ID from EEPROM based on RX_num, RX_num must be uniq for each RX
		uint8_t temp=50+RX_num*4;
		for(uint8_t i=0;i<4;i++)
			rx_id[i]=eeprom_read_byte((EE_ADDR)(temp+i));
	}
	hopping_frequency_no = 0;
	return 50000;
}
#endif
