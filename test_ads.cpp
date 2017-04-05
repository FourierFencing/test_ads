
//Code mostly borrowed from Bernd and Other teams of bernd. 
//Modified by Daniel Bee for the Wireless Fencing Project

//Added: 
//	16-bit words   										-- Untested
//	commented out Register access code					-- N/A (NOT NEEDED WITH ADS7887)
//
//Still to do: 
//	SPI PIN SELECTION (L36)
//	Look into SPI_IOC_MESSAGE
//	Ask SHEHU about L115
//	RING BUFFER STUFF
// 	Everything after IIR filter


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
#include "adcreader.h"

static void pabort(const char *s)
{
	perror(s);
	abort();
}

static const char *device = "/dev/spidev0.0";
static uint8_t mode = SPI_CPHA | SPI_CPOL;;
static uint8_t bits = 16; //16 bits in a transfer
static int drdy_GPIO = 22; //SPI Pin GPIO

static void writeReset(int fd)
{
  int ret;
  uint16_t tx1[5] = {0xffff,0xffff,0xffff,0xffff,0xffff}; //16 bit words
  uint16_t rx1[5] = {0};
  struct spi_ioc_transfer tr;

  memset(&tr,0,sizeof(struct spi_ioc_transfer)); //init to 0? 
  tr.tx_buf = (unsigned long)tx1;
  tr.rx_buf = (unsigned long)rx1;				//unused
  tr.len = sizeof(tx1);

  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1) {
    printf("\nerr=%d when trying to reset. \n",ret);
    pabort("Can't send spi message");
  }
}


static void writeReg(int fd, uint16_t v) //not needed due to lack of registers on ads7883
{
  int ret;
  uint16_t tx1[1];
  tx1[0] = v;
  uint16_t rx1[1] = {0};
  struct spi_ioc_transfer tr;

  memset(&tr,0,sizeof(struct spi_ioc_transfer));
  tr.tx_buf = (unsigned long)tx1;
  tr.rx_buf = (unsigned long)rx1;
  tr.len = sizeof(tx1);

  ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
  if (ret < 1)
    pabort("can't send spi message");
}

static uint8_t readReg(int fd) //also not needed 
{
	int ret;
	uint8_t tx1[1];
	tx1[0] = 0;
	uint16_t rx1[1] = {0};
	struct spi_ioc_transfer tr;

	memset(&tr,0,sizeof(struct spi_ioc_transfer));
	tr.tx_buf = (unsigned long)tx1;
	tr.rx_buf = (unsigned long)rx1;
	tr.len = sizeof(tx1);

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
	  pabort("can't send spi message");
	  
	return rx1[0];
}

static int readData(int fd)
{
	int ret;
	uint16_t tx1[2] = {0,0};
	uint16_t rx1[2] = {0,0};
	struct spi_ioc_transfer tr;

	memset(&tr,0,sizeof(struct spi_ioc_transfer));
	tr.tx_buf = (unsigned long)tx1; //why is this long? (32 bits)-- probs to do with spi_ioc_transfer spec
	tr.rx_buf = (unsigned long)rx1;
	tr.len = sizeof(tx1); 				//this is never used directly and is for debugging/feedback

	ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
	if (ret < 1)
          {
	  printf("\n can't send spi message, ret = %d\n",ret);
          exit(1);
          }
	  
	return (rx1[0]<<16)|(rx1[1]); //ASK SHEHU
}			


void ADCreader::ADCsetup()  //or ADCreader
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

	
	fprintf(stderr, "spi mode: %d\n", mode);
	fprintf(stderr, "bits per word: %d\n", bits);
	//do we want max speed?

	// enable master clock for the AD
	// divisor results in roughly 4.9MHz
	// this also inits the general purpose IO
	
	/////////////////////////////////////////////////////////////////////////
	gz_clock_ena(GZ_CLK_5MHz,5);   //MUST CHANGE THIS

	/////////////////////////////////////////////////////////////////////////
	
	
	// enables sysfs entry for the GPIO pin
	gpio_export(drdy_GPIO);
	// set to input
	gpio_set_dir(drdy_GPIO,0);
	// set interrupt detection to falling edge
	gpio_set_edge(drdy_GPIO,"falling");
	// get a file descriptor for the GPIO pin
	sysfs_fd = gpio_fd_open(drdy_GPIO);

	// resets the AD7705 so that it expects a write to the communication register
        printf("sending reset\n");
	writeReset(fd);
	
/* 	REGISTER WRITES AND READS ARE NOT NEEDED FOR THE ADS7887

	// tell the AD7705 that the next write will be to the clock register
	writeReg(fd,0x20);
	// write 00001100 : CLOCKDIV=1,CLK=1,expects 4.9152MHz input clock
	writeReg(fd,0x0C);

	// tell the AD7705 that the next write will be the setup register
	writeReg(fd,0x10);
	// intiates a self calibration and then after that starts converting
	writeReg(fd,0x40);
	
*/


	// we read data in an endless loop and display it
	// this needs to run in a thread ideally
	
}

void ADCreader::run()
{
	bool running = true;
	
	fprintf(stderr,"We are running! \n");
	
	while(running) {
	  // let's wait for data for max one second
	  ret = gpio_poll(sysfs_fd,1000);
	  if (ret<1) {
	    fprintf(stderr,"Poll error %d\n",ret);
	  }

	  //NOPE	// tell the AD7705 to read the data register (16 bits)
	  //NOPE	writeReg(fd,0x38);
	  
	  // read the data register by performing a single 16 bit read. DOES THIS HAVE TO BE 8 BITS??? (as in bernd and others code?)
	  int value = readData(fd)-0x8000;
	  
	  //insert data into ring buffer here?
	  
	  
	}
	  
	close(fd);
	gpio_fd_close(sysfs_fd); //what does this mean?
}

//the char should point to the filter class object that has already been set-up. 
//in this way, we can have multiple filter threads running using different filter classes. 
/*EG, In main we have: 
	Iir::Butterworth::LowPass<order> fW1, fW2, fL1, fL2;
	const float samplingrate = 1000; // Hz
	const float cutoff_frequency_W1 = 5; // Hz
	const float cutoff_frequency_W2 = 8; // Hz
	const float cutoff_frequency_L1 = 13; // Hz
	const float cutoff_frequency_L2 = 15; // Hz
	fW1.setup (order, samplingrate, cutoff_frequency_W1);
	fW2.setup (order, samplingrate, cutoff_frequency_W2);
	fL1.setup (order, samplingrate, cutoff_frequency_L1);
	fL2.setup (order, samplingrate, cutoff_frequency_L2);
	fW1.reset();
	fW2.reset();
	fL1.reset();
	fL2.reset();
	
	while(1){
	i = GET_SAMPLE();
	outW1 = ADCreader::filter(i, &fW1); //gives a single frequency output of fW1 if it exists
	outW2 = ADCreader::filter(i, &fW2); //gives a single frequency output of fW2 if it exists
	outL1 = ADCreader::filter(i, &fL1); //gives a single frequency output of fL1 if it exists
	outL2 = ADCreader::filter(i, &fL2); //gives a single frequency output of fL2 if it exists
	
	//Then thresholding & truthtable
	}
*/
int ADCreader::filter(int sample_in, char** f)  
{
	float b = f.filter(sample_in);   //let's hope that the state of each filter can be remembered seperately. 
									//what retains it's values after exciting this function? 
									//For simplicity, i could just put this line in main. 
									//still need to work out buffer stuff
	
}
