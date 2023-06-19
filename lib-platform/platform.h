#ifndef PLATFORM_H_
#define PLATFORM_H_

/* Ensure that we have a config file. */
#if defined(CONFIG_H_FILE)
#include CONFIG_H_FILE
#elif defined(HAVE_CONFIG_H)
#include "config.h"
#else
#error Need either CONFIG_H_FILE or HAVE_CONFIG_H defined.
#endif

#endif /* !PLATFORM_H_ */
