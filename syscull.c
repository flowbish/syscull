#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#define debug(format, ...) do { if (getenv("__SYSCULL_DEBUG")) fprintf(stderr, format "\n", ##__VA_ARGS__); } while (0)

// Wait in a loop until ptrace signals that child has entered or returned from
// a syscall. Returns true a syscall is in progress, or false if the child has
// terminated.
bool wait_for_syscall(pid_t child);
bool handle_sys_read(struct user_regs_struct *uregs, int state);
bool handle_sys_write(struct user_regs_struct *uregs, int state);
bool handle_sys_fork(struct user_regs_struct *uregs, int state);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("must supply command");
    }

    bool fork_fail = !!getenv("__SYSCULL_FORK");

    int child = fork();
    if (child == 0) {
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);
        // Stop self to allow parent to attach
        kill(getpid(), SIGSTOP);
        debug("executing child = %s", argv[1]);
        execvp(argv[1], &argv[1]);
        exit(1);
    }
    // Catch the child's first stop
    int status;
    waitpid(child, &status, 0);
    debug("caught first child stop");

    ptrace(PTRACE_SETOPTIONS, child, 0, PTRACE_O_TRACESYSGOOD);
    debug("tracing child's syscalls");

    // If state == 0, we are entering the syscall, otherwise we are exiting
    bool state = false;
    int previous_syscall = 0;
    while (1) {
        if (wait_for_syscall(child) != 0) break;

        // Interestingly, we get a notification twice for every syscall called
        // by the child, once when the syscall is called, before anything is
        // done, and once after the execution of the syscall. This means we
        // have to wait for essentially every other call so we can check the
        // return value.

        // Tracee register struct as specified in `sys/user.h`.
        // The important members are:
        //  * orig_rax - the syscall number (rax before it was clobbered by the kernel)
        //  * rax - the return value of the syscall
        struct user_regs_struct uregs;
        ptrace(PTRACE_GETREGS, child, NULL, &uregs);
        int syscall = (int)uregs.orig_rax;

        char *syscall_name;
        bool modified;
        switch (syscall) {
        case SYS_read:
            syscall_name = "read";
            modified = handle_sys_read(&uregs, state);
            break;
        case SYS_write:
            syscall_name = "write";
            modified = handle_sys_write(&uregs, state);
            break;
        case SYS_fork:
            syscall_name = "fork";
            modified = fork_fail ? handle_sys_fork(&uregs, state) : false;
            break;
        case SYS_getpid:
            if (previous_syscall == SYS_fork) {
                syscall_name = "fork";
                modified = fork_fail ? handle_sys_fork(&uregs, state) : false;
                break;
            }
        default:
            syscall_name = NULL;
            modified = false;
        }

        if (modified && state) {
            debug("failing %s", syscall_name);
        }
        
        int new_syscall = (int)uregs.orig_rax;
        int retval = (int)uregs.rax;

        if (syscall_name && state == true) {
            debug("%s() = %d", syscall_name, retval);
        } else {
            debug("syscall(%d) = %d", new_syscall, retval);
        }

        // Set the registers of the child if we modified them
        if (modified) {
            ptrace(PTRACE_SETREGS, child, NULL, &uregs);
        }
        state = !state;
        previous_syscall = syscall;
    }

    debug("caught child return");
    return 0;
}

bool handle_sys_read_write_common(struct user_regs_struct *uregs, int state, int count) {
    // Occasionally do some dirty stuff with the return values of the read/write syscall
    // Ignore stdin/stdout/stderr, those should always succeed;
    int fildes = (int)uregs->rdi;
    if (fildes == 0 || fildes == 1 || fildes == 2) {
        return false;
    }
    if (count % 5 == 4) {
        if (state == 0) {
            // rdx is the third argument, count to read/write
            // if we set this to zero, they are effectively a noop?
            uregs->rdx = 0;
            return true;
        } else {
            // Set the return value of -errno in rax.
            // This lets the calling program thing we failed to do EINTR.
            uregs->rax = -EINTR;
            return true;
        }
    }
    return false;
}

bool handle_sys_read(struct user_regs_struct *uregs, int state) {
    static uint64_t reads = 0;
    if (state == 0) {
        reads += 1;
    }
    return handle_sys_read_write_common(uregs, state, reads);
}

bool handle_sys_write(struct user_regs_struct *uregs, int state) {
    static uint64_t writes = 0;
    if (state == 0) {
        writes += 1;
    }
    return handle_sys_read_write_common(uregs, state, writes);
}

bool handle_sys_fork(struct user_regs_struct *uregs, int state) {
    if (state == 0) {
        // rdx is the third argument, count to read/write
        // if we set this to zero, they are effectively a noop?
        uregs->orig_rax = SYS_fork;
        return true;
    } else {
        // Set the return value of -errno in rax.
        // This lets the calling program thing we failed to do EINTR.
        uregs->rax = -EINTR;
        return true;
    }
}

bool wait_for_syscall(pid_t child) {
    int status;
    while (true) {
        ptrace(PTRACE_SYSCALL, child, NULL, NULL);
        waitpid(child, &status, 0);
        if (WIFSTOPPED(status) && WSTOPSIG(status) & 0x80) {
            return false;
        } else if (WIFEXITED(status)) {
            return true;
        }
    }
}
