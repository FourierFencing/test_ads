#include "adcreader.h"
#include <QDebug>

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/spi/spidev.h>

#include "gz_clk.h"
#include "gpio-sysfs.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static void pabort(const char *s)
{
	perror(s);
	abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode = SPI_CPHA | SPI_CPOL;;
static uint8_t bits = 8;
static uint32_t speed = 50000;
static uint16_t delay = 10;
static int drdy_GPIO = 22;

static void writeReset(int fd)
{
  int ret;
  uint8_t tx1[5] = {0xff,0xff,0xff,0xff,0xff};
  uint8_t rx1[5] = {0};
  struct spi_ioc_transfer tr;
  tr.tx_buf = (unsigned long)tx1;
  tr.rx_buf = (unsigned long)rx1;
  tr.len = ARRAY_SIZE(tx1);
  tr.delay_usecs = delay;
  tr.speed_hz = speed;
  tr.bits_per_word = bits;
  
  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");
}

static void writeReg(int fd, uint8_t v)
{
  int ret;
  uint8_t tx1[1];
  tx1[0] = v;
  uint8_t rx1[1] = {0};
  struct spi_ioc_transfer tr;
  tr.tx_buf = (unsigned long)tx1;
  tr.rx_buf = (unsigned long)rx1;
  tr.len = ARRAY_SIZE(tx1);
  tr.delay_usecs = delay;
  tr.speed_hz = speed;
  tr.bits_per_word = bits;

  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");
}

static uint8_t readReg(int fd)
{
	int ret;
	uint8_t tx1[1];
	tx1[0] = 0;
	uint8_t rx1[1] = {0};
	struct spi_ioc_transfer tr;
	tr.tx_buf = (unsigned long)tx1;
	tr.rx_buf = (unsigned long)rx1;
	tr.len = ARRAY_SIZE(tx1);
	tr.delay_usecs = delay;
	tr.speed_hz = speed;
	tr.bits_per_word = 8;

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
	  pabort("can't send spi message");
	  
	return rx1[0];
}

static int readData(int fd)
{
	int ret;
	uint8_t tx1[2] = {0,0};
	uint8_t rx1[2] = {0,0};
	struct spi_ioc_transfer tr;
	tr.tx_buf = (unsigned long)tx1;
	tr.rx_buf = (unsigned long)rx1;
	tr.len = ARRAY_SIZE(tx1);
	tr.delay_usecs = delay;
	tr.speed_hz = speed;
	tr.bits_per_word = 8;

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
	  pabort("can't send spi message");
	  
	return (rx1[0]<<8)|(rx1[1]);
}

// Sample rates as defined in the specification.
const uint32_t SAMPLE_RATES[] =
{20, 25, 100, 200, 50, 60, 250, 500};
const uint8_t SAMPLE_RATES_MASK = 0x07;

const uint8_t BITS_FS_SIZE = 2;
const uint8_t BITS_FS_OFF = 0;
const uint8_t BITS_FS_MAX = 1 << BITS_FS_SIZE;
const uint8_t BITS_FS_MASK = (BITS_FS_MAX - 1) << BITS_FS_OFF;

const uint8_t BITS_SETUP_G_SIZE = 3;
const uint8_t BITS_SETUP_G_OFF = 3;
const uint8_t BITS_SETUP_G_MAX = 1 << BITS_SETUP_G_SIZE;
const uint8_t BITS_SETUP_G_MASK = (BITS_SETUP_G_MAX - 1) << BITS_SETUP_G_OFF;

void ADCreader::setFilter(uint8_t filter, uint8_t channel){
	// Validate input.
	if( channel >= CHANNEL_N ) return;
	else if( filter >= BITS_FS_MAX ) return;

	channels[channel].reg_clock = (channels[channel].reg_clock & ~BITS_FS_MASK) | (filter << BITS_FS_OFF);
	channels[channel].init = false;
}

void ADCreader::setGain(uint8_t gain, uint8_t channel){
	// Validate input.
	if( channel >= CHANNEL_N ) return;
	else if( gain >= BITS_SETUP_G_MAX ) return;

	channels[channel].reg_setup = (channels[channel].reg_setup & ~BITS_SETUP_G_MASK) | (gain << BITS_SETUP_G_OFF);
	channels[channel].init = false;
}

void ADCreader::samplingEnable(bool enable, uint8_t channel){
	if( enable ){
		channels[channel].use = true;
		channels[channel].init = false;
		channels[channel].reg_setup = 0x40; // Self calibrate. 
	}else{
		channels[channel].use = false;
	}
}

uint32_t ADCreader::getSampleRate(uint8_t channel){
	return SAMPLE_RATES[channels[channel].reg_clock & SAMPLE_RATES_MASK];
}

void ADCreader::run()
{
	int ret = 0;
	int fd;
	int sysfs_fd;

	//int no_tty = !isatty( fileno(stdout) );

	fd = open(device, O_RDWR);
	if (fd < 0)
		pabort("can't open device");

	/*
	 * spi mode
	 */
	ret = ioctl(fd, SPI_IOC_WR_MODE, &mode);
	if (ret == -1)
		pabort("can't set spi mode");

	ret = ioctl(fd, SPI_IOC_RD_MODE, &mode);
	if (ret == -1)
		pabort("can't get spi mode");

	/*
	 * bits per word
	 */
	ret = ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't set bits per word");

	ret = ioctl(fd, SPI_IOC_RD_BITS_PER_WORD, &bits);
	if (ret == -1)
		pabort("can't get bits per word");

	/*
	 * max speed hz
	 */
	ret = ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);
	if (ret == -1)
		pabort("can't set max speed hz");

	ret = ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ, &speed);
	if (ret == -1)	channels[1].use = true;
	channels[1].reg_clock = 0x1C; // write 00011100 : CLKDIS=1,CLOCKDIV=1,CLK=1,expects 4.9152MHz input clock
	channels[1].reg_setup = 0x40; // 

	fprintf(stderr, "spi mode: %d\n", mode);
	fprintf(stderr, "bits per word: %d\n", bits);
	fprintf(stderr, "max speed: %d Hz (%d KHz)\n", speed, speed/1000);

	// enable master clock for the AD
	// divisor results in roughly 4.9MHz
	// this also inits the general purpose IO
	gz_clock_ena(GZ_CLK_5MHz,5);

	// enables sysfs entry for the GPIO pin
	gpio_export(drdy_GPIO);
	// set to input
	gpio_set_dir(drdy_GPIO,0);
	// set interrupt detection to falling edge
	gpio_set_edge(drdy_GPIO,"falling");
	// get a file descriptor for the GPIO pin
	sysfs_fd = gpio_fd_open(drdy_GPIO);

	// Hard coded for now. Should have methods.
	channels[0].use = true;
	channels[0].reg_clock = 0x1C; // write 00011100 : CLKDIS=1,CLOCKDIV=1,CLK=1,expects 4.9152MHz input clock
	channels[0].reg_setup = 0x40; // write 00001000 : self calibrate
	channels[1].use = true;
	channels[1].reg_clock = 0x1C; // write 00011100 : CLKDIS=1,CLOCKDIV=1,CLK=1,expects 4.9152MHz input clock
	channels[1].reg_setup = 0x40; // write 00001000 : self calibrate

	// Main loop that handles the ADC. Blocks on event driven pin.
	running = true;

	bool sampling = false;
	while (running) {
		// If device is not in known state, reset it and rebuild state.
		if( !init ){
			// resets the AD7705 so that it expects a write to the communication register
			writeReset(fd);
			for( int i = 0 ; i < CHANNEL_N ; i+= 1 ){
				channels[i].init = false;
			}
			init = true;
		}

		// Process channel, initalize if not done so already.
		if( channels[channel].use ){
			if( channels[channel].init ){
				// Tell ADC to perform a read operation (will DRDY Block until sample is ready).
				writeReg(fd,0x38 | channel);
				sampling = true;
			}else{
				// Initialize clock register.
				writeReg(fd,0x20 | channel);
				writeReg(fd,channels[channel].reg_clock);

				// Initialize setup register (will DRDY Block while calibrating).
				writeReg(fd,0x10 | channel);
				writeReg(fd,channels[channel].reg_setup);
				channels[channel].init = true;
			}
		}

		// Perform DRDY Block.
	    if( (ret = gpio_poll(sysfs_fd,2000)) < 1 ) {
			fprintf(stderr,"Poll timeout, possibly the device crashed or is not connected. %d\n",ret);
			init = false;
			sampling = false;
			continue;
	    }

		// If we are in the process of sampling we need to read in the data.
		if( sampling ){
			// Get value in normalized form.
			int16_t value = (int16_t) (readData(fd)-0x8000);

			// Store value in buffer.
			channels[channel].crb[channels[channel].crb_write] = value; 
			channels[channel].crb_write = (channels[channel].crb_write + 1) % CRB_LENGTH; // This should optimize to & by compiler?
			if( channels[channel].crb_write == channels[channel].crb_read ){
				// Possible buffer overflow?
				fprintf(stderr,"possible crb buffer overflow detected\r\n");
			}

			sampling = false;
		}

		// Determine next channel to process.
		uint8_t cchan = channel;
		do{
			channel = (channel + 1) % CHANNEL_N;
		}while( (cchan != channel) && !channels[channel].use );
	}

	close(fd);
	gpio_fd_close(sysfs_fd);
}

ADCreader::index_t ADCreader::appendResults(double* buffer, uint32_t size, uint8_t channel){
	// Validate input.
	if( channel >= CHANNEL_N ) return 0;

	// Determine how many elements to copy.
	ADCreader::index_t i = channels[channel].crb_write - channels[channel].crb_read;
	if( i == 0 ) return 0; // No elements to copy.
	else if( i > CRB_LENGTH ) i+= CRB_LENGTH; // Underflow correction.

	//fprintf(stderr,"%d elements in buffer\r\n", i);

	// Insert data.
	memmove( buffer, buffer + i, (size - i) * sizeof(double) );
	for( ADCreader::index_t index = i ; index > 0 ; index-= 1 ){
		buffer[size - index] = (double)channels[channel].crb[channels[channel].crb_read];
		channels[channel].crb_read = (channels[channel].crb_read + 1) % CRB_LENGTH;
	}

	return i;
}

void ADCreader::quit()
{
	running = false;
	//exit(0);
}
