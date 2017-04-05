#ifndef ADCREADER
#define ADCREADER

#include <QThread>
#include <stdint.h>

class ADCreader : public QThread
{

public:
	// The type used to hold ADC conversion results.
	typedef int_fast16_t data_t;

	// The type used to hold buffer indicies, must be unsigned.
	typedef uint_fast8_t index_t;

	ADCreader() {
		running = false;
		init = false;

		for( int i = 0 ; i < CHANNEL_N ; i+= 1 ){
			channels[i].use = false;
			channels[i].init = false;

			channels[i].crb_read = 0;
			channels[i].crb_write = 0;
		}

		channel = 0;
	};
	// Changes the filter selection for a channel. This controls the output sample rate.
	void setFilter(uint8_t filter, uint8_t channel);
	/* Get the current sample rate for the channel as defined by the specification.
	 * Actual sample rate in multi-channel mode is considerably slower.
	 * Channel changes incure a 3 sample penalty.
	 * Round robin sampling is used so the period between samples is 3 times the sum of periods of individual channels.
	 */
	void setGain(uint8_t gain, uint8_t channel);
	// Turn a channel on or off.
	void samplingEnable(bool enable, uint8_t channel);
	uint32_t getSampleRate(uint8_t channel);
	void quit();
	void run();
	index_t appendResults(double* buffer, uint32_t size, uint8_t channel);
protected:
	// Constants which define the circular buffer size (in elements).
	static const uint8_t CHANNEL_N = 6;
	static const uint8_t CRB_LENGTH_POW2 = 4;
	static const index_t CRB_LENGTH = 1 << CRB_LENGTH_POW2;
private:
	bool running;
	bool init;

	struct ADCChannel {
		bool use;
		bool init;

		uint8_t reg_clock;
		uint8_t reg_setup;

		data_t crb[CRB_LENGTH];
		index_t crb_read;
		index_t crb_write;
	} channels[CHANNEL_N];

	uint8_t channel;
};

#endif
