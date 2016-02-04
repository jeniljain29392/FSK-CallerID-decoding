/**@file ciddeco.c
 *	
 * @author Jenil Jain
 * This project is released under the GNU Public License.
 *
 * @brief Decodes CallerID message
 *
 * Send the callerID buffered bytes for FSK demodulation and decode the 
 * Caller ID message from it.
 *
 * @note Includes code and algorithms from the Zapata library and Aesterisk.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include "fskmodem.h"
#include "ciddeco.h"

#define MDMF                    0x80    // Multiple data message format
#define	SDMF                    0x04    // Simple data message format

#define DATE_TIME               0x01
#define NAME                    0x07
#define NO_NAME                 0x08
#define NUM                     0x02
#define NO_NUM                  0x04

#define MESSAGE_TYPE            0       // Message type
#define MESSAGE_LENGTH          1       // length of Message
#define DATA_TYPE               2       // Type of data
#define DATA_LENGTH             3       // Length of Data
#define DATA                    4       // Data
#define CHECKSUM                5       // Checksum
#define UNKNOWN	                10      // Unknown data byte

/* Global variables */
sem_t mutex;
int capturing = 0, buf_ready = 0;
struct timeval stop, start;
int diff;

struct callerid_state *cs = NULL;       // Structure containing Caller ID parameters 
struct pcm *pcm;
char *buffer;


/**@brief Create a callerID state machine
 * 	
 * This function returns a malloc'd instance of the callerid_state data structure.
 *
 * @param cid_signalling Type of signalling in use
 * @param demod_param	pointer to struct param containing sampling and baud rate
 * @return Returns a pointer to a malloc'd callerid_state structure, or NULL on error.
 */
struct callerid_state *callerid_new(int cid_signalling, param * demod_param)
{
    struct callerid_state *cid;

    if ((cid = calloc(1, sizeof(*cid)))) {

	cid->fskd.ispb = demod_param->ispb;             // Samples per bit data
	cid->fskd.pllispb = cid->fskd.ispb * 32;        // Total count for PLL
	cid->fskd.pllids = cid->fskd.pllispb / 32;      // PLL adustment
        cid->fskd.pllispb2 = cid->fskd.pllispb / 2;     // PLL center point                    
	cid->fskd.pll_round_off = 1 / (demod_param->ispb - cid->fskd.ispb);	
	                                                // PLL roundoff
	cid->fskd.icont = 0;                            // PLL counter Reset 
	cid->fskd.nbit = 8;                             // no. of bits in a FSK frame
	cid->fskd.instop = 2;                           // no. of stop bit after every byte in the data frame
	cid->fskd.fsk_std = cid_signalling;             // FSK standard
	cid->fskd.state = 0;
	cid->sawflag = 0;

	fskmodem_init(&cid->fskd);                      // Initializinf FSK demodulation parameters
	return cid;
    } else
	return NULL;
}


/**@brief This function frees callerid_state cid.
 * @param cid This is the callerid_state state machine to free
 */
void callerid_free(struct callerid_state *cid)
{
    free(cid);
}

/**@brief This function Display the CallerID message
 * @param cid This is the callerid_state state machine to free
 */
static void get_CID_info(struct callerid_state *cid, cid_data * data)
{
    int i, j;
    const char *months[12] = {
	"January", "February", "March", "April", "May", "June",
	"July", "August", "September", "October", "November", "December"
    };

    printf("\nCID Message is\n\n");

    int month = (cid->rawdata[4] - 48) * 10 + (cid->rawdata[5] - 48);
    sprintf(data->date, "%s %c%c", months[month - 1], cid->rawdata[6],
	    cid->rawdata[7]);
    sprintf(data->call_time, "%c%c hr : %c%c min", cid->rawdata[8],
	    cid->rawdata[9], cid->rawdata[10], cid->rawdata[11]);

    if (cid->rawdata[12] == NAME) {
	for (i = 0; i < cid->rawdata[13]; ++i)
	    sprintf(data->name + i, "%c", cid->rawdata[14 + i]);
	for (j = 0; j < cid->rawdata[15 + i]; ++j)
	    sprintf(data->number + j, "%c", cid->rawdata[16 + i + j]);
    } else if (cid->rawdata[12] == NUM) {
	for (j = 0; j < cid->rawdata[13]; ++j)
	    sprintf(data->number + j, "%c", cid->rawdata[14 + j]);
	for (i = 0; i < cid->rawdata[15 + j]; ++i)
	    sprintf(data->name + i, "%c", cid->rawdata[16 + j + i]);
    }

    memcpy(cid->name, data->name, sizeof(data->name));
    memcpy(cid->number, data->number, sizeof(data->number));
    sprintf(cid->date_time, "%s %s", data->date, data->call_time);

    printf("*****************************************************\n");
    printf("%s %s %s\n", cid->date_time, cid->number, cid->name);
    printf("*****************************************************\n\n");
}

/**@brief Decoding the CID message.
 *
 * The data block message bytes are organized as follows:                       <BR>
 * 1) Message type, 1 byte -- 0x80 (MDMF), or 0x04 (SDMF).                      <BR>
 * 2) Length , 1 byte -- This length value excludes the single-byte Checksum    <BR>
 * 3) Data type, 1 byte -- 0x01 = date & time,									
 *                      0x02 = phone number, 								
 *                      0x04 = number not present, 							
 *                      0x07 = name, 									
 *                      0x08 = name not present	                                <BR>
 * 4) Length of data, 1 byte                                                    <BR>
 * 5) Data bytes, variable number according to length                           <BR>
 * 6) Repeat Items 3, 4, and 5 as needed to complete the Caller ID message      <BR>
 * 7) Checksum, 1 byte -- Add this to the modulo-256 sum of all the 
 * previous bytes in the message, including the message type and 
 * message length; a zero result indicates no errors detected                   <BR>
 *
 * @param cid pointer to callerid_state data structure
 * @param data_byte demodulated FSK message byte
 *	
 * @return 0 if successful else -1 if error
 */
static int decode_CID_msg(struct callerid_state *cid, int data_byte)
{
    int a = data_byte;

    static int name_field = 0;
    static int number_field = 0;
    static int msg_type = 0;
    static int msg_off = 0, data_field;

    fprintf(stderr, "\t\t%d\n\n", a);
    cid->rawdata[msg_off++] = a;

    switch (cid->sawflag) {

    case MESSAGE_TYPE:
	cid->sawflag = MESSAGE_LENGTH;
	switch (a) {
	case MDMF:
	    fprintf(stderr, "\t\tMessage is MDMF\n\n");
	    msg_type = MDMF;
	    break;
	case SDMF:
	    fprintf(stderr, "\t\tMessage is SDMF\n\n");
	    msg_type = SDMF;
	    break;
	default:
	    fprintf(stderr, "\t\tUnknown Message format\n\n");
	    cid->sawflag = UNKNOWN;
	    break;
	}
	break;
    case MESSAGE_LENGTH:
	fprintf(stderr, "\t\tLength of CID message id %d\n\n", a);
	cid->sawflag = DATA_TYPE;
	break;
    case DATA_TYPE:
	cid->sawflag = DATA_LENGTH;

	switch (a) {
	case DATE_TIME:
	    fprintf(stderr, "\t\tData type is \"Date & time\"\n\n");
	    break;
	case NUM:
	    fprintf(stderr, "\t\tData type is \"Phone Number\"\n\n");
	    number_field = 1;
	    break;
	case NO_NUM:
	    fprintf(stderr, "\t\tData type is \"No Number\"\n\n");
	    number_field = 1;
	    break;
	case NAME:
	    fprintf(stderr, "\t\tData type is \"Name\" \n\n");
	    name_field = 1;
	    break;
	case NO_NAME:
	    fprintf(stderr, "\t\tData type is \"No Name\" \n\n");
	    name_field = 1;
	    break;
	default:
	    cid->sawflag = UNKNOWN;
	    break;
	}
	break;
    case DATA_LENGTH:
	fprintf(stderr, "\t\tLength of Data is %d\n\n", a);
	data_field = a;
	cid->sawflag = DATA;
	break;
    case DATA:
	if (--data_field == 0) {        // loop till we get all data bytes
	    if ((msg_type == MDMF) && name_field && number_field) {
		name_field = 0;
		number_field = 0;       // Exit when last field is complete
		cid->sawflag = CHECKSUM;
	    } else if ((msg_type == SDMF) && number_field) {
		number_field = 0;       // Exit when last field is complete
		cid->sawflag = CHECKSUM;
	    } else
		cid->sawflag = DATA_TYPE;
	}
	break;
    case CHECKSUM:
	msg_off = 0;
	if (data_byte == (256 - (cid->cksum & 0xff)))
	    printf("Checksum Passed!!\n");
	else
	    printf("Checksum Failed!!\n");
	return 1;
    case UNKNOWN:
    default:
	printf("\n\tUNKNOWN\n");
	return -1;
	break;
    }

    cid->cksum += data_byte;
    return 0;
}


/**@brief Read samples into the state machine.
 * @param cid Which state machine to act upon
 * @param ubuf containing your samples
 * @param len number of samples contained within the buffer.
 *
 * @details
 * Send received audio to the Caller*ID demodulator.
 * @retval -1 on error
 * @retval 0 for "needs more samples"
 * @retval 1 if the CallerID spill reception is complete.
 */
int callerid_feed(struct callerid_state *cid, unsigned char *ubuf, int len)
{
    int mylen;
    int olen;
    int b = 'X';
    int res;
    int x;
    short *buf;
    short *temp_buf = (short *) ubuf;

    len = len / 2;                              // Because each sample is 2 bytes
    mylen = len;

    buf = malloc(2 * len + cid->oldlen);        // Allocating and copying memory of
    memcpy(buf, cid->oldstuff, cid->oldlen);    // previous samples as well.
    mylen += cid->oldlen / 2;                   // Adjusting new length

    for (x = 0; x < len; x++)                   // Copying current buffer from the 
	buf[x + cid->oldlen / 2] = temp_buf[x]; // previous position.

    while (mylen >= (cid->fskd.ispb * 12)) {    // For demodulating a byte we require
	olen = mylen;                           // 10 or 11 bits. 12 for safer side
	res = fsk_serial(&cid->fskd, buf, &mylen, &b);
	buf += (olen - mylen);
	if (res) {                              // When we get a data byte, we give it 
	    res = decode_CID_msg(cid, b);       // to decoder. When complete CID message
	    if (res == -1)
		return -1;                      // we exit to main. When there is error
	    else if (res)
		return 1;
	}
    }
    if (mylen) {                                // Copying remaining samples to the
	memcpy(cid->oldstuff, buf, mylen * 2);  // cid->oldstuff and adjusting the length
	cid->oldlen = mylen * 2;
    } else
	cid->oldlen = 0;

    return 0;
}

/**@brief Display the Wav file header information
 * @return size of the header to remove the offset from the buffer
 */
#ifdef WAVFILE
static void getHeader(unsigned char wavbuf[])
{
    unsigned int i;
    wav_header *wh = malloc(sizeof(wav_header));
    unsigned char temp_buf[sizeof(wav_header)];

    for (i = 0; i < sizeof(wav_header); i++)
	temp_buf[i] = wavbuf[i];

    memcpy(wh, temp_buf, sizeof(wav_header));

    if (strncmp(wh->chunk_id, "RIFF", 4) || strncmp(wh->format, "WAVE", 4)) {
	fprintf(stderr, "Error: Not a wav file\n");
	exit(EXIT_FAILURE);
    }
    if (wh->audio_format != 1) {
	fprintf(stderr, "Error: Only PCM encoding supported\n");
	exit(EXIT_FAILURE);
    }

    printf(" Wav File Information\n");
    printf("\
	Chunk size      : %d\n\
	fmtchunkSize    : %d\n\
	AudioFormat     : %d\n\
	NumChannels     : %d\n\
	SampleRate      : %d\n\
	ByteRate        : %d\n\
	bps             : %d\n\
	DataChunkSize   : %d\n", wh->chunk_size, wh->fmtchunk_size, wh->audio_format,
 	wh->num_channels, wh->sample_rate, wh->byte_rate, wh->bps, wh->datachunk_size);

    free(wh);
    return;
}
#endif

void signal_handler(int sig);
void sigkill_handler(int sig);
void sigalrm_handler(int sig);

/* Signal Handler to recieve SIGINT. It also starts an alarm to stop
decoding after 4 seconds of the first ring. */

void signal_handler(int sig)
{
    fprintf(stdout, "\nCapturing CID message\n");

#ifdef WAVFILE
    capturing = 0;                      // No Capturing
    buf_ready = 1;                      // Buffer gets ready
#else
    capturing = 1;                      // Capturing starts
    buf_ready = 0;                      // Buffer gets ready after reading from PCM
    alarm(4);                           // Alarm to stop decoding after 4 sec of the 1st ring
    signal(SIGALRM, sigalrm_handler);   // Initializing signal handler for SIGALRM
#endif
    sem_post(&mutex);                   // Release the lock, so that PCM capturing starts
    signal(SIGINT, SIG_DFL);            // making SIGINT resume its default functionality 
}

/* Signal handler to terminate the process */
void sigkill_handler(int sig)
{
    fprintf(stdout, "Program terminated\n");
    kill(getpid(), SIGKILL);            // Terminate the process with SIGKILL
}

/* Signal handler to receive SIGALRM. It also stop capturing any more samples
	and start SIGINT handler for 2nd iteration */

void sigalrm_handler(int sig)
{
    fprintf(stdout, "Got 2nd RING interrupt\n");
    fprintf(stdout, "Waiting for RING interrupt,...(press ctrl-C)\n");
    signal(SIGINT, signal_handler);	// Signal handler for SIGINT for 2nd iteration 
    signal(SIGTSTP, sigkill_handler);	// Signal handler for SIGTSTP 
    capturing = 0;			// Stop capturing from sound card
    sem_wait(&mutex);			// locking the semaphore, so decoding happens
}

/**@brief Thread function to read signal samples from the sound card
 *
 * It is a continuous thread function where it waits for the interrupt to start 
 * capturing signal from the sound card. It enables the buffer_ready and releases the 
 * lock so that main thread can start decoding the CID message.
 *
 * @param ptr void pointer which will be typecasted to the struct 
 * pcm_capture containing parameters of pcm.
 */
void capture_sample(void *ptr)
{
    pcm_capture pcm_cap = *((pcm_capture *) ptr);

    struct pcm_config config;
    unsigned int bytes_read = 0;

    config.channels = pcm_cap.channels;
    config.rate = pcm_cap.rate;
    config.period_size = pcm_cap.period_size;
    config.period_count = pcm_cap.period_count;
    config.format = pcm_cap.format;
    config.start_threshold = 0;
    config.stop_threshold = 0;
    config.silence_threshold = 0;

    while (1) {
	if (capturing) {
	    /* Initialize the parameters of the pcm device to start capturing the samples */
	    pcm = pcm_open(pcm_cap.card, pcm_cap.device, PCM_IN, &config);
	    if (!pcm || !pcm_is_ready(pcm)) {
		fprintf(stderr, "Unable to open PCM device (%s)\n", pcm_get_error(pcm));
		exit(EXIT_FAILURE);
	    }

	    fprintf(stdout, "Capturing sample: %u ch, %u hz, %u bit\n",
		    pcm_cap.channels, pcm_cap.rate,
		    pcm_format_to_bits(pcm_cap.format));

	    /* Enter the loop once capturing starts and read the samples from the sound card.
	       Enables buf_ready and releases the lock */

	    while (capturing) {
		if (buf_ready == 0) {
		    sem_wait(&mutex);
		    if (!pcm_read(pcm, buffer, pcm_cap.size)) {
			bytes_read += pcm_cap.size;
			buf_ready = 1;
		    } else
			break;
		    sem_post(&mutex);
		}
	    }
	    pcm_close(pcm);
	}
    }
}


/**@brief Demodulate Caller ID 
 *
 */
int main(int argc, char *argv[])
{
    int cid_signalling = 0;     // Type of CID signal. It can be either FSK or DTMF
    unsigned int size_of_buf;

    int samp_rate = 44100;      // Default sampling rate
    int baud_rate = 1200;       // Default baud rate
    int bits = 16;              // Default sample size

    int res;

    param *demod_param;         // Storing the audio file parameters used for demosulation
    pcm_capture pcm_cap;        // Parameters for PCM capture
    cid_data *data;

    sigset_t set;
    pthread_t pcm_thr;          // New thread to read message form the sound card

#ifdef WAVFILE

    int fd_codec;               // Get the samples from the codec                                               
    int off = 0;                // Offset pointing to current reading position in the file
    char file_name[30];	        // Sample File name
    struct stat sb;             // struct to store the file stats

    unsigned int samples = 0;
    if (argc < 2) {
	printf("*********************************************************************\n");
	printf("This is the program to decode the CallerID message from an audio file\n");
	printf("Usage: ./cid_fsk 'FN' 'TO'\n");
	printf("FN : File name(path) containing the audio message\n");
	return 0;
    }

    strcpy(file_name, argv[1]);

    if ((fd_codec = open(file_name, O_RDONLY)) == -1) {
	perror("opening device");
	exit(EXIT_FAILURE);
    }

    if (stat(argv[1], &sb) == -1) {     // Reading the file stats
	perror("stat");
	exit(EXIT_FAILURE);
    }
    unsigned char wavbuf[sb.st_size];   // Buffer to store the bytes of wav file.
                                        // Read the data from the wav file to the buffer 
    res = read(fd_codec, wavbuf, sizeof(wavbuf));
    if (res <= 0)
	fprintf(stderr, "\nSamples not Read\n");

    getHeader(wavbuf);                  // Getting the wav file information

    off = sizeof(wav_header);

#endif

    /* parse command line arguments */
    argv += 2;
    while (*argv) {
	if (strcmp(*argv, "-b") == 0) {
	    argv++;
	    if (*argv)
		bits = atoi(*argv);
	} else if (strcmp(*argv, "-s") == 0) {
	    argv++;
	    if (*argv)
		samp_rate = atoi(*argv);
	} else if (strcmp(*argv, "-B") == 0) {
	    argv++;
	    if (*argv)
		baud_rate = atoi(*argv);
	}
	if (*argv)
	    argv++;
    }

    if ((demod_param = malloc(sizeof(*demod_param)))) {
	demod_param->samp_rate = samp_rate;
	demod_param->baud_rate = baud_rate;
	demod_param->ispb = samp_rate / (float) baud_rate;
    }

    pcm_cap.card = 1;
    pcm_cap.device = 0;
    pcm_cap.channels = 2;
    pcm_cap.rate = samp_rate;
    pcm_cap.period_size = 1024;
    pcm_cap.period_count = 4;
    pcm_cap.size = (pcm_cap.channels * pcm_cap.period_size * 
			pcm_cap.period_count * (bits / 8));

    switch (bits) {
    case 32:
	pcm_cap.format = PCM_FORMAT_S32_LE;
	break;
    case 24:
	pcm_cap.format = PCM_FORMAT_S24_LE;
	break;
    case 16:
	pcm_cap.format = PCM_FORMAT_S16_LE;
	break;
    default:
	fprintf(stderr, "%d bits is not supported.\n", bits);
	exit(EXIT_FAILURE);
    }

    size_of_buf = pcm_cap.size / (bits / 8);
    unsigned char buf[size_of_buf];     // Buffer containing audio samples which is passed 
                                        // for decoding the CID message

    buffer = malloc(pcm_cap.size);
    if (!buffer) {
	fprintf(stderr, "Unable to allocate %d bytes\n", pcm_cap.size);
	exit(EXIT_FAILURE);
    }
                                        // Create a callerID state machine
    if ((cs = callerid_new(cid_signalling, demod_param)) == NULL) {
	perror("callerID state machine");
	exit(EXIT_FAILURE);
    }

    if (!(data = malloc(sizeof(cid_data)))) {
	perror("Data malloc fail");
	exit(EXIT_FAILURE);
    }

    sem_init(&mutex, 0, 0);                     // Initialize the semaphore

    sigemptyset(&set);                          // Adding SIGALRM to a signal set
    sigaddset(&set, SIGALRM);                   // Blocking SIGALRM so that the new thread created
    pthread_sigmask(SIG_BLOCK, &set, NULL);     // can't handle the SIGALRM interrupt

    // Create a pthread to read audio samples from the sound card
    if (pthread_create
	(&pcm_thr, NULL, (void *) &capture_sample, (void *) &pcm_cap)) {
	perror("Pthread failed");
	exit(EXIT_FAILURE);
    }

    sigemptyset(&set);                          // Adding SIGALRM to a signal set
    sigaddset(&set, SIGALRM);                   // Unblocking SIGALRM so that only main can handle
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);   // the SIGALRM interrupt

    signal(SIGINT, signal_handler);
    signal(SIGTSTP, sigkill_handler);

    fprintf(stdout, "Waiting for RING interrupt,...(press ctrl-C)\n");

    while (1) {
	if (buf_ready) {
	    sem_wait(&mutex);

	    int i;
#ifdef WAVFILE
	    for (i = 0; i < size_of_buf; i++) {
		buf[i] = wavbuf[off + i];
	    }
	    samples += size_of_buf;
	    off += size_of_buf;

	    if (samples > sb.st_size)
		exit(0);
#else
	    int j;

	    /* In interleaved format with multiple channels, data stored in buffer 
	       is of the format RRLLRRLL (16bits), RRRLLLRRRLLL (24bits) */

	    for (i = 0; i < size_of_buf; i += (bits / 8)) {
		for (j = 0; j < (bits / 8); j++) {
		    buf[i + j] = buffer[i * (bits / 8) + j];
		}
	    }
#endif

	    /* Checking for Caller ID standard and calling functions to decode CID */
	    if (cid_signalling == CID_SIG_V23)
		res = callerid_feed(cs, buf, (size_of_buf));
	    else {
		/* call function to decode DTMF */
	    }

	    if (res < 0) {
		fprintf(stderr, "\nFailed to Decode Caller ID\n");
		callerid_free(cs);      // Free the data structure
		free(data);             // Free the buffer

		                        //Re-initialize the buffer
		if (!(data = malloc(sizeof(cid_data)))) {
		    perror("Data malloc fail");
		    exit(EXIT_FAILURE);
		}
		                        //Re-intialize the CID structure
		if ((cs =
		     callerid_new(cid_signalling, demod_param)) == NULL) {
		    perror("callerID state machine");
		    exit(EXIT_FAILURE);
		}
	    } else if (res) {
		get_CID_info(cs, data); // Display CallerID message
		callerid_free(cs);      // Free the data structure
		free(data);             // Free the buffer

		                        //Re-initialize the buffer
		if (!(data = malloc(sizeof(cid_data)))) {
		    perror("Data malloc fail");
		    exit(EXIT_FAILURE);
		}
		                        //Re-intialize the CID structure
		if ((cs =
		     callerid_new(cid_signalling, demod_param)) == NULL) {
		    perror("callerID state machine");
		    exit(EXIT_FAILURE);
		}
	    }

	    fprintf(stderr, "Read %lu bytes\n", sizeof(buf));

#ifdef WAVFILE
#else
	    buf_ready = 0;
#endif
	    sem_post(&mutex);
	}
    }

    sem_destroy(&mutex);

    return 0;
}
