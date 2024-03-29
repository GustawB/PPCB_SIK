#ifndef MIM_ERR_H
#define MIM_ERR_H

#include <stdnoreturn.h>
#include <inttypes.h>
#include <stdbool.h>

// Print information about a system error and quits.
noreturn void syserr(const char* fmt, ...);

// Print information about an error and quits.
noreturn void fatal(const char* fmt, ...);

// Print information about an error and return.
void error(const char* fmt, ...);

// Asserts the return value of the return_n_bytes function.
// Returns true false if the said function failed,
// and true otherwise.
bool assert_read(int16_t ret_val, size_t ds_val);

bool assert_write(int16_t ret_val, size_t ds_val, int server_fd, int client_fd);

#endif