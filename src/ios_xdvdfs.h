#ifndef IOS_XDVDFS_H
#define IOS_XDVDFS_H
#include <stdint.h>
#include <stddef.h>
typedef void (*ios_disc_progress)(uint64_t done, uint64_t total, const char *name, void *context);
/* Destination must be a new staging directory. No image paths escape it. */
int ios_disc_extract(const char *image, const char *destination, ios_disc_progress progress, void *context, char *error, size_t error_size);
int ios_disc_verify(const char *directory, char *error, size_t error_size);
#endif
