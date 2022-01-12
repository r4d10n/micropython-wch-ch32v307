/*
 * Copyright (c) 2021 Enjoy-Digital <florent@enjoy-digital.fr> (author: Victor Suarez Rovere <suarezvictor@gmail.com>)
*/

#ifndef __LITESDK_GPIO_H_
#define __LITESDK_GPIO_H_

#include "litesdk_csrdefs.h" //should be autogenerated

/*
Basic API
typedef litegpio_pin_t //pin number

litegpio_mode_input()
litegpio_mode_output()
litegpio_mode_open_drain()
litegpio_set_low()
litegpio_set_high()
litegpio_read()
litegpio_write()
litegpio_od_low() //open drain
litegpio_od_high() //open drain
litegpio_init()
litegpio_deinit()

Instance argument can be the predefined litegpio0 (corresponds to main GPIO), for example:
litegpio_set_low(litegpio0, 0);

*/


///////////////////////////////
// GPIO 
///////////////////////////////

//this defintion is required even of GPIO core is not used
//to avoid compilations errors at various micropython' sources
typedef uint8_t litegpio_pin_t;

#ifdef LITEGPIO_ENABLED


#if CSR_GPIO_OE_SIZE > 1
#warning GPIO of 64-bits needs testing!
#endif


typedef litegpio_out_t csr_gpio_t;
#define litegpio0 ((litegpio_t *) CSR_GPIO_BASE)
//static const litegpio_t *litegpio0 = (litegpio_t *) CSR_GPIO_BASE;


#define csr_1 ((csr_gpio_t)1)
#define csr_pin_set(v, p) ((v) | (csr_1 << (p)))
#define csr_pin_clear(v, p) ((v) & ~(csr_1 << (p)))
#define csr_pin_read(v, p) (((v) & (csr_1 << (p))) != 0)


static inline void litegpio_mode_input(litegpio_t *gpio, litegpio_pin_t pin)
{
  gpio->OE = csr_pin_clear(gpio->OE, pin); //gpio_oe_write(csr_pin_clear(gpio_oe_read(), pin));
}

static inline void litegpio_mode_output(litegpio_t *gpio, litegpio_pin_t pin)
{
  gpio->OE = csr_pin_set(gpio->OE, pin); //gpio_oe_write(csr_pin_set(gpio_oe_read(), pin));
}

static inline void litegpio_set_low(litegpio_t *gpio, litegpio_pin_t pin)
{
  gpio->OUT = csr_pin_clear(gpio->OUT, pin); //gpio_out_write(csr_pin_clear(gpio_out_read(), pin));
}

static inline void litegpio_set_high(litegpio_t *gpio, litegpio_pin_t pin)
{
  gpio->OUT = csr_pin_set(gpio->OUT, pin); //gpio_out_write(csr_pin_set(gpio_out_read(), pin));
}

static inline bool litegpio_read(litegpio_t *gpio, litegpio_pin_t pin)
{
  return csr_pin_read(gpio->IN, pin); //csr_pin_read(gpio_in_read(), pin);
}

static inline void litegpio_write(litegpio_t *gpio, litegpio_pin_t pin, bool value)
{
  //FIXME: write a more time-deterministic implementation, for example:
  //#define csr_pin_write(v, p, bit) (csr_pin_clear(v, p) | (((csr_gpio_t)(bit)) << (p)))
  //OR as in http://graphics.stanford.edu/~seander/bithacks.html#MaskedMerge
  //#define csr_pin_write(v, p, bit) ((v) ^ (((v) ^ (csr_1 << (p))) & (((csr_gpio_t)(bit)) << (p))); 

 if(value) 
    litegpio_set_high(gpio, pin);
  else
    litegpio_set_low(gpio, pin);
}

//open drain functions
//FIXME: current implementations only controls Z (output enable) of a pin always set at LOW

static inline void litegpio_mode_open_drain(litegpio_t *gpio, litegpio_pin_t pin)
{
  litegpio_set_low(gpio, pin);
  litegpio_mode_input(gpio, pin);
}

static inline void litegpio_od_low(litegpio_t *gpio, litegpio_pin_t pin)
{
  litegpio_mode_output(gpio, pin);
}

static inline void litegpio_od_high(litegpio_t *gpio, litegpio_pin_t pin)
{
  litegpio_mode_input(gpio, pin);
}


//helpers

static inline void litegpio_init(litegpio_t *gpio)
{
  (void) gpio;
}

static inline void litegpio_deinit(litegpio_t *gpio)
{
  (void) gpio;
}

//lookups base address from a pheripheral id (0, 1, 2...)
static inline litegpio_t *litegpio_instance(litepheripheral_id id)
{
  if(id != 0) //only supports single gpio
    return NULL; 
  return (litegpio_t *) CSR_GPIO_BASE;
}

#endif //LITEGPIO_ENABLED

#endif //__LITESDK_GPIO_H_

