/* pcritical_section.c - use named pipes to implement critical sections
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: pcritical_section.c,v 1.6 2012-02-22 18:55:25 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: pcritical_section.c,v 1.6 2012-02-22 18:55:25 greg Exp $";

/* use named pipes to implement critical sections.
 *
 * this is for Linux kernels that were not configured to include
 * system semaphores.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 *
 * to enter, we do a blocking read on the pipe.  if the pipe is
 * empty, we will wait until a write is done on the pipe.
 * to put the critical section in "no one home, come on in" state,
 * write and leave a character in the pipe.  then, when someone
 * wants to enter, their blocking read will get the character and
 * they can proceed into the critical section.
 *
 * there is a single "global" default critical section, and
 * it is also possible to have individual critical sections.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "pcritical_section.h"

typedef struct {
    char fname[64];
    int fd;
} named_pipe_sem_t;

/* maximum of 8 semaphores open at any one time */
#define MAX_SEM 8

static named_pipe_sem_t sems[MAX_SEM] = {
    { "", -1},
    { "", -1},
    { "", -1},
    { "", -1},
    { "", -1},
    { "", -1},
    { "", -1},
    { "", -1},
};

/* the index in sems[] of the global default semaphore */
static int sem;

/* gain private access to a critical section.  if someone else is already
 * in there, block here until they leave.
 */
int pcritical_section_enter_sem(int sem)
{
    int result;
    char ch;

    if (sem < 0 || sem >= MAX_SEM || sems[sem].fd == -1) {
        fprintf(stderr, "pcritical_section_enter_sem:  invalid sem %d\n", sem);
        return -1;
    }

    result = read(sems[sem].fd, &ch, 1);

    if (result == -1) {
        perror("pcritical_section_enter_sem:  read failed");
        return -1;
    }

    return 0;
}

/* leave a critical section.  if there is a line waiting to get in,
 * the next guy in line can enter.
 */
int pcritical_section_exit_sem(int sem)
{
    char ch;

    if (sem < 0 || sem >= MAX_SEM) {
        fprintf(stderr, "pcritical_section_exit_sem:  invalid sem %d\n", sem);
        return -1;
    }
    if (-1 == write(sems[sem].fd, &ch, 1)) {
        perror("pcritical_section_exit_sem:  write failed");
        return (-1);
    }

    return 0;
}

/* if we tried, would we be able to get in?  or would be we block?
 * watch for deadlocks resulting from calling this routine and then
 * calling critical_section_enter_sem().
 */
char pcritical_section_can_enter_sem(int sem)
{
    int result;
    result = pcritical_section_getval_sem(sem);

    if (result == -1) {
        return -1;
    } else {
        return (result > 0); 
    }

}

/* see how long the line is to get into the critical section.
 * watch for deadlocks resulting from calling this routine, seeing
 * no one in line, and then calling critical_section_enter_sem().
 */
int pcritical_section_getval_sem(int sem)
{
    fd_set read_set;
    struct timeval timeout = {0, 0};
    int result;

    if (sem < 0 || sem >= MAX_SEM) {
        fprintf(stderr, "pcritical_section_getval_sem:  invalid sem %d\n", sem);
        return -1;
    }

    FD_ZERO(&read_set);
    FD_SET(sems[sem].fd, &read_set);

    result = select(sems[sem].fd + 1, &read_set, 0, 0, &timeout);

    if (result == -1) {
        perror("pcritical_section_getval_sem:  select failed");
        return -1;
    }

    if (FD_ISSET(sems[sem].fd, &read_set)) {
        result = 1;
    } else {
        result = 0;
    }

    return result;
}

/* initialize a new pipe-based semaphore whose named pipe is name. 
 * return the index of the new semaphore in "*sem".
 */
int pcritical_section_init_named_sem(int *sem, char *name)
{
    int i;
    char found = 0;
    int result;

    for (i = 0; i < MAX_SEM; i++) {
        if (sems[i].fd == -1) {
            found = 1;
            break;
        }
    }

    if (!found) {
        fprintf(stderr, "ran out of semaphores.\n");
        return(-1);
    }

    *sem = i;

    snprintf(sems[i].fname, 32, name);

    result = mkfifo(sems[i].fname, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (result == -1 && errno != EEXIST) {
        perror("pcritical_section_init_sem:  mkfifo failed");
        return(-1);
    }

    sems[i].fd = open(sems[i].fname, O_RDWR);

    if (sems[i].fd == -1) {
        perror("pcritical_section_init_sem:  open failed");
        return(-1);
    }

    if (-1 == write(sems[i].fd, &found, 1)) {
        perror("pcritical_section_init_sem:  write failed");
        return(-1);
    }

    return 0;
}

/* create a new named pipe-based semaphore with a generic name /tmp/sem_PID */
int pcritical_section_init_sem(int *sem)
{
    char fname[32];

    snprintf(fname, 32, "/tmp/sem_%d", getpid());

    return(pcritical_section_init_named_sem(sem, fname));
}

/* delete the semaphore guarding a critical section */
void pcritical_section_delete_sem(int sem)
{
    if (sem < 0 || sem >= MAX_SEM) {
        fprintf(stderr, "pcritical_section_delete_sem:  invalid sem %d\n", sem);
        return;
    }

    if (-1 == unlink(sems[sem].fname)) {
        fprintf(stderr, "pcritical_section_delete_sem:  unlink failed\n");
        return;
    }

    sems[sem].fd = -1;
}

/* gain private access to the global default critical section.
 * if someone else is already in there, block here until they leave.
 */
void pcritical_section_enter()
{
    pcritical_section_enter_sem(sem);
}

/* delete the semaphore guarding the global default critical section */
void pcritical_section_delete()
{
    pcritical_section_delete_sem(sem);
}

/* initialize the global default critical section with no waiting line to 
 * get in.
 * the new named pipe-based semaphore with a generic name /tmp/sem_PID.
 */
void pcritical_section_init()
{
    pcritical_section_init_sem(&sem);
}

/* initialize the global default pipe-based semaphore.
 *  the named pipe will be given the file name "name".
 */
int pcritical_section_init_named(char *name)
{
    return pcritical_section_init_named_sem(&sem, name);
}

/* leave the global default critical section.  if there is a line waiting
 * to get in, the next guy in line can enter.
 */
void pcritical_section_exit()
{
    pcritical_section_exit_sem(sem);
}

/* if we tried, would we be able to get into the global default critical
 * section?  or would be we block?
 * watch for deadlocks resulting from calling this routine and then
 * calling critical_section_enter().
 */
char pcritical_section_can_enter()
{
    return pcritical_section_can_enter_sem(sem);
}

/* see how long the line is to get into the critical section.
 * watch for deadlocks resulting from calling this routine, seeing
 * no one in line, and then calling critical_section_enter().
 */
int pcritical_section_getval()
{
    return pcritical_section_getval_sem(sem);
}
