/**@file fskmodem.h
 *	
 * @author Jenil Jain
 * @copyright Copyright 2015 Insensi Inc. All rights reserved.
 * This project is released under the GNU Public License.
 *
 * @brief FSK Demodulation
 *
 * @note Includes code and algorithms from the Zapata library and Aesterisk.
 */

#ifndef FSKMODEM_H
#define FSKMODEM_H

#define NZEROS_POLES    6       /**< No. of zeros depends on order of the filter
                                For a 3rd order Bandpass filter, zeros and poles
                                are 6. But for a 3rd order Low-Pass or High-Pass
                                filter it is 3. So to keep the number of poles &
                                zeros constant we are using 3rd order Bandpass
                                and 6th order Low-pass for demodulation */

/// new filter structure
struct filter_struct {

        int 	c_coef[NZEROS_POLES + 1];       ///< array to store filter 'c' coefficients
        double 	d_coef[NZEROS_POLES + 1];       ///< array to store filter 'd' coefficients
        double 	xv[NZEROS_POLES + 1];           ///< array to store previous input values
        double 	yv[NZEROS_POLES + 1];           ///< array to store previous output values
        double 	gain;                           ///< Gain of the filter
};

typedef struct {
	int nbit;                               ///< Number of Data Bits (5,7,8)
	int parity;                             ///< Parity 0=none 1=even 2=odd 
	int instop;                             ///< Number of Stop Bits  
	int ispb;                               ///< Sample per FSK bit (Sample_rate/baud_rate)
	int fsk_std;                            ///< Type of FSK standard
	
	int xi0;                                ///< current demodulated value
	int xi1;                                ///< previous demodulated value
	int xi2;                                ///< previous to previous demodulated value
	
	int state;                              ///< Demodulation state

	int pllispb;                            ///< Pll autosense 
	int pllids;                             ///< PLL adjustment
	int pllispb2;                           ///< Center of the PLL
	int icont;                              ///< Count for DPLL
	int pll_round_off;                      /**< SPB is in float, so for proper PLL, round
                                                of is necessary */

	struct filter_struct mark_filter;       ///< Structure to store mark filter data
	struct filter_struct space_filter;      ///< Structure to store space filter data
	struct filter_struct demod_filter;      ///< Structure to store demodulator filter data

} fsk_data;

/**@brief Retrieve a serial byte into outbyte.
 */
int fsk_serial(fsk_data *fskd, short *buffer, int *len, int *outbyte);


/**@brief Initialize the FSK data 
 */
int fskmodem_init(fsk_data *fskd);

#endif 
