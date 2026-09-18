#ifndef KRISHNA_ABI_H
#define KRISHNA_ABI_H

/*
 * KRISHNA OS userspace ABI version.
 */
#define KRISHNA_ABI_VERSION 0

/*
 * System-call numbers.
 *
 * Never renumber an existing released syscall. New operations should be
 * appended so previously compiled applications continue to work.
 */
#define KRISHNA_SYSCALL_EXIT             1
#define KRISHNA_SYSCALL_WRITE            2
#define KRISHNA_SYSCALL_READ             3
#define KRISHNA_SYSCALL_OPEN             4
#define KRISHNA_SYSCALL_CLOSE            5
#define KRISHNA_SYSCALL_SEEK             6
#define KRISHNA_SYSCALL_STAT             7
#define KRISHNA_SYSCALL_GETPID           8
#define KRISHNA_SYSCALL_YIELD            9
#define KRISHNA_SYSCALL_SLEEP           10
#define KRISHNA_SYSCALL_CLOCK_GET       11
#define KRISHNA_SYSCALL_MEMORY_MAP      12
#define KRISHNA_SYSCALL_MEMORY_UNMAP    13
#define KRISHNA_SYSCALL_MEMORY_PROTECT  14
#define KRISHNA_SYSCALL_PROCESS_SPAWN   15
#define KRISHNA_SYSCALL_PROCESS_WAIT    16
#define KRISHNA_SYSCALL_PROCESS_KILL    17
#define KRISHNA_SYSCALL_CHANNEL_CREATE  18
#define KRISHNA_SYSCALL_CHANNEL_SEND    19
#define KRISHNA_SYSCALL_CHANNEL_RECEIVE 20
#define KRISHNA_SYSCALL_POLL            21
#define KRISHNA_SYSCALL_IOCTL           22

#define KRISHNA_SYSCALL_COUNT           23

/*
 * Standard handles.
 */
#define KRISHNA_STDIN   0
#define KRISHNA_STDOUT  1
#define KRISHNA_STDERR  2

/*
 * Positive error numbers.
 *
 * Kernel syscall returns use their negative forms:
 *
 *     -KRISHNA_ERROR_INVALID_ARGUMENT
 */
#define KRISHNA_ERROR_PERMISSION_DENIED    1
#define KRISHNA_ERROR_NO_SUCH_PROCESS      3
#define KRISHNA_ERROR_INTERRUPTED          4
#define KRISHNA_ERROR_IO                    5
#define KRISHNA_ERROR_BAD_FILE_DESCRIPTOR  9
#define KRISHNA_ERROR_OUT_OF_MEMORY       12
#define KRISHNA_ERROR_ACCESS_FAULT        14
#define KRISHNA_ERROR_BUSY                16
#define KRISHNA_ERROR_EXISTS              17
#define KRISHNA_ERROR_NO_SUCH_FILE        2
#define KRISHNA_ERROR_INVALID_ARGUMENT    22
#define KRISHNA_ERROR_NOT_IMPLEMENTED     38

#endif