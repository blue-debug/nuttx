/****************************************************************************
 * drivers/ioexpander/gpio_lower_half.c
 *
 *   Copyright (C) 2016 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <assert.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/ioexpander/ioexpander.h>
#include <nuttx/ioexpander/gpio.h>

#ifdef CONFIG_GPIO_LOWER_HALF

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* GPIO lower half driver state */

struct gplh_dev_s
{
  /* Publically visiable lower-half state */

  struct gpio_dev_s gpio;
 
  /* Private driver data follows */

  uint8_t pin;                      /* I/O expander pin ID */
  FAR struct ioexpander_dev_s *ioe; /* Contain I/O expander interface */
  FAR void *handle;                 /* Interrupt attach handle */
  pin_interrupt_t callback;         /* Interrupt callback */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gplh_handler(FAR struct ioexpander_dev_s *ioe,
                        ioe_pinset_t pinset, FAR void *arg);

/* GPIO Lower Half Interface methods */

static int gplh_read(FAR struct gpio_dev_s *gpio, FAR bool *value);
static int gplh_write(FAR struct gpio_dev_s *gpio, bool value);
static int gplh_attach(FAR struct gpio_dev_s *gpio, pin_interrupt_t callback);
static int gplh_enable(FAR struct gpio_dev_s *gpio, bool enable);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct gpio_operations_s g_gplh_ops =
{
  gplh_read,   /* read   */
  gplh_write,  /* write  */
  gplh_attach, /* attach */
  gplh_enable, /* enable */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gplh_handler
 *
 * Description:
 *   I/O expander interrupt callback function.
 *
 ****************************************************************************/

static int gplh_handler(FAR struct ioexpander_dev_s *ioe,
                        ioe_pinset_t pinset, FAR void *arg)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)arg;

  DEBUGASSERT(priv != NULL && priv->callback != NULL);

  gpioinfo("pin%u: pinset: %lx callback=%p\n",
           priv->pin, (unsigned long)pinset, priv->callback);

  /* We received the callback from the I/O expander, forward this to the
   * upper half GPIO driver via its callback.
   */

  return priv->callback(&priv->gpio);
}

/****************************************************************************
 * Name: gplh_read
 *
 * Description:
 *   Read the value of the I/O expander pin.
 *
 ****************************************************************************/

static int gplh_read(FAR struct gpio_dev_s *gpio, FAR bool *value)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: value=%p\n", priv->pin, value);

  /* Get the value from the I/O expander */

  return IOEXP_READPIN(priv->ioe, priv->pin, value);
}

/****************************************************************************
 * Name: gplh_write
 *
 * Description:
 *   Set the value of an I/O expander output pin
 *
 ****************************************************************************/

static int gplh_write(FAR struct gpio_dev_s *gpio, bool value)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: value=%u\n", priv->pin, value);

  /* Write the value using the I/O expander */

  return IOEXP_WRITEPIN(priv->ioe, priv->pin, value);
}

/****************************************************************************
 * Name: gplh_attach
 *
 * Description:
 *   Detach and disable any current interrupt on the pin.  Save the callback
 *   information for use when the pin interrupt is enabled.
 *
 ****************************************************************************/

static int gplh_attach(FAR struct gpio_dev_s *gpio, pin_interrupt_t callback)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: callback=%p\n", priv->pin, callback);

  /* Detach and disable any current interrupt on the pin. */

  if (priv->handle != NULL)
    {
      gpioinfo("pin%u: Detaching handle %p\n", priv->pin, priv->handle);
      (void)IOEP_DETACH(priv->ioe, priv->handle);
      priv->handle = NULL;
    }

  /* Save the callback function pointer for use when the pin interrupt
   * is enabled.
   */

  priv->callback = callback;
  return OK;
}

/****************************************************************************
 * Name: gplh_enable
 *
 * Description:
 *   Enable or disable the I/O expander pin interrupt
 *
 ****************************************************************************/

static int gplh_enable(FAR struct gpio_dev_s *gpio, bool enable)
{
  FAR struct gplh_dev_s *priv = (FAR struct gplh_dev_s *)gpio;
  int ret = OK;

  DEBUGASSERT(priv != NULL && priv->ioe != NULL);

  gpioinfo("pin%u: %s callback=%p handle=%p\n",
           priv->pin, enable ? "Enabling" : "Disabling",
           priv->callback, priv->handle);

  /* Are we enabling or disabling the pin interrupt? */

  if (enable)
    {
      /* We are enabling the pin interrupt.  Make certain that there is
       * an interrupt handler already attached.
       */

      if (priv->callback == NULL)
        {
          /* No callback has been attached */

          gpiowarn("WARNING: pin%u: Attempt to enable before attaching\n",
                   priv->pin);
          ret = -EPERM;
        }

      /* Check if the interrupt is already attached and enabled */

      else if (priv->handle == NULL)
        {
          ioe_pinset_t pinset = ((ioe_pinset_t)1 << priv->pin);

          /* We have a callback and the callback is not yet attached.
           * do it now.
           */

          gpioinfo("pin%u: Attaching %p\n", priv->pin, priv->callback);

          priv->handle = IOEP_ATTACH(priv->ioe, pinset, gplh_handler, priv);
          if (priv->handle == NULL)
            {
              gpioerr("ERROR: pin%u: IOEP_ATTACH() failed\n", priv->pin);
              ret = -EIO;
            }
        }
    }
  else
    {
      /* Check if we are already detached */

      if (priv->handle == NULL)
        {
          gpiowarn("WARNING: pin%u: Already detached\n", priv->pin);
        }
      else
        {
          gpioinfo("pin%u: Detaching handle=%p\n", priv->pin, priv->handle);
          ret = IOEP_DETACH(priv->ioe, priv->handle);
          if (ret < 0)
            {
              gpioerr("ERROR: pin%u: IOEP_DETACH() failed\n",
                      priv->pin, ret);
            }

          /* We are no longer attached */

          priv->handle = NULL;
        }
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gpio_lower_half
 *
 * Description:
 *   Create a GPIO pin device driver instance for an I/O expander pin.
 *
 * Input Parameters:
 *   ioe     - An instance of the I/O expander interface
 *   pin     - The I/O expander pin number for the driver
 *   pintype - See enum gpio_pintype_e
 *   minor   - The minor device number to use when registering the device
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int gpio_lower_half(FAR struct ioexpander_dev_s *ioe, unsigned int pin,
                    enum gpio_pintype_e pintype, int minor)
{
  FAR struct gplh_dev_s *priv;
  FAR struct gpio_dev_s *gpio;
  int ret;

  DEBUGASSERT(ioe != NULL && pin < CONFIG_IOEXPANDER_NPINS &&
              (unsigned int)pintype < GPIO_NPINTYPES);

  /* Allocate an new instance of the GPIO lower half driver */

  priv = (FAR struct gplh_dev_s *)kmm_zalloc(sizeof(struct gplh_dev_s));
  if (priv == NULL)
    {
      gpioerr("ERROR: Failed to allocate driver state\n");
      return -ENOMEM;
    }

  /* Initialize the non-zero elements of the newly allocated instance */

  priv->pin              = (uint8_t)pin;
  gpio                   = &priv->gpio;
  gpio->gp_pintype       = (uint8_t)pintype;
  gpio->gp_ops           = &g_gplh_ops;

  /* Register the GPIO driver */

  ret = gpio_pin_register(gpio, minor);
  if (ret < 0)
    {
      gpioerr("ERROR: gpio_pin_register() failed: %d\n", ret);
      kmm_free(priv);
    }

  return ret;
}

#endif /* CONFIG_GPIO_LOWER_HALF */
