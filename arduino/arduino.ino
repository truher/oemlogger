// Copyright 2020 truher
// Teensy 3.5 two-channel synchronized ADC
//
// see EEOL_2014APR24_AMP_CTRL_AN_01.pdf
// see ADC_Module.cpp
// see DMAChannel.cpp

char uidStr[17] = {0}; // the lower 2 bytes of the UID

static const char alphabet[] = {
  '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
  'U', 'V', 'W', 'X', 'Y', 'Z',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
  'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
  'u', 'v', 'w', 'x', 'y', 'z',
  '!', '#', '$', '%', '&', '(', ')', '*', '+', '-',
  ';', '<', '=', '>', '?', '@', '^', '_', '`', '{',
  '|', '}', '~'
};

const uint8_t pinLED = 13;

//const uint32_t MAX_BUFFER_SIZE = 1600; // 32767 but ideally divisible by 4 for b85... is that actually necessary?
const uint32_t MAX_BUFFER_SIZE = 1000;  // in samples, which is 16b not 8b
uint32_t current_buffer_size = 1000;
uint32_t new_buffer_size = 1000; // for next round
DMAMEM static volatile uint16_t __attribute__((aligned(32))) buffer0[MAX_BUFFER_SIZE];
DMAMEM static volatile uint16_t __attribute__((aligned(32))) buffer1[MAX_BUFFER_SIZE];
// 2 for sample width, 5/4 for b85, 1 for /0
char encoded_buf[(int)(MAX_BUFFER_SIZE * 2 * 5 / 4) + 1];

// returns the size of the encoded buffer, not including the terminating null
uint32_t encode_85(const unsigned char* in, uint32_t len, char* out) {
  char* out_start = out;
  while (len) {
    uint32_t in_chunk = 0;
    for (int8_t cnt = 24; cnt >= 0; cnt -= 8) {
      in_chunk |= *in++ << cnt;
      if (--len == 0)
        break;
    }
    for (int8_t out_offset = 4; out_offset >= 0; out_offset--) {
      out[out_offset] = alphabet[in_chunk % 85];
      in_chunk /= 85;
    }
    out += 5;
  }
  *out = 0;
  return out - out_start;
}

void restartTimer() {
  current_buffer_size = new_buffer_size;
  set_length(current_buffer_size);
  DMA_SERQ = 0;  // enable DMA channel 0
  DMA_SERQ = 1;  // enable DMA channel 1
  FTM0_SC = (FTM0_SC & ~FTM_SC_CLKS_MASK) | FTM_SC_CLKS(1);  // turn the timer back on
}

int ct = 0;
uint32_t encoded_len = 0;

// if neither channel is done, we shouldn't be here.
// if one channel is done, let the other finish, it won't hurt anything.
// if both channels are done, then set up a new run.
void maybeRestart() {
  if (DMA_ERQ & DMA_ERQ_ERQ0) return;  // channel 0 still enabled
  if (DMA_ERQ & DMA_ERQ_ERQ1) return;  // channel 1 still enabled

  FTM0_SC = (FTM0_SC & ~FTM_SC_CLKS_MASK) | FTM_SC_CLKS(0);  // turn off the timer

  digitalWriteFast(pinLED, HIGH);

  Serial.print(uidStr);
  Serial.print("\tct");
  Serial.print(ct);         // TODO: real ct channels and names
  Serial.print("\t");
  Serial.print(current_buffer_size);
  Serial.print("\t");
  encoded_len = encode_85((const unsigned char *)buffer0, current_buffer_size * 2, encoded_buf);
  Serial.write(encoded_buf, encoded_len);  // TODO: this is volts
  Serial.print("\t");
  encoded_len = encode_85((const unsigned char *)buffer1, current_buffer_size * 2, encoded_buf);
  Serial.write(encoded_buf, encoded_len);  // TODO: this is amps
  Serial.println();
  Serial.send_now();
  
  //TODO: reconfigure ADC channels etc

  digitalWriteFast(pinLED, LOW);
  ct += 1;
  if (ct > 15) ct = 0;
  restartTimer();
}

void dma_ch0_isr() {
  DMA_CINT = DMA_CINT_CINT(0);  // clear the interrupt to avoid infinite loop
  maybeRestart();
}

void dma_ch1_isr() {
  DMA_CINT = DMA_CINT_CINT(1);
  maybeRestart();
}

void set_length(uint16_t new_length) {
  DMA_TCD0_CITER_ELINKNO = new_length;  // major loop size is buffer size (max 32k)
  DMA_TCD1_CITER_ELINKNO = new_length;

  DMA_TCD0_DLASTSGA = -2 * new_length;  // after major loop, go back to the start
  DMA_TCD1_DLASTSGA = -2 * new_length;

  DMA_TCD0_BITER_ELINKNO = new_length;  // major loop size is buffer size (max 32k)
  DMA_TCD1_BITER_ELINKNO = new_length;
}

void setup() {
  snprintf(uidStr, sizeof(uidStr), "%08lX%08lX", SIM_UIDML, SIM_UIDL);
  Serial.begin(0);
  while (!Serial);  // this makes it hang until serial monitor is opened.  :-(
  pinMode(pinLED, OUTPUT);

  // FTM SETUP
  
  FTM0_POL = 0;
  FTM0_OUTMASK = 0xFF; // mask all
  FTM0_SC = 0x00; // reset status == turn off FTM
  FTM0_CNT = 0x00;  // zero the counter, or maybe this does nothing
  FTM0_CNTIN = 0; // counter initial value
  FTM0_C0SC = FTM_CSC_ELSB | FTM_CSC_MSB; // output compare, high-true
  // FTM0_MOD = 32;  // modulo, for the counter, output high on overflow
  // FTM0_MOD = 16000;  // about 200 us, like the old one
  FTM0_MOD = 16000;
  //FTM0_C0V = 16;  // match value, output low on match
  // FTM0_C0V = 8000;  // about 100 us
  FTM0_C0V = 8000;

  // bga pin b11 (row b, column 11) is port c1 ("PTC1"), which is teensy pin 22 or a8
  PORTC_PCR1 = PORT_PCR_MUX(4) // in alternative 4, ftm0_ch0 goes to b11
             | PORT_PCR_DSE    // high drive strength
             | PORT_PCR_SRE;   // slow slew rate (?)
  
  FTM0_SC = FTM_SC_CLKS(1)   // set status: system clock
            | FTM_SC_PS(0)   // no prescaling
            | FTM_SC_TOIE;   // enable overflow interrupts

  FTM0_EXTTRIG |= FTM_EXTTRIG_INITTRIGEN;  // FTM output trigger enable
  FTM0_MODE |= FTM_MODE_INIT;  // initialize the output

  FTM0_OUTMASK = 0xFE; // enable only FTM0_CH0

  // ADC SETUP

  SIM_SOPT7 = SIM_SOPT7_ADC0ALTTRGEN  // enable alternate trigger
            | SIM_SOPT7_ADC1ALTTRGEN
            | SIM_SOPT7_ADC0TRGSEL(8) // select FTM0 trigger
            | SIM_SOPT7_ADC1TRGSEL(8);

  ADC0_CFG1 |= ADC_CFG1_MODE(3);  // 16 bit
  ADC1_CFG1 |= ADC_CFG1_MODE(3);

  ADC0_CFG2 |= ADC_CFG2_ADHSC   // high speed
            |  ADC_CFG2_MUXSEL; // select "b" channels
  ADC1_CFG2 |= ADC_CFG2_ADHSC;

  ADC0_SC2 |= ADC_SC2_ADTRG    // hardware trigger
           | ADC_SC2_DMAEN;    // dma enable
  
  ADC1_SC2 |= ADC_SC2_ADTRG
           | ADC_SC2_DMAEN;

  ADC0_SC1A = ADC_SC1_AIEN     // interrupt enable, TODO differential
            | ADC_SC1_ADCH(5); // ADC0_SE5b, PTD1, D4, teensy "A0"
  ADC1_SC1A = ADC_SC1_AIEN
            | ADC_SC1_ADCH(8); // ADCx_SE8, PTB0, H10, teensy "A2"

  SIM_SCGC6 |= SIM_SCGC6_FTM0  // ftm0 clock gate
            |  SIM_SCGC6_ADC0; // adc0 clock gate
  SIM_SCGC3 |= SIM_SCGC3_ADC1; // adc1 clock gate

  // DMA SETUP

  SIM_SCGC7 |= SIM_SCGC7_DMA;     // enable DMA clock
  SIM_SCGC6 |= SIM_SCGC6_DMAMUX;  // enable DMA MUX clock
  DMA_CR = 0;                     // dma control register

  // DMA SOURCE SETUP

  DMAMUX0_CHCFG0 = DMAMUX_SOURCE_ADC0 
                 | DMAMUX_ENABLE;
  DMAMUX0_CHCFG1 = DMAMUX_SOURCE_ADC1 
                 | DMAMUX_ENABLE;
  
  DMA_TCD0_SADDR = &ADC0_RA;       // DMA channel 0 source ADC0 result A
  DMA_TCD1_SADDR = &ADC1_RA;       // DMA channel 1 source ADC1 result A

  DMA_TCD0_SOFF = 0;              // source address offset = 0
  DMA_TCD1_SOFF = 0;

  DMA_TCD0_ATTR = DMA_TCD_ATTR_SSIZE(DMA_TCD_ATTR_SIZE_16BIT);  // read 16 bits at a time from source
  DMA_TCD1_ATTR = DMA_TCD_ATTR_SSIZE(DMA_TCD_ATTR_SIZE_16BIT);

  DMA_TCD0_SLAST = 0;  // source address adjustment = 0
  DMA_TCD1_SLAST = 0;

  // DMA NBYTES
  
  DMA_TCD0_NBYTES_MLNO = 2;  // transfer 16 bits
  DMA_TCD1_NBYTES_MLNO = 2;

  // DMA DEST SETUP
  
  DMA_TCD0_DADDR = buffer0;  // destination address
  DMA_TCD1_DADDR = buffer1;

  DMA_TCD0_DOFF = 2;         // increment 16 bits
  DMA_TCD1_DOFF = 2;

  DMA_TCD0_ATTR |= DMA_TCD_ATTR_DSIZE(DMA_TCD_ATTR_SIZE_16BIT); // write 16 bits at a time to dest
  DMA_TCD1_ATTR |= DMA_TCD_ATTR_DSIZE(DMA_TCD_ATTR_SIZE_16BIT);

  set_length(current_buffer_size);
  
//  DMA_TCD0_CITER_ELINKNO = current_buffer_size;  // major loop size is buffer size (max 32k)
//  DMA_TCD1_CITER_ELINKNO = current_buffer_size;
//
//  DMA_TCD0_DLASTSGA = -2 * current_buffer_size;  // after major loop, go back to the start (bytes!)
//  DMA_TCD1_DLASTSGA = -2 * current_buffer_size;
//
//  DMA_TCD0_BITER_ELINKNO = current_buffer_size;  // major loop size is buffer size (max 32k)
//  DMA_TCD1_BITER_ELINKNO = current_buffer_size;

  DMA_TCD0_CSR = DMA_TCD_CSR_DREQ       // disable on completion
               | DMA_TCD_CSR_INTMAJOR;  // interrupt on completion
  DMA_TCD1_CSR = DMA_TCD_CSR_DREQ
               | DMA_TCD_CSR_INTMAJOR;


  // DMA ENABLE
  
  DMA_SERQ = 0;  // enable DMA channel 0
  DMA_SERQ = 1;  // enable DMA channel 1

  // RESULT INTERRUPT

  NVIC_ENABLE_IRQ(IRQ_DMA_CH0);
  NVIC_ENABLE_IRQ(IRQ_DMA_CH1);
}


// figure out how to receive commands
//
// {c}{n}\r
//
// where {c} is one of:
//
//   F = frequency in hz
//   C = channel number, or zero to scan
//   L = length in samples
//
// and {n} is an int

char cmd_buffer[100];

void loop() {
  if (Serial.available()) {
    size_t bytes_read = Serial.readBytesUntil('\r', cmd_buffer, sizeof(cmd_buffer) - 1);
    if (bytes_read < 1) return;
    cmd_buffer[bytes_read] = '\0';
   switch(*cmd_buffer) {
      case 'F':
        Serial.print("found F: ");
        if (bytes_read > 1) Serial.print(atoi(cmd_buffer+1));
        Serial.println();
        break;
      case 'C':
        Serial.print("found C: ");
        if (bytes_read > 1) Serial.print(atoi(cmd_buffer+1));
        Serial.println();
        break;
      case 'L': {
        Serial.print("found L: ");
        if (bytes_read < 2) return;
        int length = atoi(cmd_buffer + 1);
        if (length == 0) return;
        new_buffer_size = length;
        Serial.print("accepted new length: ");
        Serial.print(new_buffer_size);
        Serial.println();
        break;
      }
      default:
        Serial.print("unrecognized command: ");
        Serial.println(cmd_buffer);
    }
  }
}
