# System Call Table

All system calls return a -ERRNO if an error occurs. Zero or positive return values signify success.

| name       | number | arg 1           | arg 2    | arg 3 | arg 4 | arg 5 | arg 6 | returns                                                          |
| ---------- | ------ | --------------- | -------- | ----- | ----- | ----- | ----- | ---------------------------------------------------------------- |
| SYS_read   | 0      | fd              | buffer   | count | N/A   | N/A   | N/A   | number of bytes read                                             |
| SYS_write  | 1      | fd              | buffer   | count | N/A   | N/A   | N/A   | number of bytes written                                          |
| SYS_open   | 2      | path            | flags    | N/A   | N/A   | N/A   | N/A   | the fd of the opened file                                        |
| SYS_close  | 3      | fd              | N/A      | N/A   | N/A   | N/A   | N/A   | N/A                                                              |
| SYS_dup    | 4      | fd              | N/A      | N/A   | N/A   | N/A   | N/A   | the fd of the duplicate                                          |
| SYS_fork   | 5      | N/A             | N/A      | N/A   | N/A   | N/A   | N/A   | `0` in the child process, pid of the child in the parent process |
| SYS_vfork  | 6      | N/A             | N/A      | N/A   | N/A   | N/A   | N/A   | `0` in the child process, pid of the child in the parent process |
| SYS_exit   | 7      | ecode           | N/A      | N/A   | N/A   | N/A   | N/A   | does not return                                                  |
| SYS_wait   | 8      | pid             | stat_loc | N/A   | N/A   | N/A   | N/A   | pid of the reaped child process                                  |
| SYS_execve | 9      | executable path | argv     | envp  | N/A   | N/A   | n/A   | does not return on success                                       |
