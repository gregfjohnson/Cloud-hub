/* critical_section.c - use system semaphores to implement critical sections
 *
 * Copyright (C) 2012, Greg Johnson
 * Released under the terms of the GNU GPL v2.0.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * $Id: critical_section.c,v 1.6 2012-02-22 18:55:24 greg Exp $
 */
static const char Version[] = "Version "
    "$Id: critical_section.c,v 1.6 2012-02-22 18:55:24 greg Exp $";

/* use system semaphores to implement critical sections.
 * there is a single "global" default critical section, and
 * it is also possible to have individual critical sections.
 *
 * this is an API that is usable generically and is not specific to the
 * cloud_hub project.
 */

/* notes:  ipcrc sem sem_id can be used from the shell to delete orphan
 * semaphores, even IPC_PRIVATE ones.
 * "ipcs -s" lists semaphores, as does "cat /proc/sysvipc/sem".
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>

#include "critical_section.h"

static int sem;

typedef union {
               int val;                    /* value for SETVAL */
               struct semid_ds *buf;       /* buffer for IPC_STAT, IPC_SET */
               unsigned short int *array;  /* array for GETALL, SETALL */
               struct seminfo *__buf;      /* buffer for IPC_INFO */
       } semun;

/* gain private access to a critical section.  if someone else is already
 * in there, block here until they leave.
 */
void critical_section_enter_sem(int sem)
{
    int result;
    struct sembuf sb;
    semun s;

    sb.sem_num = 0;
    sb.sem_op = -1;
    sb.sem_flg = 0;
    result = semop(sem, &sb, 1);
    if (result == -1) {
        fprintf(stderr, "critical_section_enter:  error return\n");
        exit(1);
    }

    s.val = 0;
    result = semctl(sem, 0, SETVAL, s);
    if (result == -1) {
        perror("critical_section_enter_sem:  error return");
        exit(1);
    }
}

/* leave a critical section.  if there is a line waiting to get in,
 * the next guy in line can enter.
 */
void critical_section_exit_sem(int sem)
{
    int result;
    struct sembuf sb;

    sb.sem_num = 0;
    sb.sem_op = 1;
    sb.sem_flg = 0;
    result = semop(sem, &sb, 1);
    if (result == -1) {
        perror("critical_section_exit_sem:  error return");
        exit(1);
    }
}

/* if we tried, would we be able to get in?  or would be we block?
 * watch for deadlocks resulting from calling this routine and then
 * calling critical_section_enter_sem().
 */
char critical_section_can_enter_sem(int sem)
{
    int result;
    semun s;

    result = semctl(sem, 0, GETVAL, s);
    return (result > 0);
}

/* see how long the line is to get into the critical section.
 * watch for deadlocks resulting from calling this routine, seeing
 * no one in line, and then calling critical_section_enter_sem().
 */
int critical_section_getval_sem(int sem)
{
    int result;
    semun s;

    result = semctl(sem, 0, GETVAL, s);
    return result;
}

/* create a new critical section with no waiting line to get in.
 * assign the index of the new semaphore to "*sem".
 */
void critical_section_init_sem(int *sem)
{
    *sem = semget(IPC_PRIVATE, 1, 0600);
    critical_section_exit_sem(*sem);
}

/* delete the semaphore guarding a critical section */
void critical_section_delete_sem(int sem)
{
    semun s;
    int result = semctl(sem, 0, IPC_RMID, s);
    if (result == -1) {
        perror("critical_section_delete_sem:  error return");
    }
}

/* gain private access to the global default critical section.
 * if someone else is already in there, block here until they leave.
 */
void critical_section_enter()
{
    critical_section_enter_sem(sem);
}

/* delete the semaphore guarding the global default critical section */
void critical_section_delete()
{
    critical_section_delete_sem(sem);
}

/* initialize the global default critical section with no waiting line to get in
 */
void critical_section_init()
{
    critical_section_init_sem(&sem);
}

/* leave the global default critical section.  if there is a line waiting
 * to get in, the next guy in line can enter.
 */
void critical_section_exit()
{
    critical_section_exit_sem(sem);
}

/* if we tried, would we be able to get into the global default critical
 * section?  or would be we block?
 * watch for deadlocks resulting from calling this routine and then
 * calling critical_section_enter().
 */
char critical_section_can_enter()
{
    return critical_section_can_enter_sem(sem);
}

/* see how long the line is to get into the global default critical section.
 * watch for deadlocks resulting from calling this routine, seeing
 * no one in line, and then calling critical_section_enter().
 */
int critical_section_getval()
{
    return critical_section_getval_sem(sem);
}
