/**
/*
 * File:        finalProject.c
 * Author:      Sebastian Roubert, Jonah Okike-Hepzibah, Rahul Desai
 * Target PIC:  PIC32MX250F128B
 */

////////////////////////////////////

#include "config_1_2_2a.h"
// threading library
#include "pt_cornell_1_2_2a.h"
#include "tft_master.h"
#include "tft_gfx.h"
#include <stdio.h>
#include <math.h>

#define	SYS_FREQ 40000000


#define BAUDRATE 9600 // must match PC end
#define PB_DIVISOR (1 << OSCCONbits.PBDIV) // read the peripheral bus divider, FPBDIV

#define NOP asm("nop");
// 1/2 microsec
#define wait20 NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;NOP;
// one microsec
#define wait40 wait20;wait20;

//Set the threshold ADC cutoff value for current switching
#define cutoffADC 150 

#define yieldTime 20

volatile SpiChannel spiChn = SPI_CHANNEL2 ;	// the SPI channel to use
// for 60 MHz PB clock use divide-by-3

volatile int spiClkDiv = 2 ; // 20 MHz DAC clock


/*********************************************************************
The following code was developed by us for final project.

Our project consisted of three software components: 1) the microncontroller
data acquisition/processing along with bluetooth communication,
2) the Python script data packing into JSON and hosting a local web server,
and 3) the Javascript web application visualizing data in real-time.

Although all three components were critical to the system, the microcontroller
component was by far the most challenging and involved. It evolved over
time into something we're very proud of.

There are three major protothreads. We have 14 sensors and with keeping the
scalability of the project in mind, we just have more sensors than ADC
pins. As such, we use the first protothread to MUX through all the sensors
and read them using the CTMU. The second thread converts the ADC values
to resistance. The third and final thread communicates over bluetooth
to the Python server.
********************************************************************/


// === thread structures ============================================
// thread control structs

volatile int milliSec ;
static struct pt pt_DMA_output, pt_input, pt1 ;

//#define max_chars 32 // for input buffer
char send_buffer[max_chars];

static struct pt pt_tftDisplay, pt_muxLoop, pt_calculationThread;

// string buffer
char buffer[60];

// === Timer 2 interrupt handler =====================================
// ipl2 means "interrupt priority level 2"
// ASM output is 47 instructions for the ISR
void __ISR(_TIMER_2_VECTOR, ipl2) Timer2Handler(void)
{
    // clear the interrupt flag
    mT2ClearIntFlag();
    // keep time
    milliSec++ ;
}


int sign(int x){
    return (x>0) - (x<0);
}

// === Reading Thread ======================================================
// Reading sensors resistance

#define Vdd 3.3
#define ADC_max 1023.0
      volatile int raw_adc1,raw_adc2,raw_adc3, raw_adc4,raw_adc5,raw_adc6,raw_adc7,raw_adc8; //for ADC values read
      volatile int R1, R2, R3, R4 ,R5 , R6 ,R7 ,R8;
      static float I[4]={550e-6, 0.55e-6, 5.5e-6, 55e-6} ; // current settings in amps
      static int adc_zero[4]={113, 0, 0, 20}; // ADC reading with zero ohms at each current
      static float I_set;
      static float currents[4] = { 0.55e-6, 5.5e-6, 55e-6, 550e-6 } ;
      static int currInd[3] = {2,3,0 } ; //making use of 3 currents for the required resistance ranges
      //initializing to zero
      static int resistorsCurrent[15] ={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
      static int rawAdc[15] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
      static int resVal[15] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
      
static PT_THREAD (protothread_muxLoop(struct pt *pt))
{
    
    PT_BEGIN(pt);


      
      
    while(1) {
        static int i = 0;
        static int j = 0;
        static int l = 0 ;
        static unsigned short temp = 0 ;
        PT_YIELD_TIME_msec(yieldTime);
        
    
    for (j = 0; j<2;j++){  
        
        CTMUCONbits.ON = 1; // Turn on CTMU
        CTMUCONbits.IDISSEN = 1; // start drain of circuit
        CTMUCONbits.IDISSEN = 0; // End drain of circuit
        I_set = I[0];
        CTMUCONbits.IRNG = 0 ; //0x03; // maximum current
        
	//Switching ADC channels based on the flag
        if (j==0) {
            SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN5 );
        } else {
            SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN0 ); 
        }
        
        for (i=0; i<7; i++){
            
            mPORTBClearBits(BIT_9 | BIT_7 | BIT_8); //Clearing MUX control signal bits
            temp = i ;
            mPORTBSetBits(temp<<7) ;
            
            for (l=0; l<3; l++) {
                
                PT_YIELD_TIME_msec(40);
                CTMUCONbits.IDISSEN = 1; // start drain of circuit
                CTMUCONbits.IDISSEN = 0; // End drain of circuit
 
                CTMUCONbits.IRNG = currInd[l] ; //current selection
	        CTMUCONbits.EDG1STAT = 1;
                AcquireADC10(); // start ADC sampling
            
                while (!AD1CON1bits.DONE){}; // Wait for ADC conversion
                
                rawAdc[7*j+i] = ReadADC10(0) ; //get the adc value
                
                if (rawAdc[7*j+i]> cutoffADC) {
                    resistorsCurrent[7*j+i]=currInd[l];  //choose the correspondong current if adc value is greater than threshold 
                    break;
                }
            }
        } 
    }    
        
        
    }
    PT_END(pt) ;
    
}


// === Calculating Resistance Thread ======================================================
// the equations associated with this calculation were developed by us through experimentation
//and profiling of the CTMU at different currents while MUXing through different resistors.
static char cmd[16]; 
static int value;

static PT_THREAD (protothread_calculationThread(struct pt *pt)) 
{
    static int ind = 0 ;
    
    PT_BEGIN(pt) ;
    
    while(1) {
        
        PT_YIELD_TIME_msec(yieldTime) ;
 //Resistance calculations for different currents
        for (ind = 0; ind < 14; ind++) {
            
            if (resistorsCurrent[ind] == 0) {
            // Current is 550 microamps
            
                resVal[ind] = (int)( 0.0025 * (float) pow(rawAdc[ind],2) +3.782*((float) (rawAdc[ind])) - 864.35) ;
            } else if ( resistorsCurrent[ind] == 3 ) {
            
                // Current is 55 microamps
                resVal[ind] = (int) (0.0093*(float) pow(rawAdc[ind],2) + 47.827*rawAdc[ind] +301.86) ;
            
            } else if (resistorsCurrent[ind] == 2) {
                // Current is 5.5 microamps:
                resVal[ind] = (int)( 0.0005*(float) pow(rawAdc[ind],3) - 0.5241*(float) pow(rawAdc[ind],2) + 716.57*((float)rawAdc[ind])
                    -6278.4 );
            } 
        }
        
        
    }
    
    PT_END(pt) ;
    
    
}

// === Communication Thread ======================================================
// the following thread communicates over bluetooth to a computer running
// the Python script

static PT_THREAD (protothread1(struct pt *pt))
{
  static int time_thread_1;
    # define wait_t1 1000 // mSec

    /* A protothread function must begin with PT_BEGIN() which takes a
     pointer to a struct pt. */
  PT_BEGIN(pt);

  /* We loop forever here. */
  
  static char cmd[30];
    static int value = 0;
  while(1) {
    /* Wait until the other protothread has set its flag. */

      PT_YIELD_TIME_msec(yieldTime) ;
      
      
      sprintf( PT_send_buffer, "%d %d %d %d %d %d %d ", 
              resVal[0], resVal[1], resVal[2], resVal[3], resVal[4], resVal[5], resVal[6]) ;
      PT_SPAWN(pt, &pt_DMA_output, PutSerialBuffer(&pt_DMA_output) );
      
      sprintf( PT_send_buffer, "%d %d %d %d %d %d %d %d\n", 
              resVal[7], resVal[8], resVal[9], resVal[10], resVal[11], resVal[12], resVal[13], value) ;
      PT_SPAWN(pt, &pt_DMA_output, PutSerialBuffer(&pt_DMA_output) );
      

    /* And we loop. */
  } // END WHILE(1)

  /* All protothread functions must end with PT_END() which takes a
     pointer to a struct pt. */
  PT_END(pt);
}

// === Main  ======================================================

int main(void)
{
     
    SYSTEMConfigPerformance(PBCLK);
  
    ANSELA = 0; ANSELB = 0; CM1CON = 0; CM2CON = 0;

    // === set up i/o port pin ===============================
    //need to make three pins digital out for selection
    //Channel A: RB9
    //Channel B: RB10 - Switching to RB7
    //Channel C: RB11
    
    
    mPORTBSetPinsDigitalOut(BIT_9 | BIT_7 | BIT_8 );    //Set port as output
    mPORTBClearBits(BIT_9 | BIT_7 | BIT_8);
    //mPORTBSetBits( BIT_9);


  
  #define PARAM1  ADC_FORMAT_INTG16 | ADC_CLK_AUTO | ADC_AUTO_SAMPLING_OFF
  #define PARAM2  ADC_VREF_AVDD_AVSS | ADC_OFFSET_CAL_DISABLE | ADC_SCAN_OFF | ADC_SAMPLES_PER_INT_1 | ADC_ALT_BUF_OFF | ADC_ALT_INPUT_ON
  #define PARAM3 ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_31 | ADC_CONV_CLK_32Tcy //ADC_CONV_CLK_PB | ADC_SAMPLE_TIME_30 | ADC_CONV_CLK_Tcy //increased sampling rate to 31
  #define PARAM4 ENABLE_AN5_ANA | ENABLE_AN0_ANA 
  #define PARAM5 SKIP_SCAN_ALL

   //when we use multiple pins we need to set the channel for the specific pin we're using
  SetChanADC10( ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN5 |
          ADC_CH0_NEG_SAMPLEA_NVREF | ADC_CH0_POS_SAMPLEA_AN0);
  OpenADC10( PARAM1, PARAM2, PARAM3, PARAM4, PARAM5 ); // configure ADC using the parameters defined above

EnableADC10(); // Enable the ADC

  // Set up timer2 on,  interrupts, internal clock, prescalar 1, toggle rate
  // run at 30000 ticks is 1 mSec
  OpenTimer2(T2_ON | T2_SOURCE_INT | T2_PS_1_1, 30000);
  // set up the timer interrupt with a priority of 2
  ConfigIntTimer2(T2_INT_ON | T2_INT_PRIOR_2);
  mT2ClearIntFlag(); // and clear the interrupt flag
  milliSec = 0;


  // === now the threads ===================================
  
    PT_setup();
    
    PT_INIT(&pt1);
    
    // === setup system wide interrupts  ====================
  INTEnableSystemMultiVectoredInt();
  
    tft_init_hw();
  tft_begin();
  
  tft_fillScreen(ILI9340_BLACK);
  
  tft_setRotation(1);
  


  // schedule the threads
  while(1) {
    PT_SCHEDULE(protothread_muxLoop(&pt_muxLoop)) ;
    PT_SCHEDULE(protothread1(&pt1));
    PT_SCHEDULE(protothread_calculationThread(&pt_calculationThread)) ;
  }
} // main