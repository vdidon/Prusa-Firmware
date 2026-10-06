/**
 * @file
 * @brief Host versions of the AVR runtime helpers that the firmware C sources call directly.
 */

/* strtod.c declares the libgcc soft-float conversion with the AVR types (double is a float there):
   the host libgcc has another signature, if any, so the test binary brings its own.	*/
double __floatunsisf(unsigned long u) {
    return (float)u;
}
