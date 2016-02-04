/**	@file ciddeco.h
 *	
 *	@author Jenil Jain
 *	@copyright Copyright 2015 Insensi Inc. All rights reserved.
 *  This project is released under the GNU Public License.
 *
 * 	@brief Decodes CallerID message
 *
 *	@note Includes code and algorithms from the Zapata library and Aesterisk.
 */

#ifndef CIDDECO_FSK_H
#define CIDDECO_FSK_H

#include <stdint.h>
#include <tinyalsa/asoundlib.h>

#define CID_SIG_V23             0                               ///< Caller ID standard 
#define CID_BELLCORE_FSK        1                               ///< Caller ID standard used in US

/**@brief Wav file header
 *
 * The header is the beginning of a WAV (RIFF) file. The header is used 
 * to provide specifications on the file type, sample rate, sample size 
 * and bit size of the file, as well as its overall length.
 */
typedef struct wav_header{
    char     chunk_id[4];
    uint32_t chunk_size;
    char     format[4];
    char     fmtchunk_id[4];
    uint32_t fmtchunk_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bps;
    char     datachunk_id[4];
    uint32_t datachunk_size;
}wav_header;

/// Parameters for FSK Demodulation
typedef struct param{
	int samp_rate;                  ///< Sampling frequency can be 8kHz, 16kHz, 19.2kHz, 44.1kHz
	int baud_rate;	                ///< Typical Baud rate is 1200
	float ispb;                     ///< No. of samples required to get a data bit(sample_rate/baud_rate)
}param;

/// PCM capture parameters
typedef struct pcm_capture{
	unsigned int card; 
	unsigned int device;
	unsigned int channels; 
	unsigned int rate;
	enum pcm_format format; 
	unsigned int period_size;
	unsigned int period_count;
	unsigned int size;
}pcm_capture;

typedef struct cid_data{
	char date[20];
	char call_time[20];
	char name[20];
	char number[20];
}cid_data;

/// Defining struct for Caller ID state machine
struct callerid_state {

	fsk_data fskd;                  ///< Structure containing parameters for FSK modulation
	int rawdata[256];               ///< buffer to store the CID data bytes
	short oldstuff[1000];           ///< buffer to store previous samples
	int oldlen;					
	int pos;
	int type;
	int cksum;
	char name[64];                  ///< Array to store the Name
	char number[64];                ///< Array to store the Number
	char date_time[64];             ///< Array to store the formated date and time 
	int flags;	
	int sawflag;                    ///< Flag for the decoding state machine
	int len;

	int skipflag; 
	unsigned short crc;
};

/** @brief Create a callerID state machine
 */
struct callerid_state *callerid_new(int cid_signalling, param *demod_param);

/** @brief Read samples into the state machine.
 */
int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len);

/** @brief This function frees callerid_state cid.
 */
void callerid_free(struct callerid_state *cid);

#endif
