/**
 * Tony Givargis
 * Copyright (C), 2023
 * University of California, Irvine
 *
 * CS 238P - Operating Systems
 * scm.c
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include "scm.h"

/**
 * Needs:
 *   fstat()
 *   S_ISREG()
 *   open()
 *   close()
 *   sbrk()
 *   mmap()
 *   munmap()
 *   msync()
 */

/* research the above Needed API and design accordingly */

/*
    create a 40MB file: dd if=/dev/zero of=file bs=4096 count=10000
*/

/*
    Output:

    MAPPED REGION - All the virtual memory available to the application
    UNMAPPED REGION - Regular memory returned by malloc
    SCM REGION - Memory that corresponds to file
*/

/*
    - Use AVL to store the word
    - Handle unique words
*/

#define ALLOCATION_METADATA_SIZE (sizeof(int64_t) + sizeof(int8_t))
#define VIRTUAL_ADDRESS 0x600000000000

struct scm {
    char *memory;
    int fd;
    int8_t utilized;
    size_t capacity;
};

int reset_file(const char *pathname) {
    struct stat file_info;
    long file_size;
    char zero_buffer[4096] = {0};
    int fd;

    if ((fd = open(pathname, O_RDWR)) == -1) {
        TRACE("open file while resetting");
        return 1;
    }

    if (fstat(fd, &file_info) == -1) {
        TRACE("fstat");
        close(fd);
        return 1;
    }

    file_size = file_info.st_size;

    while (file_size > 0) {
        ssize_t bytes_written = write(fd, zero_buffer, sizeof(zero_buffer));
        if (bytes_written == -1) {
            TRACE("resetting file");
            close(fd);
            return 1;
        }
        file_size -= bytes_written;
    }

    close(fd);

    return 0;
}

int write_allocated_and_block_size(int fd, long memory_location, int8_t allocated, int64_t block_size) {
    ssize_t bytes_written;
    
    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    bytes_written = write(fd, &allocated, sizeof(int8_t));
    if (bytes_written == -1) {
        TRACE("writing metadata allocated");
        return 1;
    }

    bytes_written = write(fd, &block_size, sizeof(int64_t));
    
    if (bytes_written != sizeof(int64_t)) {
        TRACE("writing metadata utilized");
        return 1;
    }

    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    return 0;
}

int read_allocation_and_block_size(int fd, long memory_location, int8_t *allocated, int64_t *block_size) {
    ssize_t bytes_read;
    
    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    bytes_read = read(fd, allocated, sizeof(int8_t));
    if (bytes_read == -1) {
        TRACE("reading metadata allocation");
        return 1;
    }

    bytes_read = read(fd, block_size, sizeof(int64_t));
    if (bytes_read != sizeof(int64_t)) {
        TRACE("reading metadata block size");
        return 1;
    }

    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    return 0;
}

int set_allocated(int fd, long memory_location) {
    ssize_t bytes_written;
    int8_t one = 1;
    
    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    bytes_written = write(fd, &one, sizeof(int8_t));
    if (bytes_written == -1) {
        TRACE("writing metadata sign");
        return 1;
    }

    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    return 0;
}

int unset_allocated(int fd, long memory_location) {
    ssize_t bytes_written;
    int8_t zero = 0;
    
    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    bytes_written = write(fd, &zero, sizeof(int8_t));
    if (bytes_written == -1) {
        TRACE("writing metadata sign");
        return 1;
    }

    if (lseek(fd, memory_location, SEEK_SET) == -1) {
        TRACE("lseek");
        return 1;
    }

    return 0;
}

int read_block_size() {
    return 1;
}

void cleanup(struct scm *scm) {
    if (scm->fd >= 0) {
       close(scm->fd);
    }
    if (scm != NULL) {
       free(scm);
    }
}

/**
 * Initializes an SCM region using the file specified in pathname as the
 * backing device, opening the region for memory allocation activities.
 *
 * pathname: the file pathname of the backing device
 * truncate: if non-zero, truncates the SCM region, clearning all data
 *
 * return: an opaque handle or NULL on error
 */

struct scm *scm_open(const char *pathname, int truncate) {
    struct scm *scm;
    struct stat file_info;
    int8_t allocated;
    int64_t block_size;

    if (VIRTUAL_ADDRESS <= (long)sbrk(0)) {
        TRACE("violated sbreak");
        return NULL;
    }

    if ((scm = malloc(sizeof(struct scm))) == NULL) {
        TRACE("storage class memory malloc");
        return NULL;
    }

    if ((scm->fd = open(pathname, O_RDWR)) == -1) {
        free(scm);
        return NULL;
    }

    if (truncate) {
        if (reset_file(pathname)) {
            TRACE("truncate file");
            free(scm);
            return NULL;
        }
    }

    if (read_allocation_and_block_size(scm->fd, 0, &allocated, &block_size)) {
        TRACE("read_allocation_and_block_size");
        return NULL;
    }

    scm->utilized = allocated;

    if (fstat(scm->fd, &file_info) == -1) {
        TRACE("fstat");
        cleanup(scm);
        return NULL;
    }

    if (!S_ISREG(file_info.st_mode)) {
        TRACE("not a file");
        cleanup(scm);
        return NULL;
    }

    scm->capacity = file_info.st_size;

    scm->memory = mmap((void *) VIRTUAL_ADDRESS, scm->capacity, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, scm->fd, 0);
    if (scm->memory == MAP_FAILED) {
        cleanup(scm);
        return NULL;
    }

    return scm;
}

/**
 * Closes a previously opened SCM handle.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * Note: scm may be NULL
 */

void scm_close(struct scm *scm) {
    if (scm == NULL) {
        return;
    }

    if (msync((void *)VIRTUAL_ADDRESS, scm->capacity, 0) == -1) {
        TRACE("msync");
        exit(1);
    }

    if (scm->memory != NULL) {
        munmap(scm->memory, scm->capacity);
    }

    if (scm->fd >= 0) {
       close(scm->fd);
    }

    free(scm);
}


/**
 * Analogous to the standard C malloc function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * n  : the size of the requested memory in bytes
 *
 * return: a pointer to the start of the allocated memory or NULL on error
 */

void *scm_malloc(struct scm *scm, size_t n) {
    long allocation_start, block_start = 0;
    int8_t allocated;
    int64_t block_size;

    if (scm == NULL) {
        return NULL;
    }

    while (1) {
        if (read_allocation_and_block_size(scm->fd, block_start, &allocated, &block_size)) {
            TRACE("read_allocation_and_block_size");
            return NULL;
        }

        printf("Requested: %ld\n", n);
        printf("Block start: %ld\n", block_start);
        printf("Block already allocated? %s\n", allocated ? "yes" : "no");
        printf("Block size: %ld\n\n", block_size);

        if (allocated == 0) {
            if (block_size == 0) {
                if (write_allocated_and_block_size(scm->fd, block_start, 1, n)) {
                    TRACE("write_allocated_and_block_size");
                    return NULL;
                }
                allocation_start = block_start + ALLOCATION_METADATA_SIZE;
                break;
            }
            else if (block_size >= (int64_t)n) {
                if (set_allocated(scm->fd, block_start)) {
                    TRACE("write_allocated_and_block_size");
                    return NULL;
                }
                allocation_start = block_start + ALLOCATION_METADATA_SIZE;
                break;
            }

        }
        block_start += ALLOCATION_METADATA_SIZE + block_size;

        if (block_start + n + ALLOCATION_METADATA_SIZE >=  scm->capacity) {
            TRACE("memory full");
            return NULL;
        }
    }

    printf("Reserved %ld bytes at location starting %ld\n\n\n", n, allocation_start);

    return (void *)(allocation_start + VIRTUAL_ADDRESS);
}

/**
 * Analogous to the standard C strdup function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * s  : a C string to be duplicated.
 *
 * return: the base memory address of the duplicated C string or NULL on error
 */

char *scm_strdup(struct scm *scm, const char *s) {
    char *duplicated;
    size_t len;

    if (scm == NULL) {
        return NULL;
    }

    if (s == NULL) {
        return NULL;
    }

    len = strlen(s);

    duplicated = scm_malloc(scm, len + 1);
    if (duplicated == NULL) {
        return NULL;
    }

    strcpy(duplicated, s);

    return duplicated;
}


/**
 * Analogous to the standard C free function, but using SCM region.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 * p  : a pointer to the start of a previously allocated memory
 */


void scm_free(struct scm *scm, void *ptr) {
    long memory_to_free;

    if (scm == NULL || ptr == NULL) {
        return;
    }

    memory_to_free = (long)ptr - ALLOCATION_METADATA_SIZE - VIRTUAL_ADDRESS;
    printf("Freeing memory start at: %ld\n", memory_to_free);

    if (unset_allocated(scm->fd, memory_to_free)) {
        TRACE("could not free memory");
        exit(1);
    }
}


/**
 * Returns the number of SCM bytes utilized thus far.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes utilized thus far
 */

size_t scm_utilized(const struct scm *scm) {
    if(scm == NULL || scm->utilized == 0) {
        return 0;
    }
    return (size_t)scm->utilized;
}


/**
 * Returns the number of SCM bytes available in total.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the number of bytes available in total
 */

size_t scm_capacity(const struct scm *scm) {
    if (scm == NULL) {
        return 0;
    }

    return (size_t)scm->capacity;
}


/**
 * Returns the base memory address withn the SCM region, i.e., the memory
 * pointer that would have been returned by the first call to scm_malloc()
 * after a truncated initialization.
 *
 * scm: an opaque handle previously obtained by calling scm_open()
 *
 * return: the base memory address within the SCM region
 */

void *scm_mbase(struct scm *scm) {
    if (scm == NULL) {
        return NULL;
    }

    return scm->memory + ALLOCATION_METADATA_SIZE;
}
