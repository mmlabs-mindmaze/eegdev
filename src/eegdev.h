/*
 * 	Proposal for an common EEG device API
 */

#ifndef EEGDEV_H
#define EEGDEV_H

#include <stdint.h>

#ifdef __cpluplus
extern "C" {
#endif // __cplusplus

/*************************************************************************
 *                          API definitions                              *
 ************************************************************************/

typedef	struct eegdev*	heegdev;

/*! \name Error codes
 *  \{ */
#define EEG_OK				0
#define EEG_DRIVER_FAIL			1
#define	EEG_OPEN_FAIL			3
#define EEG_CONNECTION_LOST		4
#define EEG_BUFFER_FULL			5
#define	EEG_INVALID_OPTION		6
#define	EEG_INVALID_OPERATION		7
#define	EEG_KICKED_BY_CHUCK_NORRIS	8
/*! \} */

/*! \name Capabilities request codes
 *  \{ */
#define REQ_SAMPLERATE	0
#define REQ_MAX_EEG	1
#define REQ_MAX_SENS	2
#define REQ_MAX_TRIG	3
/*! \} */

/**************************************************************************
 *                            API Functions                               * 
 *                                                                        *
 *  IMPORTANT NOTE: There is no open function. This is normal. The open   *
 * function is the only system-specific function seen by the user. Its    *
 * only requirement is returning a heegdev handle. The arguments of the   *
 * function can be anything necessary                                     *
 *************************************************************************/

/*!
 * \param	dev	 the EEG device
 * \return 	the error code
 *
 * This function returns the last error that occured using the system. If
 * no error has already occured, it should return \c EEG_OK .
 */
int eeg_get_last_error(heegdev dev);

/*!
 * \param	dev	the EEG device
 * \param	cap	the requested capability 
 * \return the value of the requested capability, \c -1 in case of error
 *
 * Use this function to test the capability of the EEG system. Set \c cap
 * to one of the capabilities request codes. If \cap is:
 *
 *  - \c REQ_SAMPLERATE : returns the current sampling rate of the system in Hz
 *
 *  - \c REQ_MAX_EEG : returns the maximum number of EEG channels that the
 *  EEG system can acquired.
 *
 *  - \c REQ_MAX_EEG : returns the maximum number of analog sensor channels
 *  that the EEG system can acquired.
 *
 *  - \c REQ_MAX_TRIG : returns the number of trigger line sampled by the
 *  EEG system. 
 *
 *  \sa eeg_set_channels()
 */
int eeg_get_cap(heegdev dev, int cap);

/*!
 * \param	dev	the EEG device
 * \return \c 0 if the system has been properly closed, \c -1 in case of error
 *
 * Closes the connection to the EEG device. It frees all the internal ressources.
 * Thus, no subsequent reference to \c dev should be used.
 */
int eeg_close(heegdev dev);

/*!
 * \param	dev	the EEG device
 * \param	neeg	the number of EEG channels that will be requested
 * \param	eegind	a \c neeg sized array of indices of the requested EEG channels
 * \param	nsens	the number of analog sensors channels that will be requested
 * \param	sensind	a \c nsens sized array of indices of the requested analog sensor channels
 * \return \c 0 in case of success, \c -1 otherwise.
 *
 * Sets the number of EEG and analog sensors channels that will be requested by
 * subsequent calls to eeg_get_dataf() or eeg_get_datad(). 
 *
 * Indices for sensor and EEG channels cannot be higher than the respective number
 * of channels reported by eeg_get_cap().
 *
 * TODO: Decide whether this function can be called between eeg_start() and eeg_stop()
 *
 * \sa eeg_get_cap(), eeg_get_dataf(), eeg_get_datad()
 */
int eeg_set_channels(heegdev dev, unsigned int neeg, unsigned int* eegind,
				  unsigned int nsens, unsigned int* sensind);

/*!
 * \param	dev	the EEG device
 * \return \c 0 in case of success, \c -1 otherwise.
 *
 * Sets the filter to be applied to the channels
 *
 * TODO: Decide how to specify the filters. Decide whether this function can
 * be called between eeg_start() and eeg_stop()
 */
int eeg_set_filter(heegdev dev /* ARGUMENTS TO BE DETERMINED */);

/*!
 * \param	dev	the EEG device
 * \return \c 0 in case of success, \c -1 otherwise.
 *
 * Calibrate the EEG system. This function cannot be called between eeg_start()
 * and eeg_stop().
 *
 * TODO: Decide the specification of the function
 */
int eeg_calibrate(heegdev dev /* ARGUMENTS TO BE DETERMINED */);

/*!
 * \param	dev	the EEG device
 * \return \c 0 in case of success, \c -1 otherwise.
 *
 * Get the scaling parameters of the channels of the EEG system.
 *
 * TODO: Decide the specification of the function
 */
int eeg_get_scale(heegdev dev /* ARGUMENTS TO BE DETERMINED */);

/*!
 * \param	dev	the EEG device
 * \param	immediate 	flag for immediate start
 * \param	trigval		the value of trigger
 * \return \c 0 if acquisition has started, \c -1 otherwise.
 *
 * Starts the acquisition. Once the acquisition has started, it fills an internal
 * buffer ensuring that no sample will be lost. It is the responsability of the user
 * to call eeg_get_dataf() or eeg_get_datad() often enough to avoid the internal buffer
 * to be full.
 *
 * If \c trig is negative value, the acquisition starts immediately. If it is a
 * positive value, the acquisition will starts as soon as \c trigval is found
 * on the trigger lines.
 *
 * This function is blocking until the acquisition has started or an error occured.
 *
 * \sa eeg_stop(), eeg_get_dataf(), eeg_get_datad()
 */
int eeg_start(heegdev dev, int immediate, uint32_t trigval);

/*!
 * \param	dev	the EEG device
 * \param	immediate 	flag for immediate stop
 * \param	trigval		the value of trigger
 * \return \c 0 if acquisition has stopped, \c -1 otherwise.
 *
 * Stops the acquisition. Remaining samples in the internal buffer can be retrieved
 * by call to eeg_get_dataf() or eeg_get_datad().
 *
 * If \c trig is negative value, the acquisition stops immediately. If it is a
 * positive value, the acquisition will stops as soon as \c trigval is found
 * on the trigger lines.
 *
 * This function is blocking until the acquisition has stopped or an error occured.
 *
 * \sa eeg_start(), eeg_get_dataf(), eeg_get_datad()
 */
int eeg_stop(heegdev dev, int immediate, uint32_t trigval);

/*!
 * \param	dev		the EEG device
 * \param	ns 		number of requested samples
 * \param	eegbuff		pointer to a buffer used to hold the EEG data
 * \param	sensbuff	pointer to a buffer used to hold the sensor data
 * \param	trig		pointer to a buffer used to hold the triggers data
 * \return \c 0 if samples have been retrieved, \c -1 otherwise.
 *
 * Get the next \c ns buffered samples. Fill the pointed buffers with \c ns samples.
 * Each buffer should of course be preallocated with the correct size unless it is a
 * null pointer. The size of each buffer depends on the number of requested channels
 * specified by eeg_set_channels(). The EEG buffer should consist of \c ns by \c neeg
 * \c float, the sensor buffer of \c ns by \c nsens \c float and the trigger buffer
 * of \c ns \c uint32_t.
 *
 * The EEG and sensor buffers are filled with the following pattern (Si refers to the
 * i-th sample and Cj to the j-th channel) :
 *
 * | S1C1 | S1C2 | ... | S1Ck | S2C1 | S2C2 | .... | S2Ck | ... | SnCk |
 *
 * This function is blocking until all the requested samples are transfered to the
 * buffers or a error occurs.
 *
 * \sa eeg_stop(), eeg_get_datad()
 */
int eeg_get_dataf(heegdev dev, unsigned int ns,
			float* eegbuff, float* sensbuff, uint32_t* trig);

/*!
 * \param	dev		the EEG device
 * \param	ns 		number of requested samples
 * \param	eegbuff		pointer to a buffer used to hold the EEG data
 * \param	sensbuff	pointer to a buffer used to hold the sensor data
 * \param	trig		pointer to a buffer used to hold the triggers data
 * \return \c 0 if samples have been retrieved, \c -1 otherwise.
 *
 * Get the next \c ns buffered samples. Fill the pointed buffers with \c ns samples.
 * Each buffer should of course be preallocated with the correct size unless it is a
 * null pointer. The size of each buffer depends on the number of requested channels
 * specified by eeg_set_channels(). The EEG buffer should consist of \c ns by \c neeg
 * \c double, the sensor buffer of \c ns by \c nsens \c double and the trigger buffer
 * of \c ns \c uint32_t.
 *
 * The EEG and sensor buffers are filled with the following pattern (Si refers to the
 * i-th sample and Cj to the j-th channel) :
 *
 * | S1C1 | S1C2 | ... | S1Ck | S2C1 | S2C2 | .... | S2Ck | ... | SnCk |
 *
 * This function is blocking until all the requested samples are transfered to the
 * buffers or a error occurs.
 *
 * \sa eeg_stop(), eeg_get_dataf()
 */
int eeg_get_datad(heegdev dev, unsigned int ns,
			double* eegbuff, double* sensbuff, uint32_t* trig);



#ifdef __cpluplus
}
#endif // __cplusplus

#endif //EEGDEV_H
