#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

void halt (void);
void exit (int status);

bool create (const char *file, unsigned initial_size);
bool remove (const char *file);
int open (const char *file);
int filesize (int fd);
int read (int fd, void *buffer, unsigned size);
int write (int fd, const void *buffer, unsigned size);
void seek (int fd, unsigned position);
unsigned tell (int fd);
void close (int fd);

/*
pid_t fork (const char *thread_name);
int exec (const char *file);
int wait (pid_t pid);
*/

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// TODO: Your implementation goes here.
	// hex_dump(f->rsp, f->rsp, USER_STACK - f->rsp, true);

	switch ((&f->R)->rax){
		case SYS_HALT:
			halt();
			break;
		
		case SYS_EXIT:
			exit((&f->R)->rdi);
			break;

		// case SYS_FORK:
		// 	// printf("SYS_FORK\n");

		// case SYS_EXEC:
		// 	// printf("SYS_EXEC\n");

		// case SYS_WAIT:
		// 	// printf("SYS_WAIT\n");

		case SYS_CREATE:
			create((&f->R)->rdi, (&f->R)->rsi);
			break;

		case SYS_REMOVE:
			remove((&f->R)->rdi);
			break;

		case SYS_OPEN:
			open((&f->R)->rdi);
			break;

		case SYS_FILESIZE:
			filesize((&f->R)->rdi);
			break;
		
		case SYS_READ:
			read((&f->R)->rdi, (&f->R)->rsi, (&f->R)->rdx);
			break;

		case SYS_WRITE:
			write((&f->R)->rdi, (&f->R)->rsi, (&f->R)->rdx);
			break;

		case SYS_SEEK:
			seek((&f->R)->rdi, (&f->R)->rsi);
			break;

		case SYS_TELL:
			tell((&f->R)->rdi);
			break;

		case SYS_CLOSE:
			close((&f->R)->rdi);
			break;
	}
}

/* ========== kernel-level function ==========*/

void
halt (void) {
	power_off();
}

void
exit (int status) {
	struct thread *curr = thread_current ();
	curr->exit_code = status;
	printf ("%s: exit(%d)\n", curr->name, curr->exit_code);
	thread_exit ();
}

bool
create (const char *file, unsigned initial_size) {
	return filesys_create(file, initial_size);
}

bool
remove (const char *file) {
	return filesys_remove(file);
}

int
open (const char *file) {
	struct thread *curr;
	struct file *target;
	int fd;

	curr = thread_current ();

	target = filesys_open(file);
	if (target == NULL)
		return -1;

	fd = curr->next_fd;

	curr->fd_table[fd] = target;
	for (int i=3; i<64; i++){
		if (curr->fd_table[i] == NULL)
			curr->next_fd = i;
			break;
	}
	// TODO: fd_table이 꽉 차면 page_fault

	return fd;
}

int
filesize (int fd) {
	struct thread *curr = thread_current ();
	return file_length(curr->fd_table[fd]);
}

int
read (int fd, void *buffer, unsigned size) {
	struct thread *curr;
	unsigned read_len;
	uint8_t key;
	
	if (fd == 0){
		key = input_getc();
		read_len = file_read(key, buffer, size);
	}
	else{
		curr = thread_current ();
		read_len = file_read(curr->fd_table[fd], buffer, size);
	}

	if (read_len != size)
		return -1;
	return read_len;
}

int
write (int fd, const void *buffer, unsigned size) {
	struct thread *curr;
	unsigned write_len;

	if (fd == 1){
		putbuf(buffer, size);
		write_len = size;
	}
	else{
		curr = thread_current ();
		write_len = file_read(curr->fd_table[fd], buffer, size);
	}

	if (write_len != size)
		return -1;
	return write_len;
}

void
seek (int fd, unsigned position) {
	struct thread *curr = thread_current ();
	file_seek(curr->fd_table[fd], position);
}

unsigned
tell (int fd) {
	struct thread *curr = thread_current ();
	return file_tell(curr->fd_table[fd]);
}

void
close (int fd) {
	struct thread *curr = thread_current ();
	file_close(curr->fd_table[fd]);
	curr->fd_table[fd] = NULL;
}

/*
pid_t
fork (const char *thread_name){
	return (pid_t) syscall1 (SYS_FORK, thread_name);
}

int
exec (const char *file) {
	return (pid_t) syscall1 (SYS_EXEC, file);
}

int
wait (pid_t pid) {
	return syscall1 (SYS_WAIT, pid);
}

int
dup2 (int oldfd, int newfd){
	return syscall2 (SYS_DUP2, oldfd, newfd);
}

void *
mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
	return (void *) syscall5 (SYS_MMAP, addr, length, writable, fd, offset);
}

void
munmap (void *addr) {
	syscall1 (SYS_MUNMAP, addr);
}

bool
chdir (const char *dir) {
	return syscall1 (SYS_CHDIR, dir);
}

bool
mkdir (const char *dir) {
	return syscall1 (SYS_MKDIR, dir);
}

bool
readdir (int fd, char name[READDIR_MAX_LEN + 1]) {
	return syscall2 (SYS_READDIR, fd, name);
}

bool
isdir (int fd) {
	return syscall1 (SYS_ISDIR, fd);
}

int
inumber (int fd) {
	return syscall1 (SYS_INUMBER, fd);
}

int
symlink (const char* target, const char* linkpath) {
	return syscall2 (SYS_SYMLINK, target, linkpath);
}

int
mount (const char *path, int chan_no, int dev_no) {
	return syscall3 (SYS_MOUNT, path, chan_no, dev_no);
}

int
umount (const char *path) {
	return syscall1 (SYS_UMOUNT, path);
}
*/
